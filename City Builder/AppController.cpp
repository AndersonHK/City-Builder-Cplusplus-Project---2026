#include "AppController.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

namespace {
const int kKeyActionPress = 1;
const int kKeyActionRepeat = 2;
const int kMinimumRoadLaneCount = 1;
const int kMaximumRoadLaneCount = 4;
const long long kRegionDoubleClickMillis = 650;

void RoadTemplateControlContext(const ViewState& viewState, RoadFamily& family, TransportLayerId& layer) {
    if (viewState.activeTool == ActiveTool::RoadHighway) {
        family = RoadFamily::Highway;
        layer = TransportLayerId::Elevated;
        return;
    }

    family = RoadFamily::LocalStreet;
    layer = TransportLayerId::Ground;
}

void NormalizeRoadTemplateSelection(ViewState& viewState) {
    RoadFamily family = RoadFamily::LocalStreet;
    TransportLayerId layer = TransportLayerId::Ground;
    RoadTemplateControlContext(viewState, family, layer);
    const RoadTemplate roadTemplate = TransportNetwork::makeRoadTemplate(family, layer, viewState.roadLaneCount, viewState.roadTrafficSide, viewState.roadDirectionMode);
    viewState.roadLaneCount = roadTemplate.laneCount;
}

const char* OverlayModeName(OverlayMode overlayMode) {
    switch (overlayMode) {
        case OverlayMode::None:
            return "none";

        case OverlayMode::TrafficCapacity:
            return "traffic capacity";
    }

    return "unknown";
}

const char* RotationDirectionName(int rotationSteps) {
    switch (((rotationSteps % 4) + 4) % 4) {
        case 1:
            return "east";
        case 2:
            return "south";
        case 3:
            return "west";
        default:
            return "north";
    }
}

// Returns the human-readable label used when the active tool changes.
const char* ActiveToolName(ActiveTool activeTool) {
    switch (activeTool) {
        case ActiveTool::PollutionBrush:
            return "pollution brush";

        case ActiveTool::SmokestackLot:
            return "place smokestack lot";

        case ActiveTool::ParkLot:
            return "place park lot";

        case ActiveTool::FactoryLot:
            return "place factory lot";

        case ActiveTool::HouseLot:
            return "place house lot";

        case ActiveTool::RoadStreet:
            return "place local street";

        case ActiveTool::RoadHighway:
            return "place elevated highway";

        case ActiveTool::AddParkModule:
            return "add park module";

        case ActiveTool::RemoveModule:
            return "remove module";

        case ActiveTool::Bulldozer:
            return "bulldozer";

        case ActiveTool::Query:
            return "query";

        case ActiveTool::ZoneResidential:
            return "residential zoning";

        case ActiveTool::ZoneIndustrial:
            return "industrial zoning";
    }

    return "unknown";
}

const char* LotAssetIdForTool(ActiveTool activeTool) {
    switch (activeTool) {
        case ActiveTool::SmokestackLot:
            return "smokestack_lot";

        case ActiveTool::ParkLot:
            return "park_lot";

        case ActiveTool::FactoryLot:
            return "factory_lot";

        case ActiveTool::HouseLot:
            return "house_lot";

        default:
            return 0;
    }
}

RoadStrokeCommand BuildRciRoadStrokeCommand(const RciRoadPlan& roadPlan) {
    RoadStrokeCommand roadStrokeCommand;
    roadStrokeCommand.startTile = Int2(roadPlan.startTileX, roadPlan.startTileY);
    roadStrokeCommand.cornerTile = Int2(roadPlan.endTileX, roadPlan.endTileY);
    roadStrokeCommand.endTile = Int2(roadPlan.endTileX, roadPlan.endTileY);
    roadStrokeCommand.family = RoadFamily::LocalStreet;
    roadStrokeCommand.layer = TransportLayerId::Ground;
    roadStrokeCommand.roadTemplate = TransportNetwork::makeRoadTemplate(
        roadStrokeCommand.family,
        roadStrokeCommand.layer,
        1,
        RoadTrafficSide::RightHand,
        RoadDirectionMode::TwoWay);
    return roadStrokeCommand;
}

void ClearQuerySelection(ViewState& viewState) {
    viewState.querySelectionKind = QuerySelectionKind::None;
    viewState.queriedLotId = -1;
    viewState.queriedLotRevision = 0;
    viewState.queriedCommuteRevision = 0;
    viewState.queriedCommuteRouteSegments.clear();
    viewState.queryWindowLines.clear();
}

const char* ZoningTypeName(std::uint16_t zoningType) {
    if (zoningType == TileZoningResidential) {
        return "Residence";
    }
    if (zoningType == TileZoningIndustrial) {
        return "Industry";
    }
    return "Unzoned";
}

std::vector<std::string> BuildLotQueryWindowLines(const TileQueryResult& queryResult) {
    std::vector<std::string> lines;
    {
        std::ostringstream title;
        if (queryResult.lotIsEmpty && queryResult.hasRciLot) {
            title << "Empty " << queryResult.rciName << " lot";
        } else {
            title << "Lot #" << queryResult.lotId << " " << queryResult.lotAssetId;
        }
        lines.push_back(title.str());
    }
    if (queryResult.lotIsEmpty && queryResult.hasRciLot) {
        std::ostringstream lotLine;
        lotLine << "Lot #" << queryResult.lotId << " " << queryResult.lotAssetId;
        lines.push_back(lotLine.str());
    }
    if (!queryResult.moduleSummary.empty()) {
        lines.push_back("Modules: " + queryResult.moduleSummary);
    }
    if (queryResult.residentsLowWealthTotal > 0) {
        std::ostringstream residentsLine;
        residentsLine << "Residents low wealth: " << queryResult.residentsLowWealthCurrent << "/" << queryResult.residentsLowWealthTotal;
        lines.push_back(residentsLine.str());
    }
    if (queryResult.jobsLowWealthTotal > 0) {
        std::ostringstream jobsLine;
        jobsLine << "Jobs low wealth: " << queryResult.jobsLowWealthCurrent << "/" << queryResult.jobsLowWealthTotal;
        lines.push_back(jobsLine.str());
    }
    if (!queryResult.complaintSummary.empty()) {
        lines.push_back("Complaints: " + queryResult.complaintSummary);
    }
    if (!queryResult.parameterSummary.empty() && queryResult.parameterSummary != "none") {
        lines.push_back("Parameters: " + queryResult.parameterSummary);
    }

    return lines;
}

// Formats a road family for query output.
const char* RoadFamilyName(RoadFamily family) {
    switch (family) {
        case RoadFamily::LocalStreet:
            return "local_street";

        case RoadFamily::Highway:
            return "highway";

        default:
            return "none";
    }
}

// Formats traffic side for console status.
const char* RoadTrafficSideName(RoadTrafficSide trafficSide) {
    switch (trafficSide) {
        case RoadTrafficSide::RightHand:
            return "right-hand";

        case RoadTrafficSide::LeftHand:
            return "left-hand";

        default:
            return "unknown";
    }
}

// Formats road direction mode for console status.
const char* RoadDirectionModeName(RoadDirectionMode directionMode) {
    switch (directionMode) {
        case RoadDirectionMode::TwoWay:
            return "two-way";

        case RoadDirectionMode::OneWayForward:
            return "one-way forward";

        case RoadDirectionMode::OneWayReverse:
            return "one-way reverse";

        default:
            return "unknown";
    }
}

// Formats a transport layer for query output.
const char* TransportLayerName(TransportLayerId layer) {
    switch (layer) {
        case TransportLayerId::Ground:
            return "ground";

        case TransportLayerId::Elevated:
            return "elevated";

        case TransportLayerId::Underground:
            return "underground";

        default:
            return "unknown";
    }
}

// Formats a resolved road variant for query output.
const char* RoadRenderVariantName(RoadRenderVariant renderVariant) {
    switch (renderVariant) {
        case RoadRenderVariant::Isolated:
            return "isolated";

        case RoadRenderVariant::DeadEnd:
            return "dead_end";

        case RoadRenderVariant::Straight:
            return "straight";

        case RoadRenderVariant::Corner:
            return "corner";

        case RoadRenderVariant::Tee:
            return "tee";

        case RoadRenderVariant::Cross:
            return "cross";

        default:
            return "none";
    }
}

// Converts road direction bits into a compact compass string.
std::string DirectionMaskToString(std::uint8_t directionMask) {
    struct NamedDirection {
        std::uint8_t bit;
        const char* name;
    };

    const NamedDirection directions[] = {
        {kRoadDirectionNorth, "N"},
        {kRoadDirectionEast, "E"},
        {kRoadDirectionSouth, "S"},
        {kRoadDirectionWest, "W"},
        {kRoadDirectionNorthEast, "NE"},
        {kRoadDirectionSouthEast, "SE"},
        {kRoadDirectionSouthWest, "SW"},
        {kRoadDirectionNorthWest, "NW"}
    };

    std::ostringstream builder;
    bool isFirst = true;
    std::size_t directionIndex = 0;
    for (; directionIndex < sizeof(directions) / sizeof(directions[0]); ++directionIndex) {
        if ((directionMask & directions[directionIndex].bit) == 0) {
            continue;
        }

        if (!isFirst) {
            builder << "/";
        }

        builder << directions[directionIndex].name;
        isFirst = false;
    }

    return builder.str().empty() ? "none" : builder.str();
}

const char* TransportModeName(TransportMode mode) {
    switch (mode) {
        case TransportMode::Car:
            return "car";

        case TransportMode::Pedestrian:
            return "walking";

        default:
            return "unknown";
    }
}

std::vector<std::string> BuildRciQueryWindowLines(const TileQueryResult& queryResult) {
    std::vector<std::string> lines;
    lines.push_back(queryResult.rciName.empty() ? ZoningTypeName(queryResult.rciZoningType) : queryResult.rciName);
    lines.push_back("Empty RCI lot");
    lines.push_back(std::string("Zone: ") + ZoningTypeName(queryResult.rciZoningType));
    return lines;
}

struct RoadCommuterSummary {
    TransportLayerId layer;
    TransportMode mode;
    std::uint8_t direction;
    int demand;

