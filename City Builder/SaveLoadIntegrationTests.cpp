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

int SaveTileIndex(const CitySaveState& state, int tileX, int tileY) {
    return (tileY * state.width) + tileX;
}

void ZoneSaveRect(CitySaveState& state, const RciRect& rect, std::uint16_t zoningType) {
    int tileY = rect.minTileY;
    for (; tileY <= rect.maxTileY; ++tileY) {
        int tileX = rect.minTileX;
        for (; tileX <= rect.maxTileX; ++tileX) {
            state.tiles[SaveTileIndex(state, tileX, tileY)].zoningType = zoningType;
        }
    }
}

RciLot MakeSandboxRciLot(const RciRect& rect, std::uint16_t zoningType) {
    RciLot lot;
    lot.toolId = zoningType == TileZoningIndustrial ? "industrial" : "residential";
    lot.name = zoningType == TileZoningIndustrial ? "Industry" : "Residence";
    lot.zoningType = zoningType;
    lot.color = zoningType == TileZoningIndustrial
        ? RciColor(0.92f, 0.76f, 0.15f, 0.50f)
        : RciColor(0.18f, 0.86f, 0.32f, 0.50f);
    lot.rect = rect;
    lot.availableAfterTick = 17u;
    return lot;
}

void AddModuleState(CitySaveLotState& lot, const std::string& moduleAssetId, int localX, int localY) {
    CitySaveLotModuleState module;
    module.moduleAssetId = moduleAssetId;
    module.localOrigin = Int2(localX, localY);
    lot.modules.push_back(module);
}

CitySaveLotState MakeResidential2x4SaveLot(int lotId, int anchorTileX, int anchorTileY) {
    CitySaveLotState lot;
    lot.lotId = lotId;
    lot.assetId = "rci_residential_2x4_lot";
    lot.anchorTileX = anchorTileX;
    lot.anchorTileY = anchorTileY;
    AddModuleState(lot, "pathway_module", 0, 0);
    AddModuleState(lot, "driveway_module", 1, 0);
    AddModuleState(lot, "larger_house_module", 0, 1);
    return lot;
}

CitySaveLotState MakeResidential8x8SaveLot(int lotId, int anchorTileX, int anchorTileY) {
    CitySaveLotState lot;
    lot.lotId = lotId;
    lot.assetId = "rci_residential_8x8_lot";
    lot.anchorTileX = anchorTileX;
    lot.anchorTileY = anchorTileY;
    AddModuleState(lot, "apartment_block_module", 0, 0);
    AddModuleState(lot, "apartment_block_module", 4, 0);
    AddModuleState(lot, "apartment_block_module", 0, 4);
    AddModuleState(lot, "apartment_block_module", 4, 4);
    return lot;
}

CitySaveLotState MakeIndustrial8x8SaveLot(int lotId, int anchorTileX, int anchorTileY) {
    CitySaveLotState lot;
    lot.lotId = lotId;
    lot.assetId = "rci_industrial_8x8_lot";
    lot.anchorTileX = anchorTileX;
    lot.anchorTileY = anchorTileY;
    AddModuleState(lot, "large_factory_module", 0, 0);
    AddModuleState(lot, "distribution_center_module", 4, 0);
    AddModuleState(lot, "freight_warehouse_module", 0, 4);
    AddModuleState(lot, "freight_warehouse_module", 4, 4);
    AddModuleState(lot, "smokestack_row_module", 0, 7);
    AddModuleState(lot, "smokestack_row_module", 4, 7);
    return lot;
}

CitySaveLotState MakeIndustrial4x4SaveLot(int lotId, int anchorTileX, int anchorTileY) {
    CitySaveLotState lot;
    lot.lotId = lotId;
    lot.assetId = "rci_industrial_4x4_lot";
    lot.anchorTileX = anchorTileX;
    lot.anchorTileY = anchorTileY;
    AddModuleState(lot, "large_factory_module", 0, 0);
    return lot;
}

CitySaveState BuildCleanSandboxState(int width = 32, int height = 32) {
    CitySaveState state = City::createDefaultSaveState(City::seedForRegionCoordinate(0, 0), width, height);
    state.cameraX = 4;
    state.cameraY = 5;
    state.visibleTiles = width;
    state.simulationTick = 17u;
    state.cityParameters.assign(7, 0.0f);
    state.zoningLots.clear();
    state.lots.clear();
    state.previewLots.clear();
    return state;
}

bool HasLotAsset(const CitySaveState& state, const std::string& assetId) {
    std::size_t lotIndex = 0;
    for (; lotIndex < state.lots.size(); ++lotIndex) {
        if (state.lots[lotIndex].assetId == assetId) {
            return true;
        }
    }

    return false;
}

