#include "ChunkConfig.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
// Counts logical processors sharing a reported cache mask.
std::size_t CountBits(ULONG_PTR mask) {
    std::size_t bitCount = 0;
    while (mask != 0) {
        bitCount += (mask & 1) != 0 ? 1u : 0u;
        mask >>= 1;
    }

    return bitCount;
}

// Lists legal chunk dimensions that evenly divide a map axis.
std::vector<int> CollectDivisors(int value) {
    std::vector<int> divisors;
    int candidate = 1;
    for (; candidate <= value; ++candidate) {
        if ((value % candidate) == 0) {
            divisors.push_back(candidate);
        }
    }

    return divisors;
}

// Estimates the read-plus-write tile footprint for one simulation chunk.
std::size_t CalculateChunkWorkingSetBytes(int chunkWidth, int chunkHeight, std::size_t tileSize) {
    const std::size_t sourceTileCount = static_cast<std::size_t>(chunkWidth + 2) * static_cast<std::size_t>(chunkHeight + 2);
    const std::size_t destinationTileCount = static_cast<std::size_t>(chunkWidth) * static_cast<std::size_t>(chunkHeight);
    return (sourceTileCount + destinationTileCount) * tileSize;
}

// Prefers larger, squarer chunks while staying within the cache budget.
bool IsBetterChunkCandidate(int width, int height, std::size_t workingSetBytes, int bestWidth, int bestHeight, std::size_t bestWorkingSetBytes) {
    if ((width * height) != (bestWidth * bestHeight)) {
        return (width * height) > (bestWidth * bestHeight);
    }

    const int aspectDifference = std::abs(width - height);
    const int bestAspectDifference = std::abs(bestWidth - bestHeight);
    if (aspectDifference != bestAspectDifference) {
        return aspectDifference < bestAspectDifference;
    }

    return workingSetBytes < bestWorkingSetBytes;
}
}

// Queries Windows cache topology and returns a conservative per-thread L2 budget.
std::size_t DetectL2BytesPerLogicalThread() {
    DWORD bufferSize = 0;
    GetLogicalProcessorInformationEx(RelationCache, 0, &bufferSize);
    if (bufferSize == 0) {
        return 0;
    }

    std::vector<unsigned char> buffer(bufferSize, 0);
    if (GetLogicalProcessorInformationEx(RelationCache, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(&buffer[0]), &bufferSize) == FALSE) {
        return 0;
    }

    std::size_t bestPerLogicalThreadBytes = 0;
    DWORD offset = 0;
    while (offset < bufferSize) {
        const PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(&buffer[offset]);
        if (info->Relationship == RelationCache && info->Cache.Level == 2 && info->Cache.CacheSize > 0) {
            std::size_t sharingCount = CountBits(info->Cache.GroupMask.Mask);
            if (sharingCount == 0) {
                sharingCount = 1;
            }

            const std::size_t bytesPerLogicalThread = static_cast<std::size_t>(info->Cache.CacheSize) / sharingCount;
            if (bestPerLogicalThreadBytes == 0 || bytesPerLogicalThread < bestPerLogicalThreadBytes) {
                bestPerLogicalThreadBytes = bytesPerLogicalThread;
            }
        }

        offset += info->Size;
    }

    return bestPerLogicalThreadBytes;
}