    RoadCommuterSummary()
        : layer(TransportLayerId::Ground),
          mode(TransportMode::Car),
          direction(0u),
          demand(0) {
    }
};

void AddRoadCommuterSummary(std::vector<RoadCommuterSummary>& summaries, const CommuteRouteSegment& segment) {
    std::size_t summaryIndex = 0;
    for (; summaryIndex < summaries.size(); ++summaryIndex) {
        RoadCommuterSummary& summary = summaries[summaryIndex];
        if (summary.layer == segment.layer &&
            summary.mode == segment.mode &&
            summary.direction == segment.direction) {
            summary.demand += static_cast<int>(segment.demand);
            return;
        }
    }

    RoadCommuterSummary summary;
    summary.layer = segment.layer;
    summary.mode = segment.mode;
    summary.direction = segment.direction;
    summary.demand = static_cast<int>(segment.demand);
    summaries.push_back(summary);
}

std::vector<std::string> BuildRoadQueryWindowLines(const TileQueryResult& queryResult) {
    std::vector<std::string> lines;
    lines.push_back("Road tile");

    std::size_t roadIndex = 0;
    for (; roadIndex < queryResult.roads.size(); ++roadIndex) {
        const ResolvedRoadCell& roadCell = queryResult.roads[roadIndex];
        std::ostringstream roadLine;
        roadLine << TransportLayerName(queryResult.roadLayers[roadIndex])
            << " " << RoadFamilyName(static_cast<RoadFamily>(roadCell.family))
            << " lanes=" << static_cast<int>(roadCell.laneCount)
            << " travel=" << DirectionMaskToString(roadCell.travelMask);
        lines.push_back(roadLine.str());
    }

    std::vector<RoadCommuterSummary> commuterSummaries;
    std::size_t segmentIndex = 0;
    for (; segmentIndex < queryResult.roadCommuteSegments.size(); ++segmentIndex) {
        AddRoadCommuterSummary(commuterSummaries, queryResult.roadCommuteSegments[segmentIndex]);
    }

    if (commuterSummaries.empty()) {
        lines.push_back("Commuters: none");
        return lines;
    }

    std::sort(
        commuterSummaries.begin(),
        commuterSummaries.end(),
        [](const RoadCommuterSummary& left, const RoadCommuterSummary& right) {
            if (left.layer != right.layer) {
                return static_cast<int>(left.layer) < static_cast<int>(right.layer);
            }
            if (left.mode != right.mode) {
                return static_cast<int>(left.mode) < static_cast<int>(right.mode);
            }
            return left.direction < right.direction;
        });

    int totalDemand = 0;
    std::size_t summaryIndex = 0;
    for (; summaryIndex < commuterSummaries.size(); ++summaryIndex) {
        totalDemand += commuterSummaries[summaryIndex].demand;
    }

    {
        std::ostringstream totalLine;
        totalLine << "Commuters: " << totalDemand;
        lines.push_back(totalLine.str());
    }

    for (summaryIndex = 0; summaryIndex < commuterSummaries.size(); ++summaryIndex) {
        const RoadCommuterSummary& summary = commuterSummaries[summaryIndex];
        std::ostringstream summaryLine;
        summaryLine << TransportModeName(summary.mode)
            << " " << TransportLayerName(summary.layer)
            << " " << DirectionMaskToString(summary.direction)
            << ": " << summary.demand;
        lines.push_back(summaryLine.str());
    }

    return lines;
}
}

