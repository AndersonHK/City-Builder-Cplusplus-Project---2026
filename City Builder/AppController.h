#pragma once

#include "SimulationRuntime.h"

enum class ActiveTool {
    PollutionBrush,
    SmokestackLot,
    ParkLot,
    AddSmokestackModule,
    AddParkModule,
    RemoveModule,
    Query
};

struct ViewState {
    int cameraX;
    int cameraY;
    int visibleTiles;
    int framebufferWidth;
    int framebufferHeight;
    int hoveredTileX;
    int hoveredTileY;
    bool hasHoveredTile;
    double mouseX;
    double mouseY;
    ActiveTool activeTool;

    ViewState()
        : cameraX(0),
          cameraY(0),
          visibleTiles(256),
          framebufferWidth(2048),
          framebufferHeight(2048),
          hoveredTileX(0),
          hoveredTileY(0),
          hasHoveredTile(false),
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
    void onLeftMouseButtonHeld();
    void onKeyPressed(int key, int action);
    void onScroll(double yOffset);
    void setFramebufferSize(int framebufferWidth, int framebufferHeight);
    void setHoveredTile(int tileX, int tileY, bool isValid);

    ViewState viewState() const;

private:
    int hoveredTileX() const;
    int hoveredTileY() const;
    void clampCameraToMap();
    void panCamera(int deltaX, int deltaY);
    void printQueryResult() const;

    static const int kMinimumVisibleTiles = 128;
    static const int kMaximumVisibleTiles = 512;

    SimulationRuntime& simulationRuntime_;
    ViewState viewState_;
};