// Chooses a rectangular chunk size from cache budget, divisibility, and job-count constraints.
ChunkConfig CalculateChunkConfig(int mapWidth, int mapHeight, std::size_t tileSize, int workerThreadCount, std::size_t manualL2BytesPerLogicalThread, bool detectL2CacheSize, double usableCacheFraction, int minimumJobsPerWorkerMultiplier) {
    ChunkConfig chunkConfig;
    chunkConfig.workerThreadCount = std::max(1, workerThreadCount);
    chunkConfig.manualOverrideBytesPerLogicalThread = manualL2BytesPerLogicalThread;
    chunkConfig.detectedL2BytesPerLogicalThread = detectL2CacheSize ? DetectL2BytesPerLogicalThread() : 0;
    chunkConfig.usedDetectedCacheSize = chunkConfig.detectedL2BytesPerLogicalThread > 0 && manualL2BytesPerLogicalThread == 0;
    chunkConfig.usableCacheFraction = usableCacheFraction;
    chunkConfig.chosenL2BytesPerLogicalThread = manualL2BytesPerLogicalThread > 0 ? manualL2BytesPerLogicalThread : chunkConfig.detectedL2BytesPerLogicalThread;
    if (chunkConfig.chosenL2BytesPerLogicalThread == 0) {
        chunkConfig.chosenL2BytesPerLogicalThread = 1024u * 1024u;
    }

    const double clampedFraction = std::max(0.05, std::min(usableCacheFraction, 0.95));
    chunkConfig.usableCacheBytesPerChunk = static_cast<std::size_t>(static_cast<double>(chunkConfig.chosenL2BytesPerLogicalThread) * clampedFraction);
    if (chunkConfig.usableCacheBytesPerChunk == 0) {
        chunkConfig.usableCacheBytesPerChunk = chunkConfig.chosenL2BytesPerLogicalThread;
    }

    const int minimumChunkCount = std::max(1, chunkConfig.workerThreadCount * std::max(1, minimumJobsPerWorkerMultiplier));
    const std::vector<int> widthDivisors = CollectDivisors(mapWidth);
    const std::vector<int> heightDivisors = CollectDivisors(mapHeight);

    int bestWidth = 0;
    int bestHeight = 0;
    std::size_t bestWorkingSetBytes = 0;
    int bestChunkCount = 0;

    std::size_t widthIndex = 0;
    for (; widthIndex < widthDivisors.size(); ++widthIndex) {
        std::size_t heightIndex = 0;
        for (; heightIndex < heightDivisors.size(); ++heightIndex) {
            const int width = widthDivisors[widthIndex];
            const int height = heightDivisors[heightIndex];
            const int chunkCount = (mapWidth / width) * (mapHeight / height);
            const std::size_t workingSetBytes = CalculateChunkWorkingSetBytes(width, height, tileSize);
            if (workingSetBytes > chunkConfig.usableCacheBytesPerChunk || chunkCount < minimumChunkCount) {
                continue;
            }

            if (bestWidth == 0 || IsBetterChunkCandidate(width, height, workingSetBytes, bestWidth, bestHeight, bestWorkingSetBytes)) {
                bestWidth = width;
                bestHeight = height;
                bestWorkingSetBytes = workingSetBytes;
                bestChunkCount = chunkCount;
            }
        }
    }

    if (bestWidth == 0 || bestHeight == 0) {
        widthIndex = 0;
        for (; widthIndex < widthDivisors.size(); ++widthIndex) {
            std::size_t heightIndex = 0;
            for (; heightIndex < heightDivisors.size(); ++heightIndex) {
                const int width = widthDivisors[widthIndex];
                const int height = heightDivisors[heightIndex];
                const int chunkCount = (mapWidth / width) * (mapHeight / height);
                const std::size_t workingSetBytes = CalculateChunkWorkingSetBytes(width, height, tileSize);
                if (workingSetBytes > chunkConfig.usableCacheBytesPerChunk) {
                    continue;
                }

                if (bestWidth == 0 || IsBetterChunkCandidate(width, height, workingSetBytes, bestWidth, bestHeight, bestWorkingSetBytes)) {
                    bestWidth = width;
                    bestHeight = height;
                    bestWorkingSetBytes = workingSetBytes;
                    bestChunkCount = chunkCount;
                }
            }
        }
    }

    if (bestWidth == 0 || bestHeight == 0) {
        bestWidth = 1;
        bestHeight = 1;
        bestWorkingSetBytes = CalculateChunkWorkingSetBytes(bestWidth, bestHeight, tileSize);
        bestChunkCount = mapWidth * mapHeight;
    }

    chunkConfig.chunkWidth = bestWidth;
    chunkConfig.chunkHeight = bestHeight;
    chunkConfig.chunkWorkingSetBytes = bestWorkingSetBytes;
    chunkConfig.chunkCount = bestChunkCount;
    return chunkConfig;
}

// Builds the row-major chunk rectangles consumed by simulation and rendering.
std::vector<ChunkRect> BuildChunkLayout(int mapWidth, int mapHeight, int chunkWidth, int chunkHeight) {
    std::vector<ChunkRect> chunkLayout;
    int startY = 0;
    for (; startY < mapHeight; startY += chunkHeight) {
        int startX = 0;
        for (; startX < mapWidth; startX += chunkWidth) {
            ChunkRect chunkRect;
            chunkRect.startX = startX;
            chunkRect.startY = startY;
            chunkRect.width = chunkWidth;
            chunkRect.height = chunkHeight;
            chunkLayout.push_back(chunkRect);
        }
    }

    return chunkLayout;
}