// Connects input intent to the active game session.
AppController::AppController(GameSession& gameSession, const AppConfig& appConfig)
    : gameSession_(gameSession),
      appConfig_(appConfig),
      uiPressCaptured_(false),
      regionClickPending_(false),
      hasLastRegionClick_(false),
      lastRegionClickX_(0),
      lastRegionClickY_(0),
      lastRegionClickTime_(std::chrono::steady_clock::now()) {
}

// Records cursor position for picking and drag tools.
void AppController::onCursorMoved(double mouseX, double mouseY) {
    viewState_.mouseX = mouseX;
    viewState_.mouseY = mouseY;
}

// Dispatches the currently selected tool's primary click action.
void AppController::onLeftMouseButtonPressed() {
    uiPressCaptured_ = false;
    if (!gameSession_.isRegionMode() && handleUiClick()) {
        uiPressCaptured_ = true;
        return;
    }

    if (gameSession_.isRegionMode()) {
        regionClickPending_ = true;
        return;
    }

    const int tileX = hoveredTileX();
    const int tileY = hoveredTileY();

    switch (viewState_.activeTool) {
        case ActiveTool::PollutionBrush:
            gameSession_.runtime().queuePaintPollution(tileX, tileY, 16000000);
            break;

        case ActiveTool::SmokestackLot:
            gameSession_.runtime().queuePlaceSmokestack(tileX, tileY, viewState_.lotRotationSteps);
            break;

        case ActiveTool::ParkLot:
            gameSession_.runtime().queuePlacePark(tileX, tileY, viewState_.lotRotationSteps);
            break;

        case ActiveTool::FactoryLot:
            gameSession_.runtime().queuePlaceFactory(tileX, tileY, viewState_.lotRotationSteps);
            break;

        case ActiveTool::HouseLot:
            gameSession_.runtime().queuePlaceHouse(tileX, tileY, viewState_.lotRotationSteps);
            break;

        case ActiveTool::RoadStreet:
        case ActiveTool::RoadHighway:
            beginRoadDrag(tileX, tileY);
            break;

        case ActiveTool::AddParkModule:
            gameSession_.runtime().queueAddParkModule(tileX, tileY);
            break;

        case ActiveTool::RemoveModule:
            gameSession_.runtime().queueRemoveModuleAtTile(tileX, tileY);
            break;

        case ActiveTool::Bulldozer:
            beginBulldozeDrag(tileX, tileY);
            break;

        case ActiveTool::Query:
            printQueryResult();
            break;

        case ActiveTool::ZoneResidential:
        case ActiveTool::ZoneIndustrial:
            beginZoneDrag(tileX, tileY);
            break;
    }
}

// Commits any active road drag when the mouse button is released.
void AppController::onLeftMouseButtonReleased() {
    if (uiPressCaptured_) {
        uiPressCaptured_ = false;
        return;
    }

    if (gameSession_.isRegionMode()) {
        return;
    }

    if (viewState_.bulldozeDragActive && viewState_.activeTool == ActiveTool::Bulldozer) {
        commitBulldozeDrag(hoveredTileX(), hoveredTileY());
        return;
    }

    if (viewState_.roadDragActive && activeToolIsRoad()) {
        commitRoadDrag(hoveredTileX(), hoveredTileY());
        return;
    }

    if (viewState_.zoneDragActive && activeToolIsZoning()) {
        commitZoneDrag(hoveredTileX(), hoveredTileY());
    }
}

// Handles continuous actions while the primary mouse button remains down.
void AppController::onLeftMouseButtonHeld() {
    if (uiPressCaptured_) {
        return;
    }

    if (gameSession_.isRegionMode()) {
        return;
    }

    if (viewState_.activeTool == ActiveTool::PollutionBrush) {
        onLeftMouseButtonPressed();
        return;
    }

    if (viewState_.roadDragActive && activeToolIsRoad()) {
        viewState_.roadDragCurrentX = hoveredTileX();
        viewState_.roadDragCurrentY = hoveredTileY();
    }

    if (viewState_.bulldozeDragActive && viewState_.activeTool == ActiveTool::Bulldozer) {
        viewState_.bulldozeDragCurrentX = hoveredTileX();
        viewState_.bulldozeDragCurrentY = hoveredTileY();
    }

    if (viewState_.zoneDragActive && activeToolIsZoning()) {
        viewState_.zoneDragCurrentX = hoveredTileX();
        viewState_.zoneDragCurrentY = hoveredTileY();
    }
}

