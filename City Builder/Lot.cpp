#include "Lot.h"

#include <algorithm>
#include <limits>
#include <map>
#include <sstream>

namespace {
int NormalizeRotationSteps(int rotationSteps) {
    return ((rotationSteps % 4) + 4) % 4;
}

Int2 RotateLocalTile(const Int2& localTile, int rotationSteps) {
    switch (NormalizeRotationSteps(rotationSteps)) {
        case 1:
            return Int2(-localTile.y, localTile.x);
        case 2:
            return Int2(-localTile.x, -localTile.y);
        case 3:
            return Int2(localTile.y, -localTile.x);
        default:
            return localTile;
    }
}

std::uint8_t RotateRoadDirection(std::uint8_t roadDirection, int rotationSteps) {
    std::uint8_t direction = roadDirection;
    int step = 0;
    for (; step < NormalizeRotationSteps(rotationSteps); ++step) {
        if (direction == kRoadDirectionNorth) {
            direction = kRoadDirectionEast;
        } else if (direction == kRoadDirectionEast) {
            direction = kRoadDirectionSouth;
        } else if (direction == kRoadDirectionSouth) {
            direction = kRoadDirectionWest;
        } else if (direction == kRoadDirectionWest) {
            direction = kRoadDirectionNorth;
        }
    }

    return direction;
}
}

// Creates an empty placeholder lot used before an archetype is applied.
Lot::Lot()
    : lotId_(-1),
      anchorTileX_(0),
      anchorTileY_(0),
      rotationSteps_(0),
      nextModuleInstanceId_(1),
      commuteDemand_(0),
      commuteSatisfied_(0),
      lowWealthResidentsTotal_(0),
      lowWealthJobsTotal_(0),
      lowWealthJobsFilled_(0),
      lowWealthResidentsHaveRoadAccess_(true),
      lowWealthJobsHaveRoadAccess_(true),
      hasLongCommuteComplaint_(false),
      airPollutionEmit_(0),
      landValueEmit_(0),
      minimumOccupiedOffset_(0, 0),
      maximumOccupiedOffset_(0, 0),
      renderHeight_(0.5f),
      colorR_(0.4f),
      colorG_(0.4f),
      colorB_(0.4f),
      constructionTotalTicks_(0),
      constructionRemainingTicks_(0) {
}

// Creates a lot instance anchored in world tile coordinates.
Lot::Lot(int lotId, const std::string& assetId, int anchorTileX, int anchorTileY, int rotationSteps)
    : lotId_(lotId),
      assetId_(assetId),
      anchorTileX_(anchorTileX),
      anchorTileY_(anchorTileY),
      rotationSteps_(((rotationSteps % 4) + 4) % 4),
      nextModuleInstanceId_(1),
      commuteDemand_(0),
      commuteSatisfied_(0),
      lowWealthResidentsTotal_(0),
      lowWealthJobsTotal_(0),
      lowWealthJobsFilled_(0),
      lowWealthResidentsHaveRoadAccess_(true),
      lowWealthJobsHaveRoadAccess_(true),
      hasLongCommuteComplaint_(false),
      airPollutionEmit_(0),
      landValueEmit_(0),
      minimumOccupiedOffset_(0, 0),
      maximumOccupiedOffset_(0, 0),
      renderHeight_(0.5f),
      colorR_(0.4f),
      colorG_(0.4f),
      colorB_(0.4f),
      constructionTotalTicks_(0),
      constructionRemainingTicks_(0) {
}

// Returns the stable runtime lot id.
int Lot::id() const {
    return lotId_;
}

// Returns the archetype id used to create the lot.
const std::string& Lot::assetId() const {
    return assetId_;
}

// Returns the lot anchor's x tile.
int Lot::anchorTileX() const {
    return anchorTileX_;
}

// Returns the lot anchor's y tile.
int Lot::anchorTileY() const {
    return anchorTileY_;
}

// Returns clockwise quarter-turns from the lot archetype orientation.
int Lot::rotationSteps() const {
    return rotationSteps_;
}

