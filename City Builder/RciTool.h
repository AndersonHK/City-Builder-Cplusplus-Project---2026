#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

enum class RciPlanMode {
    LotsAndRoads,
    Lots,
    Area
};

struct RciLot {
    std::string toolId;
    std::string name;
    std::uint16_t zoningType;
    RciColor color;
    RciRect rect;

    RciLot();
};

struct RciPlan {
    std::string toolId;
    std::string name;
    std::uint16_t zoningType;
    RciColor color;
    RciPlanMode mode;
    RciRect bounds;
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
        int maxWidth);

    bool buildPlan(
        int startTileX,
        int startTileY,
        int endTileX,
        int endTileY,
        RciPlanMode mode,
        int mapWidth,
        int mapHeight,
        RciPlan& plan) const;

private:
    bool buildAreaPlan(const RciRect& bounds, RciPlan& plan) const;
    bool buildLotsPlan(const RciRect& bounds, RciPlan& plan) const;
    bool buildLotsAndRoadsPlan(const RciRect& bounds, RciPlan& plan) const;
    RciLot constructLot(const RciRect& rect) const;

    std::string id_;
    std::string name_;
    RciColor color_;
    std::uint16_t zoningType_;
    int minDepth_;
    int preferredDepth_;
    int maxDepth_;
    int minWidth_;
    int preferredWidth_;
    int maxWidth_;
};

class RciToolCatalog {
public:
    RciToolCatalog();

    bool loadFromXmlFile(const std::string& filePath);
    void setFallbackDefinition();
    const std::vector<RciTool>& tools() const;
    const RciTool* findTool(const std::string& id) const;

private:
    std::vector<RciTool> tools_;
};