// Called from RendererCallbacks with GLFW key codes. Save/load/exit remain
// available in region mode; city tool hotkeys are ignored until a city is active.
void AppController::onKeyPressed(int key, int action) {
    if (action != kKeyActionPress && action != kKeyActionRepeat) {
        return;
    }

    const HotkeyConfig& hotkeys = appConfig_.hotkeys;
    if (action == kKeyActionPress && key == hotkeys.save) {
        syncActiveCityCameraToSession();
        gameSession_.saveAutoslot();
        return;
    }

    if (action == kKeyActionPress && key == hotkeys.load) {
        gameSession_.loadAutoslot();
        applyCameraFromActiveCity();
        viewState_.roadDragActive = false;
        viewState_.bulldozeDragActive = false;
        viewState_.zoneDragActive = false;
        return;
    }

    if (action == kKeyActionPress && key == hotkeys.exitToRegion) {
        syncActiveCityCameraToSession();
        gameSession_.exitToRegion();
        viewState_.roadDragActive = false;
        viewState_.bulldozeDragActive = false;
        viewState_.zoneDragActive = false;
        return;
    }

    if (action == kKeyActionPress && key == hotkeys.toggleRoadDebug) {
        toggleRoadDebugGraphics();
        return;
    }

    if (gameSession_.isRegionMode()) {
        return;
    }

    const int cameraStep = std::max(1, viewState_.visibleTiles / kMinimumVisibleTiles);

    if (key == hotkeys.panRight) {
        panCamera(-cameraStep, -cameraStep);
        return;
    }
    if (key == hotkeys.panLeft) {
        panCamera(cameraStep, cameraStep);
        return;
    }
    if (key == hotkeys.panDown) {
        panCamera(cameraStep, -cameraStep);
        return;
    }
    if (key == hotkeys.panUp) {
        panCamera(-cameraStep, cameraStep);
        return;
    }
    if (key == hotkeys.pollutionBrush) {
        setActiveTool(ActiveTool::PollutionBrush);
        return;
    }
    if (key == hotkeys.placeSmokestack) {
        setActiveTool(ActiveTool::SmokestackLot);
        return;
    }
    if (key == hotkeys.placePark) {
        setActiveTool(ActiveTool::ParkLot);
        return;
    }
    if (key == hotkeys.placeFactory) {
        setActiveTool(ActiveTool::FactoryLot);
        return;
    }
    if (key == hotkeys.placeHouse) {
        setActiveTool(ActiveTool::HouseLot);
        return;
    }
    if (key == hotkeys.roadStreet) {
        setActiveTool(ActiveTool::RoadStreet);
        printRoadTemplate();
        return;
    }
    if (key == hotkeys.roadHighway) {
        setActiveTool(ActiveTool::RoadHighway);
        printRoadTemplate();
        return;
    }
    if (key == hotkeys.toggleTrafficOverlay) {
        toggleTrafficOverlay();
        return;
    }
    if (key == hotkeys.addParkModule) {
        setActiveTool(ActiveTool::AddParkModule);
        return;
    }
    if (key == hotkeys.removeModule) {
        setActiveTool(ActiveTool::RemoveModule);
        return;
    }
    if (key == hotkeys.bulldozer) {
        setActiveTool(ActiveTool::Bulldozer);
        return;
    }
    if (key == hotkeys.query) {
        setActiveTool(ActiveTool::Query);
        return;
    }
    if (key == hotkeys.rotateCounterClockwise) {
        rotatePlacement(-1);
        return;
    }
    if (key == hotkeys.rotateClockwise) {
        rotatePlacement(1);
        return;
    }
    if (key == hotkeys.decreaseRoadLanes) {
        viewState_.roadLaneCount = std::max(kMinimumRoadLaneCount, viewState_.roadLaneCount - 1);
        NormalizeRoadTemplateSelection(viewState_);
        printRoadTemplate();
        return;
    }
    if (key == hotkeys.increaseRoadLanes) {
        viewState_.roadLaneCount = std::min(kMaximumRoadLaneCount, viewState_.roadLaneCount + 1);
        NormalizeRoadTemplateSelection(viewState_);
        printRoadTemplate();
        return;
    }
    if (key == hotkeys.toggleRoadTrafficSide) {
        viewState_.roadTrafficSide = viewState_.roadTrafficSide == RoadTrafficSide::RightHand ? RoadTrafficSide::LeftHand : RoadTrafficSide::RightHand;
        printRoadTemplate();
        return;
    }
    if (key == hotkeys.cycleRoadDirection) {
        if (viewState_.roadDirectionMode == RoadDirectionMode::TwoWay) {
            viewState_.roadDirectionMode = RoadDirectionMode::OneWayForward;
        } else if (viewState_.roadDirectionMode == RoadDirectionMode::OneWayForward) {
            viewState_.roadDirectionMode = RoadDirectionMode::OneWayReverse;
        } else {
            viewState_.roadDirectionMode = RoadDirectionMode::TwoWay;
        }
        NormalizeRoadTemplateSelection(viewState_);
        printRoadTemplate();
        return;
    }
}

void AppController::processPendingRegionClick() {
    if (!regionClickPending_) {
        return;
    }

    regionClickPending_ = false;
    handleRegionClick();
}

// Changes zoom in fixed tile-span steps while keeping the camera centered.
void AppController::onScroll(double yOffset) {
    if (gameSession_.isRegionMode()) {
        return;
    }

    if (yOffset < 0.0 && viewState_.visibleTiles < kMaximumVisibleTiles) {
        viewState_.visibleTiles *= 2;
        viewState_.cameraX -= viewState_.visibleTiles / 4;
        viewState_.cameraY -= viewState_.visibleTiles / 4;
    } else if (yOffset > 0.0 && viewState_.visibleTiles > kMinimumVisibleTiles) {
        viewState_.visibleTiles /= 2;
        viewState_.cameraX += viewState_.visibleTiles / 2;
        viewState_.cameraY += viewState_.visibleTiles / 2;
    }

    clampCameraToMap();

    std::cout << "Zoom tiles visible: " << viewState_.visibleTiles << " camera at " << viewState_.cameraX << ", " << viewState_.cameraY << std::endl;
}

// Stores the current framebuffer size for projection and mouse picking.
void AppController::setFramebufferSize(int framebufferWidth, int framebufferHeight) {
    viewState_.framebufferWidth = std::max(1, framebufferWidth);
    viewState_.framebufferHeight = std::max(1, framebufferHeight);
}

void AppController::setModifierKeys(bool shiftDown, bool controlDown) {
    viewState_.shiftModifierDown = shiftDown;
    viewState_.controlModifierDown = controlDown;
}

// Updates the renderer-provided tile under the cursor.
void AppController::setHoveredTile(int tileX, int tileY, bool isValid) {
    viewState_.hasHoveredTile = isValid;
    if (!isValid) {
        return;
    }

    viewState_.hoveredTileX = std::max(0, std::min(tileX, gameSession_.runtime().mapWidth() - 1));
    viewState_.hoveredTileY = std::max(0, std::min(tileY, gameSession_.runtime().mapHeight() - 1));
}

void AppController::setHoveredRegionCity(int regionX, int regionY, bool isValid) {
    viewState_.hasHoveredRegion = isValid;
    if (!isValid) {
        return;
    }

    viewState_.hoveredRegionX = regionX;
    viewState_.hoveredRegionY = regionY;
}

// Builds a renderer-only preview command for the active road drag.
bool AppController::roadPreviewStroke(RoadStrokeCommand& roadStrokeCommand) const {
    if (!viewState_.roadDragActive || !activeToolIsRoad()) {
        return false;
    }

    const Int2 startTile(viewState_.roadDragStartX, viewState_.roadDragStartY);
    const Int2 endTile(viewState_.roadDragCurrentX, viewState_.roadDragCurrentY);
    const int deltaX = endTile.x - startTile.x;
    const int deltaY = endTile.y - startTile.y;
    const Int2 cornerTile = std::abs(deltaX) >= std::abs(deltaY) ? Int2(endTile.x, startTile.y) : Int2(startTile.x, endTile.y);

    roadStrokeCommand = RoadStrokeCommand();
    roadStrokeCommand.startTile = startTile;
    roadStrokeCommand.cornerTile = cornerTile;
    roadStrokeCommand.endTile = endTile;

    if (viewState_.activeTool == ActiveTool::RoadStreet) {
        roadStrokeCommand.family = RoadFamily::LocalStreet;
        roadStrokeCommand.layer = TransportLayerId::Ground;
    } else {
        roadStrokeCommand.family = RoadFamily::Highway;
        roadStrokeCommand.layer = TransportLayerId::Elevated;
    }

    roadStrokeCommand.roadTemplate = currentRoadTemplate(roadStrokeCommand.family, roadStrokeCommand.layer);
    return true;
}

