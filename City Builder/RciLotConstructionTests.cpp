#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "AssetLoader.h"
#include "City.h"
#include "CrashLogger.h"
#include "GameSession.h"
#include "SimulationTime.h"
#include "TestAssetXml.h"
#include "Tile.h"
#include "TransportTypes.h"

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
            std::cerr << "RCI lot construction tests failed: " << failures << " of " << checks << " checks failed." << std::endl;
            return EXIT_FAILURE;
        }

        std::cout << "RCI lot construction tests passed: " << checks << " checks." << std::endl;
        return EXIT_SUCCESS;
    }
};

struct DensityBuckets {
    std::vector<float> low;
    std::vector<float> medium;
    std::vector<float> high;
};

RuntimeOptions BuildSandboxRuntimeOptions(int width = 32, int height = 32) {
    RuntimeOptions options;
    options.fastForward = false;
    options.detectL2CacheSize = false;
    options.manualL2BytesPerLogicalThread = 128u * 1024u;
    options.usableL2Fraction = 0.75;
    options.mapWidth = width;
    options.mapHeight = height;
    return options;
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

int SaveTileIndex(const CitySaveState& state, int tileX, int tileY) {
    return (tileY * state.width) + tileX;
}

void SetLandValueRect(CitySaveState& state, const RciRect& rect, int landValue) {
    int tileY = rect.minTileY;
    for (; tileY <= rect.maxTileY; ++tileY) {
        int tileX = rect.minTileX;
        for (; tileX <= rect.maxTileX; ++tileX) {
            state.tiles[SaveTileIndex(state, tileX, tileY)].landValue = landValue;
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
    lot.frontDirection = kRoadDirectionNorth;
    lot.availableAfterTick = 17u;
    return lot;
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

template <typename Predicate>
bool WaitForSandboxState(GameSession& session, CitySaveState& state, Predicate predicate) {
    int attempt = 0;
    for (; attempt < 300; ++attempt) {
        state = ExportActiveSessionState(session);
        if (predicate(state)) {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
}

bool HasLotAssetPrefix(const CitySaveState& state, const std::string& prefix) {
    std::size_t lotIndex = 0;
    for (; lotIndex < state.lots.size(); ++lotIndex) {
        if (state.lots[lotIndex].assetId.compare(0, prefix.size(), prefix) == 0) {
            return true;
        }
    }

    return false;
}

bool RciLotMatchesPlanLot(const RciLot& storedLot, const RciLot& planLot) {
    return storedLot.zoningType == planLot.zoningType &&
        storedLot.frontDirection == planLot.frontDirection &&
        storedLot.rect.minTileX == planLot.rect.minTileX &&
        storedLot.rect.minTileY == planLot.rect.minTileY &&
        storedLot.rect.maxTileX == planLot.rect.maxTileX &&
        storedLot.rect.maxTileY == planLot.rect.maxTileY;
}

bool CityStateContainsPlanLots(const CitySaveState& state, const RciPlan& plan) {
    if (state.zoningLots.size() != plan.lots.size()) {
        return false;
    }

    std::size_t planLotIndex = 0u;
    for (; planLotIndex < plan.lots.size(); ++planLotIndex) {
        bool found = false;
        std::size_t storedLotIndex = 0u;
        for (; storedLotIndex < state.zoningLots.size(); ++storedLotIndex) {
            if (RciLotMatchesPlanLot(state.zoningLots[storedLotIndex], plan.lots[planLotIndex])) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }

    return true;
}

bool CityStateZonesPlanLots(const CitySaveState& state, const RciPlan& plan) {
    std::size_t planLotIndex = 0u;
    for (; planLotIndex < plan.lots.size(); ++planLotIndex) {
        const RciRect& rect = plan.lots[planLotIndex].rect;
        int tileY = rect.minTileY;
        for (; tileY <= rect.maxTileY; ++tileY) {
            int tileX = rect.minTileX;
            for (; tileX <= rect.maxTileX; ++tileX) {
                if (tileX < 0 ||
                    tileY < 0 ||
                    tileX >= state.width ||
                    tileY >= state.height ||
                    state.tiles[SaveTileIndex(state, tileX, tileY)].zoningType != plan.zoningType) {
                    return false;
                }
            }
        }
    }

    return true;
}

bool RciPlanHasHorizontalRoadAt(const RciPlan& plan, int tileY) {
    std::size_t roadIndex = 0u;
    for (; roadIndex < plan.roadPlans.size(); ++roadIndex) {
        const RciRoadPlan& roadPlan = plan.roadPlans[roadIndex];
        if (roadPlan.startTileY == roadPlan.endTileY && roadPlan.startTileY == tileY) {
            return true;
        }
    }

    return false;
}

const LotModule* FindModule(const LoadedGameAssets& assets, const std::string& id) {
    std::size_t moduleIndex = 0;
    for (; moduleIndex < assets.modules.size(); ++moduleIndex) {
        if (assets.modules[moduleIndex].id == id) {
            return &assets.modules[moduleIndex];
        }
    }

    return 0;
}

const LotAsset* FindLotAsset(const LoadedGameAssets& assets, const std::string& id) {
    std::size_t lotIndex = 0;
    for (; lotIndex < assets.lots.size(); ++lotIndex) {
        if (assets.lots[lotIndex].id == id) {
            return &assets.lots[lotIndex];
        }
    }

    return 0;
}

const RciGrowthRule* FindGrowthRule(const LoadedGameAssets& assets, std::uint16_t zoningType) {
    std::size_t ruleIndex = 0;
    for (; ruleIndex < assets.rciGrowthRules.size(); ++ruleIndex) {
        if (assets.rciGrowthRules[ruleIndex].zoningType == zoningType) {
            return &assets.rciGrowthRules[ruleIndex];
        }
    }

    return 0;
}

float ModuleParameterAmount(const LotModule& module, int parameterId) {
    std::size_t contributionIndex = 0;
    for (; contributionIndex < module.parameterContributions.size(); ++contributionIndex) {
        const CityParameterContribution& contribution = module.parameterContributions[contributionIndex];
        if (contribution.parameterId == parameterId) {
            return contribution.amount;
        }
    }

    return 0.0f;
}

float LotAssetCapacity(const LoadedGameAssets& assets, const LotAsset& lotAsset, int parameterId) {
    float capacity = 0.0f;
    std::size_t placementIndex = 0;
    for (; placementIndex < lotAsset.initialModules.size(); ++placementIndex) {
        const LotModule* module = FindModule(assets, lotAsset.initialModules[placementIndex].moduleId);
        if (module != 0) {
            capacity += ModuleParameterAmount(*module, parameterId);
        }
    }

    return capacity;
}

std::string ZoningName(std::uint16_t zoningType) {
    if (zoningType == TileZoningResidential) {
        return "residential";
    }
    if (zoningType == TileZoningIndustrial) {
        return "industrial";
    }
    return "unknown";
}

std::string FootprintKey(std::uint16_t zoningType, int width, int height) {
    std::ostringstream key;
    key << ZoningName(zoningType) << ":" << width << "x" << height;
    return key.str();
}

std::string DensityGroupKey(std::uint16_t zoningType, int width, int height, const std::string& densityBand) {
    std::ostringstream key;
    key << FootprintKey(zoningType, width, height) << ":" << densityBand;
    return key.str();
}

bool LoadCheckedAssets(TestRunner& runner, LoadedGameAssets& assets, CityParameterRegistry& registry) {
    std::string errorMessage;
    const char* dataDirectories[] = {
        "Data",
        "City Builder\\Data"
    };

    std::size_t dataDirectoryIndex = 0;
    for (; dataDirectoryIndex < sizeof(dataDirectories) / sizeof(dataDirectories[0]); ++dataDirectoryIndex) {
        if (!DirectoryExists(dataDirectories[dataDirectoryIndex])) {
            continue;
        }
        if (LoadGameAssets(dataDirectories[dataDirectoryIndex], registry, assets, errorMessage)) {
            return true;
        }
    }

    runner.expect(false, "game asset XML loads" + (errorMessage.empty() ? std::string() : "\nLoader error: " + errorMessage));
    return false;
}

bool InvalidAssetsRejected(const std::string& moduleXml, const std::string& lotXml, const CityParameterRegistry& registry) {
    const std::string root = MakeTempAssetDirectory("CityBuilderRciAssetInvalid");
    WriteTextAssetFile(root + "\\Modules\\test_module.xml", moduleXml);
    WriteTextAssetFile(root + "\\Lots\\test_lot.xml", lotXml);

    LoadedGameAssets assets;
    std::string errorMessage;
    ScopedCrashLogSuppression suppressExpectedAssetErrors;
    return !LoadGameAssets(root, registry, assets, errorMessage) && !errorMessage.empty();
}

void ExpectGrowthRuleShape(TestRunner& runner, const RciGrowthRule* rule, const std::string& name, std::size_t minimumRows, float expectedMaximum) {
    runner.expect(rule != 0, name + " growth rule exists");
    if (rule == 0) {
        return;
    }

    runner.expect(rule->densityPoints.size() >= minimumRows, name + " growth rule keeps smooth population progression");
    runner.expect(!rule->densityPoints.empty() && rule->densityPoints.front().population == 0, name + " growth starts at population zero");
    runner.expect(!rule->densityPoints.empty() && rule->densityPoints.back().population == 1000000, name + " growth reaches its maximum at one million population");
    runner.expect(!rule->densityPoints.empty() && std::fabs(rule->densityPoints.back().maxDensityPerTile - expectedMaximum) < 0.001f, name + " growth maximum density matches XML cap");

    std::size_t pointIndex = 1;
    for (; pointIndex < rule->densityPoints.size(); ++pointIndex) {
        runner.expect(rule->densityPoints[pointIndex].population > rule->densityPoints[pointIndex - 1u].population, name + " growth populations are strictly increasing");
        runner.expect(rule->densityPoints[pointIndex].maxDensityPerTile >= rule->densityPoints[pointIndex - 1u].maxDensityPerTile, name + " growth densities are nondecreasing");
    }
}

void TestLotConstructionDurationLoading(TestRunner& runner) {
    CityParameterRegistry registry;
    const std::string root = MakeTempAssetDirectory("CityBuilderAssetDurations");
    WriteTextAssetFile(
        root + "\\Modules\\test_module.xml",
        "<module id=\"test_module\">"
        "<size width=\"1\" height=\"1\" />"
        "<effects airPollution=\"0\" landValue=\"0\" />"
        "</module>");
    WriteTextAssetFile(
        root + "\\Lots\\days_lot.xml",
        "<lot id=\"days_lot\" constructionDays=\"3\">"
        "<anchor x=\"0\" y=\"0\" />"
        "<footprint x=\"0\" y=\"0\" width=\"1\" height=\"1\" />"
        "<modules><moduleRef id=\"test_module\" x=\"0\" y=\"0\" /></modules>"
        "</lot>");
    WriteTextAssetFile(
        root + "\\Lots\\ticks_lot.xml",
        "<lot id=\"ticks_lot\" constructionTicks=\"5\">"
        "<anchor x=\"0\" y=\"0\" />"
        "<footprint x=\"0\" y=\"0\" width=\"1\" height=\"1\" />"
        "<modules><moduleRef id=\"test_module\" x=\"0\" y=\"0\" /></modules>"
        "</lot>");

    LoadedGameAssets assets;
    std::string errorMessage;
    runner.expect(LoadGameAssets(root, registry, assets, errorMessage), "construction duration test assets load" + (errorMessage.empty() ? std::string() : "\nLoader error: " + errorMessage));
    const LotAsset* daysLot = FindLotAsset(assets, "days_lot");
    const LotAsset* ticksLot = FindLotAsset(assets, "ticks_lot");
    runner.expect(daysLot != 0 && daysLot->constructionTicks == static_cast<int>(SimulationTime::daysToTicks(3u)), "constructionDays converts to stored ticks");
    runner.expect(ticksLot != 0 && ticksLot->constructionTicks == 5, "legacy constructionTicks remains raw ticks");
}

void TestRciCatalogTemplateCoverageAndDensityOrdering(TestRunner& runner) {
    CityParameterRegistry registry;
    LoadedGameAssets assets;
    if (!LoadCheckedAssets(runner, assets, registry)) {
        return;
    }

    ExpectGrowthRuleShape(runner, FindGrowthRule(assets, TileZoningResidential), "residential", 10u, 8.0f);
    ExpectGrowthRuleShape(runner, FindGrowthRule(assets, TileZoningIndustrial), "industrial", 8u, 3.0f);

    const LotAsset* houseLot = FindLotAsset(assets, "house_lot");
    runner.expect(houseLot != 0 && houseLot->accessDefinitions.size() == 2u, "house declares driveway and garden access");
    if (houseLot != 0) {
        bool foundGardenAccess = false;
        bool foundDrivewayAccess = false;
        std::size_t accessIndex = 0;
        for (; accessIndex < houseLot->accessDefinitions.size(); ++accessIndex) {
            const LotAccessDefinition& access = houseLot->accessDefinitions[accessIndex];
            if (access.localTile.x == 0 && access.localTile.y == 0 && access.direction == kRoadDirectionNorth && access.modeMask == kTransportModePedestrian) {
                foundGardenAccess = true;
            }
            if (access.localTile.x == 1 && access.localTile.y == 0 && access.direction == kRoadDirectionNorth && access.modeMask == kTransportModeCar) {
                foundDrivewayAccess = true;
            }
        }
        runner.expect(foundGardenAccess, "house garden access is a front pedestrian connection");
        runner.expect(foundDrivewayAccess, "house driveway access is a front car connection");
    }

    const LotModule* slenderTower = FindModule(assets, "slender_tower_module");
    runner.expect(slenderTower != 0 && slenderTower->width != slenderTower->height, "slender tower is not a square module");
    runner.expect(FindModule(assets, "wide_rowhouse_module") == 0, "wide rowhouse module is not present");

    std::map<std::string, int> groupCounts;
    std::map<std::string, DensityBuckets> densitiesByFootprint;
    int rciLotCount = 0;

    std::size_t lotIndex = 0;
    for (; lotIndex < assets.lots.size(); ++lotIndex) {
        const LotAsset& lotAsset = assets.lots[lotIndex];
        if (lotAsset.zoningType != TileZoningResidential && lotAsset.zoningType != TileZoningIndustrial) {
            continue;
        }

        ++rciLotCount;
        runner.expect(!lotAsset.name.empty(), lotAsset.id + " has a display name");
        runner.expect(lotAsset.densityBand == "low" || lotAsset.densityBand == "medium" || lotAsset.densityBand == "high", lotAsset.id + " declares a valid density band");
        runner.expect(lotAsset.hasFrontDirection, lotAsset.id + " declares an explicit front");

        if (lotAsset.zoningType == TileZoningResidential) {
            runner.expect(lotAsset.accessDefinitions.size() == 2u, lotAsset.id + " has front-only residential access");
        }

        std::size_t accessIndex = 0;
        for (; accessIndex < lotAsset.accessDefinitions.size(); ++accessIndex) {
            const LotAccessDefinition& access = lotAsset.accessDefinitions[accessIndex];
            runner.expect(access.localTile.y == 0 && access.direction == kRoadDirectionNorth, lotAsset.id + " access remains on the authored front edge");
        }

        int primaryCount = 0;
        std::size_t placementIndex = 0;
        for (; placementIndex < lotAsset.initialModules.size(); ++placementIndex) {
            const LotModule* module = FindModule(assets, lotAsset.initialModules[placementIndex].moduleId);
            runner.expect(module != 0, lotAsset.id + " references a known module");
            if (module == 0 || module->density.empty()) {
                continue;
            }

            ++primaryCount;
            runner.expect(module->density == lotAsset.densityBand, lotAsset.id + " primary module matches lot density band");
        }
        runner.expect(primaryCount >= 1 && primaryCount <= 2, lotAsset.id + " has one or two primary modules");

        if (lotAsset.footprintWidth >= 2 && lotAsset.footprintWidth <= 8 &&
            lotAsset.footprintHeight >= 2 && lotAsset.footprintHeight <= 8) {
            const int capacityParameterId = lotAsset.zoningType == TileZoningResidential
                ? registry.residentsLowWealthId()
                : registry.jobsDirtyIndustryId();
            const float capacity = LotAssetCapacity(assets, lotAsset, capacityParameterId);
            const float density = capacity / static_cast<float>(lotAsset.footprintWidth * lotAsset.footprintHeight);
            groupCounts[DensityGroupKey(lotAsset.zoningType, lotAsset.footprintWidth, lotAsset.footprintHeight, lotAsset.densityBand)] += 1;
            DensityBuckets& buckets = densitiesByFootprint[FootprintKey(lotAsset.zoningType, lotAsset.footprintWidth, lotAsset.footprintHeight)];
            if (lotAsset.densityBand == "low") {
                buckets.low.push_back(density);
            } else if (lotAsset.densityBand == "medium") {
                buckets.medium.push_back(density);
            } else if (lotAsset.densityBand == "high") {
                buckets.high.push_back(density);
            }
        }
    }

    runner.expect(rciLotCount == 588, "RCI catalog contains two templates for every 2x2 through 8x8 footprint, density, and zoning type");
    const std::uint16_t zoningTypes[] = {TileZoningResidential, TileZoningIndustrial};
    const char* densityBands[] = {"low", "medium", "high"};
    std::size_t zoningIndex = 0;
    for (; zoningIndex < sizeof(zoningTypes) / sizeof(zoningTypes[0]); ++zoningIndex) {
        int width = 2;
        for (; width <= 8; ++width) {
            int height = 2;
            for (; height <= 8; ++height) {
                std::size_t bandIndex = 0;
                for (; bandIndex < sizeof(densityBands) / sizeof(densityBands[0]); ++bandIndex) {
                    const std::string groupKey = DensityGroupKey(zoningTypes[zoningIndex], width, height, densityBands[bandIndex]);
                    runner.expect(groupCounts[groupKey] >= 2, groupKey + " has at least two valid templates");
                }

                const std::string footprintKey = FootprintKey(zoningTypes[zoningIndex], width, height);
                const DensityBuckets& buckets = densitiesByFootprint[footprintKey];
                if (!buckets.low.empty() && !buckets.medium.empty()) {
                    runner.expect(*std::max_element(buckets.low.begin(), buckets.low.end()) < *std::min_element(buckets.medium.begin(), buckets.medium.end()), footprintKey + " low templates are less dense than medium templates");
                }
                if (!buckets.medium.empty() && !buckets.high.empty()) {
                    runner.expect(*std::max_element(buckets.medium.begin(), buckets.medium.end()) < *std::min_element(buckets.high.begin(), buckets.high.end()), footprintKey + " medium templates are less dense than high templates");
                }
            }
        }
    }
}

void TestInvalidAssetValidation(TestRunner& runner) {
    CityParameterRegistry registry;
    const std::string validModule =
        "<module id=\"test_module\" density=\"low\">"
        "<size width=\"1\" height=\"1\" />"
        "<effects airPollution=\"0\" landValue=\"0\" />"
        "<parameters><driver id=\"residents.low_wealth\" amount=\"1\" /></parameters>"
        "</module>";
    const std::string validRciLot =
        "<lot id=\"test_lot\" name=\"Test Lot\" zoningType=\"residential\" densityBand=\"low\">"
        "<anchor x=\"0\" y=\"0\" />"
        "<footprint x=\"0\" y=\"0\" width=\"1\" height=\"1\" />"
        "<front direction=\"north\" />"
        "<modules><moduleRef id=\"test_module\" x=\"0\" y=\"0\" /></modules>"
        "<access><connection x=\"0\" y=\"0\" modes=\"pedestrian\" /></access>"
        "</lot>";

    runner.expect(
        InvalidAssetsRejected(
            "<module id=\"test_module\" density=\"suburban\">"
            "<size width=\"1\" height=\"1\" />"
            "<effects airPollution=\"0\" landValue=\"0\" />"
            "</module>",
            validRciLot,
            registry),
        "invalid primary module density rejects at load");

    runner.expect(
        InvalidAssetsRejected(
            validModule,
            "<lot id=\"test_lot\" name=\"Test Lot\" zoningType=\"residential\">"
            "<anchor x=\"0\" y=\"0\" />"
            "<footprint x=\"0\" y=\"0\" width=\"1\" height=\"1\" />"
            "<front direction=\"north\" />"
            "<modules><moduleRef id=\"test_module\" x=\"0\" y=\"0\" /></modules>"
            "<access><connection x=\"0\" y=\"0\" modes=\"pedestrian\" /></access>"
            "</lot>",
            registry),
        "constructor-enabled RCI lot without densityBand rejects at load");

    {
        const std::string root = MakeTempAssetDirectory("CityBuilderAssetMissingRciGrowth");
        WriteTextAssetFile(root + "\\Modules\\test_module.xml", validModule);
        WriteTextAssetFile(root + "\\Lots\\test_lot.xml", validRciLot);
        LoadedGameAssets assets;
        std::string errorMessage;
        ScopedCrashLogSuppression suppressExpectedAssetErrors;
        runner.expect(!LoadGameAssets(root, registry, assets, errorMessage) && !errorMessage.empty(), "constructor-enabled RCI lot without growth rule rejects at load");
    }

    {
        const std::string root = MakeTempAssetDirectory("CityBuilderAssetDuplicateRciGrowthDensity");
        WriteTextAssetFile(root + "\\Modules\\test_module.xml", validModule);
        WriteTextAssetFile(root + "\\Lots\\test_lot.xml", validRciLot);
        WriteTextAssetFile(
            root + "\\RCI\\rci_tools.xml",
            "<rciTools>"
            "<tool id=\"residential\" zoningType=\"residential\" />"
            "<rciGrowth zoningType=\"residential\" desirabilityThreshold=\"60\">"
            "<maxDensityPerTile population=\"0\" value=\"1\" />"
            "<maxDensityPerTile population=\"0\" value=\"2\" />"
            "</rciGrowth>"
            "</rciTools>");
        LoadedGameAssets assets;
        std::string errorMessage;
        ScopedCrashLogSuppression suppressExpectedAssetErrors;
        runner.expect(!LoadGameAssets(root, registry, assets, errorMessage) && !errorMessage.empty(), "duplicate RCI density population rejects at load");
    }
}

void RunRciConstructorFrontAccessGrowthTest(TestRunner& runner) {
    try {
        GameSession session(BuildSandboxRuntimeOptions());
        CitySaveState initialState = BuildCleanSandboxState();
        SetLandValueRect(initialState, RciRect(4, 5, 5, 8), kLandValueDisplayCap);
        AddSandboxCity(session, initialState);
        runner.expect(session.enterCity(0, 0), "RCI front-access sandbox city enters");

        session.runtime().queuePlaceStreetRoad(Int2(4, 3), Int2(5, 3), Int2(5, 3));
        session.runtime().queueZoneLot(MakeSandboxRciLot(RciRect(4, 5, 5, 8), TileZoningResidential));

        CitySaveState state;
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u &&
                    candidate.zoningLots.size() == 1u &&
                    !candidate.transport.tiles.empty();
            }),
            "RCI front-access setup creates one parcel and road");

        session.runtime().setGameSpeed(GameSpeed::FastForward);
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick > 17u &&
                    HasLotAssetPrefix(candidate, "rci_residential_2x4_low_") &&
                    candidate.zoningLots.empty();
            }),
            "front-facing residential parcel grows from its driveway and garden access");
        session.runtime().setGameSpeed(GameSpeed::Paused);
        session.shutdown();
    } catch (const std::exception& error) {
        runner.expect(false, std::string("RCI front-access growth test threw exception: ") + error.what());
    } catch (...) {
        runner.expect(false, "RCI front-access growth test threw unknown exception");
    }
}

void RunRciConstructorSideRoadRejectedTest(TestRunner& runner) {
    try {
        GameSession session(BuildSandboxRuntimeOptions());
        CitySaveState initialState = BuildCleanSandboxState();
        SetLandValueRect(initialState, RciRect(4, 5, 5, 8), kLandValueDisplayCap);
        AddSandboxCity(session, initialState);
        runner.expect(session.enterCity(0, 0), "RCI side-road sandbox city enters");

        session.runtime().queuePlaceStreetRoad(Int2(1, 5), Int2(1, 8), Int2(1, 8));
        session.runtime().queueZoneLot(MakeSandboxRciLot(RciRect(4, 5, 5, 8), TileZoningResidential));

        CitySaveState state;
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u &&
                    candidate.zoningLots.size() == 1u &&
                    !candidate.transport.tiles.empty();
            }),
            "RCI side-road setup creates one parcel and side road");

        session.runtime().setGameSpeed(GameSpeed::FastForward);
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick > 60u &&
                    candidate.lots.empty() &&
                    candidate.zoningLots.size() == 1u;
            }),
            "side-only road does not satisfy front-only residential access");
        session.runtime().setGameSpeed(GameSpeed::Paused);
        session.shutdown();
    } catch (const std::exception& error) {
        runner.expect(false, std::string("RCI side-road rejection test threw exception: ") + error.what());
    } catch (...) {
        runner.expect(false, "RCI side-road rejection test threw unknown exception");
    }
}

