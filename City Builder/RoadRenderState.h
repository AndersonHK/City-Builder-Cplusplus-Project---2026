#pragma once

#include <cstdint>

#include "TransportTypes.h"

struct RoadRenderState {
    RoadRenderVariant variant;
    RoadBaseGlyph baseGlyph;
    RoadArrowGlyph arrowGlyph;
    std::uint8_t laneGraphicMask;
    std::uint8_t dividerMask;

    RoadRenderState();
};

RoadRenderVariant ChooseRenderVariant(std::uint8_t junctionMask);
RoadBaseGlyph ChooseBaseGlyph(RoadFamily family, RoadRenderVariant renderVariant, std::uint8_t junctionMask);
RoadArrowGlyph ChooseArrowGlyph(std::uint8_t laneIntentMask);
RoadArrowGlyph ChooseTurnArrowGlyph(std::uint8_t laneIntentMask);
std::uint8_t PackLaneGraphicMask(std::uint8_t sidewalkEdges, std::uint8_t crosswalkEdges);
std::uint8_t PackDividerMask(std::uint8_t sameDirectionEdges, std::uint8_t opposingDirectionEdges);