// Builds a renderer-only preview request for the active lot placement tool.
bool AppController::lotPreviewRequest(std::string& lotAssetId, int& tileX, int& tileY, int& rotationSteps) const {
    if (gameSession_.isRegionMode()) {
        return false;
    }

    const char* activeLotAssetId = LotAssetIdForTool(viewState_.activeTool);
    if (activeLotAssetId == 0 || !viewState_.hasHoveredTile) {
        return false;
    }

    lotAssetId = activeLotAssetId;
    tileX = viewState_.hoveredTileX;
    tileY = viewState_.hoveredTileY;
    rotationSteps = viewState_.lotRotationSteps;
    return true;
}

bool AppController::bulldozePreviewRect(int& minTileX, int& minTileY, int& maxTileX, int& maxTileY) const {
    if (gameSession_.isRegionMode() ||
        viewState_.activeTool != ActiveTool::Bulldozer ||
        !viewState_.bulldozeDragActive) {
        return false;
    }

    minTileX = std::min(viewState_.bulldozeDragStartX, viewState_.bulldozeDragCurrentX);
    minTileY = std::min(viewState_.bulldozeDragStartY, viewState_.bulldozeDragCurrentY);
    maxTileX = std::max(viewState_.bulldozeDragStartX, viewState_.bulldozeDragCurrentX);
    maxTileY = std::max(viewState_.bulldozeDragStartY, viewState_.bulldozeDragCurrentY);
    return true;
}

bool AppController::zonePreviewRect(int& minTileX, int& minTileY, int& maxTileX, int& maxTileY, std::uint16_t& zoningType) const {
    if (gameSession_.isRegionMode() || !activeToolIsZoning() || !viewState_.zoneDragActive) {
        return false;
    }

    minTileX = std::min(viewState_.zoneDragStartX, viewState_.zoneDragCurrentX);
    minTileY = std::min(viewState_.zoneDragStartY, viewState_.zoneDragCurrentY);
    maxTileX = std::max(viewState_.zoneDragStartX, viewState_.zoneDragCurrentX);
    maxTileY = std::max(viewState_.zoneDragStartY, viewState_.zoneDragCurrentY);
    zoningType = viewState_.zoneDragType;
    return zoningType != TileZoningNone;
}

bool AppController::rciPreviewPlan(RciPlan& plan) const {
    return buildActiveRciPlan(plan);
}

bool AppController::loadUiLayoutFromXmlFile(const std::string& filePath) {
    return uiLayout_.loadFromXmlFile(filePath);
}

bool AppController::loadRciToolsFromXmlFile(const std::string& filePath) {
    return rciTools_.loadFromXmlFile(filePath);
}

const UiLayout& AppController::uiLayout() const {
    return uiLayout_;
}

std::string AppController::activeUiAction() const {
    switch (viewState_.activeTool) {
        case ActiveTool::RoadStreet:
            return "select_road_street";

        case ActiveTool::Bulldozer:
            return "select_bulldozer";

        case ActiveTool::Query:
            return "select_query";

        case ActiveTool::ZoneResidential:
            return "select_rci_residential";

        case ActiveTool::ZoneIndustrial:
            return "select_rci_industrial";

        default:
            return std::string();
    }
}

// Keeps the camera span inside the map after pan or zoom changes.
void AppController::clampCameraToMap() {
    const int horizontalSlack = std::max(64, viewState_.visibleTiles / 2);
    const int verticalSlack = std::max(64, viewState_.visibleTiles / 2);
    const int minimumX = -horizontalSlack;
    const int minimumY = -verticalSlack;
    const int maximumX = gameSession_.runtime().mapWidth() - viewState_.visibleTiles + horizontalSlack;
    const int maximumY = gameSession_.runtime().mapHeight() - viewState_.visibleTiles + verticalSlack;
    viewState_.cameraX = std::max(minimumX, std::min(viewState_.cameraX, maximumX));
    viewState_.cameraY = std::max(minimumY, std::min(viewState_.cameraY, maximumY));
}

// Applies a camera-relative pan request and clamps it to the map.
void AppController::panCamera(int deltaX, int deltaY) {
    viewState_.cameraX += deltaX;
    viewState_.cameraY += deltaY;
    clampCameraToMap();
}

// Switches tools and clears transient drag state.
void AppController::setActiveTool(ActiveTool activeTool) {
    viewState_.activeTool = activeTool;
    viewState_.roadDragActive = false;
    viewState_.bulldozeDragActive = false;
    viewState_.zoneDragActive = false;
    viewState_.zoneDragToolId.clear();
    if (activeToolIsRoad()) {
        NormalizeRoadTemplateSelection(viewState_);
    }
    std::cout << "Selected tool: " << ActiveToolName(activeTool) << std::endl;
}

void AppController::toggleTrafficOverlay() {
    viewState_.overlayMode = viewState_.overlayMode == OverlayMode::TrafficCapacity ? OverlayMode::None : OverlayMode::TrafficCapacity;
    std::cout << "Overlay: " << OverlayModeName(viewState_.overlayMode) << std::endl;
}

void AppController::toggleRoadDebugGraphics() {
    viewState_.roadDebugGraphicsEnabled = !viewState_.roadDebugGraphicsEnabled;
    std::cout << "Road debug graphics: " << (viewState_.roadDebugGraphicsEnabled ? "on" : "off") << std::endl;
}

void AppController::rotatePlacement(int deltaSteps) {
    viewState_.lotRotationSteps = ((viewState_.lotRotationSteps + deltaSteps) % 4 + 4) % 4;
    std::cout << "Lot placement front: " << RotationDirectionName(viewState_.lotRotationSteps) << std::endl;
}

// Reports whether the active tool uses the road drag workflow.
bool AppController::activeToolIsRoad() const {
    return viewState_.activeTool == ActiveTool::RoadStreet || viewState_.activeTool == ActiveTool::RoadHighway;
}

bool AppController::activeToolIsZoning() const {
    return viewState_.activeTool == ActiveTool::ZoneResidential || viewState_.activeTool == ActiveTool::ZoneIndustrial;
}

std::string AppController::activeRciToolId() const {
    if (viewState_.activeTool == ActiveTool::ZoneResidential) {
        return "residential";
    }

    if (viewState_.activeTool == ActiveTool::ZoneIndustrial) {
        return "industrial";
    }

    return std::string();
}

RciPlanMode AppController::currentRciPlanMode() const {
    if (viewState_.controlModifierDown) {
        return RciPlanMode::Area;
    }

    if (viewState_.shiftModifierDown) {
        return RciPlanMode::Lots;
    }

    return RciPlanMode::LotsAndRoads;
}

