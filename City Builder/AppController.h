#pragma once

#include "SimulationRuntime.h"

enum class ActiveTool {
    PollutionBrush,
    SmokestackLot,
    ParkLot,
    Query
};

struct ViewState {
    int cameraX;
    int cameraY;
    int visibleTiles;
    double mouseX;
    double mouseY;
    ActiveTool activeTool;

    ViewState()
        : cameraX(0),
          cameraY(0),
          visibleTiles(256),
          mouseX(0.0),
          mouseY(0.0),
          activeTool(ActiveTool::PollutionBrush) {
    }
};

class AppController {
public:
    explicit AppController(SimulationRuntime& simulationRuntime);

    void onCursorMoved(double mouseX, double mouseY);
    void onLeftMouseButtonPressed();
    void onKeyPressed(int key, int action);
    void onScroll(double yOffset);

    ViewState viewState() const;

private:
    int hoveredTileX() const;
    int hoveredTileY() const;
    void printQueryResult() const;

    static const int kWindowWidth = 2048;
    static const int kWindowHeight = 2048;
    static const int kMinimumVisibleTiles = 128;
    static const int kMaximumVisibleTiles = 512;

    SimulationRuntime& simulationRuntime_;
    ViewState viewState_;
};