// Exposes placed modules for tooling and effects.
const std::vector<LotModulePlacement>& Lot::modules() const {
    return modules_;
}

// Returns cached occupied footprint offsets relative to the anchor.
const std::vector<Int2>& Lot::occupiedOffsets() const {
    return occupiedOffsets_;
}

// Returns cached occupied world tile indices for fast occupancy updates.
const std::vector<int>& Lot::occupiedTileIndices() const {
    return occupiedTileIndices_;
}

// Returns the lot's aggregate city-parameter contributions.
const std::vector<CityParameterContribution>& Lot::parameterContributions() const {
    return parameterContributions_;
}

// Returns coalesced route segments for the latest accepted commute assignment.
const std::vector<CommuteRouteSegment>& Lot::commuteRouteSegments() const {
    return commuteRouteSegments_;
}

const std::vector<CommuteRouteRecord>& Lot::commuteRoutes() const {
    return commuteRoutes_;
}

// Returns low-wealth commute demand assigned to this lot in the latest pass.
int Lot::commuteDemand() const {
    return commuteDemand_;
}

// Returns commute demand that successfully found a compatible destination.
int Lot::commuteSatisfied() const {
    return commuteSatisfied_;
}

int Lot::lowWealthResidentsTotal() const {
    return lowWealthResidentsTotal_;
}

int Lot::lowWealthJobsTotal() const {
    return lowWealthJobsTotal_;
}

int Lot::lowWealthJobsFilled() const {
    return lowWealthJobsFilled_;
}

bool Lot::hasMissingRoadAccessComplaint() const {
    return (lowWealthResidentsTotal_ > 0 && !lowWealthResidentsHaveRoadAccess_) ||
        (lowWealthJobsTotal_ > 0 && !lowWealthJobsHaveRoadAccess_);
}

bool Lot::hasLongCommuteComplaint() const {
    return hasLongCommuteComplaint_;
}

// Returns the world-space minimum x tile of the lot footprint.
int Lot::minimumTileX() const {
    return anchorTileX_ + minimumOccupiedOffset_.x;
}

// Returns the world-space minimum y tile of the lot footprint.
int Lot::minimumTileY() const {
    return anchorTileY_ + minimumOccupiedOffset_.y;
}

// Returns the cached footprint width in tiles.
int Lot::footprintWidth() const {
    return maximumOccupiedOffset_.x - minimumOccupiedOffset_.x + 1;
}

// Returns the cached footprint height in tiles.
int Lot::footprintHeight() const {
    return maximumOccupiedOffset_.y - minimumOccupiedOffset_.y + 1;
}

bool Lot::isUnderConstruction() const {
    return constructionRemainingTicks_ > 0;
}

int Lot::constructionTotalTicks() const {
    return constructionTotalTicks_;
}

int Lot::constructionRemainingTicks() const {
    return constructionRemainingTicks_;
}

float Lot::constructionProgress() const {
    if (constructionTotalTicks_ <= 0) {
        return 1.0f;
    }

    const float completedTicks = static_cast<float>(constructionTotalTicks_ - constructionRemainingTicks_);
    return std::max(0.0f, std::min(1.0f, completedTicks / static_cast<float>(constructionTotalTicks_)));
}

// Sets an explicit lot footprint before modules are attached.
void Lot::setExplicitFootprint(const Int2& localOrigin, int width, int height, int mapWidth) {
    explicitFootprintOffsets_.clear();
    if (width <= 0 || height <= 0) {
        rebuildCachedState(mapWidth);
        return;
    }

    int tileY = 0;
    for (; tileY < height; ++tileY) {
        int tileX = 0;
        for (; tileX < width; ++tileX) {
            explicitFootprintOffsets_.push_back(Int2(localOrigin.x + tileX, localOrigin.y + tileY));
        }
    }

    rebuildCachedState(mapWidth);
}