bool AppController::buildActiveRciPlan(RciPlan& plan) const {
    if (gameSession_.isRegionMode() || !activeToolIsZoning() || !viewState_.zoneDragActive) {
        return false;
    }

    const std::string toolId = viewState_.zoneDragToolId.empty() ? activeRciToolId() : viewState_.zoneDragToolId;
    const RciTool* tool = rciTools_.findTool(toolId);
    if (tool == 0) {
        return false;
    }

    return tool->buildPlan(
        viewState_.zoneDragStartX,
        viewState_.zoneDragStartY,
        viewState_.zoneDragCurrentX,
        viewState_.zoneDragCurrentY,
        currentRciPlanMode(),
        gameSession_.runtime().mapWidth(),
        gameSession_.runtime().mapHeight(),
        plan);
}

bool AppController::handleUiClick() {
    std::string action;
    if (!uiLayout_.hitTestAction(viewState_.mouseX, viewState_.mouseY, viewState_.framebufferWidth, viewState_.framebufferHeight, action)) {
        return false;
    }

    invokeUiAction(action);
    return true;
}

void AppController::invokeUiAction(const std::string& action) {
    if (action == "toggle_side_menu") {
        viewState_.roadDragActive = false;
        viewState_.bulldozeDragActive = false;
        viewState_.zoneDragActive = false;
        uiLayout_.toggleMenu("side_tools");
        return;
    }

    if (action == "select_bulldozer") {
        setActiveTool(ActiveTool::Bulldozer);
    } else if (action == "select_road_street") {
        setActiveTool(ActiveTool::RoadStreet);
        printRoadTemplate();
    } else if (action == "select_query") {
        setActiveTool(ActiveTool::Query);
    } else if (action == "select_rci_residential") {
        setActiveTool(ActiveTool::ZoneResidential);
    } else if (action == "select_rci_industrial") {
        setActiveTool(ActiveTool::ZoneIndustrial);
    }
}

bool AppController::handleRegionClick() {
    if (!viewState_.hasHoveredRegion) {
        hasLastRegionClick_ = false;
        return false;
    }

    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const long long elapsedMillis = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRegionClickTime_).count();
    const bool isDoubleClick = hasLastRegionClick_ &&
        lastRegionClickX_ == viewState_.hoveredRegionX &&
        lastRegionClickY_ == viewState_.hoveredRegionY &&
        elapsedMillis <= kRegionDoubleClickMillis;

    lastRegionClickX_ = viewState_.hoveredRegionX;
    lastRegionClickY_ = viewState_.hoveredRegionY;
    lastRegionClickTime_ = now;
    hasLastRegionClick_ = true;

    if (isDoubleClick) {
        hasLastRegionClick_ = false;
        const bool enteredCity = gameSession_.enterCity(viewState_.hoveredRegionX, viewState_.hoveredRegionY);
        if (enteredCity) {
            applyCameraFromActiveCity();
        }
        return enteredCity;
    }

    return false;
}

void AppController::applyCameraFromActiveCity() {
    const City* activeCity = gameSession_.activeCity();
    if (activeCity == 0 || !gameSession_.isCityMode()) {
        return;
    }

    viewState_.cameraX = activeCity->cameraX();
    viewState_.cameraY = activeCity->cameraY();
    viewState_.visibleTiles = std::max(kMinimumVisibleTiles, std::min(activeCity->visibleTiles(), kMaximumVisibleTiles));
    viewState_.roadDragActive = false;
    viewState_.bulldozeDragActive = false;
    viewState_.zoneDragActive = false;
    viewState_.queriedLotId = -1;
    viewState_.queriedLotRevision = 0;
    viewState_.queriedCommuteRevision = 0;
    viewState_.queriedCommuteRouteSegments.clear();
    viewState_.queryWindowLines.clear();
    ++viewState_.queryRouteRevision;
    clampCameraToMap();
}

void AppController::syncActiveCityCameraToSession() {
    if (!gameSession_.isCityMode()) {
        return;
    }

    clampCameraToMap();
    gameSession_.setActiveCityCamera(viewState_.cameraX, viewState_.cameraY, viewState_.visibleTiles);
}

// Starts a two-leg road stroke anchored at the clicked tile.
void AppController::beginRoadDrag(int tileX, int tileY) {
    viewState_.roadDragActive = true;
    viewState_.roadDragStartX = tileX;
    viewState_.roadDragStartY = tileY;
    viewState_.roadDragCurrentX = tileX;
    viewState_.roadDragCurrentY = tileY;
}

// Converts the active road drag into a queued road placement command.
void AppController::commitRoadDrag(int tileX, int tileY) {
    const Int2 startTile(viewState_.roadDragStartX, viewState_.roadDragStartY);
    const Int2 endTile(tileX, tileY);

    const int deltaX = endTile.x - startTile.x;
    const int deltaY = endTile.y - startTile.y;
    Int2 cornerTile = std::abs(deltaX) >= std::abs(deltaY) ? Int2(endTile.x, startTile.y) : Int2(startTile.x, endTile.y);

    if (viewState_.activeTool == ActiveTool::RoadStreet) {
        RoadStrokeCommand roadStrokeCommand;
        roadStrokeCommand.startTile = startTile;
        roadStrokeCommand.cornerTile = cornerTile;
        roadStrokeCommand.endTile = endTile;
        roadStrokeCommand.family = RoadFamily::LocalStreet;
        roadStrokeCommand.layer = TransportLayerId::Ground;
        roadStrokeCommand.roadTemplate = currentRoadTemplate(roadStrokeCommand.family, roadStrokeCommand.layer);
        gameSession_.runtime().queuePlaceRoadStroke(roadStrokeCommand);
    } else if (viewState_.activeTool == ActiveTool::RoadHighway) {
        RoadStrokeCommand roadStrokeCommand;
        roadStrokeCommand.startTile = startTile;
        roadStrokeCommand.cornerTile = cornerTile;
        roadStrokeCommand.endTile = endTile;
        roadStrokeCommand.family = RoadFamily::Highway;
        roadStrokeCommand.layer = TransportLayerId::Elevated;
        roadStrokeCommand.roadTemplate = currentRoadTemplate(roadStrokeCommand.family, roadStrokeCommand.layer);
        gameSession_.runtime().queuePlaceRoadStroke(roadStrokeCommand);
    }

    viewState_.roadDragActive = false;
}

void AppController::beginBulldozeDrag(int tileX, int tileY) {
    viewState_.bulldozeDragActive = true;
    viewState_.bulldozeDragStartX = tileX;
    viewState_.bulldozeDragStartY = tileY;
    viewState_.bulldozeDragCurrentX = tileX;
    viewState_.bulldozeDragCurrentY = tileY;
}

