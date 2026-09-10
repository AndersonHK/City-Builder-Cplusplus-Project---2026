#include "TransportRouter.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
int checks = 0, failures = 0;
std::uint64_t threadCycles() {
    ULONG64 value = 0;
    QueryThreadCycleTime(GetCurrentThread(), &value);
    return value;
}
void expect(bool ok, const char *message) {
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
TransportPathRequest request(std::uint32_t a, std::uint32_t b) {
    TransportPathRequest r;
    r.startNodeIds = {a};
    r.goalNodeIds = {b};
    r.maximumCost = 600000;
    r.useRouteJitter = false;
    return r;
}
void connect(TransportCostMap &map, int x, int y, std::uint8_t dir, int cost = 100,
             TransportMode mode = TransportMode::Pedestrian) {
    map.addDirectionalCost(TransportLayerId::Ground, mode, y * map.width() + x, dir, static_cast<std::uint16_t>(cost),
                           100);
}
void testRouteClock() {
    TransportCostMap map;
    map.initialize(4, 1);
    connect(map, 0, 0, kRoadDirectionEast, 1000, TransportMode::Car);
    connect(map, 1, 0, kRoadDirectionEast, 2000, TransportMode::Car);
    connect(map, 2, 0, kRoadDirectionEast, 4000, TransportMode::Car);
    map.finalizeTransferEdges();
    TransportRouter router;
    router.prepare(map);
    TransportRoutingScratch scratch;
    TransportPathResult path;
    expect(router.findPath(request(map.nodeId(TransportLayerId::Ground, TransportMode::Car, 0),
                                   map.nodeId(TransportLayerId::Ground, TransportMode::Car, 3)), scratch, path),
           "timed car route is reachable");
    std::vector<float> seconds;
    const float cost = router.pathCost(path, CommuteTimeOfDay::Morning, &seconds);
    expect(cost == path.totalCost && cost == 67000, "timing extraction preserves the exact route cost");
    expect(seconds == std::vector<float>({0, 1, 3, 7}),
           "route clock follows local speed and excludes the stationary sixty-second car-start cost");
}
void testDifferential() {
    std::mt19937 random(71823);
    for (int fixture = 0; fixture < 12; ++fixture) {
        TransportCostMap map;
        map.initialize(16, 16);
        for (int mode = 0; mode < 2; ++mode)
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    map.addBuildingAccess(TransportLayerId::Ground, static_cast<TransportMode>(mode), y * 16 + x, 1);
                    for (int d = 0; d < 8; ++d)
                        if (random() % 4)
                            connect(map, x, y, RoadDirectionFromIndex(d), 10 + random() % 900,
                                    static_cast<TransportMode>(mode));
                }
        for (int i = 0; i < 16; ++i)
            map.addTransferEdge(random() % 512, random() % 512, 1 + random() % 50, 100);
        map.finalizeTransferEdges();
        TransportCongestionCurve curve;
        curve.points = {{0, 1.5f}, {1, 1}, {2, .1f}};
        map.setCongestionCurve(curve);
        for (auto time : {CommuteTimeOfDay::Morning, CommuteTimeOfDay::Evening}) {
            auto &loads = map.trafficLoadStateForMutation(time);
            for (std::size_t n = 0; n < 512; ++n)
                for (int d = 0; d < 8; ++d)
                    loads.cells[n].oldLoads[d] = random() % 250;
        }
        TransportRouter router;
        router.prepare(map);
        TransportPathScratch referenceScratch;
        TransportRoutingScratch scratch;
        for (int i = 0; i < 60; ++i) {
            auto r = request(random() % 512, random() % 512);
            r.startNodeIds.push_back(random() % 512);
            r.goalNodeIds.push_back(random() % 512);
            r.commuteTimeOfDay = i % 2 ? CommuteTimeOfDay::Morning : CommuteTimeOfDay::Evening;
            if (i % 3 == 0)
                r.maximumCost = static_cast<float>(random() % 80000);
            TransportPathResult oldPath, newPath;
            const bool a = map.findPath(r, referenceScratch, oldPath), b = router.findPath(r, scratch, newPath);
            expect(a == b, "directed multimode A* reachability equals Dijkstra");
            if (a && b) {
                expect(std::abs(oldPath.totalCost - newPath.totalCost) < .125f,
                       "directed multimode A* distance equals Dijkstra");
                const float cost = newPath.totalCost;
                expect(router.reprice(newPath, r.commuteTimeOfDay) && newPath.totalCost == cost,
                       "path reconstructs legal edges and exact snapshot cost");
            }
        }
    }
}
void testNearestAndUpdates() {
    TransportCostMap map;
    map.initialize(32, 16);
    for (int x = 0; x < 15; ++x) {
        connect(map, x, 0, kRoadDirectionEast);
        connect(map, x + 1, 0, kRoadDirectionWest);
    }
    connect(map, 24, 0, kRoadDirectionEast);
    map.finalizeTransferEdges();
    TransportRouter router;
    router.prepare(map);
    const auto node = [&map](int x) { return map.nodeId(TransportLayerId::Ground, TransportMode::Pedestrian, x); };
    TransportDestinationIndex index;
    index.build(router, {{10, 3, {node(3), node(3)}}, {11, 7, {node(3), node(7)}}, {12, 20, {node(25)}}});
    expect(index.available() == 30, "capacity counted once across duplicate/multiple access nodes");
    auto r = request(node(0), node(15));
    TransportRoutingScratch morning, evening;
    router.beginNearest(r, index, morning);
    std::size_t dest;
    TransportPathResult path;
    expect(router.nextNearest(index, -1, morning, dest, path) && dest == 0 && path.totalCost == 300,
           "nearest first candidate");
    index.consume(dest, 3);
    auto back = request(node(3), node(0));
    TransportPathResult returnPath;
    expect(router.findPath(back, evening, returnPath), "independent evening scratch");
    expect(router.nextNearest(index, -1, morning, dest, path) && dest == 1,
           "same access supports multiple destinations without restart");
    index.consume(dest, 7);
    expect(!router.nextNearest(index, -1, morning, dest, path),
           "remaining disconnected vacancy does not create a route");
    expect(morning.stats.searches == 1, "all splits use one source search");
    index.build(router, {{12, 20, {node(25)}}});
    router.beginNearest(r, index, morning);
    expect(morning.stats.searches == 0 && !router.nextNearest(index, -1, morning, dest, path),
           "empty reachable component avoids graph exploration");
    index.build(router, {{10, 0, {node(3)}}});
    router.beginNearest(r, index, morning);
    expect(morning.stats.searches == 0, "saturated market starts no search");
    router.findPath(r, evening, path);
    const auto before = router.snapshot();
    map.beginNextLoadFromOldLoad(CommuteTimeOfDay::Morning);
    map.applyPathLoad(CommuteTimeOfDay::Morning, path, 200, true);
    map.commitNextLoad(CommuteTimeOfDay::Morning);
    router.prepare(map);
    expect(router.snapshot() != before && router.lastMetricEdgesUpdated() < router.edgeCount() * 2,
           "load commit refreshes only touched origin weights");
    expect(router.reprice(path, CommuteTimeOfDay::Morning) && path.totalCost == 6000,
           "repricing sees committed traffic");
    router.prepare(map);
    expect(router.lastMetricEdgesUpdated() == 0, "unchanged snapshot performs no metric work");
    map.clearCostsForTile(TransportLayerId::Ground, 5);
    router.prepare(map);
    expect(!router.reprice(path, CommuteTimeOfDay::Morning), "deleted edge invalidates stored path");
    expect(!router.findPath(r, evening, path), "deleted edge cannot survive graph rebuild");
}
void testFieldsAndPool() {
    TransportCostMap map;
    map.initialize(32, 8);
    for (int x = 0; x < 31; ++x)
        connect(map, x, 0, kRoadDirectionEast, 100, TransportMode::Car);
    map.finalizeTransferEdges();
    TransportRouter router;
    router.prepare(map);
    const auto node = [&map](int x) { return map.nodeId(TransportLayerId::Ground, TransportMode::Car, x); };
    TransportDistanceField toward, outward;
    router.buildField({node(31)}, CommuteTimeOfDay::Morning, true, toward);
    router.buildField({node(0)}, CommuteTimeOfDay::Evening, false, outward);
    expect(router.fieldCost(toward, node(0)) == 63100, "reverse field charges source mode exactly once");
    expect(router.fieldCost(outward, node(31)) == 63100, "outward field charges facility mode exactly once");
    TransportRoutingScratch scratch;
    TransportPathResult path;
    expect(!router.findPath(request(node(31), node(0)), scratch, path), "directed field does not imply reverse travel");
    std::vector<TransportPathResult> serial(64), parallel(64);
    for (std::size_t i = 0; i < serial.size(); ++i)
        router.findPath(request(node(0), node(static_cast<int>(i % 32))), scratch, serial[i]);
    TransportRoutingPool pool(4);
    for (int repeat = 0; repeat < 3; ++repeat) {
        pool.run(parallel.size(), [&](std::size_t i, TransportRoutingScratch &local) {
            router.findPath(request(node(0), node(static_cast<int>(i % 32))), local, parallel[i]);
        });
        for (std::size_t i = 0; i < serial.size(); ++i)
            expect(serial[i].totalCost == parallel[i].totalCost && serial[i].steps.size() == parallel[i].steps.size(),
                   "persistent worker pool deterministic results");
    }
    bool threw = false;
    try {
        pool.run(3, [](std::size_t, TransportRoutingScratch &) { throw std::runtime_error("test"); });
    } catch (const std::runtime_error &) {
        threw = true;
    }
    expect(threw, "worker exception returns to batch owner");
    pool.run(1, [](std::size_t, TransportRoutingScratch &) {});
    map.clearLoads();
    router.prepare(map);
    expect(!std::isfinite(router.fieldCost(toward, node(0))), "stale field rejected after metric reset");
}
void testOverlay() {
    TransportCostMap map;
    map.initialize(48, 32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 48; ++x)
            for (int d = 0; d < 4; ++d)
                connect(map, x, y, RoadDirectionFromIndex(d), 100 + (x * 7 + y * 11) % 43);
    map.addTransferEdge(map.nodeId(TransportLayerId::Ground, TransportMode::Pedestrian, 0),
                        map.nodeId(TransportLayerId::Ground, TransportMode::Pedestrian, 47 + 31 * 48), 20, 100);
    map.finalizeTransferEdges();
    TransportRouter router;
    router.prepare(map);
    TransportRoutingOverlay overlay;
    expect(overlay.build(router, CommuteTimeOfDay::Morning, 8), "bounded overlay builds all boundary witnesses");
    TransportRoutingScratch a, b;
    std::mt19937 random(872);
    for (int i = 0; i < 80; ++i) {
        auto r = request(map.nodeId(TransportLayerId::Ground, TransportMode::Pedestrian, random() % 1536),
                         map.nodeId(TransportLayerId::Ground, TransportMode::Pedestrian, random() % 1536));
        TransportPathResult plain, hierarchical;
        router.findPath(r, a, plain);
        overlay.findPath(router, r, b, hierarchical);
        expect(plain.success == hierarchical.success && plain.totalCost == hierarchical.totalCost,
               "overlay distance equals complete base search");
        expect(router.reprice(hierarchical, CommuteTimeOfDay::Morning) && hierarchical.totalCost == plain.totalCost,
               "shortcut witnesses unpack to legal equal-cost path");
    }
    expect(!overlay.build(router, CommuteTimeOfDay::Morning, 8, 1), "oversized overlay rejected within storage budget");
    auto r = request(map.nodeId(TransportLayerId::Ground, TransportMode::Pedestrian, 0),
                     map.nodeId(TransportLayerId::Ground, TransportMode::Pedestrian, 1535));
    TransportPathResult path;
    expect(overlay.findPath(router, r, a, path), "incomplete overlay falls back to full compact graph");
    overlay.build(router, CommuteTimeOfDay::Morning, 8);
    map.clearLoads();
    router.prepare(map);
    expect(overlay.findPath(router, r, a, path), "stale customized overlay falls back after metric change");
    // Authoritative counters must remain reversible above the previous 16-bit ceiling.
    map.beginNextLoadFromOldLoad(CommuteTimeOfDay::Morning);
    map.applyPathLoad(CommuteTimeOfDay::Morning, path, 60000, true);
    map.applyPathLoad(CommuteTimeOfDay::Morning, path, 60000, true);
    map.commitNextLoad(CommuteTimeOfDay::Morning);
    expect(map.trafficLoadState(CommuteTimeOfDay::Morning).transferLoads[0].oldLoad == 120000,
           "traffic accumulation retains values above 65535");
    map.beginNextLoadFromOldLoad(CommuteTimeOfDay::Morning);
    map.applyPathLoad(CommuteTimeOfDay::Morning, path, 60000, false);
    map.commitNextLoad(CommuteTimeOfDay::Morning);
    expect(map.trafficLoadState(CommuteTimeOfDay::Morning).transferLoads[0].oldLoad == 60000,
           "removing one large route restores exact remaining load");
}
void benchmark(int width) {
    TransportCostMap map;
    map.initialize(width, width);
    // Reproducible dense directed grid, worst case for geometrical shortcuts.
    for (int y = 0; y < width; ++y)
        for (int x = 0; x < width; ++x) {
            for (int d = 0; d < 4; ++d)
                connect(map, x, y, RoadDirectionFromIndex(d));
        }
    map.finalizeTransferEdges();
    const auto now = [] { return std::chrono::steady_clock::now(); };
    const auto ms = [](std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    TransportRouter router;
    auto t = now();
    router.prepare(map);
    const double build = ms(t, now());
    std::vector<TransportPathRequest> queries;
    std::mt19937 random(157);
    for (int i = 0; i < 64; ++i)
        queries.push_back(
            request(map.nodeId(TransportLayerId::Ground, TransportMode::Pedestrian, random() % (width * width)),
                    map.nodeId(TransportLayerId::Ground, TransportMode::Pedestrian, random() % (width * width))));
    TransportPathScratch legacy;
    TransportRoutingScratch scratch;
    TransportPathResult path;
    map.findPath(queries[0], legacy, path);
    router.findPath(queries[0], scratch, path);
    t = now();
    auto cycles = threadCycles();
    double checksumOld = 0;
    for (auto &q : queries) {
        map.findPath(q, legacy, path);
        checksumOld += path.totalCost;
    }
    const auto referenceCycles = threadCycles() - cycles;
    const double reference = ms(t, now());
    t = now();
    cycles = threadCycles();
    double checksumNew = 0;
    std::uint64_t settled = 0;
    for (auto &q : queries) {
        router.findPath(q, scratch, path);
        checksumNew += path.totalCost;
        settled += scratch.stats.settled;
    }
    const auto compactCycles = threadCycles() - cycles;
    const double compact = ms(t, now());
    expect(checksumOld == checksumNew, "benchmark exact distance checksum");
    TransportDistanceField field;
    t = now();
    router.buildField(queries[0].goalNodeIds, CommuteTimeOfDay::Morning, true, field);
    const double fieldTime = ms(t, now());
    std::cout << "BENCH grid=" << width << " nodes=" << router.nodeCount() << " edges=" << router.edgeCount()
              << " build_ms=" << build << " reference_64_ms=" << reference << " astar_64_ms=" << compact
              << " settled=" << settled << " graph_bytes=" << router.allocatedBytes()
              << " scratch_bytes=" << scratch.allocatedBytes() << " field_ms=" << fieldTime << '\n';
    std::cout << "CYCLES reference=" << referenceCycles << " compact=" << compactCycles
              << " point_queries_per_million=" << (compactCycles ? 64e6 / compactCycles : 0) << '\n';
    TransportRoutingOverlay overlay;
    t = now();
    const bool built = overlay.build(router, CommuteTimeOfDay::Morning, 16, 32000000);
    const double customization = ms(t, now());
    t = now();
    double checksumOverlay = 0;
    for (auto &q : queries) {
        overlay.findPath(router, q, scratch, path);
        checksumOverlay += path.totalCost;
    }
    const double overlayQuery = ms(t, now());
    expect(checksumOverlay == checksumNew, "overlay benchmark preserves exact distance checksum");
    std::cout << "OVERLAY built=" << built << " customization_ms=" << customization << " query_64_ms=" << overlayQuery
              << " shortcuts=" << overlay.shortcutCount() << " witness_edges=" << overlay.storedEdges() << '\n';
    for (int workers : {1, 2, 4, 8}) {
        TransportRoutingPool pool(workers);
        std::vector<TransportPathResult> results(queries.size());
        pool.run(queries.size(), [&](std::size_t i, TransportRoutingScratch &local) {
            router.findPath(queries[i], local, results[i]);
        });
        std::vector<std::uint64_t> workerCycles(queries.size());
        t = now();
        pool.run(queries.size(), [&](std::size_t i, TransportRoutingScratch &local) {
            const auto before = threadCycles();
            router.findPath(queries[i], local, results[i]);
            workerCycles[i] = threadCycles() - before;
        });
        std::uint64_t totalCycles = 0;
        for (auto value : workerCycles)
            totalCycles += value;
        std::cout << "POOL workers=" << workers << " query_64_ms=" << ms(t, now())
                  << " aggregate_query_cycles=" << totalCycles << '\n';
    }
}
} // namespace
int main(int argc, char **argv) {
    testRouteClock();
    testDifferential();
    testNearestAndUpdates();
    testFieldsAndPool();
    testOverlay();
    if (argc > 1 && std::string(argv[1]) == "--benchmark")
        benchmark(argc > 2 ? std::stoi(argv[2]) : 256);
    std::cout << "TransportRoutingTests: " << checks << " checks, " << failures << " failures.\n";
    return failures ? 1 : 0;
}
