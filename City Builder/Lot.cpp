#include "Lot.h"

#include <algorithm>
#include <limits>
#include <map>
#include <sstream>

namespace {
bool ContainsTile(const std::vector<Int2>& tiles, const Int2& tile) {
    std::size_t tileIndex = 0;
    for (; tileIndex < tiles.size(); ++tileIndex) {
        if (tiles[tileIndex] == tile) {
            return true;
        }
    }

    return false;
}
}

Lot::Lot()
    : lotId_(-1),
      anchorTileX_(0),
      anchorTileY_(0),
      nextModuleInstanceId_(1),
      airPollutionEmit_(0),
      landValueEmit_(0),
      minimumOccupiedOffset_(0, 0),
      maximumOccupiedOffset_(0, 0),
      renderHeight_(0.5f),
      colorR_(0.4f),
      colorG_(0.4f),
      colorB_(0.4f) {
}

Lot::Lot(int lotId, const std::string& assetId, int anchorTileX, int anchorTileY)
    : lotId_(lotId),
      assetId_(assetId),
      anchorTileX_(anchorTileX),
      anchorTileY_(anchorTileY),
      nextModuleInstanceId_(1),
      airPollutionEmit_(0),
      landValueEmit_(0),
      minimumOccupiedOffset_(0, 0),
      maximumOccupiedOffset_(0, 0),
      renderHeight_(0.5f),
      colorR_(0.4f),
      colorG_(0.4f),
      colorB_(0.4f) {
}

int Lot::id() const {
    return lotId_;
}

const std::string& Lot::assetId() const {
    return assetId_;
}

int Lot::anchorTileX() const {
    return anchorTileX_;
}

int Lot::anchorTileY() const {
    return anchorTileY_;
}

const std::vector<LotModulePlacement>& Lot::modules() const {
    return modules_;
}

const std::vector<Int2>& Lot::occupiedOffsets() const {
    return occupiedOffsets_;
}

const std::vector<int>& Lot::occupiedTileIndices() const {
    return occupiedTileIndices_;
}

int Lot::addModule(const LotModule& module, const Int2& localOrigin, int mapWidth) {
    LotModulePlacement placement;
    placement.instanceId = nextModuleInstanceId_++;
    placement.module = &module;
    placement.localOrigin = localOrigin;
    modules_.push_back(placement);
    rebuildCachedState(mapWidth);
    return placement.instanceId;
}

bool Lot::removeModule(int moduleInstanceId, int mapWidth) {
    std::size_t moduleIndex = 0;
    for (; moduleIndex < modules_.size(); ++moduleIndex) {
        if (modules_[moduleIndex].instanceId != moduleInstanceId) {
            continue;
        }

        modules_.erase(modules_.begin() + static_cast<std::ptrdiff_t>(moduleIndex));
        rebuildCachedState(mapWidth);
        return true;
    }

    return false;
}

int Lot::moduleInstanceIdAtLocalTile(const Int2& localTile) const {
    std::size_t moduleIndex = 0;
    for (; moduleIndex < modules_.size(); ++moduleIndex) {
        const LotModulePlacement& placement = modules_[moduleIndex];
        const int minimumX = placement.localOrigin.x;
        const int minimumY = placement.localOrigin.y;
        const int maximumX = placement.localOrigin.x + placement.module->width;
        const int maximumY = placement.localOrigin.y + placement.module->height;
        if (localTile.x >= minimumX && localTile.x < maximumX && localTile.y >= minimumY && localTile.y < maximumY) {
            return placement.instanceId;
        }
    }

    return -1;
}

bool Lot::occupiesLocalTile(const Int2& localTile) const {
    return ContainsTile(occupiedOffsets_, localTile);
}

void Lot::rebaseAnchorToMinimumTile(int mapWidth) {
    if (occupiedOffsets_.empty()) {
        return;
    }

    const Int2 newAnchorOffset = minimumOccupiedOffset_;
    anchorTileX_ += newAnchorOffset.x;
    anchorTileY_ += newAnchorOffset.y;

    std::size_t moduleIndex = 0;
    for (; moduleIndex < modules_.size(); ++moduleIndex) {
        modules_[moduleIndex].localOrigin.x -= newAnchorOffset.x;
        modules_[moduleIndex].localOrigin.y -= newAnchorOffset.y;
    }

    rebuildCachedState(mapWidth);
}

void Lot::applyEffects(std::vector<Tile>& tiles) const {
    if (occupiedTileIndices_.empty()) {
        return;
    }

    const int tileCount = static_cast<int>(occupiedTileIndices_.size());
    const int pollutionPerTile = airPollutionEmit_ / tileCount;
    const int landValuePerTile = landValueEmit_ / tileCount;

    std::size_t tileIndex = 0;
    for (; tileIndex < occupiedTileIndices_.size(); ++tileIndex) {
        Tile& tile = tiles[occupiedTileIndices_[tileIndex]];
        tile.airPollution += pollutionPerTile;
        tile.landValue += landValuePerTile;
    }
}