void RunRciConstructorBaselineLandValueRecoveryTest(TestRunner& runner) {
    try {
        GameSession session(BuildSandboxRuntimeOptions());
        CitySaveState initialState = BuildCleanSandboxState();
        SetLandValueRect(initialState, RciRect(4, 5, 5, 8), 0);
        AddSandboxCity(session, initialState);
        runner.expect(session.enterCity(0, 0), "RCI baseline land-value sandbox city enters");

        session.runtime().queuePlaceStreetRoad(Int2(4, 3), Int2(5, 3), Int2(5, 3));
        session.runtime().queueZoneLot(MakeSandboxRciLot(RciRect(4, 5, 5, 8), TileZoningResidential));

        CitySaveState state;
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u &&
                    candidate.zoningLots.size() == 1u &&
                    !candidate.transport.tiles.empty();
            }),
            "RCI zero-land-value setup creates one parcel and front road");

        session.runtime().setGameSpeed(GameSpeed::FastForward);
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick > 17u &&
                    HasLotAssetPrefix(candidate, "rci_residential_2x4_low_") &&
                    candidate.zoningLots.empty();
            }),
            "XML baseline land value recovers imported zero-land parcels");
        session.runtime().setGameSpeed(GameSpeed::Paused);
        session.shutdown();
    } catch (const std::exception& error) {
        runner.expect(false, std::string("RCI baseline land-value recovery test threw exception: ") + error.what());
    } catch (...) {
        runner.expect(false, "RCI baseline land-value recovery test threw unknown exception");
    }
}

