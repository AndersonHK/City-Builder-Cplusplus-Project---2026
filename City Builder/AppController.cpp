#include "AppController.h"

#include <algorithm>
#include <iostream>

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
const int kKeyT = 84;
const int kKeyY = 89;
const int kKeyA = 65;

const char* ActiveToolName(ActiveTool activeTool) {
    switch (activeTool) {
        case ActiveTool::PollutionBrush:
            return "pollution brush";

        case ActiveTool::SmokestackLot:
            return "place smokestack lot";

        case ActiveTool::ParkLot:
            return "place park lot";

        case ActiveTool::AddSmokestackModule:
            return "add smokestack module";

        case ActiveTool::AddParkModule:
            return "add park module";

        case ActiveTool::RemoveModule:
            return "remove module";

        case ActiveTool::Query:
            return "query";
    }

    return "unknown";
}
}

AppController::AppController(SimulationRuntime& simulationRuntime)
    : simulationRuntime_(simulationRuntime) {
}

void AppController::onCursorMoved(double mouseX, double mouseY) {
    viewState_.mouseX = mouseX;
    viewState_.mouseY = mouseY;
}

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

        case ActiveTool::AddSmokestackModule:
            simulationRuntime_.queueAddSmokestackModule(tileX, tileY);
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

void AppController::onLeftMouseButtonHeld() {
    if (viewState_.activeTool != ActiveTool::PollutionBrush) {
        return;
    }

    onLeftMouseButtonPressed();
}

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
            setActiveTool(ActiveTool::AddSmokestackModule);
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
    }
}

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

void AppController::setFramebufferSize(int framebufferWidth, int framebufferHeight) {
    viewState_.framebufferWidth = std::max(1, framebufferWidth);
    viewState_.framebufferHeight = std::max(1, framebufferHeight);
}

void AppController::setHoveredTile(int tileX, int tileY, bool isValid) {
    viewState_.hasHoveredTile = isValid;
    if (!isValid) {
        return;
    }

    viewState_.hoveredTileX = std::max(0, std::min(tileX, simulationRuntime_.mapWidth() - 1));
    viewState_.hoveredTileY = std::max(0, std::min(tileY, simulationRuntime_.mapHeight() - 1));
}

void AppController::clampCameraToMap() {
    viewState_.cameraX = std::max(0, std::min(viewState_.cameraX, simulationRuntime_.mapWidth() - viewState_.visibleTiles));
    viewState_.cameraY = std::max(0, std::min(viewState_.cameraY, simulationRuntime_.mapHeight() - viewState_.visibleTiles));
}

void AppController::panCamera(int deltaX, int deltaY) {
    viewState_.cameraX += deltaX;
    viewState_.cameraY += deltaY;
    clampCameraToMap();
}

void AppController::setActiveTool(ActiveTool activeTool) {
    viewState_.activeTool = activeTool;
    std::cout << "Selected tool: " << ActiveToolName(activeTool) << std::endl;
}

ViewState AppController::viewState() const {
    return viewState_;
}

int AppController::hoveredTileX() const {
    if (viewState_.hasHoveredTile) {
        return viewState_.hoveredTileX;
    }

    const double normalizedX = viewState_.mouseX / static_cast<double>(std::max(1, viewState_.framebufferWidth));
    const int tileX = viewState_.cameraX + static_cast<int>(normalizedX * viewState_.visibleTiles);
    return std::max(0, std::min(tileX, simulationRuntime_.mapWidth() - 1));
}

int AppController::hoveredTileY() const {
    if (viewState_.hasHoveredTile) {
        return viewState_.hoveredTileY;
    }

    const double normalizedY = (static_cast<double>(std::max(1, viewState_.framebufferHeight)) - viewState_.mouseY) / static_cast<double>(std::max(1, viewState_.framebufferHeight));
    const int tileY = viewState_.cameraY + static_cast<int>(normalizedY * viewState_.visibleTiles);
    return std::max(0, std::min(tileY, simulationRuntime_.mapHeight() - 1));
}

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

    std::cout << std::endl;
}