LotRenderInstance Lot::buildRenderInstance() const {
    LotRenderInstance renderInstance;
    renderInstance.lotId = lotId_;
    renderInstance.originX = anchorTileX_ + minimumOccupiedOffset_.x;
    renderInstance.originY = anchorTileY_ + minimumOccupiedOffset_.y;
    renderInstance.width = maximumOccupiedOffset_.x - minimumOccupiedOffset_.x + 1;
    renderInstance.height = maximumOccupiedOffset_.y - minimumOccupiedOffset_.y + 1;
    renderInstance.renderHeight = renderHeight_;
    renderInstance.colorR = colorR_;
    renderInstance.colorG = colorG_;
    renderInstance.colorB = colorB_;
    return renderInstance;
}

std::string Lot::moduleSummary() const {
    std::map<std::string, int> moduleCounts;

    std::size_t moduleIndex = 0;
    for (; moduleIndex < modules_.size(); ++moduleIndex) {
        if (modules_[moduleIndex].module == 0) {
            continue;
        }

        ++moduleCounts[modules_[moduleIndex].module->id];
    }

    std::ostringstream summary;
    bool isFirst = true;
    std::map<std::string, int>::const_iterator iterator = moduleCounts.begin();
    for (; iterator != moduleCounts.end(); ++iterator) {
        if (!isFirst) {
            summary << ", ";
        }

        summary << iterator->first << " x" << iterator->second;
        isFirst = false;
    }

    return summary.str();
}

void Lot::rebuildCachedState(int mapWidth) {
    occupiedOffsets_.clear();
    occupiedTileIndices_.clear();
    airPollutionEmit_ = 0;
    landValueEmit_ = 0;
    renderHeight_ = 0.25f;
    colorR_ = 0.35f;
    colorG_ = 0.35f;
    colorB_ = 0.35f;

    if (modules_.empty()) {
        minimumOccupiedOffset_ = Int2(0, 0);
        maximumOccupiedOffset_ = Int2(0, 0);
        return;
    }

    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();
    float weightedColorR = 0.0f;
    float weightedColorG = 0.0f;
    float weightedColorB = 0.0f;
    float totalWeight = 0.0f;

    std::size_t moduleIndex = 0;
    for (; moduleIndex < modules_.size(); ++moduleIndex) {
        const LotModulePlacement& placement = modules_[moduleIndex];
        if (placement.module == 0) {
            continue;
        }

        airPollutionEmit_ += placement.module->airPollutionEmit;
        landValueEmit_ += placement.module->landValueEmit;
        renderHeight_ = std::max(renderHeight_, placement.module->renderHeight);

        const float moduleWeight = static_cast<float>(placement.module->width * placement.module->height);
        weightedColorR += placement.module->colorR * moduleWeight;
        weightedColorG += placement.module->colorG * moduleWeight;
        weightedColorB += placement.module->colorB * moduleWeight;
        totalWeight += moduleWeight;

        int tileY = 0;
        for (; tileY < placement.module->height; ++tileY) {
            int tileX = 0;
            for (; tileX < placement.module->width; ++tileX) {
                const Int2 localTile(placement.localOrigin.x + tileX, placement.localOrigin.y + tileY);
                if (!ContainsTile(occupiedOffsets_, localTile)) {
                    occupiedOffsets_.push_back(localTile);
                }

                minX = std::min(minX, localTile.x);
                minY = std::min(minY, localTile.y);
                maxX = std::max(maxX, localTile.x);
                maxY = std::max(maxY, localTile.y);
            }
        }
    }

    if (totalWeight > 0.0f) {
        colorR_ = weightedColorR / totalWeight;
        colorG_ = weightedColorG / totalWeight;
        colorB_ = weightedColorB / totalWeight;
    }

    minimumOccupiedOffset_ = Int2(minX, minY);
    maximumOccupiedOffset_ = Int2(maxX, maxY);

    std::sort(occupiedOffsets_.begin(), occupiedOffsets_.end(), [](const Int2& left, const Int2& right) {
        if (left.y != right.y) {
            return left.y < right.y;
        }

        return left.x < right.x;
    });

    occupiedTileIndices_.reserve(occupiedOffsets_.size());
    std::size_t occupiedIndex = 0;
    for (; occupiedIndex < occupiedOffsets_.size(); ++occupiedIndex) {
        const Int2& occupiedOffset = occupiedOffsets_[occupiedIndex];
        const int tileX = anchorTileX_ + occupiedOffset.x;
        const int tileY = anchorTileY_ + occupiedOffset.y;
        occupiedTileIndices_.push_back((tileY * mapWidth) + tileX);
    }
}
