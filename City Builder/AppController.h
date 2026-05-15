#pragma once

#include <string>

#include "SimulationRuntime.h"

enum class ActiveTool {
    PollutionBrush,
    SmokestackLot,
    ParkLot,
    FactoryLot,
    HouseLot,
    RoadStreet,
    RoadHighway,
    AddParkModule,
    RemoveModule,
    Query
};

enum class OverlayMode {
    None,
    TrafficCapacity
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
    bool roadDragActive;
    int roadDragStartX;
    int roadDragStartY;
    int roadDragCurrentX;
    int roadDragCurrentY;
    int roadLaneCount;
    RoadTrafficSide roadTrafficSide;
    RoadDirectionMode roadDirectionMode;
    int lotRotationSteps;
    OverlayMode overlayMode;
    int queriedLotId;
    std::uint64_t queryRouteRevision;
    std::vector<CommuteRouteSegment> queriedCommuteRouteSegments;

    // Initializes the default camera span and active tool for a new session.
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
          activeTool(ActiveTool::PollutionBrush),
          roadDragActive(false),
          roadDragStartX(0),
          roadDragStartY(0),
          roadDragCurrentX(0),
          roadDragCurrentY(0),
          roadLaneCount(1),
          roadTrafficSide(RoadTrafficSide::RightHand),
          roadDirectionMode(RoadDirectionMode::TwoWay),
          lotRotationSteps(0),
          overlayMode(OverlayMode::None),
          queriedLotId(-1),
          queryRouteRevision(0) {
    }
};

class AppController {
public:
    explicit AppController(SimulationRuntime& simulationRuntime);

    void onCursorMoved(double mouseX, double mouseY);
    void onLeftMouseButtonPressed();
    void onLeftMouseButtonReleased();
    void onLeftMouseButtonHeld();
    void onKeyPressed(int key, int action);
    void onScroll(double yOffset);
    void setFramebufferSize(int framebufferWidth, int framebufferHeight);
    void setHoveredTile(int tileX, int tileY, bool isValid);
    bool roadPreviewStroke(RoadStrokeCommand& roadStrokeCommand) const;
    bool lotPreviewRequest(std::string& lotAssetId, int& tileX, int& tileY, int& rotationSteps) const;

    ViewState viewState() const;

private:
    int hoveredTileX() const;
    int hoveredTileY() const;
    void clampCameraToMap();
    void panCamera(int deltaX, int deltaY);
    void setActiveTool(ActiveTool activeTool);
    void toggleTrafficOverlay();
    void rotatePlacement(int deltaSteps);
    bool activeToolIsRoad() const;
    void beginRoadDrag(int tileX, int tileY);
    void commitRoadDrag(int tileX, int tileY);
    RoadTemplate currentRoadTemplate(RoadFamily family, TransportLayerId layer) const;
    void printRoadTemplate() const;
    void printQueryResult();

    static const int kMinimumVisibleTiles = 32;
    static const int kMaximumVisibleTiles = 512;

    SimulationRuntime& simulationRuntime_;
    ViewState viewState_;
};