void AppController::commitBulldozeDrag(int tileX, int tileY) {
    viewState_.bulldozeDragCurrentX = tileX;
    viewState_.bulldozeDragCurrentY = tileY;
    gameSession_.runtime().queueBulldozeArea(
        viewState_.bulldozeDragStartX,
        viewState_.bulldozeDragStartY,
        viewState_.bulldozeDragCurrentX,
        viewState_.bulldozeDragCurrentY);
    viewState_.bulldozeDragActive = false;
}

void AppController::beginZoneDrag(int tileX, int tileY) {
    viewState_.zoneDragActive = true;
    viewState_.zoneDragStartX = tileX;
    viewState_.zoneDragStartY = tileY;
    viewState_.zoneDragCurrentX = tileX;
    viewState_.zoneDragCurrentY = tileY;
    viewState_.zoneDragToolId = activeRciToolId();
    const RciTool* tool = rciTools_.findTool(viewState_.zoneDragToolId);
    viewState_.zoneDragType = tool == 0 ? TileZoningNone : tool->zoningType();
}

void AppController::commitZoneDrag(int tileX, int tileY) {
    viewState_.zoneDragCurrentX = tileX;
    viewState_.zoneDragCurrentY = tileY;

    RciPlan plan;
    if (buildActiveRciPlan(plan)) {
        std::size_t roadIndex = 0;
        for (; roadIndex < plan.roadPlans.size(); ++roadIndex) {
            gameSession_.runtime().queuePlaceRoadStroke(BuildRciRoadStrokeCommand(plan.roadPlans[roadIndex]));
        }

        if (plan.mode == RciPlanMode::Area || plan.lots.empty()) {
            std::size_t zoneRectIndex = 0;
            for (; zoneRectIndex < plan.zoneRects.size(); ++zoneRectIndex) {
                gameSession_.runtime().queueZoneArea(
                    plan.zoneRects[zoneRectIndex].minTileX,
                    plan.zoneRects[zoneRectIndex].minTileY,
                    plan.zoneRects[zoneRectIndex].maxTileX,
                    plan.zoneRects[zoneRectIndex].maxTileY,
                    plan.zoningType);
            }
        } else {
            std::size_t lotIndex = 0;
            for (; lotIndex < plan.lots.size(); ++lotIndex) {
                gameSession_.runtime().queueZoneLot(plan.lots[lotIndex]);
            }
        }
    } else {
        gameSession_.runtime().queueZoneArea(
            viewState_.zoneDragStartX,
            viewState_.zoneDragStartY,
            viewState_.zoneDragCurrentX,
            viewState_.zoneDragCurrentY,
            viewState_.zoneDragType);
    }

    viewState_.zoneDragActive = false;
    viewState_.zoneDragToolId.clear();
}

// Builds the currently selected modular road template for a placement command.
RoadTemplate AppController::currentRoadTemplate(RoadFamily family, TransportLayerId layer) const {
    return TransportNetwork::makeRoadTemplate(family, layer, viewState_.roadLaneCount, viewState_.roadTrafficSide, viewState_.roadDirectionMode);
}

// Prints active road-template settings whenever the lightweight controls change.
void AppController::printRoadTemplate() const {
    RoadFamily family = RoadFamily::LocalStreet;
    TransportLayerId layer = TransportLayerId::Ground;
    RoadTemplateControlContext(viewState_, family, layer);
    const RoadTemplate roadTemplate = currentRoadTemplate(family, layer);
    std::cout << "Road template: lanes=" << roadTemplate.laneCount
        << " traffic=" << RoadTrafficSideName(viewState_.roadTrafficSide)
        << " mode=" << RoadDirectionModeName(viewState_.roadDirectionMode)
        << std::endl;
}

void AppController::refreshQueryResultIfNeeded() {
    if (gameSession_.isRegionMode() || viewState_.querySelectionKind == QuerySelectionKind::None) {
        return;
    }

    const TileQueryResult queryResult = gameSession_.runtime().queryTile(viewState_.queriedTileX, viewState_.queriedTileY);
    if (!queryResult.isValid) {
        ClearQuerySelection(viewState_);
        ++viewState_.queryRouteRevision;
        return;
    }

    if (viewState_.querySelectionKind == QuerySelectionKind::Lot) {
        if (!queryResult.hasLot || queryResult.lotId != viewState_.queriedLotId) {
            ClearQuerySelection(viewState_);
            ++viewState_.queryRouteRevision;
            return;
        }

        if (queryResult.lotRevision == viewState_.queriedLotRevision &&
            queryResult.commuteRevision == viewState_.queriedCommuteRevision) {
            return;
        }

        viewState_.queriedLotRevision = queryResult.lotRevision;
        viewState_.queriedCommuteRevision = queryResult.commuteRevision;
        viewState_.queriedCommuteRouteSegments = queryResult.commuteRouteSegments;
        viewState_.queryWindowLines = BuildLotQueryWindowLines(queryResult);
        ++viewState_.queryRouteRevision;
        return;
    }

    if (viewState_.querySelectionKind == QuerySelectionKind::Road) {
        if (queryResult.roads.empty()) {
            ClearQuerySelection(viewState_);
            ++viewState_.queryRouteRevision;
            return;
        }

        if (queryResult.roadRevision == viewState_.queriedLotRevision &&
            queryResult.commuteRevision == viewState_.queriedCommuteRevision) {
            return;
        }

        viewState_.queriedLotRevision = queryResult.roadRevision;
        viewState_.queriedCommuteRevision = queryResult.commuteRevision;
        viewState_.queriedCommuteRouteSegments = queryResult.roadCommuteSegments;
        viewState_.queryWindowLines = BuildRoadQueryWindowLines(queryResult);
        ++viewState_.queryRouteRevision;
        return;
    }

    if (viewState_.querySelectionKind == QuerySelectionKind::Rci) {
        if (queryResult.hasLot) {
            viewState_.querySelectionKind = QuerySelectionKind::Lot;
            viewState_.queriedLotId = queryResult.lotId;
            viewState_.queriedLotRevision = queryResult.lotRevision;
            viewState_.queriedCommuteRevision = queryResult.commuteRevision;
            viewState_.queriedCommuteRouteSegments = queryResult.commuteRouteSegments;
            viewState_.queryWindowLines = BuildLotQueryWindowLines(queryResult);
            ++viewState_.queryRouteRevision;
            return;
        }

        if (!queryResult.hasRciLot) {
            ClearQuerySelection(viewState_);
            ++viewState_.queryRouteRevision;
            return;
        }

        viewState_.queriedLotRevision = queryResult.roadRevision;
        viewState_.queriedCommuteRevision = queryResult.commuteRevision;
        viewState_.queriedCommuteRouteSegments.clear();
        viewState_.queryWindowLines = BuildRciQueryWindowLines(queryResult);
        ++viewState_.queryRouteRevision;
        return;
    }

    ClearQuerySelection(viewState_);
    ++viewState_.queryRouteRevision;
}

