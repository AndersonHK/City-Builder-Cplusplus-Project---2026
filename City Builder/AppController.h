#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "GameSession.h"

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
    Bulldozer,
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
    bool bulldozeDragActive;
    int bulldozeDragStartX;
    int bulldozeDragStartY;
    int bulldozeDragCurrentX;
    int bulldozeDragCurrentY;
    int roadLaneCount;
    RoadTrafficSide roadTrafficSide;
    RoadDirectionMode roadDirectionMode;
    int lotRotationSteps;
    OverlayMode overlayMode;
    bool roadDebugGraphicsEnabled;
    int queriedLotId;
    int queriedTileX;
    int queriedTileY;
    std::uint64_t queriedLotRevision;
    std::uint64_t queriedCommuteRevision;
    std::uint64_t queryRouteRevision;
    std::vector<CommuteRouteSegment> queriedCommuteRouteSegments;
    std::vector<std::string> queryWindowLines;
    int hoveredRegionX;
    int hoveredRegionY;
    bool hasHoveredRegion;

    // Initializes the default camera span and active tool for a new session.
    ViewState()
        : cameraX(384),
          cameraY(384),
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
          bulldozeDragActive(false),
          bulldozeDragStartX(0),
          bulldozeDragStartY(0),
          bulldozeDragCurrentX(0),
          bulldozeDragCurrentY(0),
          roadLaneCount(1),
          roadTrafficSide(RoadTrafficSide::RightHand),
          roadDirectionMode(RoadDirectionMode::TwoWay),
          lotRotationSteps(0),
          overlayMode(OverlayMode::None),
          roadDebugGraphicsEnabled(false),
          queriedLotId(-1),
          queriedTileX(0),
          queriedTileY(0),
          queriedLotRevision(0),
          queriedCommuteRevision(0),
          queryRouteRevision(0),
          hoveredRegionX(0),
          hoveredRegionY(0),
          hasHoveredRegion(false) {
    }
};

class AppController {
public:
    explicit AppController(GameSession& gameSession);

    void onCursorMoved(double mouseX, double mouseY);
    void onLeftMouseButtonPressed();
    void onLeftMouseButtonReleased();
    void onLeftMouseButtonHeld();
    void onKeyPressed(int key, int action);
    void onScroll(double yOffset);
    void setFramebufferSize(int framebufferWidth, int framebufferHeight);
    void setHoveredTile(int tileX, int tileY, bool isValid);
    void setHoveredRegionCity(int regionX, int regionY, bool isValid);
    bool roadPreviewStroke(RoadStrokeCommand& roadStrokeCommand) const;
    bool lotPreviewRequest(std::string& lotAssetId, int& tileX, int& tileY, int& rotationSteps) const;
    bool bulldozePreviewRect(int& minTileX, int& minTileY, int& maxTileX, int& maxTileY) const;
    void refreshQueryResultIfNeeded();
    void processPendingRegionClick();

    ViewState viewState() const;

private:
    int hoveredTileX() const;
    int hoveredTileY() const;
    void clampCameraToMap();
    void panCamera(int deltaX, int deltaY);
    void setActiveTool(ActiveTool activeTool);
    void toggleTrafficOverlay();
    void toggleRoadDebugGraphics();
    void rotatePlacement(int deltaSteps);
    bool activeToolIsRoad() const;
    bool handleRegionClick();
    void applyCameraFromActiveCity();
    void syncActiveCityCameraToSession();
    void beginRoadDrag(int tileX, int tileY);
    void commitRoadDrag(int tileX, int tileY);
    void beginBulldozeDrag(int tileX, int tileY);
    void commitBulldozeDrag(int tileX, int tileY);
    RoadTemplate currentRoadTemplate(RoadFamily family, TransportLayerId layer) const;
    void printRoadTemplate() const;
    void printQueryResult();

    static const int kMinimumVisibleTiles = 32;
    static const int kMaximumVisibleTiles = 2048;

    GameSession& gameSession_;
    ViewState viewState_;
    bool regionClickPending_;
    bool hasLastRegionClick_;
    int lastRegionClickX_;
    int lastRegionClickY_;
    std::chrono::steady_clock::time_point lastRegionClickTime_;
};
