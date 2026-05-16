#include "RciTool.h"

#include "Tile.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {
struct Segment {
    int startOffset;
    int length;

    Segment()
        : startOffset(0),
          length(0) {
    }

    Segment(int start, int segmentLength)
        : startOffset(start),
          length(segmentLength) {
    }
};

std::string ReadFileToString(const std::string& filePath) {
    std::ifstream file(filePath.c_str(), std::ios::in | std::ios::binary);
    if (!file) {
        return std::string();
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

std::string AttributeValue(const std::string& tag, const std::string& attributeName, const std::string& fallback) {
    const std::string needle = attributeName + "=\"";
    const std::string::size_type attributeStart = tag.find(needle);
    if (attributeStart == std::string::npos) {
        return fallback;
    }

    const std::string::size_type valueStart = attributeStart + needle.size();
    const std::string::size_type valueEnd = tag.find('"', valueStart);
    if (valueEnd == std::string::npos) {
        return fallback;
    }

    return tag.substr(valueStart, valueEnd - valueStart);
}

int AttributeIntValue(const std::string& tag, const std::string& attributeName, int fallback) {
    const std::string value = AttributeValue(tag, attributeName, std::string());
    if (value.empty()) {
        return fallback;
    }

    return std::atoi(value.c_str());
}

int AttributeIntValueAny(const std::string& tag, const std::string& primaryName, const std::string& alternateName, int fallback) {
    const std::string primaryValue = AttributeValue(tag, primaryName, std::string());
    if (!primaryValue.empty()) {
        return std::atoi(primaryValue.c_str());
    }

    return AttributeIntValue(tag, alternateName, fallback);
}

float AttributeFloatValue(const std::string& tag, const std::string& attributeName, float fallback) {
    const std::string value = AttributeValue(tag, attributeName, std::string());
    if (value.empty()) {
        return fallback;
    }

    return static_cast<float>(std::atof(value.c_str()));
}

RciColor AttributeColorValue(const std::string& tag, const RciColor& fallback) {
    return RciColor(
        AttributeFloatValue(tag, "colorR", fallback.r),
        AttributeFloatValue(tag, "colorG", fallback.g),
        AttributeFloatValue(tag, "colorB", fallback.b),
        AttributeFloatValue(tag, "colorA", fallback.a));
}

std::uint16_t ZoningTypeFromText(const std::string& value) {
    if (value == "1" || value == "residential" || value == "residence" || value == "r" || value == "TileZoningResidential") {
        return TileZoningResidential;
    }

    if (value == "2" || value == "industrial" || value == "industry" || value == "i" || value == "TileZoningIndustrial") {
        return TileZoningIndustrial;
    }

    return TileZoningNone;
}

std::uint16_t ZoningTypeFromToolTag(const std::string& tag, const std::string& id) {
    const std::uint16_t explicitType = ZoningTypeFromText(AttributeValue(tag, "zoningType", std::string()));
    if (explicitType != TileZoningNone) {
        return explicitType;
    }

    return ZoningTypeFromText(id);
}

int ClampInt(int value, int minimum, int maximum) {
    return std::max(minimum, std::min(value, maximum));
}

RciRect NormalizeBounds(int startTileX, int startTileY, int endTileX, int endTileY, int mapWidth, int mapHeight) {
    if (mapWidth <= 0 || mapHeight <= 0) {
        return RciRect();
    }

    return RciRect(
        ClampInt(std::min(startTileX, endTileX), 0, mapWidth - 1),
        ClampInt(std::min(startTileY, endTileY), 0, mapHeight - 1),
        ClampInt(std::max(startTileX, endTileX), 0, mapWidth - 1),
        ClampInt(std::max(startTileY, endTileY), 0, mapHeight - 1));
}

bool PartitionSegments(int length, int minimum, int preferred, int maximum, std::vector<Segment>& segments) {
    segments.clear();
    if (length <= 0 || minimum <= 0 || maximum < minimum || length < minimum) {
        return false;
    }

    int bestCount = 0;
    int bestScore = 0;
    const int maxCount = length / minimum;
    int count = 1;
    for (; count <= maxCount; ++count) {
        const int base = length / count;
        const int extra = length % count;
        const int largest = base + (extra > 0 ? 1 : 0);
        if (base < minimum || largest > maximum) {
            continue;
        }

        const int score = (count - extra) * std::abs(base - preferred) + extra * std::abs(largest - preferred);
        if (bestCount == 0 || score < bestScore) {
            bestCount = count;
            bestScore = score;
        }
    }

    if (bestCount == 0) {
        return false;
    }

    const int base = length / bestCount;
    const int extra = length % bestCount;
    int cursor = 0;
    for (count = 0; count < bestCount; ++count) {
        const int segmentLength = base + (count < extra ? 1 : 0);
        segments.push_back(Segment(cursor, segmentLength));
        cursor += segmentLength;
    }

    return true;
}

bool PartitionBlocksWithRoads(int totalLength, int minimum, int preferred, int maximum, std::vector<Segment>& blocks, std::vector<int>& roadOffsets) {
    blocks.clear();
    roadOffsets.clear();
    if (totalLength < minimum + 2 || minimum <= 0 || maximum < minimum) {
        return false;
    }

    const int interiorLength = totalLength - 2;
    const int maxCount = std::max(1, (interiorLength + 1) / (minimum + 1));
    int bestCount = 0;
    int bestScore = 0;
    int count = 1;
    for (; count <= maxCount; ++count) {
        const int blockOnlyLength = interiorLength - (count - 1);
        if (blockOnlyLength < count * minimum) {
            continue;
        }

        const int base = blockOnlyLength / count;
        const int extra = blockOnlyLength % count;
        const int largest = base + (extra > 0 ? 1 : 0);
        if (base < minimum || largest > maximum) {
            continue;
        }

        const int score = (count - extra) * std::abs(base - preferred) + extra * std::abs(largest - preferred);
        if (bestCount == 0 || score < bestScore) {
            bestCount = count;
            bestScore = score;
        }
    }

    if (bestCount == 0) {
        return false;
    }

    roadOffsets.push_back(0);
    const int blockOnlyLength = interiorLength - (bestCount - 1);
    const int base = blockOnlyLength / bestCount;
    const int extra = blockOnlyLength % bestCount;
    int cursor = 1;
    for (count = 0; count < bestCount; ++count) {
        const int segmentLength = base + (count < extra ? 1 : 0);
        blocks.push_back(Segment(cursor, segmentLength));
        cursor += segmentLength;
        if (count + 1 < bestCount) {
            roadOffsets.push_back(cursor);
            ++cursor;
        }
    }
    roadOffsets.push_back(totalLength - 1);
    return true;
}

bool SplitBlockIntoTwoDepths(int blockDepth, int minimum, int preferred, int maximum, int& firstDepth, int& secondDepth) {
    int bestFirst = 0;
    int bestSecond = 0;
    int bestScore = 0;
    int candidateFirst = minimum;
    for (; candidateFirst <= maximum; ++candidateFirst) {
        const int candidateSecond = blockDepth - candidateFirst;
        if (candidateSecond < minimum || candidateSecond > maximum) {
            continue;
        }

        const int score = std::abs(candidateFirst - preferred) + std::abs(candidateSecond - preferred) + std::abs(candidateFirst - candidateSecond);
        if (bestFirst == 0 || score < bestScore) {
            bestFirst = candidateFirst;
            bestSecond = candidateSecond;
            bestScore = score;
        }
    }

    if (bestFirst == 0) {
        return false;
    }

    firstDepth = bestFirst;
    secondDepth = bestSecond;
    return true;
}
}

RciColor::RciColor()
    : r(0.0f),
      g(0.0f),
      b(0.0f),
      a(0.0f) {
}

RciColor::RciColor(float red, float green, float blue, float alpha)
    : r(red),
      g(green),
      b(blue),
      a(alpha) {
}

RciRect::RciRect()
    : minTileX(0),
      minTileY(0),
      maxTileX(-1),
      maxTileY(-1) {
}

RciRect::RciRect(int minX, int minY, int maxX, int maxY)
    : minTileX(minX),
      minTileY(minY),
      maxTileX(maxX),
      maxTileY(maxY) {
}

int RciRect::width() const {
    return isValid() ? maxTileX - minTileX + 1 : 0;
}

int RciRect::height() const {
    return isValid() ? maxTileY - minTileY + 1 : 0;
}

bool RciRect::isValid() const {
    return maxTileX >= minTileX && maxTileY >= minTileY;
}

bool RciRect::intersects(const RciRect& other) const {
    if (!isValid() || !other.isValid()) {
        return false;
    }

    return minTileX <= other.maxTileX &&
        maxTileX >= other.minTileX &&
        minTileY <= other.maxTileY &&
        maxTileY >= other.minTileY;
}

RciRoadPlan::RciRoadPlan()
    : startTileX(0),
      startTileY(0),
      endTileX(0),
      endTileY(0) {
}

RciRoadPlan::RciRoadPlan(int startX, int startY, int endX, int endY)
    : startTileX(startX),
      startTileY(startY),
      endTileX(endX),
      endTileY(endY) {
}

RciLot::RciLot()
    : zoningType(TileZoningNone) {
}

RciPlan::RciPlan()
    : zoningType(TileZoningNone),
      mode(RciPlanMode::Area) {
}

RciTool::RciTool()
    : color_(0.18f, 0.86f, 0.32f, 0.50f),
      zoningType_(TileZoningResidential),
      minDepth_(2),
      preferredDepth_(4),
      maxDepth_(8),
      minWidth_(2),
      preferredWidth_(16),
      maxWidth_(24) {
}

const std::string& RciTool::id() const {
    return id_;
}

const std::string& RciTool::name() const {
    return name_;
}

const RciColor& RciTool::color() const {
    return color_;
}

std::uint16_t RciTool::zoningType() const {
    return zoningType_;
}

int RciTool::minDepth() const {
    return minDepth_;
}

int RciTool::preferredDepth() const {
    return preferredDepth_;
}

int RciTool::maxDepth() const {
    return maxDepth_;
}

int RciTool::minWidth() const {
    return minWidth_;
}

int RciTool::preferredWidth() const {
    return preferredWidth_;
}

int RciTool::maxWidth() const {
    return maxWidth_;
}

void RciTool::setDefinition(
    const std::string& id,
    const std::string& name,
    const RciColor& color,
    std::uint16_t zoningType,
    int minDepth,
    int preferredDepth,
    int maxDepth,
    int minWidth,
    int preferredWidth,
    int maxWidth) {
    id_ = id;
    name_ = name.empty() ? id : name;
    color_ = color;
    zoningType_ = zoningType;

    minDepth_ = std::max(1, minDepth);
    maxDepth_ = std::max(minDepth_, maxDepth);
    preferredDepth_ = ClampInt(preferredDepth, minDepth_, maxDepth_);

    minWidth_ = std::max(1, minWidth);
    maxWidth_ = std::max(minWidth_, maxWidth);
    preferredWidth_ = ClampInt(preferredWidth, minWidth_, maxWidth_);
}

bool RciTool::buildPlan(
    int startTileX,
    int startTileY,
    int endTileX,
    int endTileY,
    RciPlanMode mode,
    int mapWidth,
    int mapHeight,
    RciPlan& plan) const {
    plan = RciPlan();
    const RciRect bounds = NormalizeBounds(startTileX, startTileY, endTileX, endTileY, mapWidth, mapHeight);
    if (!bounds.isValid() || zoningType_ == TileZoningNone) {
        return false;
    }

    plan.toolId = id_;
    plan.name = name_;
    plan.zoningType = zoningType_;
    plan.color = color_;
    plan.mode = mode;
    plan.bounds = bounds;

    if (mode == RciPlanMode::Area) {
        return buildAreaPlan(bounds, plan);
    }

    if (mode == RciPlanMode::Lots) {
        return buildLotsPlan(bounds, plan);
    }

    return buildLotsAndRoadsPlan(bounds, plan);
}

bool RciTool::buildAreaPlan(const RciRect& bounds, RciPlan& plan) const {
    plan.zoneRects.push_back(bounds);
    return true;
}

bool RciTool::buildLotsPlan(const RciRect& bounds, RciPlan& plan) const {
    std::vector<Segment> widthSegments;
    std::vector<Segment> depthSegments;
    if (!PartitionSegments(bounds.width(), minWidth_, 2, maxWidth_, widthSegments) ||
        !PartitionSegments(bounds.height(), minDepth_, preferredDepth_, maxDepth_, depthSegments)) {
        return buildAreaPlan(bounds, plan);
    }

    std::size_t depthIndex = 0;
    for (; depthIndex < depthSegments.size(); ++depthIndex) {
        std::size_t widthIndex = 0;
        for (; widthIndex < widthSegments.size(); ++widthIndex) {
            const RciRect lotRect(
                bounds.minTileX + widthSegments[widthIndex].startOffset,
                bounds.minTileY + depthSegments[depthIndex].startOffset,
                bounds.minTileX + widthSegments[widthIndex].startOffset + widthSegments[widthIndex].length - 1,
                bounds.minTileY + depthSegments[depthIndex].startOffset + depthSegments[depthIndex].length - 1);
            plan.lots.push_back(constructLot(lotRect));
            plan.zoneRects.push_back(lotRect);
        }
    }

    return !plan.lots.empty();
}

bool RciTool::buildLotsAndRoadsPlan(const RciRect& bounds, RciPlan& plan) const {
    std::vector<Segment> blockColumns;
    std::vector<Segment> blockRows;
    std::vector<int> roadColumnOffsets;
    std::vector<int> roadRowOffsets;

    const int minimumBlockDepth = minDepth_ * 2;
    const int preferredBlockDepth = preferredDepth_ * 2;
    const int maximumBlockDepth = maxDepth_ * 2;
    if (!PartitionBlocksWithRoads(bounds.width(), minWidth_, preferredWidth_, maxWidth_, blockColumns, roadColumnOffsets) ||
        !PartitionBlocksWithRoads(bounds.height(), minimumBlockDepth, preferredBlockDepth, maximumBlockDepth, blockRows, roadRowOffsets)) {
        return buildLotsPlan(bounds, plan);
    }

    std::size_t roadIndex = 0;
    for (; roadIndex < roadRowOffsets.size(); ++roadIndex) {
        const int roadY = bounds.minTileY + roadRowOffsets[roadIndex];
        plan.roadPlans.push_back(RciRoadPlan(bounds.minTileX, roadY, bounds.maxTileX, roadY));
    }

    for (roadIndex = 0; roadIndex < roadColumnOffsets.size(); ++roadIndex) {
        const int roadX = bounds.minTileX + roadColumnOffsets[roadIndex];
        plan.roadPlans.push_back(RciRoadPlan(roadX, bounds.minTileY, roadX, bounds.maxTileY));
    }

    std::size_t rowIndex = 0;
    for (; rowIndex < blockRows.size(); ++rowIndex) {
        const int blockY = bounds.minTileY + blockRows[rowIndex].startOffset;
        int firstDepth = 0;
        int secondDepth = 0;
        if (!SplitBlockIntoTwoDepths(blockRows[rowIndex].length, minDepth_, preferredDepth_, maxDepth_, firstDepth, secondDepth)) {
            continue;
        }

        std::size_t columnIndex = 0;
        for (; columnIndex < blockColumns.size(); ++columnIndex) {
            const int blockX = bounds.minTileX + blockColumns[columnIndex].startOffset;
            std::vector<Segment> lotWidthSegments;
            if (!PartitionSegments(blockColumns[columnIndex].length, minWidth_, 2, maxWidth_, lotWidthSegments)) {
                continue;
            }

            const int rowDepths[2] = {firstDepth, secondDepth};
            int rowStartOffsets[2] = {0, firstDepth};
            int lotRow = 0;
            for (; lotRow < 2; ++lotRow) {
                std::size_t lotColumnIndex = 0;
                for (; lotColumnIndex < lotWidthSegments.size(); ++lotColumnIndex) {
                    const RciRect lotRect(
                        blockX + lotWidthSegments[lotColumnIndex].startOffset,
                        blockY + rowStartOffsets[lotRow],
                        blockX + lotWidthSegments[lotColumnIndex].startOffset + lotWidthSegments[lotColumnIndex].length - 1,
                        blockY + rowStartOffsets[lotRow] + rowDepths[lotRow] - 1);
                    plan.lots.push_back(constructLot(lotRect));
                    plan.zoneRects.push_back(lotRect);
                }
            }
        }
    }

    if (plan.lots.empty()) {
        return buildLotsPlan(bounds, plan);
    }

    return true;
}

RciLot RciTool::constructLot(const RciRect& rect) const {
    RciLot lot;
    lot.toolId = id_;
    lot.name = name_;
    lot.zoningType = zoningType_;
    lot.color = color_;
    lot.rect = rect;
    return lot;
}

RciToolCatalog::RciToolCatalog() {
    setFallbackDefinition();
}

bool RciToolCatalog::loadFromXmlFile(const std::string& filePath) {
    const std::string xml = ReadFileToString(filePath);
    if (xml.empty()) {
        setFallbackDefinition();
        return false;
    }

    std::vector<RciTool> loadedTools;
    std::string::size_type searchStart = 0u;
    while (true) {
        const std::string::size_type toolStart = xml.find("<tool", searchStart);
        if (toolStart == std::string::npos) {
            break;
        }

        const std::string::size_type toolEnd = xml.find('>', toolStart);
        if (toolEnd == std::string::npos) {
            break;
        }

        const std::string toolTag = xml.substr(toolStart, toolEnd - toolStart + 1u);
        const std::string id = AttributeValue(toolTag, "id", std::string());
        const std::uint16_t zoningType = ZoningTypeFromToolTag(toolTag, id);
        if (!id.empty() && zoningType != TileZoningNone) {
            RciTool tool;
            const RciColor fallbackColor = zoningType == TileZoningResidential ?
                RciColor(0.18f, 0.86f, 0.32f, 0.50f) :
                RciColor(0.92f, 0.76f, 0.15f, 0.50f);
            tool.setDefinition(
                id,
                AttributeValue(toolTag, "name", id),
                AttributeColorValue(toolTag, fallbackColor),
                zoningType,
                AttributeIntValue(toolTag, "minDepth", 2),
                AttributeIntValueAny(toolTag, "preferredDepth", "preferedDepth", zoningType == TileZoningResidential ? 4 : 8),
                AttributeIntValue(toolTag, "maxDepth", 8),
                AttributeIntValue(toolTag, "minWidth", 2),
                AttributeIntValueAny(toolTag, "preferredWidth", "preferedWidth", 16),
                AttributeIntValue(toolTag, "maxWidth", 24));
            loadedTools.push_back(tool);
        }

        searchStart = toolEnd + 1u;
    }

    if (loadedTools.empty()) {
        setFallbackDefinition();
        return false;
    }

    tools_ = loadedTools;
    return true;
}

void RciToolCatalog::setFallbackDefinition() {
    tools_.clear();

    RciTool residential;
    residential.setDefinition(
        "residential",
        "Residence",
        RciColor(0.18f, 0.86f, 0.32f, 0.50f),
        TileZoningResidential,
        2,
        4,
        8,
        2,
        16,
        24);
    tools_.push_back(residential);

    RciTool industrial;
    industrial.setDefinition(
        "industrial",
        "Industry",
        RciColor(0.92f, 0.76f, 0.15f, 0.50f),
        TileZoningIndustrial,
        2,
        8,
        8,
        2,
        16,
        24);
    tools_.push_back(industrial);
}

const std::vector<RciTool>& RciToolCatalog::tools() const {
    return tools_;
}

const RciTool* RciToolCatalog::findTool(const std::string& id) const {
    std::size_t toolIndex = 0;
    for (; toolIndex < tools_.size(); ++toolIndex) {
        if (tools_[toolIndex].id() == id) {
            return &tools_[toolIndex];
        }
    }

    return 0;
}