void RunRoadAwareRciPlanCommitTest(TestRunner& runner) {
    try {
        GameSession session(BuildSandboxRuntimeOptions());
        AddSandboxCity(session, BuildCleanSandboxState());
        runner.expect(session.enterCity(0, 0), "road-aware RCI sandbox city enters");

        session.runtime().queuePlaceStreetRoad(Int2(4, 2), Int2(17, 2), Int2(17, 2));
        CitySaveState roadState;
        runner.expect(
            WaitForSandboxState(session, roadState, [](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u && !candidate.transport.tiles.empty();
            }),
            "road-aware RCI test creates existing frontage road");
        const std::size_t frontageRoadTileCount = roadState.transport.tiles.size();

        RciToolCatalog catalog;
        const RciTool* residential = catalog.findTool("residential");
        runner.expect(residential != 0, "road-aware RCI test finds residential tool");
        if (residential == 0) {
            session.shutdown();
            return;
        }

        RciPlan previewPlan;
        runner.expect(
            session.runtime().buildRciPlan(*residential, 4, 4, 17, 11, RciPlanMode::LotsAndRoads, previewPlan),
            "road-aware RCI runtime builds smart preview plan");
        runner.expect(!previewPlan.lots.empty(), "road-aware RCI preview creates parcels");
        runner.expect(!RciPlanHasHorizontalRoadAt(previewPlan, 4), "road-aware RCI preview skips duplicate edge road");

        session.runtime().queueRciPlan(previewPlan);
        CitySaveState committedState;
        runner.expect(
            WaitForSandboxState(session, committedState, [&previewPlan, frontageRoadTileCount](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u &&
                    candidate.transport.tiles.size() == frontageRoadTileCount &&
                    CityStateContainsPlanLots(candidate, previewPlan) &&
                    CityStateZonesPlanLots(candidate, previewPlan);
            }),
            "road-aware RCI commit matches preview parcels without adding duplicate roads");

        session.shutdown();
    } catch (const std::exception& error) {
        runner.expect(false, std::string("road-aware RCI plan commit test threw exception: ") + error.what());
    } catch (...) {
        runner.expect(false, "road-aware RCI plan commit test threw unknown exception");
    }
}
}

int main() {
    InitializeCrashLogger("City Builder RciLotConstructionTests");
    TestRunner runner;
    TestLotConstructionDurationLoading(runner);
    TestRciCatalogTemplateCoverageAndDensityOrdering(runner);
    TestInvalidAssetValidation(runner);
    RunRciConstructorFrontAccessGrowthTest(runner);
    RunRciConstructorSideRoadRejectedTest(runner);
    RunRciConstructorBaselineLandValueRecoveryTest(runner);
    RunRoadAwareRciPlanCommitTest(runner);
    return runner.finish();
}