// Adds a module placement and refreshes the lot footprint caches.
int Lot::addModule(const LotModule& module, const Int2& localOrigin, int mapWidth, int footprintWidth, int footprintHeight) {
    LotModulePlacement placement;
    placement.instanceId = nextModuleInstanceId_++;
    placement.module = &module;
    placement.localOrigin = localOrigin;
    placement.footprintWidth = footprintWidth > 0 ? footprintWidth : module.width;
    placement.footprintHeight = footprintHeight > 0 ? footprintHeight : module.height;
    modules_.push_back(placement);
    rebuildCachedState(mapWidth);
    return placement.instanceId;
}

// Removes a module by instance id and refreshes the lot footprint caches.
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

// Removes all building modules while preserving an explicit RCI parcel footprint.
void Lot::clearModules(int mapWidth) {
    modules_.clear();
    constructionTotalTicks_ = 0;
    constructionRemainingTicks_ = 0;
    clearCommutes();
    rebuildCachedState(mapWidth);
}

void Lot::startConstruction(int totalTicks, int mapWidth) {
    setConstructionState(totalTicks, totalTicks, mapWidth);
}

void Lot::setConstructionState(int totalTicks, int remainingTicks, int mapWidth) {
    constructionTotalTicks_ = std::max(0, totalTicks);
    constructionRemainingTicks_ = std::max(0, std::min(remainingTicks, constructionTotalTicks_));
    clearCommutes();
    rebuildCachedState(mapWidth);
}

bool Lot::advanceConstructionTick(int mapWidth) {
    if (constructionRemainingTicks_ <= 0) {
        return false;
    }

    --constructionRemainingTicks_;
    rebuildCachedState(mapWidth);
    return true;
}

// Finds the module occupying a tile inside the lot-local footprint.
int Lot::moduleInstanceIdAtLocalTile(const Int2& localTile) const {
    std::size_t moduleIndex = 0;
    for (; moduleIndex < modules_.size(); ++moduleIndex) {
        const LotModulePlacement& placement = modules_[moduleIndex];
        const int minimumX = placement.localOrigin.x;
        const int minimumY = placement.localOrigin.y;
        const int maximumX = placement.localOrigin.x + placement.footprintWidth;
        const int maximumY = placement.localOrigin.y + placement.footprintHeight;
        if (localTile.x >= minimumX && localTile.x < maximumX && localTile.y >= minimumY && localTile.y < maximumY) {
            return placement.instanceId;
        }
    }

    return -1;
}

