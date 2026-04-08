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
const int kKeyA = 65;
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

        case ActiveTool::Query:
            printQueryResult();
            break;
    }
}

void AppController::onKeyPressed(int key, int action) {
    if (action != kKeyActionPress && action != kKeyActionRepeat) {
        return;
    }

    const int cameraStep = std::max(1, viewState_.visibleTiles / kMinimumVisibleTiles);

    switch (key) {
        case kKeyRight:
            viewState_.cameraX = std::min(viewState_.cameraX + cameraStep, simulationRuntime_.mapWidth() - viewState_.visibleTiles);
            return;

        case kKeyLeft:
            viewState_.cameraX = std::max(0, viewState_.cameraX - cameraStep);
            return;

        case kKeyDown:
            viewState_.cameraY = std::max(0, viewState_.cameraY - cameraStep);
            return;

        case kKeyUp:
            viewState_.cameraY = std::min(viewState_.cameraY + cameraStep, simulationRuntime_.mapHeight() - viewState_.visibleTiles);
            return;

        case kKeyQ:
            viewState_.activeTool = ActiveTool::PollutionBrush;
            std::cout << "Selected tool: pollution brush" << std::endl;
            return;

        case kKeyW:
            viewState_.activeTool = ActiveTool::SmokestackLot;
            std::cout << "Selected tool: smokestack lot" << std::endl;
            return;

        case kKeyE:
            viewState_.activeTool = ActiveTool::ParkLot;
            std::cout << "Selected tool: 2x2 park lot" << std::endl;
            return;

        case kKeyA:
            viewState_.activeTool = ActiveTool::Query;
            std::cout << "Selected tool: query" << std::endl;
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

    viewState_.cameraX = std::max(0, std::min(viewState_.cameraX, simulationRuntime_.mapWidth() - viewState_.visibleTiles));
    viewState_.cameraY = std::max(0, std::min(viewState_.cameraY, simulationRuntime_.mapHeight() - viewState_.visibleTiles));

    std::cout << "Zoom tiles visible: " << viewState_.visibleTiles << " camera at " << viewState_.cameraX << ", " << viewState_.cameraY << std::endl;
}

ViewState AppController::viewState() const {
    return viewState_;
}

int AppController::hoveredTileX() const {
    const double normalizedX = viewState_.mouseX / static_cast<double>(kWindowWidth);
    const int tileX = viewState_.cameraX + static_cast<int>(normalizedX * viewState_.visibleTiles);
    return std::max(0, std::min(tileX, simulationRuntime_.mapWidth() - 1));
}

int AppController::hoveredTileY() const {
    const double normalizedY = (static_cast<double>(kWindowHeight) - viewState_.mouseY) / static_cast<double>(kWindowHeight);
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
              << " and air pollution " << queryResult.tile.airPollution << " at generation " << queryResult.generation << std::endl;
}
