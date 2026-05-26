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
#include "LotAutoLayoutResolver.h"
#include "LotModulePlacementGeometry.h"
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
    options.showNonFatalAssetWarningDialogs = false;
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

RciToolCatalog LoadRciToolCatalogFixture(TestRunner& runner) {
    const std::string root = MakeTempAssetDirectory("CityBuilderRciToolCatalog");
    const std::string rciToolsPath = root + "\\RCI\\rci_tools.xml";
    WriteTextAssetFile(
        rciToolsPath,
        "<rci>"
        "<zone id=\"residential_low\" name=\"Low Density Residence\" zoningType=\"residential_low\" labelStringId=\"zone.tool.residential_low\" minDepth=\"2\" preferredDepth=\"4\" maxDepth=\"8\" minWidth=\"2\" preferredWidth=\"16\" maxWidth=\"24\" colorR=\"0.44\" colorG=\"0.92\" colorB=\"0.46\" colorA=\"0.50\" />"
        "<zone id=\"residential_high\" name=\"High Density Residence\" zoningType=\"residential_high\" labelStringId=\"zone.tool.residential_high\" minDepth=\"2\" preferredDepth=\"4\" maxDepth=\"8\" minWidth=\"2\" preferredWidth=\"16\" maxWidth=\"24\" colorR=\"0.10\" colorG=\"0.48\" colorB=\"0.20\" colorA=\"0.50\" />"
        "<zone id=\"industrial\" name=\"Industry\" zoningType=\"industrial\" labelStringId=\"zone.tool.industrial\" minDepth=\"2\" preferredDepth=\"8\" maxDepth=\"8\" minWidth=\"2\" preferredWidth=\"16\" maxWidth=\"24\" colorR=\"0.92\" colorG=\"0.76\" colorB=\"0.15\" colorA=\"0.50\" />"
        "<rciType id=\"low_wealth_residential\" name=\"Low Wealth Residential\" desirabilityOverlayStringId=\"overlay.desirability.low_wealth_residential\" demandParameterId=\"residents.low_wealth\" zoneTypes=\"low_density_residential,high_density_residential\" colorR=\"0.18\" colorG=\"0.72\" colorB=\"0.28\" colorA=\"0.50\" />"
        "<rciType id=\"dirty_industry\" name=\"Dirty Industry\" desirabilityOverlayStringId=\"overlay.desirability.dirty_industry\" demandParameterId=\"jobs.dirty_industry\" zoneTypes=\"industrial\" colorR=\"0.92\" colorG=\"0.76\" colorB=\"0.15\" colorA=\"0.50\" />"
        "</rci>");

    RciToolCatalog catalog;
    runner.expect(catalog.loadFromXmlFile(rciToolsPath), "RCI tool catalog fixture loads");
    return catalog;
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

template <typename Predicate>
bool FastForwardSandboxUntil(GameSession& session, CitySaveState& state, Predicate predicate) {
    int attempt = 0;
    for (; attempt < 300; ++attempt) {
        session.runtime().setGameSpeed(GameSpeed::FastForward);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        session.runtime().setGameSpeed(GameSpeed::Paused);
        state = ExportActiveSessionState(session);
        if (predicate(state)) {
            return true;
        }
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

bool TileRectHasZoning(const CitySaveState& state, const RciRect& rect, std::uint16_t zoningType) {
    int tileY = rect.minTileY;
    for (; tileY <= rect.maxTileY; ++tileY) {
        int tileX = rect.minTileX;
        for (; tileX <= rect.maxTileX; ++tileX) {
            if (state.tiles[SaveTileIndex(state, tileX, tileY)].zoningType != zoningType) {
                return false;
            }
        }
    }

    return true;
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

bool ContainsId(const std::vector<std::string>& ids, const std::string& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

int ModuleParameterAmount(const LotModule& module, int parameterId) {
    std::size_t contributionIndex = 0;
    for (; contributionIndex < module.parameterContributions.size(); ++contributionIndex) {
        const CityParameterContribution& contribution = module.parameterContributions[contributionIndex];
        if (contribution.parameterId == parameterId) {
            return contribution.amount;
        }
    }

    return 0;
}

int LotAssetCapacity(const LoadedGameAssets& assets, const LotAsset& lotAsset, int parameterId, int width, int height);

int LotAssetCapacity(const LoadedGameAssets& assets, const LotAsset& lotAsset, int parameterId) {
    return LotAssetCapacity(assets, lotAsset, parameterId, lotAsset.footprintWidth, lotAsset.footprintHeight);
}

int LotAssetCapacity(const LoadedGameAssets& assets, const LotAsset& lotAsset, int parameterId, int width, int height) {
    if (!lotAsset.autoLayout.empty()) {
        std::string primaryModuleId;
        int capacity = 0;
        std::size_t ruleIndex = 0;
        for (; ruleIndex < lotAsset.autoLayout.moduleRules.size(); ++ruleIndex) {
            const LotAutoModuleRule& rule = lotAsset.autoLayout.moduleRules[ruleIndex];
            if (!rule.isPrimary ||
                !LotAutoSizeConditionMatches(rule.condition, width, height)) {
                continue;
            }

            const LotModule* module = FindModule(assets, rule.moduleId);
            if (module == 0) {
                return 0;
            }

            primaryModuleId = rule.moduleId;
            if (rule.affectsSimulation) {
                capacity += ModuleParameterAmount(*module, parameterId);
            }
            break;
        }

        if (primaryModuleId.empty()) {
            return 0;
        }

        ruleIndex = 0;
        for (; ruleIndex < lotAsset.autoLayout.moduleRules.size(); ++ruleIndex) {
            const LotAutoModuleRule& rule = lotAsset.autoLayout.moduleRules[ruleIndex];
            if (rule.isPrimary ||
                !rule.affectsSimulation ||
                !LotAutoSizeConditionMatches(rule.condition, width, height)) {
                continue;
            }
            if (!rule.primaryModuleIds.empty() && !ContainsId(rule.primaryModuleIds, primaryModuleId)) {
                continue;
            }

            const LotModule* module = FindModule(assets, rule.moduleId);
            if (module != 0) {
                capacity += ModuleParameterAmount(*module, parameterId);
            }
        }

        return capacity;
    }

    int capacity = 0;
    std::size_t placementIndex = 0;
    for (; placementIndex < lotAsset.initialModules.size(); ++placementIndex) {
        const LotModule* module = FindModule(assets, lotAsset.initialModules[placementIndex].moduleId);
        if (module != 0) {
            capacity += ModuleParameterAmount(*module, parameterId);
        }
    }

    return capacity;
}

std::uint8_t RotateRoadDirectionForTest(std::uint8_t roadDirection, int rotationSteps) {
    std::uint8_t direction = roadDirection;
    int step = 0;
    const int normalizedRotation = ((rotationSteps % 4) + 4) % 4;
    for (; step < normalizedRotation; ++step) {
        if (direction == kRoadDirectionNorth) {
            direction = kRoadDirectionEast;
        } else if (direction == kRoadDirectionEast) {
            direction = kRoadDirectionSouth;
        } else if (direction == kRoadDirectionSouth) {
            direction = kRoadDirectionWest;
        } else if (direction == kRoadDirectionWest) {
            direction = kRoadDirectionNorth;
        }
    }

    return direction;
}

float MaximumDensityForZoningType(const LoadedGameAssets& assets, std::uint16_t zoningType) {
    const RciGrowthRule* rule = FindGrowthRule(assets, zoningType);
    if (rule == 0 || rule->densityPoints.empty()) {
        return 0.0f;
    }

    float maximumDensity = 0.0f;
    std::size_t pointIndex = 0;
    for (; pointIndex < rule->densityPoints.size(); ++pointIndex) {
        maximumDensity = std::max(maximumDensity, rule->densityPoints[pointIndex].maxDensityPerTile);
    }
    return maximumDensity;
}

float StarterDensityFloorForTest(std::uint16_t zoningType) {
    if (zoningType == TileZoningResidentialLow || zoningType == TileZoningResidentialHigh) {
        return 0.65f;
    }
    if (zoningType == TileZoningIndustrial) {
        return 0.75f;
    }
    return 0.0f;
}

std::string JoinTruncatedIds(const std::vector<std::string>& ids, std::size_t limit) {
    std::ostringstream text;
    const std::size_t shownCount = std::min(limit, ids.size());
    std::size_t idIndex = 0;
    for (; idIndex < shownCount; ++idIndex) {
        if (idIndex != 0u) {
            text << ", ";
        }
        text << ids[idIndex];
    }
    if (ids.size() > shownCount) {
        text << ", ... and " << (ids.size() - shownCount) << " more";
    }
    return text.str();
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
        "City Builder\\Data",
        "Data"
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
    WriteTextAssetFile(
        root + "\\Lots\\disabled_bad_lot.xml",
        "<lot id=\"disabled_bad_lot\" disabled=\"true\">"
        "<unsupported />"
        "</lot>");

    LoadedGameAssets assets;
    std::string errorMessage;
    runner.expect(LoadGameAssets(root, registry, assets, errorMessage), "construction duration test assets load" + (errorMessage.empty() ? std::string() : "\nLoader error: " + errorMessage));
    const LotAsset* daysLot = FindLotAsset(assets, "days_lot");
    const LotAsset* ticksLot = FindLotAsset(assets, "ticks_lot");
    runner.expect(daysLot != 0 && daysLot->constructionTicks == static_cast<int>(SimulationTime::daysToTicks(3u)), "constructionDays converts to stored ticks");
    runner.expect(ticksLot != 0 && ticksLot->constructionTicks == 5, "legacy constructionTicks remains raw ticks");
    runner.expect(FindLotAsset(assets, "disabled_bad_lot") == 0, "disabled lot files are ignored instead of loaded");
    runner.expect(assets.invalidLotReports.empty(), "disabled lot files are not validated or reported as invalid");
}

void TestModulePlacementClaimedFootprintAndVisualAlignment(TestRunner& runner) {
    CityParameterRegistry registry;
    const std::string root = MakeTempAssetDirectory("CityBuilderPlacementGeometry");
    WriteTextAssetFile(
        root + "\\Modules\\narrow_house_module.xml",
        "<module id=\"narrow_house_module\">"
        "<size width=\"2\" height=\"2\" />"
        "<effects airPollution=\"0\" landValue=\"0\" />"
        "</module>");
    WriteTextAssetFile(
        root + "\\Lots\\centered_narrow_house_lot.xml",
        "<lot id=\"centered_narrow_house_lot\">"
        "<anchor x=\"0\" y=\"0\" />"
        "<footprint x=\"0\" y=\"0\" width=\"3\" height=\"2\" />"
        "<modules><moduleRef id=\"narrow_house_module\" x=\"0\" y=\"0\" footprintWidth=\"3\" footprintHeight=\"2\" alignX=\"center\" /></modules>"
        "</lot>");

    LoadedGameAssets assets;
    std::string errorMessage;
    runner.expect(LoadGameAssets(root, registry, assets, errorMessage), "claimed footprint placement test assets load" + (errorMessage.empty() ? std::string() : "\nLoader error: " + errorMessage));
    const LotAsset* lotAsset = FindLotAsset(assets, "centered_narrow_house_lot");
    const LotModule* moduleAsset = FindModule(assets, "narrow_house_module");
    runner.expect(lotAsset != 0, "claimed footprint lot loads");
    runner.expect(moduleAsset != 0, "claimed footprint module loads");
    if (lotAsset == 0 || moduleAsset == 0 || lotAsset->initialModules.empty()) {
        return;
    }

    const LotModulePlacementGeometry geometry = ResolveLotModulePlacementGeometry(lotAsset->initialModules[0], *moduleAsset);
    runner.expect(geometry.footprintWidth == 3 && geometry.footprintHeight == 2, "module placement can claim a wider tile footprint than its body");
    runner.expect(std::fabs(geometry.renderOffsetX - 0.5f) < 0.001f, "center alignment produces subtile visual offset");
    runner.expect(std::fabs(geometry.renderWidth - 2.0f) < 0.001f && std::fabs(geometry.renderHeight - 2.0f) < 0.001f, "visual body keeps module dimensions by default");

    const LotModulePlacementGeometry rotatedGeometry = RotateLotModulePlacementGeometry(geometry, 1);
    runner.expect(rotatedGeometry.footprintWidth == 2 && rotatedGeometry.footprintHeight == 3, "claimed footprint rotates with the lot");
    runner.expect(std::fabs(rotatedGeometry.renderOffsetY - 0.5f) < 0.001f, "subtile visual offset rotates with the claimed footprint");
    runner.expect(LotModulePlacementGeometryVisualFits(rotatedGeometry), "rotated visual body remains inside the claimed footprint");
}

void TestDecorativePropPlacementsDoNotAffectSimulation(TestRunner& runner) {
    LotModule house;
    house.id = "house_module";
    house.width = 2;
    house.height = 2;
    house.airPollutionEmit = 8;
    house.parkEffectEmit = 4;
    house.landValueEmit = 12;

    LotModule tree;
    tree.id = "tree_prop_module";
    tree.width = 1;
    tree.height = 1;
    tree.airPollutionEmit = 80;
    tree.parkEffectEmit = 40;
    tree.landValueEmit = 120;

    Lot lot(1, "test_lot", 0, 0, 0);
    lot.addModule(house, Int2(0, 0), 8);
    lot.addModule(tree, Int2(0, 0), 8, 1, 1, 0.0f, 0.0f, 1.0f, 1.0f, false, false);

    runner.expect(lot.occupiedOffsets().size() == 4u, "decorative props do not claim additional occupied tiles");
    runner.expect(lot.moduleSummary() == "house_module x1", "decorative props stay out of lot module query summary");

    std::vector<Tile> tiles(16);
    int baseLandValue = 0;
    std::size_t tileIndex = 0;
    for (; tileIndex < tiles.size(); ++tileIndex) {
        baseLandValue += tiles[tileIndex].landValue;
    }

    lot.applyEffects(tiles);
    int totalAirPollution = 0;
    int totalParkEffect = 0;
    int totalLandValue = 0;
    for (tileIndex = 0; tileIndex < tiles.size(); ++tileIndex) {
        totalAirPollution += tiles[tileIndex].airPollution;
        totalParkEffect += tiles[tileIndex].parkEffect;
        totalLandValue += tiles[tileIndex].landValue;
    }
    runner.expect(totalAirPollution == house.airPollutionEmit, "decorative props do not add pollution effects");
    runner.expect(totalParkEffect == house.parkEffectEmit, "decorative props do not add park effects");
    runner.expect(totalLandValue - baseLandValue == house.landValueEmit, "decorative props do not add land-value effects");

    std::vector<LotRenderInstance> renderInstances;
    lot.buildRenderInstances(renderInstances);
    runner.expect(renderInstances.size() == 6u, "decorative props still produce render instances");

    LotModule yard;
    yard.id = "yard_module";
    yard.width = 1;
    yard.height = 1;
    LotModulePropDefinition treeProp;
    treeProp.moduleId = tree.id;
    treeProp.module = &tree;
    yard.props.push_back(treeProp);

    Lot propLot(2, "module_prop_test_lot", 0, 0, 0);
    propLot.addModule(yard, Int2(0, 0), 8, 1, 1, 0.0f, 0.0f, 1.0f, 1.0f, false, true);
    std::vector<LotRenderInstance> propRenderInstances;
    propLot.buildRenderInstances(propRenderInstances);
    runner.expect(propRenderInstances.size() == 3u, "module-defined props produce render instances without being lot placements");
    runner.expect(propLot.moduleSummary().empty(), "module-defined props stay out of lot module query summary");
}

void TestLowResidentialRowhouseYardTemplateRules(TestRunner& runner) {
    CityParameterRegistry registry;
    LoadedGameAssets assets;
    if (!LoadCheckedAssets(runner, assets, registry)) {
        return;
    }

    const LotModule* gardenModule = FindModule(assets, "garden_module");
    const LotModule* yardModule = FindModule(assets, "yard_module");
    const LotModule* fenceModule = FindModule(assets, "fence_module");
    const LotModule* rowHouseModule = FindModule(assets, "row_house_module");
    const LotModule* duplexModule = FindModule(assets, "duplex_module");
    const LotModule* thinRowhouseModule = FindModule(assets, "rci_residential_thin_rowhouse_2w_module");
    const LotModule* wideThinRowhouseModule = FindModule(assets, "rci_residential_thin_rowhouse_3w_module");
    runner.expect(gardenModule != 0, "garden module exists");
    runner.expect(yardModule != 0, "yard module exists");
    runner.expect(fenceModule != 0, "fence module exists");
    runner.expect(FindModule(assets, "deep_rowhouse_module") == 0, "bespoke two-wide deep rowhouse module is not present");
    runner.expect(FindModule(assets, "rci_residential_deep_rowhouse_3w_module") == 0, "bespoke three-wide deep rowhouse module is not present");
    runner.expect(thinRowhouseModule != 0, "two-wide thin rowhouse module exists");
    runner.expect(wideThinRowhouseModule != 0, "three-wide thin rowhouse module exists");
    if (rowHouseModule != 0 && thinRowhouseModule != 0) {
        runner.expect(
            thinRowhouseModule->height == 1 &&
                thinRowhouseModule->width == 2 &&
                ModuleParameterAmount(*thinRowhouseModule, registry.residentsLowWealthId()) == 4 &&
                ModuleParameterAmount(*rowHouseModule, registry.residentsLowWealthId()) == 8,
            "two-wide thin and full rowhouse capacities are 4 and 8");
        runner.expect(
            rowHouseModule->renderMeshKey == "rowhouse_2_roof" &&
                thinRowhouseModule->renderMeshKey == "rowhouse_2_roof",
            "two-wide rowhouse modules share the segmented three-roof mesh");
    }
    if (duplexModule != 0 && wideThinRowhouseModule != 0) {
        runner.expect(
            wideThinRowhouseModule->height == 1 &&
                wideThinRowhouseModule->width == 3 &&
                ModuleParameterAmount(*wideThinRowhouseModule, registry.residentsLowWealthId()) == 6 &&
                ModuleParameterAmount(*duplexModule, registry.residentsLowWealthId()) == 12,
            "three-wide thin and full rowhouse capacities are 6 and 12");
        runner.expect(
            duplexModule->renderMeshKey == "rowhouse_3_roof" &&
                wideThinRowhouseModule->renderMeshKey == "rowhouse_3_roof",
            "three-wide rowhouse modules share the segmented three-roof mesh");
    }
    const LotModule* trailerModule = FindModule(assets, "trailer_module");
    runner.expect(
        trailerModule != 0 &&
            trailerModule->width == 2 &&
            trailerModule->height == 1 &&
            ModuleParameterAmount(*trailerModule, registry.residentsLowWealthId()) == 4,
        "starter trailer is a two-by-one four-resident house variant");

    const char* houseVariantIds[] = {
        "rci_residential_house_4_module",
        "rci_residential_house_5_module",
        "rci_residential_house_6_module",
        "rci_residential_house_7_module",
        "rci_residential_house_8_module"
    };
    const int houseVariantCapacities[] = {4, 5, 6, 7, 8};
    std::size_t houseVariantIndex = 0;
    for (; houseVariantIndex < sizeof(houseVariantIds) / sizeof(houseVariantIds[0]); ++houseVariantIndex) {
        const LotModule* houseVariant = FindModule(assets, houseVariantIds[houseVariantIndex]);
        runner.expect(houseVariant != 0, std::string(houseVariantIds[houseVariantIndex]) + " exists");
        if (houseVariant == 0) {
            continue;
        }

        runner.expect(
            houseVariant->width == 2 &&
                houseVariant->height == 2 &&
                houseVariant->renderMeshKey == "gabled_roof" &&
                ModuleParameterAmount(*houseVariant, registry.residentsLowWealthId()) == houseVariantCapacities[houseVariantIndex],
            std::string(houseVariantIds[houseVariantIndex]) + " is a two-by-two gabled low-density capacity step");

        bool foundGardenUnderlay = false;
        bool foundPathUnderlay = false;
        std::size_t propIndex = 0;
        for (; propIndex < houseVariant->props.size(); ++propIndex) {
            const LotModulePropDefinition& prop = houseVariant->props[propIndex];
            if (prop.moduleId == "garden_module" &&
                prop.footprintWidth == 2 &&
                prop.footprintHeight == 2 &&
                prop.hasRenderWidth &&
                prop.hasRenderHeight) {
                foundGardenUnderlay = true;
            }
            if (prop.moduleId == "pathway_module" &&
                prop.footprintWidth == 2 &&
                prop.footprintHeight == 2 &&
                prop.hasRenderWidth &&
                prop.renderWidth < 0.5f) {
                foundPathUnderlay = true;
            }
        }

        runner.expect(foundGardenUnderlay, std::string(houseVariantIds[houseVariantIndex]) + " paints garden under unoccupied visual footprint");
        runner.expect(foundPathUnderlay, std::string(houseVariantIds[houseVariantIndex]) + " carries a narrow path underlay");
    }
    if (yardModule != 0 && gardenModule != 0) {
        runner.expect(
            std::fabs(yardModule->colorR - gardenModule->colorR) < 0.001f &&
                std::fabs(yardModule->colorG - gardenModule->colorG) < 0.001f &&
                std::fabs(yardModule->colorB - gardenModule->colorB) < 0.001f,
            "yard grass uses the brighter residential garden color");
    }

    if (yardModule != 0) {
        bool foundYardTreeProp = false;
        bool foundTreeChoice = false;
        bool foundEmptyChoice = false;
        std::size_t propIndex = 0;
        for (; propIndex < yardModule->props.size(); ++propIndex) {
            const LotModulePropDefinition& prop = yardModule->props[propIndex];
            if (prop.moduleId != "garden_tree_module") {
                continue;
            }

            foundYardTreeProp = true;
            std::size_t alternativeIndex = 0;
            for (; alternativeIndex < prop.alternatives.size(); ++alternativeIndex) {
                if (prop.alternatives[alternativeIndex].moduleId == "garden_tree_module" &&
                    prop.alternatives[alternativeIndex].weight == 1) {
                    foundTreeChoice = true;
                }
                if (prop.alternatives[alternativeIndex].moduleId == "none" &&
                    prop.alternatives[alternativeIndex].weight == 3) {
                    foundEmptyChoice = true;
                }
            }
        }

        runner.expect(foundYardTreeProp, "yard module owns its random tree prop");
        runner.expect(foundTreeChoice && foundEmptyChoice, "yard tree prop uses a one-in-four tree rate");
    }

    runner.expect(FindLotAsset(assets, "rci_residential_low_court_lot") == 0, "disabled low court lot is ignored by the catalog");

    const char* lotIds[] = {
        "rci_residential_low_garden_lot"
    };
    std::size_t lotIdIndex = 0;
    for (; lotIdIndex < sizeof(lotIds) / sizeof(lotIds[0]); ++lotIdIndex) {
        const LotAsset* lotAsset = FindLotAsset(assets, lotIds[lotIdIndex]);
        runner.expect(lotAsset != 0, std::string(lotIds[lotIdIndex]) + " loads");
        if (lotAsset == 0) {
            continue;
        }

        bool foundThreeWidePrimary = false;
        bool foundThreeWideSecondRow = false;
        bool foundYardPath = false;
        bool foundNarrowGardenPath = false;
        bool foundNarrowDriveway = false;
        bool foundCapacitySteppedHouses[5] = {false, false, false, false, false};
        bool foundLotLayerTreeProp = false;
        bool foundThreeWideThinDepthFourRule = false;
        std::size_t ruleIndex = 0;
        for (; ruleIndex < lotAsset->autoLayout.moduleRules.size(); ++ruleIndex) {
            const LotAutoModuleRule& rule = lotAsset->autoLayout.moduleRules[ruleIndex];
            if ((rule.isPrimary && rule.moduleId == "row_house_module") ||
                rule.moduleId == "rci_residential_thin_rowhouse_2w_module" ||
                (rule.isPrimary && rule.moduleId == "narrow_rowhouse_module")) {
                runner.expect(
                    rule.condition.minWidth == 2 && rule.condition.maxWidth == 2,
                    lotAsset->id + " normal rowhouse rules only match 2-wide parcels");
            }
            if ((rule.isPrimary && rule.moduleId == "duplex_module") ||
                rule.moduleId == "rci_residential_thin_rowhouse_3w_module" ||
                (rule.moduleId == "duplex_module" && ContainsId(rule.primaryModuleIds, "duplex_module"))) {
                runner.expect(
                    rule.condition.minWidth == 3 && rule.condition.maxWidth == 3,
                    lotAsset->id + " wide rowhouse rules only match 3-wide parcels");
            }
            if (rule.isPrimary && rule.moduleId == "row_house_module") {
                runner.expect(rule.condition.minDepth == 2 && rule.condition.maxDepth == 8, lotAsset->id + " two-wide rowhouse primary stays flexible through depth 8");
            }
            if (rule.isPrimary && rule.moduleId == "duplex_module") {
                foundThreeWidePrimary = true;
                runner.expect(rule.condition.minDepth == 2 && rule.condition.maxDepth == 8, lotAsset->id + " three-wide rowhouse primary stays flexible through depth 8");
            }
            if (rule.moduleId == "row_house_module" &&
                ContainsId(rule.primaryModuleIds, "row_house_module") &&
                rule.affectsSimulation &&
                rule.claimsFootprint) {
                runner.expect(rule.condition.minDepth == 5 && rule.condition.maxDepth == 8 && rule.yOffset == 3, lotAsset->id + " composes deep 2-wide lots from a second rowhouse row");
            }
            if (rule.moduleId == "duplex_module" &&
                ContainsId(rule.primaryModuleIds, "duplex_module") &&
                rule.affectsSimulation &&
                rule.claimsFootprint) {
                foundThreeWideSecondRow = rule.condition.minDepth == 5 && rule.condition.maxDepth == 8 && rule.yOffset == 3;
            }
            if (rule.moduleId == "garden_tree_module" &&
                !rule.affectsSimulation &&
                !rule.claimsFootprint) {
                foundLotLayerTreeProp = true;
            }
            if (rule.moduleId == "rci_residential_thin_rowhouse_3w_module" &&
                ContainsId(rule.primaryModuleIds, "duplex_module") &&
                LotAutoSizeConditionMatches(rule.condition, 3, 4) &&
                rule.affectsSimulation &&
                rule.claimsFootprint) {
                foundThreeWideThinDepthFourRule = true;
            }
            if (rule.moduleId == "driveway_module" &&
                !rule.affectsSimulation &&
                !rule.claimsFootprint &&
                rule.hasRenderWidth &&
                rule.renderWidth < 0.5f) {
                foundNarrowDriveway = true;
            }
            std::size_t variantIndex = 0;
            for (; variantIndex < sizeof(houseVariantIds) / sizeof(houseVariantIds[0]); ++variantIndex) {
                if (rule.isPrimary &&
                    rule.moduleId == houseVariantIds[variantIndex] &&
                    rule.hasRenderWidth &&
                    rule.renderWidth < 2.0f &&
                    rule.renderAlignX == kLotModulePlacementAlignCenter &&
                    rule.renderAlignY == kLotModulePlacementAlignCenter) {
                    foundCapacitySteppedHouses[variantIndex] = true;
                }
            }
        }

        std::size_t lineIndex = 0;
        for (; lineIndex < lotAsset->autoLayout.lineRules.size(); ++lineIndex) {
            const LotAutoLineRule& rule = lotAsset->autoLayout.lineRules[lineIndex];
            if (rule.moduleId == "pathway_module" &&
                (ContainsId(rule.primaryModuleIds, "row_house_module") || ContainsId(rule.primaryModuleIds, "duplex_module")) &&
                !rule.affectsSimulation &&
                rule.claimsFootprint) {
                foundYardPath = true;
            }
            if (rule.moduleId == "pathway_module" &&
                rule.primaryModuleIds.empty() &&
                !rule.affectsSimulation &&
                !rule.claimsFootprint &&
                rule.hasRenderWidth &&
                rule.renderWidth < 0.5f) {
                foundNarrowGardenPath = true;
            }
        }

        bool foundYardFill = false;
        std::size_t fillIndex = 0;
        for (; fillIndex < lotAsset->autoLayout.fillRules.size(); ++fillIndex) {
            const LotAutoFillRule& rule = lotAsset->autoLayout.fillRules[fillIndex];
            if (rule.moduleId == "yard_module" &&
                ContainsId(rule.primaryModuleIds, "duplex_module") &&
                !rule.affectsSimulation &&
                rule.claimsFootprint) {
                foundYardFill = true;
            }
        }

        bool foundFenceEdge = false;
        std::size_t edgeIndex = 0;
        for (; edgeIndex < lotAsset->autoLayout.edgeRules.size(); ++edgeIndex) {
            const LotAutoEdgeRule& rule = lotAsset->autoLayout.edgeRules[edgeIndex];
            if (rule.moduleId == "fence_module" &&
                rule.sourceModuleId == "yard_module" &&
                !rule.affectsSimulation &&
                !rule.claimsFootprint) {
                foundFenceEdge = true;
            }
        }

        runner.expect(!foundLotLayerTreeProp, lotAsset->id + " leaves random tree props on modules rather than lot rules");
        runner.expect(foundYardPath, lotAsset->id + " rowhouse yards place a claimed render-only grey path");
        runner.expect(foundYardFill, lotAsset->id + " rowhouse layouts fill remaining space with non-sim yard tiles");
        runner.expect(foundFenceEdge, lotAsset->id + " yard tiles request boundary fence props");
        runner.expect(foundThreeWidePrimary, lotAsset->id + " has a flexible 3-wide rowhouse primary");
        runner.expect(foundThreeWideSecondRow, lotAsset->id + " composes deep 3-wide lots from a second wide rowhouse row");
        runner.expect(foundThreeWideThinDepthFourRule, lotAsset->id + " adds a 3x4 full-plus-thin rowhouse variation");
        runner.expect(foundNarrowDriveway, lotAsset->id + " uses a narrow driveway overlay");
        runner.expect(foundNarrowGardenPath, lotAsset->id + " uses a narrow garden path overlay");
        std::size_t variantIndex = 0;
        for (; variantIndex < sizeof(foundCapacitySteppedHouses) / sizeof(foundCapacitySteppedHouses[0]); ++variantIndex) {
            runner.expect(foundCapacitySteppedHouses[variantIndex], lotAsset->id + " uses each 4-8 resident house visual scale step");
        }
    }
}

void TestRciCatalogTemplateCoverageAndDensityOrdering(TestRunner& runner) {
    CityParameterRegistry registry;
    LoadedGameAssets assets;
    if (!LoadCheckedAssets(runner, assets, registry)) {
        return;
    }

    const RciGrowthRule* lowDensityResidential = FindGrowthRule(assets, TileZoningResidentialLow);
    const RciGrowthRule* highDensityResidential = FindGrowthRule(assets, TileZoningResidentialHigh);
    ExpectGrowthRuleShape(runner, lowDensityResidential, "low density residential", 6u, 1.45f);
    ExpectGrowthRuleShape(runner, highDensityResidential, "high density residential", 6u, 8.0f);
    ExpectGrowthRuleShape(runner, FindGrowthRule(assets, TileZoningIndustrial), "industrial", 8u, 3.0f);
    runner.expect(std::fabs(assets.rciConstructorRedevelopmentCapacityIncrease - 0.2f) < 0.001f, "RCI redevelopment capacity threshold loads from XML");
    if (lowDensityResidential != 0 && highDensityResidential != 0) {
        runner.expect(
            lowDensityResidential->densityPoints.size() == highDensityResidential->densityPoints.size(),
            "high density residential uses the same growth curve length as low density residential");
        const std::size_t comparablePointCount = std::min(lowDensityResidential->densityPoints.size(), highDensityResidential->densityPoints.size());
        if (comparablePointCount > 1u) {
            std::size_t pointIndex = 0u;
            for (; pointIndex + 1u < comparablePointCount; ++pointIndex) {
                runner.expect(
                    lowDensityResidential->densityPoints[pointIndex].population == highDensityResidential->densityPoints[pointIndex].population,
                    "high density residential uses the same population breakpoints as low density residential before the cap");
                runner.expect(
                    std::fabs(lowDensityResidential->densityPoints[pointIndex].maxDensityPerTile - highDensityResidential->densityPoints[pointIndex].maxDensityPerTile) < 0.001f,
                    "high density residential uses the same density values as low density residential before the cap");
            }
        }
    }

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
            if (access.isDynamic) {
                runner.expect(
                    access.yReference == kLotAutoReferenceLotStart &&
                        access.direction == kRoadDirectionNorth,
                    lotAsset.id + " dynamic access remains on the authored front edge");
            } else {
                runner.expect(access.localTile.y == 0 && access.direction == kRoadDirectionNorth, lotAsset.id + " access remains on the authored front edge");
            }
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

        std::size_t autoRuleIndex = 0;
        for (; autoRuleIndex < lotAsset.autoLayout.moduleRules.size(); ++autoRuleIndex) {
            const LotAutoModuleRule& rule = lotAsset.autoLayout.moduleRules[autoRuleIndex];
            const LotModule* module = FindModule(assets, rule.moduleId);
            runner.expect(module != 0, lotAsset.id + " references a known auto module");
            if (module == 0 || !rule.isPrimary) {
                continue;
            }

            ++primaryCount;
            runner.expect(module->density == lotAsset.densityBand, lotAsset.id + " auto primary module matches lot density band");
        }
        runner.expect(primaryCount >= 1, lotAsset.id + " has primary module rules");

        const int capacityParameterId = lotAsset.zoningType == TileZoningResidential
            ? registry.residentsLowWealthId()
            : registry.jobsDirtyIndustryId();
        const int minWidth = lotAsset.compatibility.isExplicit ? lotAsset.compatibility.minWidth : lotAsset.footprintWidth;
        const int maxWidth = lotAsset.compatibility.isExplicit ? lotAsset.compatibility.maxWidth : lotAsset.footprintWidth;
        const int minDepth = lotAsset.compatibility.isExplicit ? lotAsset.compatibility.minDepth : lotAsset.footprintHeight;
        const int maxDepth = lotAsset.compatibility.isExplicit ? lotAsset.compatibility.maxDepth : lotAsset.footprintHeight;
        int width = std::max(2, minWidth);
        for (; width <= std::min(8, maxWidth); ++width) {
            int height = std::max(2, minDepth);
            for (; height <= std::min(8, maxDepth); ++height) {
                const int capacity = LotAssetCapacity(assets, lotAsset, capacityParameterId, width, height);
                if (capacity <= 0) {
                    continue;
                }

                const float density = capacity / static_cast<float>(width * height);
                groupCounts[DensityGroupKey(lotAsset.zoningType, width, height, lotAsset.densityBand)] += 1;
                DensityBuckets& buckets = densitiesByFootprint[FootprintKey(lotAsset.zoningType, width, height)];
                if (lotAsset.densityBand == "low") {
                    buckets.low.push_back(density);
                } else if (lotAsset.densityBand == "medium") {
                    buckets.medium.push_back(density);
                } else if (lotAsset.densityBand == "high") {
                    buckets.high.push_back(density);
                }
            }
        }
    }

    runner.expect(rciLotCount == 11, "RCI catalog skips disabled deferred low court template");
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
                    const bool lowResidentialCourtDisabled = zoningTypes[zoningIndex] == TileZoningResidential &&
                        std::string(densityBands[bandIndex]) == "low";
                    const int expectedTemplateCount = lowResidentialCourtDisabled ? 1 : 2;
                    runner.expect(groupCounts[groupKey] >= expectedTemplateCount, groupKey + " has the expected number of enabled templates");
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

void TestRciConstructorCanSelectEveryTemplate(TestRunner& runner) {
    CityParameterRegistry registry;
    LoadedGameAssets assets;
    if (!LoadCheckedAssets(runner, assets, registry)) {
        return;
    }

    SimulationRuntime runtime(BuildSandboxRuntimeOptions(16, 16));
    std::map<std::string, bool> selectedTemplates;
    std::map<std::string, bool> templatesWithSupportedCapacity;
    std::map<std::string, std::string> templateDiagnostics;
    std::map<std::string, bool> visitedProbes;
    int constructorTemplateCount = 0;

    std::size_t lotIndex = 0;
    for (; lotIndex < assets.lots.size(); ++lotIndex) {
        const LotAsset& lotAsset = assets.lots[lotIndex];
        if (lotAsset.zoningType != TileZoningResidentialLow &&
            lotAsset.zoningType != TileZoningResidentialHigh &&
            lotAsset.zoningType != TileZoningIndustrial) {
            continue;
        }

        ++constructorTemplateCount;
        if (selectedTemplates.find(lotAsset.id) == selectedTemplates.end()) {
            selectedTemplates[lotAsset.id] = false;
        }
        templatesWithSupportedCapacity[lotAsset.id] = false;
        const int capacityParameterId = lotAsset.zoningType == TileZoningIndustrial
            ? registry.jobsDirtyIndustryId()
            : registry.residentsLowWealthId();
        const float maximumDensity = MaximumDensityForZoningType(assets, lotAsset.zoningType);
        if (maximumDensity <= 0.0f) {
            templateDiagnostics[lotAsset.id] = "no density rule";
            continue;
        }

        const int minWidth = lotAsset.compatibility.isExplicit ? lotAsset.compatibility.minWidth : lotAsset.footprintWidth;
        const int maxWidth = lotAsset.compatibility.isExplicit ? lotAsset.compatibility.maxWidth : lotAsset.footprintWidth;
        const int minDepth = lotAsset.compatibility.isExplicit ? lotAsset.compatibility.minDepth : lotAsset.footprintHeight;
        const int maxDepth = lotAsset.compatibility.isExplicit ? lotAsset.compatibility.maxDepth : lotAsset.footprintHeight;
        bool queuedReachabilityProbe = false;
        int templateWidth = std::max(1, minWidth);
        for (; templateWidth <= maxWidth && !queuedReachabilityProbe; ++templateWidth) {
            int templateHeight = std::max(1, minDepth);
            for (; templateHeight <= maxDepth && !queuedReachabilityProbe; ++templateHeight) {
                const int capacity = LotAssetCapacity(assets, lotAsset, capacityParameterId, templateWidth, templateHeight);
                if (capacity <= 0) {
                    continue;
                }

                templatesWithSupportedCapacity[lotAsset.id] = true;
                const float footprintArea = static_cast<float>(std::max(1, templateWidth * templateHeight));
                const float requiredDensity = capacity / footprintArea;
                if (requiredDensity > maximumDensity + 0.001f) {
                    std::ostringstream reason;
                    reason << "density " << requiredDensity << " > max " << maximumDensity
                        << " at " << templateWidth << "x" << templateHeight;
                    templateDiagnostics[lotAsset.id] = reason.str();
                    continue;
                }

                const float densityCap = std::max(
                    StarterDensityFloorForTest(lotAsset.zoningType),
                    std::min(maximumDensity, requiredDensity + 0.0001f));
                const float demandBudget = capacity + 0.01f;
                std::ostringstream probeKeyBuilder;
                probeKeyBuilder << lotAsset.zoningType << "|" << lotAsset.rciTypeId << "|"
                    << templateWidth << "x" << templateHeight << "|"
                    << demandBudget << "|" << densityCap << "|"
                    << static_cast<int>(lotAsset.hasFrontDirection ? lotAsset.frontDirection : kRoadDirectionNorth);
                const std::string probeKey = probeKeyBuilder.str();
                if (visitedProbes[probeKey]) {
                    queuedReachabilityProbe = true;
                    continue;
                }

                templateDiagnostics[lotAsset.id].clear();
                visitedProbes[probeKey] = true;
                std::uint32_t variationSeed = 0u;
                for (; variationSeed < 64u; ++variationSeed) {
                    std::string selectedLotAssetId;
                    int selectedRotationSteps = 0;
                    int selectedCapacity = 0;
                    if (!runtime.selectRciConstructorLotAssetForDiagnostics(
                            lotAsset.zoningType,
                            lotAsset.rciTypeId,
                            templateWidth,
                            templateHeight,
                            demandBudget,
                            densityCap,
                            lotAsset.hasFrontDirection ? lotAsset.frontDirection : kRoadDirectionNorth,
                            variationSeed,
                            selectedLotAssetId,
                            selectedRotationSteps,
                            selectedCapacity)) {
                        continue;
                    }

                    selectedTemplates[selectedLotAssetId] = true;
                }
                queuedReachabilityProbe = true;
            }
        }
    }

    std::vector<std::string> unreachableTemplates;
    std::map<std::string, bool>::const_iterator selectedIt = selectedTemplates.begin();
    for (; selectedIt != selectedTemplates.end(); ++selectedIt) {
        if (selectedIt->second) {
            continue;
        }

        std::ostringstream reason;
        reason << selectedIt->first;
        if (!templatesWithSupportedCapacity[selectedIt->first]) {
            reason << " (no supported parcel has capacity)";
        } else if (!templateDiagnostics[selectedIt->first].empty()) {
            reason << " (" << templateDiagnostics[selectedIt->first] << ")";
        }
        unreachableTemplates.push_back(reason.str());
    }

    runner.expect(constructorTemplateCount == 11, "constructor reachability test scans the enabled compact RCI template catalog");
    runner.expect(
        unreachableTemplates.empty(),
        "every RCI lot template can be selected by the constructor for some parcel, demand, density, front, and variation seed"
            + (unreachableTemplates.empty() ? std::string() : "\nUnreachable templates: " + JoinTruncatedIds(unreachableTemplates, 24u)));
}

void TestDirtyIndustryModulesEmitDensityLandValue(TestRunner& runner) {
    CityParameterRegistry registry;
    LoadedGameAssets assets;
    if (!LoadCheckedAssets(runner, assets, registry)) {
        return;
    }

    const int dirtyIndustryId = registry.jobsDirtyIndustryId();
    std::vector<int> lowLandValues;
    std::vector<int> mediumLandValues;
    std::vector<int> highLandValues;
    int checkedModuleCount = 0;

    std::size_t moduleIndex = 0;
    for (; moduleIndex < assets.modules.size(); ++moduleIndex) {
        const LotModule& module = assets.modules[moduleIndex];
        if (ModuleParameterAmount(module, dirtyIndustryId) <= 0) {
            continue;
        }

        ++checkedModuleCount;
        runner.expect(module.landValueEmit > 0, module.id + " emits developed industrial land value");
        if (module.density == "low") {
            lowLandValues.push_back(module.landValueEmit);
        } else if (module.density == "medium") {
            mediumLandValues.push_back(module.landValueEmit);
        } else if (module.density == "high") {
            highLandValues.push_back(module.landValueEmit);
        } else {
            runner.expect(false, module.id + " declares a dirty-industry density band");
        }
    }

    runner.expect(checkedModuleCount >= 12, "dirty industry primary module catalog is loaded");
    runner.expect(!lowLandValues.empty(), "dirty industry has low-density land-value modules");
    runner.expect(!mediumLandValues.empty(), "dirty industry has medium-density land-value modules");
    runner.expect(!highLandValues.empty(), "dirty industry has high-density land-value modules");
    if (!lowLandValues.empty() && !mediumLandValues.empty()) {
        runner.expect(*std::max_element(lowLandValues.begin(), lowLandValues.end()) < *std::min_element(mediumLandValues.begin(), mediumLandValues.end()), "medium dirty-industry modules emit more land value than low dirty-industry modules");
    }
    if (!mediumLandValues.empty() && !highLandValues.empty()) {
        runner.expect(*std::max_element(mediumLandValues.begin(), mediumLandValues.end()) < *std::min_element(highLandValues.begin(), highLandValues.end()), "high dirty-industry modules emit more land value than medium dirty-industry modules");
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
        "<lot id=\"rci_test_lot\" name=\"Test Lot\" zoningType=\"residential\" densityBand=\"low\">"
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
            "<module id=\"test_module\" density=\"low\">"
            "<size width=\"1\" height=\"1\" />"
            "<effects airPollution=\"0\" landValue=\"0\" />"
            "<parameters><driver id=\"residents.low_wealth\" amount=\"3.2\" /></parameters>"
            "</module>",
            validRciLot,
            registry),
        "fractional module capacity rejects at load");

    runner.expect(
        InvalidAssetsRejected(
            validModule,
            "<lot id=\"rci_test_lot\" name=\"Test Lot\" zoningType=\"residential\">"
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

        runner.expect(
            FastForwardSandboxUntil(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick > 17u &&
                    HasLotAssetPrefix(candidate, "rci_residential_low_") &&
                    candidate.zoningLots.empty();
            }),
            "front-facing residential parcel grows from its driveway and garden access");
        const TileQueryResult queryResult = session.runtime().queryTile(4, 5);
        runner.expect(
            queryResult.hasLot && queryResult.rciCapacityMaximum > 0,
            "RCI building query reports maximum capacity");
        runner.expect(
            queryResult.rciCapacityCurrent >= 0 && queryResult.rciCapacityCurrent <= queryResult.rciCapacityMaximum,
            "RCI building query reports actual capacity clamped to maximum");
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

        runner.expect(
            FastForwardSandboxUntil(session, state, [](const CitySaveState& candidate) {
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

void RunRciConstructorZeroLandValueStarterFloorTest(TestRunner& runner) {
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

        runner.expect(
            FastForwardSandboxUntil(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick > 17u &&
                    HasLotAssetPrefix(candidate, "rci_residential_low_") &&
                    candidate.zoningLots.empty();
            }),
            "starter density floor allows imported zero-land parcels to grow");
        session.runtime().setGameSpeed(GameSpeed::Paused);
        session.shutdown();
    } catch (const std::exception& error) {
        runner.expect(false, std::string("RCI baseline land-value recovery test threw exception: ") + error.what());
    } catch (...) {
        runner.expect(false, "RCI baseline land-value recovery test threw unknown exception");
    }
}

void RunManualParkPlacementClearsRciZoningTest(TestRunner& runner) {
    try {
        GameSession session(BuildSandboxRuntimeOptions());
        CitySaveState initialState = BuildCleanSandboxState();
        AddSandboxCity(session, initialState);
        runner.expect(session.enterCity(0, 0), "park-over-zoning sandbox city enters");

        const RciRect parcelRect(4, 5, 5, 6);
        session.runtime().queueZoneLot(MakeSandboxRciLot(parcelRect, TileZoningResidential));

        CitySaveState state;
        runner.expect(
            WaitForSandboxState(session, state, [&parcelRect](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u &&
                    candidate.zoningLots.size() == 1u &&
                    TileRectHasZoning(candidate, parcelRect, TileZoningResidential);
            }),
            "park-over-zoning setup creates one empty parcel");

        session.runtime().queuePlacePark(5, 6, 0);
        runner.expect(
            WaitForSandboxState(session, state, [&parcelRect](const CitySaveState& candidate) {
                return candidate.lots.size() == 1u &&
                    candidate.zoningLots.empty() &&
                    TileRectHasZoning(candidate, parcelRect, TileZoningNone);
            }),
            "manual park placement clears zoning and empty parcel under its footprint");
        session.shutdown();
    } catch (const std::exception& error) {
        runner.expect(false, std::string("park-over-zoning test threw exception: ") + error.what());
    } catch (...) {
        runner.expect(false, "park-over-zoning test threw unknown exception");
    }
}

void RunRciConstructorCursorSkipsFailedSourcesTest(TestRunner& runner) {
    try {
        GameSession session(BuildSandboxRuntimeOptions());
        CitySaveState initialState = BuildCleanSandboxState();
        SetLandValueRect(initialState, RciRect(0, 0, initialState.width - 1, initialState.height - 1), kLandValueDisplayCap);
        AddSandboxCity(session, initialState);
        runner.expect(session.enterCity(0, 0), "RCI cursor sandbox city enters");

        const RciRect badParcels[] = {
            RciRect(2, 5, 3, 8),
            RciRect(5, 5, 6, 8),
            RciRect(8, 5, 9, 8),
            RciRect(11, 5, 12, 8),
            RciRect(14, 5, 15, 8)
        };
        const RciRect goodParcel(20, 5, 21, 8);
        std::size_t badIndex = 0u;
        for (; badIndex < 5u; ++badIndex) {
            session.runtime().queueZoneLot(MakeSandboxRciLot(badParcels[badIndex], TileZoningResidential));
        }
        session.runtime().queuePlaceStreetRoad(Int2(20, 3), Int2(21, 3), Int2(21, 3));
        session.runtime().queueZoneLot(MakeSandboxRciLot(goodParcel, TileZoningResidential));

        CitySaveState state;
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u &&
                    candidate.zoningLots.size() == 6u &&
                    !candidate.transport.tiles.empty();
            }),
            "RCI cursor setup creates five blocked parcels and one buildable later parcel");

        runner.expect(
            FastForwardSandboxUntil(session, state, [](const CitySaveState& candidate) {
                return candidate.simulationTick > 17u &&
                    HasLotAssetPrefix(candidate, "rci_residential_low_");
            }),
            "RCI constructor cursor advances past failed parcels to later buildable parcel");
        session.runtime().setGameSpeed(GameSpeed::Paused);
        session.shutdown();
    } catch (const std::exception& error) {
        runner.expect(false, std::string("RCI constructor cursor test threw exception: ") + error.what());
    } catch (...) {
        runner.expect(false, "RCI constructor cursor test threw unknown exception");
    }
}

void RunRciLandValueQueryLevelTest(TestRunner& runner) {
    try {
        GameSession session(BuildSandboxRuntimeOptions());
        CitySaveState initialState = BuildCleanSandboxState();
        SetLandValueRect(initialState, RciRect(0, 0, initialState.width - 1, initialState.height - 1), kLandValueDisplayCap);
        AddSandboxCity(session, initialState);
        runner.expect(session.enterCity(0, 0), "RCI land-value query sandbox city enters");

        session.runtime().queuePlaceLot("rci_residential_medium_court_lot", 4, 5, 0);

        CitySaveState state;
        runner.expect(
            WaitForSandboxState(session, state, [](const CitySaveState& candidate) {
                return candidate.lots.size() == 1u;
            }),
            "RCI land-value query setup places a built RCI lot");

        const TileQueryResult queryResult = session.runtime().queryTile(4, 5);
        runner.expect(queryResult.hasLot, "RCI land-value query finds the placed lot");
        runner.expect(
            queryResult.rciLandValueLevel == "low" ||
                queryResult.rciLandValueLevel == "medium" ||
                queryResult.rciLandValueLevel == "high",
            "RCI land-value query reports a low/medium/high level");
        session.shutdown();
    } catch (const std::exception& error) {
        runner.expect(false, std::string("RCI land-value query test threw exception: ") + error.what());
    } catch (...) {
        runner.expect(false, "RCI land-value query test threw unknown exception");
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

        RciToolCatalog catalog = LoadRciToolCatalogFixture(runner);
        const RciTool* residential = catalog.findTool("residential_high");
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

void RunLargeLowDensityRciPlanCommitTest(TestRunner& runner) {
    try {
        GameSession session(BuildSandboxRuntimeOptions(64, 64));
        AddSandboxCity(session, BuildCleanSandboxState(64, 64));
        runner.expect(session.enterCity(0, 0), "large low-density RCI sandbox city enters");

        RciToolCatalog catalog = LoadRciToolCatalogFixture(runner);
        const RciTool* residential = catalog.findTool("residential_low");
        runner.expect(residential != 0, "large low-density RCI test finds residential tool");
        if (residential == 0) {
            session.shutdown();
            return;
        }

        RciPlan previewPlan;
        runner.expect(
            session.runtime().buildRciPlan(*residential, 8, 8, 55, 55, RciPlanMode::LotsAndRoads, previewPlan),
            "large low-density RCI runtime builds smart preview plan");
        runner.expect(!previewPlan.lots.empty(), "large low-density RCI preview creates parcels");
        runner.expect(!previewPlan.roadPlans.empty(), "large low-density RCI preview creates roads");

        session.runtime().queueRciPlan(previewPlan);
        CitySaveState committedState;
        runner.expect(
            WaitForSandboxState(session, committedState, [](const CitySaveState& candidate) {
                return candidate.simulationTick == 17u &&
                    !candidate.transport.tiles.empty() &&
                    !candidate.zoningLots.empty();
            }),
            "large low-density RCI commit applies roads and parcels");

        session.shutdown();
    } catch (const std::exception& error) {
        runner.expect(false, std::string("large low-density RCI plan commit test threw exception: ") + error.what());
    } catch (...) {
        runner.expect(false, "large low-density RCI plan commit test threw unknown exception");
    }
}
}

int main() {
    InitializeCrashLogger("City Builder RciLotConstructionTests");
    TestRunner runner;
    TestLotConstructionDurationLoading(runner);
    TestModulePlacementClaimedFootprintAndVisualAlignment(runner);
    TestDecorativePropPlacementsDoNotAffectSimulation(runner);
    TestLowResidentialRowhouseYardTemplateRules(runner);
    TestRciCatalogTemplateCoverageAndDensityOrdering(runner);
    TestRciConstructorCanSelectEveryTemplate(runner);
    TestDirtyIndustryModulesEmitDensityLandValue(runner);
    TestInvalidAssetValidation(runner);
    RunRciConstructorFrontAccessGrowthTest(runner);
    RunRciConstructorSideRoadRejectedTest(runner);
    RunRciConstructorZeroLandValueStarterFloorTest(runner);
    RunManualParkPlacementClearsRciZoningTest(runner);
    RunRciConstructorCursorSkipsFailedSourcesTest(runner);
    RunRciLandValueQueryLevelTest(runner);
    RunRoadAwareRciPlanCommitTest(runner);
    RunLargeLowDensityRciPlanCommitTest(runner);
    return runner.finish();
}