// Moves the anchor to the minimum occupied tile after footprint shrinkage.
void Lot::rebaseAnchorToMinimumTile(int mapWidth) {
    if (occupiedOffsets_.empty()) {
        return;
    }
    if (!explicitFootprintOffsets_.empty()) {
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

    std::size_t footprintIndex = 0;
    for (; footprintIndex < explicitFootprintOffsets_.size(); ++footprintIndex) {
        explicitFootprintOffsets_[footprintIndex].x -= newAnchorOffset.x;
        explicitFootprintOffsets_[footprintIndex].y -= newAnchorOffset.y;
    }

    rebuildCachedState(mapWidth);
}

// Applies each module's statistical effects to its occupied tiles.
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

// Builds the renderer-facing placeholder prism description.
LotRenderInstance Lot::buildRenderInstance() const {
    LotRenderInstance renderInstance;
    renderInstance.lotId = lotId_;
    renderInstance.originX = anchorTileX_ + minimumOccupiedOffset_.x;
    renderInstance.originY = anchorTileY_ + minimumOccupiedOffset_.y;
    renderInstance.width = maximumOccupiedOffset_.x - minimumOccupiedOffset_.x + 1;
    renderInstance.height = maximumOccupiedOffset_.y - minimumOccupiedOffset_.y + 1;
    renderInstance.renderHeight = renderHeight_ * constructionProgress();
    renderInstance.colorR = colorR_;
    renderInstance.colorG = colorG_;
    renderInstance.colorB = colorB_;
    return renderInstance;
}

void Lot::buildRenderInstances(std::vector<LotRenderInstance>& instances) const {
    float groundR = 0.0f;
    float groundG = 0.0f;
    float groundB = 0.0f;
    lotGroundColor(groundR, groundG, groundB);

    std::size_t occupiedIndex = 0;
    for (; occupiedIndex < occupiedOffsets_.size(); ++occupiedIndex) {
        LotRenderInstance groundInstance;
        groundInstance.lotId = lotId_;
        groundInstance.originX = anchorTileX_ + occupiedOffsets_[occupiedIndex].x;
        groundInstance.originY = anchorTileY_ + occupiedOffsets_[occupiedIndex].y;
        groundInstance.width = 1;
        groundInstance.height = 1;
        groundInstance.renderHeight = 0.055f;
        groundInstance.colorR = groundR;
        groundInstance.colorG = groundG;
        groundInstance.colorB = groundB;
        if (assetId_ == "house_lot") {
            const Int2 pedestrianAccessTile = RotateLocalTile(Int2(0, 0), rotationSteps_);
            if (occupiedOffsets_[occupiedIndex].x == pedestrianAccessTile.x && occupiedOffsets_[occupiedIndex].y == pedestrianAccessTile.y) {
                groundInstance.surfacePattern = 1u;
                groundInstance.surfaceDirection = RotateRoadDirection(kRoadDirectionNorth, rotationSteps_);
            }
        }
        instances.push_back(groundInstance);
    }

    std::size_t moduleIndex = 0;
    for (; moduleIndex < modules_.size(); ++moduleIndex) {
        const LotModulePlacement& placement = modules_[moduleIndex];
        if (placement.module == 0) {
            continue;
        }

        LotRenderInstance moduleInstance;
        moduleInstance.lotId = lotId_;
        moduleInstance.originX = anchorTileX_ + placement.localOrigin.x;
        moduleInstance.originY = anchorTileY_ + placement.localOrigin.y;
        moduleInstance.width = placement.footprintWidth;
        moduleInstance.height = placement.footprintHeight;
        moduleInstance.renderHeight = placement.module->renderHeight * constructionProgress();
        moduleInstance.colorR = placement.module->colorR;
        moduleInstance.colorG = placement.module->colorG;
        moduleInstance.colorB = placement.module->colorB;
        instances.push_back(moduleInstance);
    }
}

// Produces a compact module count string for tile queries.
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

// Produces a compact city-parameter string for tile queries.
std::string Lot::parameterSummary(const CityParameterRegistry& registry) const {
    std::ostringstream summary;
    bool isFirst = true;

    std::size_t contributionIndex = 0;
    for (; contributionIndex < parameterContributions_.size(); ++contributionIndex) {
        const CityParameterContribution& contribution = parameterContributions_[contributionIndex];
        if (contribution.parameterId < 0 || contribution.parameterId >= static_cast<int>(registry.count()) || contribution.amount == 0.0f) {
            continue;
        }

        if (!isFirst) {
            summary << ", ";
        }

        summary << registry.definition(contribution.parameterId).id << "=" << contribution.amount;
        isFirst = false;
    }

    if (summary.str().empty()) {
        return "none";
    }

    return summary.str();
}

std::string Lot::complaintSummary() const {
    if (hasMissingRoadAccessComplaint()) {
        return "lack of road access";
    }

    if (hasLongCommuteComplaint_) {
        return "commute time";
    }

    if (lowWealthResidentsTotal_ > 0 && commuteSatisfied_ < commuteDemand_) {
        return "lack of jobs";
    }

    if (lowWealthJobsTotal_ > 0 && lowWealthJobsFilled_ < lowWealthJobsTotal_) {
        return "lack of workers";
    }

    if (airPollutionEmit_ > 0) {
        return "pollution";
    }

    return std::string();
}

// Clears committed commute visualization/statistics for a fresh assignment pass.
void Lot::clearCommutes() {
    commuteRoutes_.clear();
    commuteRouteSegments_.clear();
    commuteDemand_ = 0;
    commuteSatisfied_ = 0;
    lowWealthResidentsTotal_ = 0;
    lowWealthJobsTotal_ = 0;
    lowWealthJobsFilled_ = 0;
    lowWealthResidentsHaveRoadAccess_ = true;
    lowWealthJobsHaveRoadAccess_ = true;
    hasLongCommuteComplaint_ = false;
}

void Lot::clearCommuteRoutes() {
    commuteRoutes_.clear();
    rebuildCommuteRouteSummary();
}

void Lot::setLowWealthResidentsTotal(int residents) {
    lowWealthResidentsTotal_ = std::max(0, residents);
}

void Lot::setLowWealthJobsTotal(int jobs) {
    lowWealthJobsTotal_ = std::max(0, jobs);
}

void Lot::setLowWealthJobsFilled(int jobs) {
    lowWealthJobsFilled_ = std::max(0, jobs);
}

void Lot::setLowWealthResidentsRoadAccess(bool hasRoadAccess) {
    lowWealthResidentsHaveRoadAccess_ = hasRoadAccess;
}

void Lot::setLowWealthJobsRoadAccess(bool hasRoadAccess) {
    lowWealthJobsHaveRoadAccess_ = hasRoadAccess;
}

void Lot::setCommuteDemand(int demand) {
    commuteDemand_ = std::max(0, demand);
}

// Adds commute demand whether or not it finds a route this pass.
void Lot::addCommuteDemand(int demand) {
    if (demand > 0) {
        commuteDemand_ += demand;
    }
}

// Adds one accepted commute route to the lot's latest assignment data.
void Lot::addCommuteRoute(
    int destinationLotId,
    int demand,
    std::uint16_t transportLoad,
    bool longCommute,
    bool morningMediumRetry,
    bool eveningMediumRetry,
    const TransportPathResult& morningPathResult,
    const TransportPathResult& eveningPathResult,
    const std::vector<CommuteRouteSegment>& morningSegments,
    const std::vector<CommuteRouteSegment>& eveningSegments) {
    if (demand <= 0) {
        return;
    }

    CommuteRouteRecord route;
    route.destinationLotId = destinationLotId;
    route.demand = demand;
    route.transportLoad = transportLoad;
    route.longCommute = longCommute;
    route.morningMediumRetry = morningMediumRetry;
    route.eveningMediumRetry = eveningMediumRetry;
    route.morningPathResult = morningPathResult;
    route.eveningPathResult = eveningPathResult;
    route.morningSegments = morningSegments;
    route.eveningSegments = eveningSegments;
    commuteRoutes_.push_back(route);
    rebuildCommuteRouteSummary();
}

void Lot::addLowWealthJobsFilled(int jobs) {
    if (jobs > 0) {
        lowWealthJobsFilled_ += jobs;
    }
}

void Lot::flagLongCommute() {
    hasLongCommuteComplaint_ = true;
}

void Lot::rebuildCommuteRouteSummary() {
    commuteRouteSegments_.clear();
    commuteSatisfied_ = 0;
    hasLongCommuteComplaint_ = false;

    std::size_t routeIndex = 0;
    for (; routeIndex < commuteRoutes_.size(); ++routeIndex) {
        const CommuteRouteRecord& route = commuteRoutes_[routeIndex];
        if (route.demand <= 0) {
            continue;
        }

        commuteSatisfied_ += route.demand;
        if (route.longCommute) {
            hasLongCommuteComplaint_ = true;
        }

        commuteRouteSegments_.insert(commuteRouteSegments_.end(), route.morningSegments.begin(), route.morningSegments.end());
    }
}

// Recomputes occupied tiles, bounds, render height, and aggregate color.
void Lot::rebuildCachedState(int mapWidth) {
    occupiedOffsets_.clear();
    occupiedTileIndices_.clear();
    parameterContributions_.clear();
    airPollutionEmit_ = 0;
    landValueEmit_ = 0;
    renderHeight_ = 0.25f;
    colorR_ = 0.35f;
    colorG_ = 0.35f;
    colorB_ = 0.35f;

    if (modules_.empty() && explicitFootprintOffsets_.empty()) {
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
    const bool activeModuleEffects = !isUnderConstruction();

    occupiedOffsets_ = explicitFootprintOffsets_;
    std::size_t moduleIndex = 0;
    for (; moduleIndex < modules_.size(); ++moduleIndex) {
        const LotModulePlacement& placement = modules_[moduleIndex];
        if (placement.module == 0) {
            continue;
        }

        if (activeModuleEffects) {
            airPollutionEmit_ += placement.module->airPollutionEmit;
            landValueEmit_ += placement.module->landValueEmit;
        }
        renderHeight_ = std::max(renderHeight_, placement.module->renderHeight);

        std::size_t contributionIndex = 0;
        for (; contributionIndex < placement.module->parameterContributions.size(); ++contributionIndex) {
            if (!activeModuleEffects) {
                continue;
            }

            const CityParameterContribution& moduleContribution = placement.module->parameterContributions[contributionIndex];
            if (moduleContribution.parameterId < 0 || moduleContribution.amount == 0.0f) {
                continue;
            }

            bool merged = false;
            std::size_t existingIndex = 0;
            for (; existingIndex < parameterContributions_.size(); ++existingIndex) {
                if (parameterContributions_[existingIndex].parameterId == moduleContribution.parameterId) {
                    parameterContributions_[existingIndex].amount += moduleContribution.amount;
                    merged = true;
                    break;
                }
            }

            if (!merged) {
                parameterContributions_.push_back(moduleContribution);
            }
        }

        const float moduleWeight = static_cast<float>(placement.footprintWidth * placement.footprintHeight);
        weightedColorR += placement.module->colorR * moduleWeight;
        weightedColorG += placement.module->colorG * moduleWeight;
        weightedColorB += placement.module->colorB * moduleWeight;
        totalWeight += moduleWeight;

        int tileY = 0;
        for (; tileY < placement.footprintHeight; ++tileY) {
            int tileX = 0;
            for (; tileX < placement.footprintWidth; ++tileX) {
                const Int2 localTile(placement.localOrigin.x + tileX, placement.localOrigin.y + tileY);
                occupiedOffsets_.push_back(localTile);
                minX = std::min(minX, localTile.x);
                minY = std::min(minY, localTile.y);
                maxX = std::max(maxX, localTile.x);
                maxY = std::max(maxY, localTile.y);
            }
        }
    }

    std::size_t explicitIndex = 0;
    for (; explicitIndex < explicitFootprintOffsets_.size(); ++explicitIndex) {
        const Int2& localTile = explicitFootprintOffsets_[explicitIndex];
        minX = std::min(minX, localTile.x);
        minY = std::min(minY, localTile.y);
        maxX = std::max(maxX, localTile.x);
        maxY = std::max(maxY, localTile.y);
    }

    std::sort(occupiedOffsets_.begin(), occupiedOffsets_.end(), [](const Int2& left, const Int2& right) {
        if (left.y != right.y) {
            return left.y < right.y;
        }

        return left.x < right.x;
    });

    occupiedOffsets_.erase(std::unique(occupiedOffsets_.begin(), occupiedOffsets_.end(), [](const Int2& left, const Int2& right) {
        return left.x == right.x && left.y == right.y;
    }), occupiedOffsets_.end());

    if (totalWeight > 0.0f) {
        colorR_ = weightedColorR / totalWeight;
        colorG_ = weightedColorG / totalWeight;
        colorB_ = weightedColorB / totalWeight;
    }

    minimumOccupiedOffset_ = Int2(minX, minY);
    maximumOccupiedOffset_ = Int2(maxX, maxY);

    occupiedTileIndices_.reserve(occupiedOffsets_.size());
    std::size_t occupiedIndex = 0;
    for (; occupiedIndex < occupiedOffsets_.size(); ++occupiedIndex) {
        const Int2& occupiedOffset = occupiedOffsets_[occupiedIndex];
        const int tileX = anchorTileX_ + occupiedOffset.x;
        const int tileY = anchorTileY_ + occupiedOffset.y;
        occupiedTileIndices_.push_back((tileY * mapWidth) + tileX);
    }
}

void Lot::lotGroundColor(float& red, float& green, float& blue) const {
    if (assetId_ == "house_lot" || assetId_ == "park_lot") {
        red = 0.18f;
        green = 0.48f;
        blue = 0.22f;
        return;
    }

    red = 0.43f;
    green = 0.43f;
    blue = 0.41f;
}
