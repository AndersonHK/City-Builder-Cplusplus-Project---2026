#include "AppController.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

namespace {
const int kKeyActionPress = 1;
const int kKeyActionRepeat = 2;
const int kKeyRight = 262;
const int kKeyLeft = 263;
const int kKeyDown = 264;
const int kKeyUp = 265;
const int kKeyQ = 81;
const int kKeyW = 87;
const int kKeyE = 69;
const int kKeyR = 82;
const int kKeyO = 79;
const int kKeyH = 72;
const int kKeyT = 84;
const int kKeyY = 89;
const int kKeyA = 65;
const int kKeyC = 67;
const int kKeyLeftBracket = 91;
const int kKeyRightBracket = 93;
const int kMinimumRoadLaneCount = 1;
const int kMaximumRoadLaneCount = 4;

// Returns the human-readable label used when the active tool changes.
const char* ActiveToolName(ActiveTool activeTool) {
    switch (activeTool) {
        case ActiveTool::PollutionBrush:
            return "pollution brush";

        case ActiveTool::SmokestackLot:
            return "place smokestack lot";

        case ActiveTool::ParkLot:
            return "place park lot";

        case ActiveTool::RoadStreet:
            return "place local street";

        case ActiveTool::RoadHighway:
            return "place elevated highway";

        case ActiveTool::AddParkModule:
            return "add park module";

        case ActiveTool::RemoveModule:
            return "remove module";

        case ActiveTool::Query:
            return "query";
    }

    return "unknown";
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
}

// Connects input intent to the simulation command queue.
AppController::AppController(SimulationRuntime& simulationRuntime)
    : simulationRuntime_(simulationRuntime) {
}

// Records cursor position for picking and drag tools.
void AppController::onCursorMoved(double mouseX, double mouseY) {
    viewState_.mouseX = mouseX;
    viewState_.mouseY = mouseY;
}

// Dispatches the currently selected tool's primary click action.
void AppController::onLeftMouseButtonPressed() {
    const int tileX = hoveredTileX();
    const int tileY = hoveredTileY();

    switch (viewState_.activeTool) {
        case ActiveTool::PollutionBrush:
            simulationRuntime_.queuePaintPollution(tileX, tileY, 16000000);
            break;

        case ActiveTool::SmokestackLot:
            simulationRuntime_.queuePlaceSmokestack(tileX, tileY);
            break;

        case ActiveTool::ParkLot:
            simulationRuntime_.queuePlacePark(tileX, tileY);
            break;

        case ActiveTool::RoadStreet:
        case ActiveTool::RoadHighway:
            beginRoadDrag(tileX, tileY);
            break;

        case ActiveTool::AddParkModule:
            simulationRuntime_.queueAddParkModule(tileX, tileY);
            break;

        case ActiveTool::RemoveModule:
            simulationRuntime_.queueRemoveModuleAtTile(tileX, tileY);
            break;

        case ActiveTool::Query:
            printQueryResult();
            break;
    }
}

// Commits any active road drag when the mouse button is released.
void AppController::onLeftMouseButtonReleased() {
    if (!viewState_.roadDragActive || !activeToolIsRoad()) {
        return;
    }

    commitRoadDrag(hoveredTileX(), hoveredTileY());
}

// Handles continuous actions while the primary mouse button remains down.
void AppController::onLeftMouseButtonHeld() {
    if (viewState_.activeTool == ActiveTool::PollutionBrush) {
        onLeftMouseButtonPressed();
        return;
    }

    if (viewState_.roadDragActive && activeToolIsRoad()) {
        viewState_.roadDragCurrentX = hoveredTileX();
        viewState_.roadDragCurrentY = hoveredTileY();
    }
}

// Maps keyboard input to camera movement, tool selection, and queries.
void AppController::onKeyPressed(int key, int action) {
    if (action != kKeyActionPress && action != kKeyActionRepeat) {
        return;
    }

    const int cameraStep = std::max(1, viewState_.visibleTiles / kMinimumVisibleTiles);

    switch (key) {
        case kKeyRight:
            panCamera(-cameraStep, -cameraStep);
            return;

        case kKeyLeft:
            panCamera(cameraStep, cameraStep);
            return;

        case kKeyDown:
            panCamera(cameraStep, -cameraStep);
            return;

        case kKeyUp:
            panCamera(-cameraStep, cameraStep);
            return;

        case kKeyQ:
            setActiveTool(ActiveTool::PollutionBrush);
            return;

        case kKeyW:
            setActiveTool(ActiveTool::SmokestackLot);
            return;

        case kKeyE:
            setActiveTool(ActiveTool::ParkLot);
            return;

        case kKeyR:
            setActiveTool(ActiveTool::RoadStreet);
            printRoadTemplate();
            return;

        case kKeyH:
            setActiveTool(ActiveTool::RoadHighway);
            printRoadTemplate();
            return;

        case kKeyT:
            setActiveTool(ActiveTool::AddParkModule);
            return;

        case kKeyY:
            setActiveTool(ActiveTool::RemoveModule);
            return;

        case kKeyA:
            setActiveTool(ActiveTool::Query);
            return;

        case kKeyLeftBracket:
            viewState_.roadLaneCount = std::max(kMinimumRoadLaneCount, viewState_.roadLaneCount - 1);
            printRoadTemplate();
            return;

        case kKeyRightBracket:
            viewState_.roadLaneCount = std::min(kMaximumRoadLaneCount, viewState_.roadLaneCount + 1);
            printRoadTemplate();
            return;

        case kKeyC:
            viewState_.roadTrafficSide = viewState_.roadTrafficSide == RoadTrafficSide::RightHand ? RoadTrafficSide::LeftHand : RoadTrafficSide::RightHand;
            printRoadTemplate();
            return;

        case kKeyO:
            if (viewState_.roadDirectionMode == RoadDirectionMode::TwoWay) {
                viewState_.roadDirectionMode = RoadDirectionMode::OneWayForward;
            } else if (viewState_.roadDirectionMode == RoadDirectionMode::OneWayForward) {
                viewState_.roadDirectionMode = RoadDirectionMode::OneWayReverse;
            } else {
                viewState_.roadDirectionMode = RoadDirectionMode::TwoWay;
            }
            printRoadTemplate();
            return;
    }
}

// Changes zoom in fixed tile-span steps while keeping the camera centered.
void AppController::onScroll(double yOffset) {
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

// Updates the renderer-provided tile under the cursor.
void AppController::setHoveredTile(int tileX, int tileY, bool isValid) {
    viewState_.hasHoveredTile = isValid;
    if (!isValid) {
        return;
    }

    viewState_.hoveredTileX = std::max(0, std::min(tileX, simulationRuntime_.mapWidth() - 1));
    viewState_.hoveredTileY = std::max(0, std::min(tileY, simulationRuntime_.mapHeight() - 1));
}

// Keeps the camera span inside the map after pan or zoom changes.
void AppController::clampCameraToMap() {
    viewState_.cameraX = std::max(0, std::min(viewState_.cameraX, simulationRuntime_.mapWidth() - viewState_.visibleTiles));
    viewState_.cameraY = std::max(0, std::min(viewState_.cameraY, simulationRuntime_.mapHeight() - viewState_.visibleTiles));
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
    std::cout << "Selected tool: " << ActiveToolName(activeTool) << std::endl;
}

// Reports whether the active tool uses the road drag workflow.
bool AppController::activeToolIsRoad() const {
    return viewState_.activeTool == ActiveTool::RoadStreet || viewState_.activeTool == ActiveTool::RoadHighway;
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
        simulationRuntime_.queuePlaceRoadStroke(roadStrokeCommand);
    } else if (viewState_.activeTool == ActiveTool::RoadHighway) {
        RoadStrokeCommand roadStrokeCommand;
        roadStrokeCommand.startTile = startTile;
        roadStrokeCommand.cornerTile = cornerTile;
        roadStrokeCommand.endTile = endTile;
        roadStrokeCommand.family = RoadFamily::Highway;
        roadStrokeCommand.layer = TransportLayerId::Elevated;
        roadStrokeCommand.roadTemplate = currentRoadTemplate(roadStrokeCommand.family, roadStrokeCommand.layer);
        simulationRuntime_.queuePlaceRoadStroke(roadStrokeCommand);
    }

    viewState_.roadDragActive = false;
}

// Builds the currently selected modular road template for a placement command.
RoadTemplate AppController::currentRoadTemplate(RoadFamily family, TransportLayerId layer) const {
    return TransportNetwork::makeRoadTemplate(family, layer, viewState_.roadLaneCount, viewState_.roadTrafficSide, viewState_.roadDirectionMode);
}

// Prints active road-template settings whenever the lightweight controls change.
void AppController::printRoadTemplate() const {
    std::cout << "Road template: lanes=" << viewState_.roadLaneCount
        << " traffic=" << RoadTrafficSideName(viewState_.roadTrafficSide)
        << " mode=" << RoadDirectionModeName(viewState_.roadDirectionMode)
        << std::endl;
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
    return std::max(0, std::min(tileX, simulationRuntime_.mapWidth() - 1));
}

// Falls back to screen-space picking only when the renderer has no raycast hit.
int AppController::hoveredTileY() const {
    if (viewState_.hasHoveredTile) {
        return viewState_.hoveredTileY;
    }

    const double normalizedY = (static_cast<double>(std::max(1, viewState_.framebufferHeight)) - viewState_.mouseY) / static_cast<double>(std::max(1, viewState_.framebufferHeight));
    const int tileY = viewState_.cameraY + static_cast<int>(normalizedY * viewState_.visibleTiles);
    return std::max(0, std::min(tileY, simulationRuntime_.mapHeight() - 1));
}

// Prints a compact debug readout for the hovered tile.
void AppController::printQueryResult() const {
    const int tileX = hoveredTileX();
    const int tileY = hoveredTileY();
    const TileQueryResult queryResult = simulationRuntime_.queryTile(tileX, tileY);
    if (!queryResult.isValid) {
        std::cout << "Query tool result: invalid tile selection." << std::endl;
        return;
    }

    std::cout << "Query tool result: tile " << tileX << "x " << tileY << "y has land value " << queryResult.tile.landValue
              << " and air pollution " << queryResult.tile.airPollution << " at generation " << queryResult.generation;

    if (queryResult.hasLot) {
        std::cout << " and belongs to lot #" << queryResult.lotId << " (" << queryResult.lotAssetId << ") with modules: " << queryResult.moduleSummary;
    }

    if (!queryResult.roads.empty()) {
        std::size_t roadIndex = 0;
        for (; roadIndex < queryResult.roads.size(); ++roadIndex) {
            const ResolvedRoadCell& roadCell = queryResult.roads[roadIndex];
            std::cout
                << " | road[" << TransportLayerName(queryResult.roadLayers[roadIndex]) << "] family=" << RoadFamilyName(static_cast<RoadFamily>(roadCell.family))
                << " lanes=" << static_cast<int>(roadCell.laneCount)
                << " elements=" << static_cast<int>(roadCell.elementMask)
                << " exits=" << DirectionMaskToString(roadCell.exitMask)
                << " sidewalks=" << DirectionMaskToString(roadCell.sidewalkMask)
                << " junction=" << DirectionMaskToString(roadCell.junctionMask)
                << " variant=" << RoadRenderVariantName(static_cast<RoadRenderVariant>(roadCell.renderVariant))
                << " carCost=" << roadCell.carCost
                << " pedCost=" << roadCell.pedestrianCost;
        }
    }

    std::cout << std::endl;
}