bool HasResidential4x4LowDensityLot(const CitySaveState& state) {
    return HasLotAsset(state, "rci_residential_4x4_lot") ||
        HasLotAsset(state, "rci_residential_4x4_courtyard_lot");
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

void RunRciConstructorAreaGrowthTest(TestRunner& runner) {
    const RuntimeOptions options = BuildSandboxRuntimeOptions();

    try {
        GameSession session(options);
        AddSandboxCity(session, BuildCleanSandboxState());
        runner.expect(session.enterCity(0, 0), "RCI area growth sandbox city enters");

        session.runtime().queuePlaceStreetRoad(Int2(4, 3), Int2(8, 3), Int2(8, 3));
        session.runtime().queueZoneLot(MakeSandboxRciLot(RciRect(4, 5, 5, 8), TileZoningResidential));
        session.runtime().queueZoneLot(MakeSandboxRciLot(RciRect(6, 5, 7, 8), TileZoningResidential));

        CitySaveState state;
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u &&
                    candidate.zoningLots.size() == 2u &&
                    !candidate.transport.tiles.empty();
            }),
            "RCI merge setup creates two adjacent parcels with road access");

        session.runtime().setGameSpeed(GameSpeed::FastForward);
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick > 17u &&
                    HasResidential4x4LowDensityLot(candidate) &&
                    candidate.lots.size() == 1u &&
                    candidate.zoningLots.empty();
            }),
            "two adjacent residential 2x4 parcels merge into one valid 4x4 lot");
        session.runtime().setGameSpeed(GameSpeed::Paused);
        session.shutdown();
    } catch (const std::exception& error) {
        runner.expect(false, std::string("RCI area growth test threw exception: ") + error.what());
    } catch (...) {
        runner.expect(false, "RCI area growth test threw unknown exception");
    }
}

void RunRciConstructorDesirabilityTest(TestRunner& runner) {
    const RuntimeOptions options = BuildSandboxRuntimeOptions();

    try {
        GameSession session(options);
        AddSandboxCity(session, BuildCleanSandboxState());
        runner.expect(session.enterCity(0, 0), "RCI desirability sandbox city enters");

        session.runtime().queueZoneLot(MakeSandboxRciLot(RciRect(4, 5, 5, 8), TileZoningResidential));
        session.runtime().queueZoneLot(MakeSandboxRciLot(RciRect(6, 5, 7, 8), TileZoningResidential));

        CitySaveState state;
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u && candidate.zoningLots.size() == 2u;
            }),
            "RCI landlocked setup creates two parcels without road access");

        session.runtime().setGameSpeed(GameSpeed::FastForward);
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick > 24u &&
                    candidate.lots.empty() &&
                    candidate.zoningLots.size() == 2u;
            }),
            "landlocked residential candidate fails desirability and does not grow");
        session.runtime().setGameSpeed(GameSpeed::Paused);
        session.shutdown();
    } catch (const std::exception& error) {
        runner.expect(false, std::string("RCI desirability test threw exception: ") + error.what());
    } catch (...) {
        runner.expect(false, "RCI desirability test threw unknown exception");
    }
}

void RunRciConstructorRedevelopmentTest(TestRunner& runner) {
    const RuntimeOptions options = BuildSandboxRuntimeOptions();

    try {
        CitySaveState initialState = BuildCleanSandboxState();
        ZoneSaveRect(initialState, RciRect(4, 5, 5, 8), TileZoningResidential);
        ZoneSaveRect(initialState, RciRect(6, 5, 7, 8), TileZoningResidential);
        initialState.lots.push_back(MakeResidential2x4SaveLot(1, 4, 5));
        initialState.lots.push_back(MakeResidential2x4SaveLot(2, 6, 5));
        initialState.nextLotId = 3;

        GameSession session(options);
        AddSandboxCity(session, initialState);
        runner.expect(session.enterCity(0, 0), "RCI redevelopment sandbox city enters");
        session.runtime().queuePlaceStreetRoad(Int2(4, 3), Int2(8, 3), Int2(8, 3));

        CitySaveState state;
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u &&
                    candidate.lots.size() == 2u &&
                    !candidate.transport.tiles.empty();
            }),
            "RCI redevelopment setup preserves two completed source buildings");

        session.runtime().setGameSpeed(GameSpeed::FastForward);
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick > 17u &&
                    HasResidential4x4LowDensityLot(candidate) &&
                    candidate.lots.size() == 1u;
            }),
            "completed lower-capacity RCI buildings redevelop into a higher-capacity merged lot");
        session.runtime().setGameSpeed(GameSpeed::Paused);
        session.shutdown();
    } catch (const std::exception& error) {
        runner.expect(false, std::string("RCI redevelopment test threw exception: ") + error.what());
    } catch (...) {
        runner.expect(false, "RCI redevelopment test threw unknown exception");
    }
}

