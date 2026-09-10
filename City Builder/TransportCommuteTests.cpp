#include "SimulationRuntime.h"
#include "PublicationFingerprint.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>

struct TransportCommuteTestAccess {
    int checks = 0;
    std::vector<std::uint64_t> serialReplay;
    void expect(bool ok, const char *message) {
        ++checks;
        if (!ok)
            throw std::runtime_error(message);
    }
    void verifyPublication(SimulationRuntime &runtime, SimulationRuntime::TileBuffer &buffer) {
        runtime.refreshPublishedLotSnapshot(buffer);
        auto cold = buffer;
        cold.lotRenderRevision = ~runtime.lotsRevision_;
        cold.commuteRenderRevision = ~runtime.commuteRevision_;
        runtime.refreshPublishedLotSnapshot(cold);
        PublicationFingerprint actual, rebuilt;
        actual.fields(buffer.publishedLots, buffer.publishedLotInfos, buffer.publishedCommuteRouteSegments,
                      buffer.publishedLotOccupancy);
        rebuilt.fields(cold.publishedLots, cold.publishedLotInfos, cold.publishedCommuteRouteSegments,
                       cold.publishedLotOccupancy);
        expect(actual.value == rebuilt.value, "reused publication exactly matches a full rebuild");
    }
    void run(int workers) {
        RuntimeOptions options;
        options.mapWidth = 96;
        options.mapHeight = 32;
        options.routingWorkers = workers;
        options.showNonFatalAssetWarningDialogs = false;
        options.detectL2CacheSize = false;
        options.manualL2BytesPerLogicalThread = 128 * 1024;
        SimulationRuntime runtime(options);
        runtime.lots_.clear();
        runtime.commutesDirty_ = true;
        auto &buffer = runtime.tileBuffers_[runtime.simulationReadBufferIndex_];
        for (auto &tile : buffer.tiles)
            tile.zoningType = TileZoningNone;
        LotAsset asset;
        asset.id = "routing_fixture";
        asset.footprintWidth = asset.footprintHeight = 1;
        LotAccessDefinition access;
        access.direction = kRoadDirectionNorth;
        access.modeMask = kTransportModePedestrian;
        asset.accessDefinitions.push_back(access);
        runtime.lotAssetIndexById_[asset.id] = runtime.lotAssets_.size();
        runtime.lotAssets_.push_back(asset);
        std::vector<LotModule> modules(6);
        for (int i = 0; i < 5; ++i) {
            modules[i].id = "routing_module_" + std::to_string(i);
            CityParameterContribution contribution;
            contribution.parameterId = i < 3 ? runtime.cityParameterRegistry_.residentsLowWealthId()
                                             : runtime.cityParameterRegistry_.jobsDirtyIndustryId();
            contribution.amount = i < 3 ? 8 : (i == 3 ? 5 : 7);
            modules[i].parameterContributions.push_back(contribution);
            const int x = i < 3 ? i * 2 + 1 : (i == 3 ? 20 : 60);
            Lot lot(i + 1, asset.id, x, 1);
            lot.addModule(modules[i], Int2(0, 0), 96);
            runtime.lots_.push_back(lot);
        }
        auto &map = const_cast<TransportCostMap &>(runtime.transportNetwork_.costMap());
        map.clear();
        for (int x = 0; x < 95; ++x) {
            map.addDirectionalCost(TransportLayerId::Ground, TransportMode::Pedestrian, x, kRoadDirectionEast, 100,
                                   100);
            map.addDirectionalCost(TransportLayerId::Ground, TransportMode::Pedestrian, x + 1, kRoadDirectionWest, 100,
                                   100);
        }
        for (int x = 0; x < 96; ++x)
            map.addBuildingAccess(TransportLayerId::Ground, TransportMode::Pedestrian, x, kRoadDirectionSouth);
        map.finalizeTransferEdges();
        const auto tick = [&] {
            runtime.runCommuteAssignment(buffer);
            ++runtime.simulationTick_;
        };
        std::size_t replayStage = 0;
        const auto recordReplay = [&] {
            verifyPublication(runtime, buffer);
            std::uint64_t hash = 1469598103934665603ull;
            const auto add = [&](std::uint64_t value) { hash = (hash ^ value) * 1099511628211ull; };
            for (const auto &lot : runtime.lots_) {
                add(lot.id());
                add(lot.commuteSatisfied());
                add(lot.lowWealthJobsFilled());
                for (const auto &route : lot.commuteRoutes()) {
                    add(route.destinationLotId);
                    add(route.demand);
                    add(route.morningMediumRetry);
                    add(route.eveningMediumRetry);
                    for (const auto *path : {&route.morningPathResult, &route.eveningPathResult}) {
                        add(static_cast<std::uint64_t>(path->totalCost));
                        for (const auto &step : path->steps) {
                            add(step.fromNodeId);
                            add(step.toNodeId);
                            add(step.roadDirection);
                            add(static_cast<unsigned>(step.kind));
                        }
                    }
                }
            }
            if (workers == 1)
                serialReplay.push_back(hash);
            else
                expect(replayStage < serialReplay.size() && serialReplay[replayStage] == hash,
                       "worker counts produce identical complete route replay");
            ++replayStage;
        };
        tick();
        recordReplay();
        expect(runtime.lots_[0].commuteSatisfied() == 8 && runtime.lots_[0].commuteRoutes().size() == 2,
               "source demand splits across employers");
        expect(runtime.lots_[1].commuteSatisfied() == 4 && runtime.lots_[2].commuteSatisfied() == 0,
               "source-order reducer respects finite job capacity");
        expect(runtime.lots_[3].lowWealthJobsFilled() == 5 && runtime.lots_[4].lowWealthJobsFilled() == 7,
               "filled job ledger matches routes");
        expect(runtime.timingSnapshot().commuteSearches == 5,
               "two resumable outward searches plus three return searches");
        for (int i = 1; i <= 3; ++i)
            runtime.queueCommuteRecalculationForLot(i);
        tick();
        const auto stableSnapshot = runtime.commuteRouter_.snapshot();
        expect(runtime.timingSnapshot().commuteSearches == 0,
               "maintained assignments and saturated market need no new searches");
        tick();
        expect(runtime.commuteRouter_.snapshot() == stableSnapshot,
               "maintained routes do not subtract and re-add loads");
        expect(runtime.timingSnapshot().commuteAccessRefreshes == 0, "unchanged lots reuse cached endpoints");
        map.addDirectionalCost(TransportLayerId::Ground, TransportMode::Pedestrian, 90, kRoadDirectionEast, 100, 100);
        runtime.queueCommuteRecalculationForRoadTopologyChange({90});
        for (int i = 0; i < 4; ++i)
            tick();
        expect(runtime.lots_[0].commuteSatisfied() + runtime.lots_[1].commuteSatisfied() +
                       runtime.lots_[2].commuteSatisfied() ==
                   12,
               "unrelated graph rebuild does not resurrect occupied vacancies");
        for (const auto &lot : runtime.lots_)
            expect(lot.lowWealthJobsFilled() <= lot.lowWealthJobsTotal(), "no destination oversubscribed");
        // A speed edit changes timing on the same path, including published copies,
        // without mutating snapshots already handed to the renderer.
        const auto oldTiming = runtime.lots_[0].commuteRoutes()[0].morningSeconds;
        const auto oldTimes = *oldTiming;
        map.clearCostsForTile(TransportLayerId::Ground, 3);
        map.addDirectionalCost(TransportLayerId::Ground, TransportMode::Pedestrian, 3, kRoadDirectionEast, 400, 100);
        map.addDirectionalCost(TransportLayerId::Ground, TransportMode::Pedestrian, 3, kRoadDirectionWest, 100, 100);
        map.addBuildingAccess(TransportLayerId::Ground, TransportMode::Pedestrian, 3, kRoadDirectionSouth);
        runtime.queueCommuteRecalculationForRoadTopologyChange({3});
        tick();
        const auto& timedRoute = runtime.lots_[0].commuteRoutes()[0];
        const auto newTiming = timedRoute.morningSegments[0].elapsedSeconds;
        expect(newTiming && newTiming->size() == timedRoute.morningPathResult.steps.size() + 1,
               "route timing includes every movement boundary");
        expect(newTiming->back() > oldTimes.back() && *oldTiming == oldTimes,
               "repricing updates displayed speed while old snapshots remain immutable");
        expect(std::abs(newTiming->back() * 1000 - timedRoute.morningPathResult.totalCost) < 0.1f,
               "walking arrow clock agrees with the routing cost");
        verifyPublication(runtime, buffer);
        // Remove an employer; only its indexed source dependents are forced.
        runtime.queueCommuteSourcesForDestination(4);
        expect(runtime.forcedCommuteLotIds_.size() == 1 && runtime.forcedCommuteLotIds_[0] == 1,
               "destination removal follows back references");
        runtime.removeCommuteLoadsForLot(runtime.lots_[3]);
        runtime.lots_.erase(runtime.lots_.begin() + 3);
        tick();
        expect(runtime.lots_[0].commuteSatisfied() + runtime.lots_[1].commuteSatisfied() +
                       runtime.lots_[2].commuteSatisfied() ==
                   7,
               "deleted employer releases only affected assignments");
        expect(runtime.lots_.back().lowWealthJobsFilled() == 7, "remaining employer retains exact capacity");
        recordReplay();
        // Cut the only route to the remaining employer and force through the region index.
        map.clearCostsForTile(TransportLayerId::Ground, 32);
        runtime.queueCommuteRecalculationForRoadTopologyChange({32});
        tick();
        expect(runtime.lots_[0].commuteSatisfied() + runtime.lots_[1].commuteSatisfied() +
                       runtime.lots_[2].commuteSatisfied() ==
                   0,
               "road cut removes impossible round trips");
        // Reconnect and prove failure caches don't keep workers unemployed.
        map.addDirectionalCost(TransportLayerId::Ground, TransportMode::Pedestrian, 32, kRoadDirectionEast, 100, 100);
        map.addDirectionalCost(TransportLayerId::Ground, TransportMode::Pedestrian, 32, kRoadDirectionWest, 100, 100);
        runtime.queueCommuteRecalculationForRoadTopologyChange({32});
        for (int i = 0; i < 4; ++i)
            tick();
        expect(runtime.lots_[0].commuteSatisfied() + runtime.lots_[1].commuteSatisfied() +
                       runtime.lots_[2].commuteSatisfied() ==
                   7,
               "new connectivity wakes failed sources");
        // A nearer employer is an incoming-only sink; the source must resume past
        // its failed return and still reach the farther valid employer.
        modules[5] = modules[3];
        modules[5].id = "sink_employer";
        Lot sink(6, asset.id, 20, 1);
        sink.addModule(modules[5], Int2(0, 0), 96);
        runtime.lots_.push_back(sink);
        map.clearCostsForTile(TransportLayerId::Ground, 20);
        map.addBuildingAccess(TransportLayerId::Ground, TransportMode::Pedestrian, 20, kRoadDirectionSouth);
        const auto node = [&map](int x) { return map.nodeId(TransportLayerId::Ground, TransportMode::Pedestrian, x); };
        map.addTransferEdge(node(19), node(21), 200, 100);
        map.addTransferEdge(node(21), node(19), 200, 100);
        map.finalizeTransferEdges();
        runtime.commutesDirty_ = true;
        tick();
        expect(runtime.lots_[0].commuteSatisfied() == 7 && runtime.lots_[0].commuteRoutes()[0].destinationLotId == 5,
               "evening rejection resumes to later round-trip destination");
        expect(runtime.lots_.back().lowWealthJobsFilled() == 0, "morning-only destination gets no workers");
        recordReplay();
        for (int capacity : {12, 4}) {
            modules[4].parameterContributions[0].amount = capacity;
            runtime.lots_[3].clearModules(96);
            runtime.lots_[3].addModule(modules[4], Int2(0, 0), 96);
            ++runtime.lotsRevision_;
            runtime.queueCommuteRecalculationForLot(5);
            for (int i = 0; i < 4; ++i)
                tick();
            expect(runtime.lots_[0].commuteSatisfied() + runtime.lots_[1].commuteSatisfied() +
                           runtime.lots_[2].commuteSatisfied() ==
                       capacity,
                   "capacity increase/decrease updates allocation without oversubscription");
            expect(runtime.lots_[3].lowWealthJobsFilled() == capacity,
                   "capacity edits keep published staffing current");
            recordReplay();
        }
        // Exercise the actual module-removal path while a home owns live routes.
        const int removedSatisfied = runtime.lots_[0].commuteSatisfied();
        const int satisfiedBeforeRemoval = runtime.commuteSatisfiedTotal_;
        expect(removedSatisfied > 0, "removed home has assigned workers");
        runtime.setLotOccupancy(runtime.lots_[0].id(), runtime.lots_[0].occupiedTileIndices());
        expect(runtime.tryRemoveModuleAtTile(1, 1, buffer), "residential module removal succeeds");
        expect(runtime.commuteSatisfiedTotal_ == satisfiedBeforeRemoval - removedSatisfied,
               "home removal immediately releases satisfaction ledger");
        expect(runtime.findLotById(5)->lowWealthJobsFilled() == 4 - removedSatisfied,
               "home removal immediately releases published staffing");
        for (int i = 0; i < 4; ++i)
            tick();
        expect(runtime.commuteSatisfiedTotal_ == 4 && runtime.findLotById(5)->lowWealthJobsFilled() == 4,
               "remaining homes can consume vacancies released by module removal");
        recordReplay();
        // Construction and zoning must still invalidate cached visual geometry.
        Lot construction(7, asset.id, 80, 1);
        construction.addModule(modules[5], Int2(0, 0), 96);
        construction.startConstruction(4, 96);
        runtime.lots_.push_back(construction);
        ++runtime.lotsRevision_;
        runtime.setLotOccupancy(construction.id(), construction.occupiedTileIndices());
        for (int i = 0; i < 4; ++i) {
            runtime.advanceLotConstruction(buffer);
            tick();
            verifyPublication(runtime, buffer);
        }
        const auto zoningTile = runtime.lots_[0].occupiedTileIndices()[0];
        buffer.tiles[zoningTile].zoningType = TileZoningResidentialHigh;
        ++runtime.commuteRevision_;
        verifyPublication(runtime, buffer);
        // Road queries retain local statistics but display complete matching legs.
        runtime.publishedBufferIndex_ = runtime.simulationReadBufferIndex_;
        std::fill(buffer.publishedLotOccupancy.begin(), buffer.publishedLotOccupancy.end(), -1);
        for (auto& road : buffer.publishedRoads)
            road.family = static_cast<std::uint8_t>(RoadFamily::LocalStreet);
        const auto segment = [](int x0, int y0, int x1, int y1) {
            CommuteRouteSegment s;
            s.startTileX = x0; s.startTileY = y0; s.endTileX = x1; s.endTileY = y1; s.demand = 5;
            return s;
        };
        buffer.publishedCommuteRouteSegments = {
            segment(1, 0, 5, 0), segment(5, 0, 5, 5), segment(5, 5, 9, 5), segment(0, 0, 8, 8)};
        buffer.publishedCommuteRouteRanges = {{0, 3}, {3, 4}};
        const auto bent = runtime.queryTile(3, 0);
        expect(bent.roadCommuteSegments.size() == 1, "road traffic counts only the segment through the queried tile");
        expect(bent.commuteRouteSegments.size() == 3 && bent.commuteRouteSegments.back().endTileX == 9,
               "road query displays the entire bent route beyond the clicked straight section");
        expect(runtime.queryTile(3, 3).roadCommuteSegments.size() == 1, "diagonal road route matches its actual tiles");
        expect(runtime.queryTile(2, 4).roadCommuteSegments.empty(), "diagonal bounding box does not select unrelated routes");
    }
    void benchmark(int width, bool full) {
        RuntimeOptions options;
        options.mapWidth = width;
        options.mapHeight = width;
        options.routingWorkers = 4;
        options.showNonFatalAssetWarningDialogs = false;
        options.detectL2CacheSize = false;
        options.manualL2BytesPerLogicalThread = 128 * 1024;
        SimulationRuntime runtime(options);
        runtime.lots_.clear();
        runtime.commutesDirty_ = true;
        auto &buffer = runtime.tileBuffers_[runtime.simulationReadBufferIndex_];
        for (auto &tile : buffer.tiles)
            tile.zoningType = TileZoningNone;
        LotAsset asset;
        asset.id = "routing_benchmark";
        asset.footprintWidth = asset.footprintHeight = 1;
        LotAccessDefinition access;
        access.direction = kRoadDirectionNorth;
        access.modeMask = kTransportModePedestrian;
        asset.accessDefinitions.push_back(access);
        runtime.lotAssetIndexById_[asset.id] = runtime.lotAssets_.size();
        runtime.lotAssets_.push_back(asset);
        LotModule home, job;
        home.id = "benchmark_home";
        job.id = "benchmark_job";
        CityParameterContribution contribution;
        contribution.amount = 8;
        contribution.parameterId = runtime.cityParameterRegistry_.residentsLowWealthId();
        home.parameterContributions.push_back(contribution);
        contribution.parameterId = runtime.cityParameterRegistry_.jobsDirtyIndustryId();
        job.parameterContributions.push_back(contribution);
        const int activeWidth = full ? width : width / 2, activeHeight = full ? width : width / 4;
        for (int y = 1; y < activeHeight; y += 4)
            for (int x = 1; x < activeWidth; x += 2) {
                Lot lot(static_cast<int>(runtime.lots_.size() + 1), asset.id, x, y);
                lot.addModule((x / 2) % 2 ? job : home, Int2(0, 0), width);
                runtime.lots_.push_back(std::move(lot));
            }
        auto &map = const_cast<TransportCostMap &>(runtime.transportNetwork_.costMap());
        map.clear();
        const auto road = [&](int x, int y) {
            return x >= 0 && y >= 0 && x < activeWidth && y < activeHeight && (y % 4 == 0 || x % 16 == 0);
        };
        for (int y = 0; y < activeHeight; ++y)
            for (int x = 0; x < activeWidth; ++x)
                if (road(x, y)) {
                    for (int d = 0; d < 4; ++d) {
                        const auto dir = RoadDirectionFromIndex(d);
                        if (road(x + RoadDirectionDeltaX(dir), y + RoadDirectionDeltaY(dir)))
                            map.addDirectionalCost(TransportLayerId::Ground, TransportMode::Pedestrian, y * width + x,
                                                   dir, 100, 10000);
                    }
                    if (y % 4 == 0)
                        map.addBuildingAccess(TransportLayerId::Ground, TransportMode::Pedestrian, y * width + x,
                                              kRoadDirectionSouth);
                }
        map.finalizeTransferEdges();
        auto start = std::chrono::steady_clock::now();
        runtime.runCommuteAssignment(buffer);
        const double initial =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        for (int i = 0; i < 4; ++i) {
            ++runtime.simulationTick_;
            runtime.runCommuteAssignment(buffer);
        }
        std::vector<double> samples;
        std::uint64_t searches = 0, refreshes = 0;
        for (int i = 0; i < 120; ++i) {
            ++runtime.simulationTick_;
            start = std::chrono::steady_clock::now();
            runtime.runCommuteAssignment(buffer);
            samples.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            searches += runtime.timingSnapshot().commuteSearches;
            refreshes += runtime.timingSnapshot().commuteAccessRefreshes;
        }
        std::sort(samples.begin(), samples.end());
        double sum = 0;
        for (auto sample : samples)
            sum += sample;
        std::cout << "COMMUTE_BENCH width=" << width << " fill=" << (full ? "full" : "1/8")
                  << " lots=" << runtime.lots_.size() << " initial_ms=" << initial
                  << " mean_ms=" << sum / samples.size() << " p50_ms=" << samples[60] << " p95_ms=" << samples[114]
                  << " p99_ms=" << samples[118] << " searches=" << searches << " access_refreshes=" << refreshes
                  << '\n';
    }
};
int main(int argc, char **argv) {
    try {
        TransportCommuteTestAccess test;
        test.run(1);
        test.run(4);
        if (argc > 1 && std::string(argv[1]) == "--benchmark") {
            const int width = argc > 2 ? std::stoi(argv[2]) : 256;
            test.benchmark(width, false);
            test.benchmark(width, true);
        }
        std::cout << "TransportCommuteTests: " << test.checks << " checks passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
