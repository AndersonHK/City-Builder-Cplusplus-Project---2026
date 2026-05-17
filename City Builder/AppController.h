#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "AppConfig.h"
#include "GameSession.h"
#include "RciTool.h"
#include "Tile.h"
#include "UiWidgets.h"

enum class ActiveTool {
    PollutionBrush,
    SmokestackLot,
    ParkLot,
    FactoryLot,
    HouseLot,
    RoadStreet,
    RoadRoad,
    RoadAvenue,
    RoadHighway,
    AddParkModule,
    RemoveModule,
    Bulldozer,
    Query,
    ZoneResidential,
    ZoneIndustrial,
    ZoneUnzone
};

enum class OverlayMode {
    None,
    TrafficCapacity,
    LandValue
};

enum class QuerySelectionKind {
    None,
    Lot,
    Road,
    Rci
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
    bool zoneDragActive;
    int zoneDragStartX;
    int zoneDragStartY;
    int zoneDragCurrentX;
    int zoneDragCurrentY;
    std::uint16_t zoneDragType;
    std::string zoneDragToolId;
    bool shiftModifierDown;
    bool controlModifierDown;
    int roadLaneCount;
    RoadTrafficSide roadTrafficSide;
    RoadDirectionMode roadDirectionMode;
    int lotRotationSteps;
    OverlayMode overlayMode;
    bool roadDebugGraphicsEnabled;
    QuerySelectionKind querySelectionKind;
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
          zoneDragActive(false),
          zoneDragStartX(0),
          zoneDragStartY(0),
          zoneDragCurrentX(0),
          zoneDragCurrentY(0),
          zoneDragType(TileZoningNone),
          shiftModifierDown(false),
          controlModifierDown(false),
          roadLaneCount(1),
          roadTrafficSide(RoadTrafficSide::RightHand),
          roadDirectionMode(RoadDirectionMode::TwoWay),
          lotRotationSteps(0),
          overlayMode(OverlayMode::None),
          roadDebugGraphicsEnabled(false),
          querySelectionKind(QuerySelectionKind::None),
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
    AppController(GameSession& gameSession, const AppConfig& appConfig);

    void onCursorMoved(double mouseX, double mouseY);
    void onLeftMouseButtonPressed();
    void onLeftMouseButtonReleased();
    void onLeftMouseButtonHeld();
    void onKeyPressed(int key, int action);
    void onScroll(double yOffset);
    void setFramebufferSize(int framebufferWidth, int framebufferHeight);
    void setModifierKeys(bool shiftDown, bool controlDown);
    void setHoveredTile(int tileX, int tileY, bool isValid);
    void setHoveredRegionCity(int regionX, int regionY, bool isValid);
    bool roadPreviewStroke(RoadStrokeCommand& roadStrokeCommand) const;
    bool lotPreviewRequest(std::string& lotAssetId, int& tileX, int& tileY, int& rotationSteps) const;
    bool bulldozePreviewRect(int& minTileX, int& minTileY, int& maxTileX, int& maxTileY) const;
    bool zonePreviewRect(int& minTileX, int& minTileY, int& maxTileX, int& maxTileY, std::uint16_t& zoningType) const;
    bool rciPreviewPlan(RciPlan& plan) const;
    bool loadUiLayoutFromXmlFile(const std::string& filePath);
    bool loadRciToolsFromXmlFile(const std::string& filePath);
    const UiLayout& uiLayout() const;
    std::string activeUiAction() const;
    std::vector<std::string> activeUiActions() const;
    bool quitRequested() const;
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
    void toggleLandValueOverlay();
    void toggleRoadDebugGraphics();
    void setGameSpeed(GameSpeed gameSpeed);
    bool modalMenuOpen() const;
    void clearTransientInteractions();
    void toggleEscapeMenu();
    void rotatePlacement(int deltaSteps);
    bool activeToolIsRoad() const;
    bool activeToolIsZoning() const;
    std::string activeRciToolId() const;
    RciPlanMode currentRciPlanMode() const;
    bool buildActiveRciPlan(RciPlan& plan) const;
    bool handleUiClick();
    bool handleUiClickForMenus(const std::vector<std::string>& menuIds);
    void invokeUiAction(const std::string& action);
    bool handleRegionClick();
    bool enterPendingRegionCity();
    void applyCameraFromActiveCity();
    void syncActiveCityCameraToSession();
    void beginRoadDrag(int tileX, int tileY);
    void commitRoadDrag(int tileX, int tileY);
    void beginBulldozeDrag(int tileX, int tileY);
    void commitBulldozeDrag(int tileX, int tileY);
    void beginZoneDrag(int tileX, int tileY);
    void commitZoneDrag(int tileX, int tileY);
    RoadTemplate currentRoadTemplate(RoadFamily family, TransportLayerId layer) const;
    void printRoadTemplate() const;
    void printQueryResult();

    static const int kMinimumVisibleTiles = 32;
    static const int kMaximumVisibleTiles = 2048;

    GameSession& gameSession_;
    const AppConfig& appConfig_;
    ViewState viewState_;
    UiLayout uiLayout_;
    RciToolCatalog rciTools_;
    bool uiPressCaptured_;
    bool regionClickPending_;
    bool quitRequested_;
    bool pendingRegionEnter_;
    int pendingRegionEnterX_;
    int pendingRegionEnterY_;
    bool hasLastRegionClick_;
    int lastRegionClickX_;
    int lastRegionClickY_;
    std::chrono::steady_clock::time_point lastRegionClickTime_;
};