void RunRciConstructorDensityCapTest(TestRunner& runner) {
    RuntimeOptions options = BuildSandboxRuntimeOptions();

    try {
        CitySaveState lowPopulationState = BuildCleanSandboxState();
        ZoneSaveRect(lowPopulationState, RciRect(20, 20, 23, 23), TileZoningIndustrial);
        lowPopulationState.lots.push_back(MakeIndustrial4x4SaveLot(1, 20, 20));
        lowPopulationState.nextLotId = 2;

        GameSession lowPopulationSession(options);
        AddSandboxCity(lowPopulationSession, lowPopulationState);
        runner.expect(lowPopulationSession.enterCity(0, 0), "RCI density low-population sandbox city enters");
        lowPopulationSession.runtime().queuePlaceStreetRoad(Int2(4, 3), Int2(8, 3), Int2(8, 3));
        lowPopulationSession.runtime().queueZoneLot(MakeSandboxRciLot(RciRect(4, 5, 7, 8), TileZoningResidential));

        CitySaveState state;
        runner.expect(
            WaitForSandboxState(lowPopulationSession, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u &&
                    candidate.zoningLots.size() == 1u &&
                    !candidate.transport.tiles.empty();
            }),
            "RCI density low-population setup creates one 4x4 residential parcel");

        lowPopulationSession.runtime().setGameSpeed(GameSpeed::FastForward);
        runner.expect(
            WaitForSandboxState(lowPopulationSession, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick > 17u &&
                    HasResidential4x4LowDensityLot(candidate) &&
                    !HasLotAsset(candidate, "rci_residential_4x4_walkup_lot");
            }),
            "high-density residential 4x4 is rejected at population zero by the density cap");
        lowPopulationSession.runtime().setGameSpeed(GameSpeed::Paused);
        lowPopulationSession.shutdown();

        options.mapWidth = 64;
        options.mapHeight = 64;
        CitySaveState highPopulationState = BuildCleanSandboxState(64, 64);
        int lotId = 1;
        int placedResidentialLots = 0;
        int row = 0;
        for (; row < 3 && placedResidentialLots < 13; ++row) {
            int column = 0;
            for (; column < 5 && placedResidentialLots < 13; ++column) {
                const int x = 16 + (column * 8);
                const int y = row * 8;
                ZoneSaveRect(highPopulationState, RciRect(x, y, x + 7, y + 7), TileZoningResidential);
                highPopulationState.lots.push_back(MakeResidential8x8SaveLot(lotId++, x, y));
                ++placedResidentialLots;
            }
        }

        int placedIndustrialLots = 0;
        row = 0;
        for (; row < 4 && placedIndustrialLots < 18; ++row) {
            int column = 0;
            for (; column < 5 && placedIndustrialLots < 18; ++column) {
                const int x = 16 + (column * 8);
                const int y = 24 + (row * 8);
                ZoneSaveRect(highPopulationState, RciRect(x, y, x + 7, y + 7), TileZoningIndustrial);
                highPopulationState.lots.push_back(MakeIndustrial8x8SaveLot(lotId++, x, y));
                ++placedIndustrialLots;
            }
        }
        highPopulationState.nextLotId = lotId;

        GameSession highPopulationSession(options);
        AddSandboxCity(highPopulationSession, highPopulationState);
        runner.expect(highPopulationSession.enterCity(0, 0), "RCI density high-population sandbox city enters");
        highPopulationSession.runtime().queuePlaceStreetRoad(Int2(4, 3), Int2(8, 3), Int2(8, 3));
        highPopulationSession.runtime().queueZoneLot(MakeSandboxRciLot(RciRect(4, 5, 7, 8), TileZoningResidential));

        runner.expect(
            WaitForSandboxState(highPopulationSession, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u &&
                    !candidate.transport.tiles.empty() &&
                    candidate.zoningLots.size() == 1u;
            }),
            "RCI density high-population setup creates one 4x4 residential parcel");

        highPopulationSession.runtime().setGameSpeed(GameSpeed::FastForward);
        runner.expect(
            WaitForSandboxState(highPopulationSession, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick > 17u &&
                    HasLotAsset(candidate, "rci_residential_4x4_walkup_lot");
            }),
            "high-density residential 4x4 is accepted once population raises the interpolated density cap");
        highPopulationSession.runtime().setGameSpeed(GameSpeed::Paused);
        highPopulationSession.shutdown();
    } catch (const std::exception& error) {
        runner.expect(false, std::string("RCI density cap test threw exception: ") + error.what());
    } catch (...) {
        runner.expect(false, "RCI density cap test threw unknown exception");
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
    RunRciConstructorAreaGrowthTest(runner);
    RunRciConstructorDesirabilityTest(runner);
    RunRciConstructorRedevelopmentTest(runner);
    RunRciConstructorDensityCapTest(runner);
    RunDiscardInvalidatesPreviewTest(runner);
    return runner.finish();
}
