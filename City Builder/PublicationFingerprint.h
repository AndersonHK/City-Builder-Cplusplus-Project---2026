#pragma once
#include "SimulationRuntime.h"
#include <initializer_list>

// Field-wise hashing excludes struct padding. Used only in separate verification
// runs to compare all lot-publication payloads before/after an optimization.
struct PublicationFingerprint {
    std::uint64_t value = 1469598103934665603ull;
    template <typename T> void add(const T &input) {
        const auto *bytes = reinterpret_cast<const unsigned char *>(&input);
        for (std::size_t i = 0; i < sizeof(input); ++i)
            value = (value ^ bytes[i]) * 1099511628211ull;
    }
    void add(const std::string &input) {
        add(input.size());
        for (char c : input) add(c);
    }
    template <typename... T> void fields(const T &...input) {
        (void)std::initializer_list<int>{(add(input), 0)...};
    }
    void add(const CommuteRouteSegment &s) {
        fields(s.startTileX, s.startTileY, s.endTileX, s.endTileY, s.layer, s.mode, s.timeOfDay, s.direction, s.demand);
    }
    template <typename T> void add(const std::vector<T> &input) {
        add(input.size());
        for (const auto &item : input) add(item);
    }
    void add(const PublishedLotInfo &p) {
        fields(p.lotId, p.assetId, p.zoningType, p.isEmpty, p.minimumTileX, p.minimumTileY, p.footprintWidth,
               p.footprintHeight, p.moduleSummary, p.parameterSummary, p.commuteDemand, p.commuteSatisfied,
               p.worstCommuteCategory, p.residentsLowWealthCurrent, p.residentsLowWealthTotal, p.jobsLowWealthCurrent,
               p.jobsLowWealthTotal, p.rciCapacityCurrent, p.rciCapacityMaximum, p.complaintSummary, p.commuteRouteSegments);
    }
    void add(const LotRenderInstance &p) {
        fields(p.lotId, p.originX, p.originY, p.width, p.height, p.renderOffsetX, p.renderOffsetY, p.renderWidth,
               p.renderHeightOverride, p.renderHeight, p.colorR, p.colorG, p.colorB, p.surfacePattern,
               p.surfaceDirection, p.zoningType, p.renderMeshHandle, p.meshRotation);
    }
};
