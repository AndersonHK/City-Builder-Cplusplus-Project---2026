#pragma once

#include <cstddef>
#include <vector>

struct ChunkRect {
    int startX;
    int startY;
    int width;
    int height;

    // Defaults to a valid one-tile rectangle.
    ChunkRect()
        : startX(0),
          startY(0),
          width(1),
          height(1) {
    }
};

struct ChunkConfig {
    std::size_t detectedL2BytesPerLogicalThread;
    std::size_t manualOverrideBytesPerLogicalThread;
    std::size_t chosenL2BytesPerLogicalThread;
    double usableCacheFraction;
    std::size_t usableCacheBytesPerChunk;
    int chunkWidth;
    int chunkHeight;
    std::size_t chunkWorkingSetBytes;
    int workerThreadCount;
    int chunkCount;
    bool usedDetectedCacheSize;

    // Starts with conservative defaults until CalculateChunkConfig fills the decision fields.
    ChunkConfig()
        : detectedL2BytesPerLogicalThread(0),
          manualOverrideBytesPerLogicalThread(0),
          chosenL2BytesPerLogicalThread(0),
          usableCacheFraction(0.75),
          usableCacheBytesPerChunk(0),
          chunkWidth(1),
          chunkHeight(1),
          chunkWorkingSetBytes(0),
          workerThreadCount(1),
          chunkCount(1),
          usedDetectedCacheSize(false) {
    }
};

std::size_t DetectL2BytesPerLogicalThread();
ChunkConfig CalculateChunkConfig(int mapWidth, int mapHeight, std::size_t tileSize, int workerThreadCount, std::size_t manualL2BytesPerLogicalThread, bool detectL2CacheSize, double usableCacheFraction, int minimumJobsPerWorkerMultiplier);
std::vector<ChunkRect> BuildChunkLayout(int mapWidth, int mapHeight, int chunkWidth, int chunkHeight);