// Returns a copy of the input/view state for renderer-side camera work.
ViewState AppController::viewState() const {
    return viewState_;
}

// Falls back to screen-space picking only when the renderer has no raycast hit.
int AppController::hoveredTileX() const {
    if (viewState_.hasHoveredTile) {
        return viewState_.hoveredTileX;
    }

    const double normalizedX = viewState_.mouseX / static_cast<double>(std::max(1, viewState_.framebufferWidth));
    const int tileX = viewState_.cameraX + static_cast<int>(normalizedX * viewState_.visibleTiles);
    return std::max(0, std::min(tileX, gameSession_.runtime().mapWidth() - 1));
}

// Falls back to screen-space picking only when the renderer has no raycast hit.
int AppController::hoveredTileY() const {
    if (viewState_.hasHoveredTile) {
        return viewState_.hoveredTileY;
    }

    const double normalizedY = (static_cast<double>(std::max(1, viewState_.framebufferHeight)) - viewState_.mouseY) / static_cast<double>(std::max(1, viewState_.framebufferHeight));
    const int tileY = viewState_.cameraY + static_cast<int>(normalizedY * viewState_.visibleTiles);
    return std::max(0, std::min(tileY, gameSession_.runtime().mapHeight() - 1));
}

// Updates query UI state and selected route overlays for the hovered tile. The
// console readout is optional because this function is part of normal query-tool
// gameplay, not just a debug command.
void AppController::printQueryResult() {
    const int tileX = hoveredTileX();
    const int tileY = hoveredTileY();
    const TileQueryResult queryResult = gameSession_.runtime().queryTile(tileX, tileY);
    const bool printQueryDebug = appConfig_.debug.printQueryValuesToConsole;
    if (!queryResult.isValid) {
        ClearQuerySelection(viewState_);
        ++viewState_.queryRouteRevision;
        if (printQueryDebug) {
            std::cout << "Query tool result: invalid tile selection." << std::endl;
        }
        return;
    }

    if (printQueryDebug) {
        std::cout << "Query tool result: tile " << tileX << "x " << tileY << "y has land value " << queryResult.tile.landValue
                  << " and air pollution " << queryResult.tile.airPollution << " at generation " << queryResult.generation;
    }

    if (queryResult.hasLot) {
        viewState_.querySelectionKind = QuerySelectionKind::Lot;
        viewState_.queriedLotId = queryResult.lotId;
        viewState_.queriedTileX = tileX;
        viewState_.queriedTileY = tileY;
        viewState_.queriedLotRevision = queryResult.lotRevision;
        viewState_.queriedCommuteRevision = queryResult.commuteRevision;
        viewState_.queriedCommuteRouteSegments = queryResult.commuteRouteSegments;
        viewState_.queryWindowLines = BuildLotQueryWindowLines(queryResult);
        ++viewState_.queryRouteRevision;
        if (printQueryDebug) {
            std::cout << " and belongs to lot #" << queryResult.lotId
                << " (" << queryResult.lotAssetId << ") with modules: " << queryResult.moduleSummary
                << " parameters: " << queryResult.parameterSummary
                << " commute=" << queryResult.commuteSatisfied << "/" << queryResult.commuteDemand;
            if (queryResult.lotIsEmpty && queryResult.hasRciLot) {
                std::cout << " rci=" << queryResult.rciName;
            }
        }
    } else if (!queryResult.roads.empty()) {
        viewState_.querySelectionKind = QuerySelectionKind::Road;
        viewState_.queriedLotId = -1;
        viewState_.queriedTileX = tileX;
        viewState_.queriedTileY = tileY;
        viewState_.queriedLotRevision = queryResult.lotRevision;
        viewState_.queriedCommuteRevision = queryResult.commuteRevision;
        viewState_.queriedCommuteRouteSegments = queryResult.roadCommuteSegments;
        viewState_.queryWindowLines = BuildRoadQueryWindowLines(queryResult);
        ++viewState_.queryRouteRevision;
        if (printQueryDebug) {
            std::cout << " with " << queryResult.roadCommuteSegments.size() << " commuter route segment(s) through its lanes";
        }
    } else if (queryResult.hasRciLot) {
        viewState_.querySelectionKind = QuerySelectionKind::Rci;
        viewState_.queriedLotId = -1;
        viewState_.queriedTileX = tileX;
        viewState_.queriedTileY = tileY;
        viewState_.queriedLotRevision = queryResult.lotRevision;
        viewState_.queriedCommuteRevision = queryResult.commuteRevision;
        viewState_.queriedCommuteRouteSegments.clear();
        viewState_.queryWindowLines = BuildRciQueryWindowLines(queryResult);
        ++viewState_.queryRouteRevision;
        if (printQueryDebug) {
            std::cout << " and belongs to empty RCI lot " << queryResult.rciName;
        }
    } else {
        ClearQuerySelection(viewState_);
        ++viewState_.queryRouteRevision;
    }

    if (printQueryDebug && !queryResult.roads.empty()) {
        std::size_t roadIndex = 0;
        for (; roadIndex < queryResult.roads.size(); ++roadIndex) {
            const ResolvedRoadCell& roadCell = queryResult.roads[roadIndex];
            std::cout
                << " | road[" << TransportLayerName(queryResult.roadLayers[roadIndex]) << "] family=" << RoadFamilyName(static_cast<RoadFamily>(roadCell.family))
                << " lanes=" << static_cast<int>(roadCell.laneCount)
                << " laneTypes=" << static_cast<int>(roadCell.laneTypeMask)
                << " surfaces=" << static_cast<int>(roadCell.surfaceMask)
                << " travel=" << DirectionMaskToString(roadCell.travelMask)
                << " exits=" << DirectionMaskToString(roadCell.exitMask)
                << " laneGraphics=" << DirectionMaskToString(roadCell.surfaceEdgeMask)
                << " junction=" << DirectionMaskToString(roadCell.junctionMask)
                << " variant=" << RoadRenderVariantName(static_cast<RoadRenderVariant>(roadCell.renderVariant))
                << " costs(car/ped/bike/bus)="
                << roadCell.laneTypeCosts[static_cast<std::size_t>(RoadLaneTypeId::Car)] << "/"
                << roadCell.laneTypeCosts[static_cast<std::size_t>(RoadLaneTypeId::Pedestrian)] << "/"
                << roadCell.laneTypeCosts[static_cast<std::size_t>(RoadLaneTypeId::Bike)] << "/"
                << roadCell.laneTypeCosts[static_cast<std::size_t>(RoadLaneTypeId::Bus)];
        }
    }

    if (printQueryDebug) {
        std::cout << std::endl;
    }
}
