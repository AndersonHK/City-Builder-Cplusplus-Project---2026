#include "RoadGraphic.h"

#include <algorithm>

#include "RoadRenderState.h"

namespace {
std::uint8_t SidewalkMask(const RoadRenderState& renderState) {
    return renderState.laneGraphicMask & kRoadSurfaceSidewalkEdgeMask;
}

std::uint8_t CrosswalkMask(const RoadRenderState& renderState) {
    return static_cast<std::uint8_t>((renderState.laneGraphicMask >> kRoadSurfaceCrosswalkShift) & kRoadSurfaceSidewalkEdgeMask);
}

std::uint8_t WhiteDividerMask(const RoadRenderState& renderState) {
    return static_cast<std::uint8_t>((renderState.dividerMask >> kRoadDividerWhiteShift) & kRoadSurfaceSidewalkEdgeMask);
}

std::uint8_t YellowDividerMask(const RoadRenderState& renderState) {
    return static_cast<std::uint8_t>((renderState.dividerMask >> kRoadDividerYellowShift) & kRoadSurfaceSidewalkEdgeMask);
}
}

RoadGraphic::RoadGraphic()
    : primitive_(RoadGraphicPrimitive::None),
      directionMask_(0) {
}

RoadGraphic RoadGraphic::none() {
    return RoadGraphic();
}

RoadGraphic RoadGraphic::asphalt(std::uint8_t directionMask) {
    RoadGraphic graphic;
    graphic.primitive_ = RoadGraphicPrimitive::Asphalt;
    graphic.directionMask_ = directionMask;
    return graphic;
}

RoadGraphic RoadGraphic::sidewalk(std::uint8_t directionMask) {
    RoadGraphic graphic;
    graphic.primitive_ = RoadGraphicPrimitive::Sidewalk;
    graphic.directionMask_ = directionMask;
    return graphic;
}

RoadGraphic RoadGraphic::crosswalk(std::uint8_t directionMask) {
    RoadGraphic graphic;
    graphic.primitive_ = RoadGraphicPrimitive::Crosswalk;
    graphic.directionMask_ = directionMask;
    return graphic;
}

RoadGraphic RoadGraphic::median(std::uint8_t directionMask) {
    RoadGraphic graphic;
    graphic.primitive_ = RoadGraphicPrimitive::Median;
    graphic.directionMask_ = directionMask;
    return graphic;
}

RoadGraphic RoadGraphic::divider(std::uint8_t directionMask) {
    RoadGraphic graphic;
    graphic.primitive_ = RoadGraphicPrimitive::Divider;
    graphic.directionMask_ = directionMask;
    return graphic;
}

RoadGraphicPrimitive RoadGraphic::primitive() const {
    return primitive_;
}

std::uint8_t RoadGraphic::directionMask() const {
    return directionMask_;
}

void RoadGraphic::setDirectionMask(std::uint8_t directionMask) {
    directionMask_ = directionMask;
}

void RoadGraphic::applyToRenderState(RoadRenderState& renderState) const {
    applyToRenderState(renderState, directionMask_);
}

void RoadGraphic::applyToRenderState(RoadRenderState& renderState, std::uint8_t directionMask) const {
    const std::uint8_t mask = directionMask & kRoadSurfaceSidewalkEdgeMask;
    if (primitive_ == RoadGraphicPrimitive::Sidewalk) {
        renderState.laneGraphicMask = PackLaneGraphicMask(static_cast<std::uint8_t>(SidewalkMask(renderState) | mask), CrosswalkMask(renderState));
    } else if (primitive_ == RoadGraphicPrimitive::Crosswalk) {
        renderState.laneGraphicMask = PackLaneGraphicMask(SidewalkMask(renderState), static_cast<std::uint8_t>(CrosswalkMask(renderState) | mask));
    } else if (primitive_ == RoadGraphicPrimitive::Median) {
        renderState.dividerMask = PackDividerMask(WhiteDividerMask(renderState), static_cast<std::uint8_t>(YellowDividerMask(renderState) | mask));
    } else if (primitive_ == RoadGraphicPrimitive::Divider) {
        renderState.dividerMask = PackDividerMask(static_cast<std::uint8_t>(WhiteDividerMask(renderState) | mask), YellowDividerMask(renderState));
    }
}
