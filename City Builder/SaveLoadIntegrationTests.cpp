#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <direct.h>

#include "City.h"
#include "CrashLogger.h"
#include "GameSession.h"
#include "Tile.h"

namespace {
struct TestRunner {
    int checks;
    int failures;

    TestRunner()
        : checks(0),
          failures(0) {
    }

    void expect(bool condition, const std::string& message) {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << std::endl;
        }
    }

    int finish() const {
        if (failures != 0) {
            std::cerr << "Save/load integration tests failed: " << failures << " of " << checks << " checks failed." << std::endl;
            return EXIT_FAILURE;
        }

        std::cout << "Save/load integration tests passed: " << checks << " checks." << std::endl;
        return EXIT_SUCCESS;
    }
};

std::string MakeTemporarySaveDirectory() {
    char tempPath[MAX_PATH];
    const DWORD tempPathLength = GetTempPathA(MAX_PATH, tempPath);
    std::ostringstream pathBuilder;
    if (tempPathLength == 0 || tempPathLength >= MAX_PATH) {
        pathBuilder << ".\\";
    } else {
        pathBuilder << tempPath;
    }

    pathBuilder << "CityBuilder_SaveLoad_" << GetCurrentProcessId() << "_" << GetTickCount64();
    const std::string path = pathBuilder.str();
    _mkdir(path.c_str());
    return path;
}

void CleanupTemporarySaveDirectory(const std::string& saveDirectory) {
    DeleteFileA((saveDirectory + "\\region.bin").c_str());
    DeleteFileA((saveDirectory + "\\city_0_0.bin").c_str());
    RemoveDirectoryA(saveDirectory.c_str());
}

RuntimeOptions BuildSandboxRuntimeOptions() {
    RuntimeOptions options;
    options.fastForward = false;
    options.detectL2CacheSize = false;
    options.manualL2BytesPerLogicalThread = 128u * 1024u;
    options.usableL2Fraction = 0.75;
    options.mapWidth = 32;
    options.mapHeight = 32;
    return options;
}

CitySaveState BuildInitialSandboxState() {
    CitySaveState state = City::createDefaultSaveState(City::seedForRegionCoordinate(0, 0), 32, 32);
    state.cameraX = 4;
    state.cameraY = 5;
    state.visibleTiles = 32;
    state.simulationTick = 17u;
    state.cityParameters.assign(4, 0.0f);
    state.cityParameters[0] = 12.5f;
    state.cityParameters[1] = 3.75f;

    const int pollutedIndex = (3 * state.width) + 2;
    state.tiles[pollutedIndex].airPollution = 123456;
    state.tiles[pollutedIndex].landValue = 777777;
    state.tiles[pollutedIndex].isVacant = false;

    int tileY = 10;
    for (; tileY <= 15; ++tileY) {
        int tileX = 10;
        for (; tileX <= 15; ++tileX) {
            state.tiles[(tileY * state.width) + tileX].zoningType = TileZoningResidential;
        }
    }

    RciLot residentialLot;
    residentialLot.toolId = "residential";
    residentialLot.name = "Residential";
    residentialLot.zoningType = TileZoningResidential;
    residentialLot.color = RciColor(0.20f, 0.82f, 0.36f, 0.55f);
    residentialLot.rect = RciRect(10, 10, 11, 13);
    residentialLot.availableAfterTick = 17u;
    state.zoningLots.push_back(residentialLot);
    return state;
}

void AddSandboxCity(GameSession& session, const CitySaveState& initialState) {
    session.region().clear();
    std::unique_ptr<City> city(new City("Sandbox", 0, 0, initialState.width, initialState.height));
    city->setSaveState(initialState, true);
    session.region().addCity(std::move(city));
}

CitySaveState ExportActiveSessionState(GameSession& session) {
    CitySaveState state = session.runtime().exportCitySaveState();
    const City* city = session.activeCity();
    if (city != 0) {
        state.cameraX = city->cameraX();
        state.cameraY = city->cameraY();
        state.visibleTiles = city->visibleTiles();
    }
    return state;
}

