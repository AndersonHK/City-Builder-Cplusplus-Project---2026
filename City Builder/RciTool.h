#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "RoadTemplate.h"

struct RciColor {
    float r;
    float g;
    float b;
    float a;

    RciColor();
    RciColor(float red, float green, float blue, float alpha);
};

struct RciRect {
    int minTileX;
    int minTileY;
    int maxTileX;
    int maxTileY;

    RciRect();
    RciRect(int minX, int minY, int maxX, int maxY);

    int width() const;
    int height() const;
    bool isValid() const;
    bool intersects(const RciRect& other) const;
};

struct RciRoadPlan {
    int startTileX;
    int startTileY;
    int endTileX;
    int endTileY;

    RciRoadPlan();
    RciRoadPlan(int startX, int startY, int endX, int endY);
};

RoadStrokeCommand BuildRciRoadStrokeCommand(const RciRoadPlan& roadPlan);

enum class RciPlanMode {
    LotsAndRoads,
    Lots,
    Area
};

struct RciPlanningContext {
    int mapWidth;
    int mapHeight;
    RciRect bounds;
    RciPlanMode mode;
    std::vector<std::uint8_t> paintableTiles;
    std::vector<std::uint8_t> groundRoadTiles;
    std::vector<std::uint8_t> groundRoadAxisMasks;

    RciPlanningContext();
};

struct RciLot {
    std::string toolId;
    std::string name;
    std::uint16_t zoningType;
    std::uint8_t frontDirection;
    RciColor color;
    RciRect rect;
    std::uint64_t availableAfterTick;

    RciLot();
};

struct RciPlan {
    std::string toolId;
    std::string name;
    std::uint16_t zoningType;
    RciColor color;
    RciPlanMode mode;
    RciRect bounds;
    std::vector<RciRect> paintRects;
    std::vector<RciLot> lots;
    std::vector<RciRect> zoneRects;
    std::vector<RciRoadPlan> roadPlans;

    RciPlan();
};

class RciTool {
public:
    RciTool();

    const std::string& id() const;
    const std::string& name() const;
    const std::string& labelStringId() const;
    const std::string& desirabilityOverlayStringId() const;
    const RciColor& color() const;
    std::uint16_t zoningType() const;
    int minDepth() const;
    int preferredDepth() const;
    int maxDepth() const;
    int minWidth() const;
    int preferredWidth() const;
    int maxWidth() const;

    void setDefinition(
        const std::string& id,
        const std::string& name,
        const RciColor& color,
        std::uint16_t zoningType,
        int minDepth,
        int preferredDepth,
        int maxDepth,
        int minWidth,
        int preferredWidth,
        int maxWidth,
        const std::string& labelStringId = std::string(),
        const std::string& desirabilityOverlayStringId = std::string());

    bool buildPlan(
        int startTileX,
        int startTileY,
        int endTileX,
        int endTileY,
        RciPlanMode mode,
        int mapWidth,
        int mapHeight,
        RciPlan& plan) const;
    bool buildPlan(const RciPlanningContext& context, RciPlan& plan) const;

private:
    bool buildAreaPlan(const RciRect& bounds, RciPlan& plan) const;
    bool buildLotsPlan(const RciRect& bounds, RciPlan& plan) const;
    bool buildLotsAndRoadsPlan(const RciRect& bounds, RciPlan& plan) const;
    RciLot constructLot(const RciRect& rect) const;

    std::string id_;
    std::string name_;
    std::string labelStringId_;
    std::string desirabilityOverlayStringId_;
    RciColor color_;
    std::uint16_t zoningType_;
    int minDepth_;
    int preferredDepth_;
    int maxDepth_;
    int minWidth_;
    int preferredWidth_;
    int maxWidth_;
};

class RciType {
public:
    RciType();

    const std::string& id() const;
    const std::string& name() const;
    const std::string& desirabilityOverlayStringId() const;
    const std::string& demandParameterId() const;
    const RciColor& color() const;
    const std::vector<std::uint16_t>& allowedZoningTypes() const;
    bool allowsZoningType(std::uint16_t zoningType) const;

    void setDefinition(
        const std::string& id,
        const std::string& name,
        const std::string& desirabilityOverlayStringId,
        const std::string& demandParameterId,
        const RciColor& color,
        const std::vector<std::uint16_t>& allowedZoningTypes);

private:
    std::string id_;
    std::string name_;
    std::string desirabilityOverlayStringId_;
    std::string demandParameterId_;
    RciColor color_;
    std::vector<std::uint16_t> allowedZoningTypes_;
};

class RciToolCatalog {
public:
    RciToolCatalog();

    bool loadFromXmlFile(const std::string& filePath);
    const std::vector<RciTool>& tools() const;
    const RciTool* findTool(const std::string& id) const;
    const std::vector<RciType>& rciTypes() const;
    const RciType* findRciType(const std::string& id) const;

private:
    std::vector<RciTool> tools_;
    std::vector<RciType> rciTypes_;
};