bool WaitForSandboxCommands(GameSession& session, CitySaveState& state) {
    const int pollutedIndex = (3 * 32) + 2;
    int attempt = 0;
    for (; attempt < 200; ++attempt) {
        state = ExportActiveSessionState(session);
        if (state.tiles.size() == 32u * 32u &&
            state.tiles[pollutedIndex].airPollution == 123456 &&
            state.tiles[(10 * 32) + 10].zoningType == TileZoningResidential &&
            !state.transport.strokes.empty() &&
            !state.transport.tiles.empty() &&
            !state.lots.empty()) {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
}

template <typename Predicate>
bool WaitForSandboxState(GameSession& session, CitySaveState& state, Predicate predicate) {
    int attempt = 0;
    for (; attempt < 200; ++attempt) {
        state = ExportActiveSessionState(session);
        if (predicate(state)) {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
}

bool EqualTile(const Tile& left, const Tile& right) {
    return left.landValue == right.landValue &&
        left.airPollution == right.airPollution &&
        left.isVacant == right.isVacant &&
        left.zoningType == right.zoningType;
}

bool EqualInt2(const Int2& left, const Int2& right) {
    return left.x == right.x && left.y == right.y;
}

bool EqualRciLot(const RciLot& left, const RciLot& right) {
    return left.toolId == right.toolId &&
        left.name == right.name &&
        left.zoningType == right.zoningType &&
        left.color.r == right.color.r &&
        left.color.g == right.color.g &&
        left.color.b == right.color.b &&
        left.color.a == right.color.a &&
        left.rect.minTileX == right.rect.minTileX &&
        left.rect.minTileY == right.rect.minTileY &&
        left.rect.maxTileX == right.rect.maxTileX &&
        left.rect.maxTileY == right.rect.maxTileY &&
        left.availableAfterTick == right.availableAfterTick;
}

bool EqualLotModuleState(const CitySaveLotModuleState& left, const CitySaveLotModuleState& right) {
    return left.moduleAssetId == right.moduleAssetId &&
        EqualInt2(left.localOrigin, right.localOrigin);
}

bool EqualLotState(const CitySaveLotState& left, const CitySaveLotState& right) {
    if (left.lotId != right.lotId ||
        left.assetId != right.assetId ||
        left.anchorTileX != right.anchorTileX ||
        left.anchorTileY != right.anchorTileY ||
        left.rotationSteps != right.rotationSteps ||
        left.constructionTotalTicks != right.constructionTotalTicks ||
        left.constructionRemainingTicks != right.constructionRemainingTicks ||
        left.modules.size() != right.modules.size()) {
        return false;
    }

    std::size_t moduleIndex = 0;
    for (; moduleIndex < left.modules.size(); ++moduleIndex) {
        if (!EqualLotModuleState(left.modules[moduleIndex], right.modules[moduleIndex])) {
            return false;
        }
    }

    return true;
}

bool EqualLotRenderInstance(const LotRenderInstance& left, const LotRenderInstance& right) {
    return left.lotId == right.lotId &&
        left.originX == right.originX &&
        left.originY == right.originY &&
        left.width == right.width &&
        left.height == right.height &&
        left.renderHeight == right.renderHeight &&
        left.colorR == right.colorR &&
        left.colorG == right.colorG &&
        left.colorB == right.colorB &&
        left.surfacePattern == right.surfacePattern &&
        left.surfaceDirection == right.surfaceDirection;
}

bool EqualRoadLanePlacement(const RoadLanePlacement& left, const RoadLanePlacement& right) {
    return left.tileX == right.tileX &&
        left.tileY == right.tileY &&
        left.tileIndex == right.tileIndex &&
        left.family == right.family &&
        left.layer == right.layer &&
        left.templateId == right.templateId &&
        left.strokeId == right.strokeId &&
        left.laneIndex == right.laneIndex &&
        left.axis == right.axis &&
        left.crossSectionMask == right.crossSectionMask &&
        left.laneType == right.laneType &&
        left.surface == right.surface &&
        left.role == right.role &&
        left.separatorStyle == right.separatorStyle &&
        left.laneTravelMask == right.laneTravelMask &&
        left.arrowTravelMask == right.arrowTravelMask &&
        left.sideMin == right.sideMin &&
        left.sideMax == right.sideMax &&
        left.sidewalkEdgeMask == right.sidewalkEdgeMask &&
        left.sameDirectionDividerMask == right.sameDirectionDividerMask &&
        left.opposingDirectionDividerMask == right.opposingDirectionDividerMask &&
        left.active == right.active;
}

bool EqualTransportStroke(const TransportStrokeSaveState& left, const TransportStrokeSaveState& right) {
    return left.strokeId == right.strokeId &&
        EqualInt2(left.startTile, right.startTile) &&
        EqualInt2(left.cornerTile, right.cornerTile) &&
        EqualInt2(left.endTile, right.endTile) &&
        left.family == right.family &&
        left.layer == right.layer &&
        left.laneCount == right.laneCount &&
        left.trafficSide == right.trafficSide &&
        left.directionMode == right.directionMode;
}

bool EqualTransportErase(const TransportTileEraseSaveState& left, const TransportTileEraseSaveState& right) {
    return left.layer == right.layer && left.tileIndex == right.tileIndex;
}

bool EqualTransportTile(const TransportTileSaveState& left, const TransportTileSaveState& right) {
    if (left.layer != right.layer ||
        left.tileIndex != right.tileIndex ||
        left.lanes.size() != right.lanes.size()) {
        return false;
    }

    std::size_t laneIndex = 0;
    for (; laneIndex < left.lanes.size(); ++laneIndex) {
        if (!EqualRoadLanePlacement(left.lanes[laneIndex], right.lanes[laneIndex])) {
            return false;
        }
    }

    return true;
}

template <typename T, typename EqualFn>
bool EqualVector(const std::vector<T>& left, const std::vector<T>& right, EqualFn equalFn) {
    if (left.size() != right.size()) {
        return false;
    }

    std::size_t index = 0;
    for (; index < left.size(); ++index) {
        if (!equalFn(left[index], right[index])) {
            return false;
        }
    }

    return true;
}

bool EqualCitySaveState(const CitySaveState& left, const CitySaveState& right, std::string& detail) {
    if (left.width != right.width ||
        left.height != right.height ||
        left.nextLotId != right.nextLotId ||
        left.cameraX != right.cameraX ||
        left.cameraY != right.cameraY ||
        left.visibleTiles != right.visibleTiles ||
        left.simulationTick != right.simulationTick) {
        detail = "city scalar metadata differs";
        return false;
    }

    if (!EqualVector(left.tiles, right.tiles, EqualTile)) {
        detail = "tile vector differs";
        return false;
    }

    if (!EqualVector(left.zoningLots, right.zoningLots, EqualRciLot)) {
        detail = "RCI zoning lot vector differs";
        return false;
    }

    if (!EqualVector(left.lots, right.lots, EqualLotState)) {
        detail = "lot vector differs";
        return false;
    }

    if (!EqualVector(left.previewLots, right.previewLots, EqualLotRenderInstance)) {
        detail = "preview lot vector differs";
        return false;
    }

    if (left.cityParameters != right.cityParameters) {
        detail = "city parameter vector differs";
        return false;
    }

    if (left.transport.nextRoadStrokeId != right.transport.nextRoadStrokeId ||
        !EqualVector(left.transport.strokes, right.transport.strokes, EqualTransportStroke) ||
        !EqualVector(left.transport.erasures, right.transport.erasures, EqualTransportErase) ||
        !EqualVector(left.transport.tiles, right.transport.tiles, EqualTransportTile)) {
        detail = "transport save state differs";
        return false;
    }

    detail.clear();
    return true;
}

void RunSaveLoadRoundTripTest(TestRunner& runner) {
    const std::string saveDirectory = MakeTemporarySaveDirectory();
    const RuntimeOptions options = BuildSandboxRuntimeOptions();

    try {
        GameSession session(options);
        session.setSaveDirectoryOverride(saveDirectory);
        AddSandboxCity(session, BuildInitialSandboxState());
        runner.expect(session.enterCity(0, 0), "sandbox city enters with 32x32 runtime");

        session.runtime().queuePaintPollution(2, 3, 123456);
        session.runtime().queueZoneArea(10, 10, 15, 15, TileZoningResidential);
        session.runtime().queuePlaceStreetRoad(Int2(1, 1), Int2(12, 1), Int2(12, 6));
        session.runtime().queuePlaceHouse(18, 8, 0);

        CitySaveState baseline;
        runner.expect(WaitForSandboxCommands(session, baseline), "paused sandbox processes queued commands before save");
        session.runtime().stop();
        baseline = ExportActiveSessionState(session);
        runner.expect(baseline.width == 32 && baseline.height == 32, "baseline state uses sandbox dimensions");
        runner.expect(!baseline.transport.tiles.empty(), "baseline state includes authored transport tile lanes");

        runner.expect(session.saveAutoslot(), "sandbox autoslot saves to temporary directory");
        session.runtime().stop();

        GameSession loadedSession(options);
        loadedSession.setSaveDirectoryOverride(saveDirectory);
        runner.expect(loadedSession.loadAutoslot(), "sandbox autoslot loads from temporary directory");
        runner.expect(loadedSession.enterCity(0, 0), "loaded sandbox city enters");
        loadedSession.runtime().stop();

        const CitySaveState loadedState = ExportActiveSessionState(loadedSession);
        std::string difference;
        runner.expect(EqualCitySaveState(baseline, loadedState, difference), difference.empty() ? "loaded state matches saved state" : difference);
        loadedSession.shutdown();
        session.shutdown();
    } catch (const std::exception& error) {
        runner.expect(false, std::string("save/load round trip threw exception: ") + error.what());
    } catch (...) {
        runner.expect(false, "save/load round trip threw unknown exception");
    }

    CleanupTemporarySaveDirectory(saveDirectory);
}

void RunPausedCommandAndUnzoneTest(TestRunner& runner) {
    const RuntimeOptions options = BuildSandboxRuntimeOptions();

    try {
        GameSession session(options);
        AddSandboxCity(session, BuildInitialSandboxState());
        runner.expect(session.enterCity(0, 0), "paused command sandbox city enters");

        session.runtime().queuePlaceHouse(18, 8, 0);
        CitySaveState state;
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u && !candidate.lots.empty();
            }),
            "paused command frame places a lot without advancing the date");

        session.runtime().queueBulldozeAtTile(18, 9);
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u && candidate.lots.empty();
            }),
            "paused command frame bulldozes a lot and publishes the changed state");

        session.runtime().queueZoneArea(10, 10, 10, 10, TileZoningNone);
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.tiles.size() == 32u * 32u &&
                    candidate.tiles[(10 * 32) + 10].zoningType == TileZoningNone &&
                    candidate.tiles[(10 * 32) + 11].zoningType == TileZoningResidential &&
                    candidate.tiles[(13 * 32) + 11].zoningType == TileZoningResidential;
            }),
            "unzone clears only the selected parcel tile instead of the whole parcel");

        session.runtime().queuePlaceStreetRoad(Int2(2, 20), Int2(12, 20), Int2(12, 20));
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u && !candidate.transport.tiles.empty();
            }),
            "paused command frame places a road without advancing the date");

        session.runtime().queueBulldozeArea(2, 20, 12, 20);
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u && candidate.transport.tiles.empty();
            }),
            "paused area bulldoze removes road tiles without advancing the date");

        session.runtime().setGameSpeed(GameSpeed::Play);
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick > 17u;
            }),
            "play speed advances one dated tick");

        const std::uint64_t playLimiterTick = state.simulationTick;
        session.runtime().queuePaintPollution(6, 6, 333333);
        runner.expect(
            WaitForSandboxState(session, state, [playLimiterTick](const CitySaveState& candidate) {
                return candidate.simulationTick == playLimiterTick &&
                    candidate.tiles.size() == 32u * 32u &&
                    candidate.tiles[(6 * 32) + 6].airPollution == 333333;
            }),
            "play limiter idle applies a queued tool command without advancing the date");
        session.runtime().setGameSpeed(GameSpeed::Paused);

        session.shutdown();
    } catch (const std::exception& error) {
        runner.expect(false, std::string("paused command/unzone test threw exception: ") + error.what());
    } catch (...) {
        runner.expect(false, "paused command/unzone test threw unknown exception");
    }
}

void RunDiscardInvalidatesPreviewTest(TestRunner& runner) {
    const std::string saveDirectory = MakeTemporarySaveDirectory();
    const RuntimeOptions options = BuildSandboxRuntimeOptions();

    try {
        GameSession session(options);
        session.setSaveDirectoryOverride(saveDirectory);
        AddSandboxCity(session, BuildInitialSandboxState());
        runner.expect(session.enterCity(0, 0), "preview discard sandbox city enters");

        session.runtime().queuePaintPollution(4, 4, 222222);
        CitySaveState state;
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u &&
                    candidate.tiles.size() == 32u * 32u &&
                    candidate.tiles[(4 * 32) + 4].airPollution == 222222;
            }),
            "preview discard sandbox records an unsaved edit");

        session.exitToRegion();
        City* city = session.activeCity();
        runner.expect(city != 0, "exiting to region retains the active city shell");
        if (city != 0) {
            const std::uint64_t dirtyPreviewRevision = city->previewRevision();
            runner.expect(city->hasSaveState() && city->isSaveStateDirty(), "exited active city keeps only dirty cached city data");
            runner.expect(session.discardActiveCityChanges(), "discard active city changes succeeds");
            runner.expect(!city->hasSaveState(), "discard unloads the full cached city state");
            runner.expect(city->previewRevision() != dirtyPreviewRevision, "discard invalidates stale dirty region preview");
        }

        session.shutdown();
    } catch (const std::exception& error) {
        runner.expect(false, std::string("discard preview invalidation test threw exception: ") + error.what());
    } catch (...) {
        runner.expect(false, "discard preview invalidation test threw unknown exception");
    }

    CleanupTemporarySaveDirectory(saveDirectory);
}
}

int main() {
    InitializeCrashLogger("City Builder SaveLoadIntegrationTests");
    TestRunner runner;
    RunSaveLoadRoundTripTest(runner);
    RunPausedCommandAndUnzoneTest(runner);
    RunDiscardInvalidatesPreviewTest(runner);
    return runner.finish();
}
