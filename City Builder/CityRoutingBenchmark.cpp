// The same driver is compiled against the baseline and current sources. No saves
// are written. Full mode follows simulationLoop's pass order without a renderer.
#include "GameSession.h"
#include "PublicationFingerprint.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <set>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
std::uint64_t cycles() {
    ULONG64 value = 0;
    if (!QueryProcessCycleTime(GetCurrentProcess(), &value))
        throw std::runtime_error("QueryProcessCycleTime failed");
    return value;
}
double cpuMilliseconds() {
    FILETIME creation, exit, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        throw std::runtime_error("GetProcessTimes failed");
    const auto value = [](FILETIME time) {
        return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    };
    return (value(kernel) + value(user)) / 10000.0;
}
double percentile(const std::vector<double> &sorted, double fraction) {
    return sorted[static_cast<std::size_t>(std::ceil(fraction * sorted.size())) - 1];
}

}

// Reuses the test-only friend access; this driver never starts a simulation thread.
struct TransportCommuteTestAccess {
    static bool verifyPublication;
    static std::string routeDisplayOutput;
#ifdef CITY_ROUTING_NEW
    static void verifyRouteQueries(const SimulationRuntime& runtime, const char* stage) {
        const auto& buffer = runtime.tileBuffers_[runtime.publishedBufferIndex_];
        const auto& segments = buffer.publishedCommuteRouteSegments;
        const auto& ranges = buffer.publishedCommuteRouteRanges;
        std::size_t next = 0, diagonalCount = 0, longest = 0, queryCount = 0;
        std::set<std::pair<int, int>> sampleTiles;
        for (std::size_t r = 0; r < ranges.size(); ++r) {
            const auto& range = ranges[r];
            if (range.first != next || range.second <= range.first || range.second > segments.size())
                throw std::runtime_error("Invalid published route range");
            next = range.second;
            for (std::size_t i = range.first; i < range.second; ++i) {
                const auto& segment = segments[i];
                if (!segment.elapsedSeconds || segment.timingEnd >= segment.elapsedSeconds->size() ||
                    segment.timingEnd <= segment.timingBegin ||
                    !std::is_sorted(segment.elapsedSeconds->begin() + segment.timingBegin,
                                    segment.elapsedSeconds->begin() + segment.timingEnd + 1))
                    throw std::runtime_error("Published route has invalid travel times");
                const int dx = std::abs(segment.endTileX - segment.startTileX);
                const int dy = std::abs(segment.endTileY - segment.startTileY);
                if (dx && dy && dx != dy)
                    throw std::runtime_error("Published route does not follow grid edges");
                diagonalCount += dx && dy;
                longest = std::max(longest, static_cast<std::size_t>(std::max(dx, dy)));
                if (i > range.first && (segments[i - 1].endTileX != segment.startTileX ||
                    segments[i - 1].endTileY != segment.startTileY))
                    throw std::runtime_error("Published route has disconnected segments");
            }
            if (r % std::max<std::size_t>(1, ranges.size() / 64) == 0)
                sampleTiles.emplace(segments[range.first].startTileX, segments[range.first].startTileY);
        }
        if (next != segments.size())
            throw std::runtime_error("Published segments missing route membership");
        for (const auto& tile : sampleTiles) {
            const auto query = runtime.queryTile(tile.first, tile.second);
            if (query.hasLot || query.roads.empty()) continue;
            std::set<const std::vector<float>*> retainedClocks;
            for (const auto& clock : query.commuteClocks) retainedClocks.insert(clock.get());
            for (const auto* spans : {&query.commuteRouteSegments, &query.roadCommuteSegments})
                for (const auto& segment : *spans)
                    if (!retainedClocks.count(segment.elapsedSeconds))
                        throw std::runtime_error("Query did not retain its travel-time snapshot");
            std::vector<CommuteRouteSegment> local, complete;
            for (const auto& range : ranges) {
                bool touches = false;
                for (std::size_t i = range.first; i < range.second; ++i) {
                    const auto& segment = segments[i];
                    const int dx = segment.endTileX - segment.startTileX;
                    const int dy = segment.endTileY - segment.startTileY;
                    const int length = std::max(std::abs(dx), std::abs(dy));
                    // Expand the saved grid walk independently of queryTile's geometry test.
                    for (int step = 0; step <= length; ++step) {
                        if (segment.startTileX + step * ((dx > 0) - (dx < 0)) == tile.first &&
                            segment.startTileY + step * ((dy > 0) - (dy < 0)) == tile.second) {
                            local.push_back(segment);
                            touches = true;
                            break;
                        }
                    }
                }
                if (touches) complete.insert(complete.end(), segments.begin() + range.first, segments.begin() + range.second);
            }
            PublicationFingerprint expected, actual;
            expected.fields(local, complete);
            actual.fields(query.roadCommuteSegments, query.commuteRouteSegments);
            if (expected.value != actual.value)
                throw std::runtime_error("Road query lost or added saved-city route segments");
            ++queryCount;
        }
        std::cout << "ROUTE_DISPLAY stage=" << stage << " legs=" << ranges.size()
                  << " segments=" << segments.size() << " diagonal_segments=" << diagonalCount
                  << " longest_segment_tiles=" << longest << " road_queries=" << queryCount << std::endl;
        if (!routeDisplayOutput.empty() && std::string(stage) == "measured_end") {
            for (const auto& range : ranges) {
                if (range.second - range.first < 8 || range.second - range.first > 16) continue;
                const auto& last = segments[range.second - 1];
                const float seconds = (*last.elapsedSeconds)[last.timingEnd];
                if (seconds < 120 || seconds > 600) continue;
                std::ofstream output(routeDisplayOutput);
                if (!output) throw std::runtime_error("Cannot write route display sample");
                output << std::setprecision(9);
                for (auto i = range.first; i < range.second; ++i) {
                    const auto& s = segments[i];
                    const auto steps = s.timingEnd - s.timingBegin;
                    for (std::size_t j = i == range.first ? 0 : 1; j <= steps; ++j) {
                        output << s.startTileX + (s.endTileX - s.startTileX) * static_cast<float>(j) / steps << ' '
                               << s.startTileY + (s.endTileY - s.startTileY) * static_cast<float>(j) / steps << ' '
                               << (*s.elapsedSeconds)[s.timingBegin + j] << '\n';
                    }
                }
                std::cout << "ROUTE_SAMPLE seconds=" << seconds << " file=" << routeDisplayOutput << std::endl;
                break;
            }
        }
    }
#endif
    static void outcome(const SimulationRuntime &runtime, const char *stage) {
        std::int64_t residents = 0, jobs = 0, satisfied = 0, filled = 0, routes = 0, steps = 0, complaints = 0;
        for (const auto &lot : runtime.lots_) {
            residents += lot.lowWealthResidentsTotal();
            jobs += lot.lowWealthJobsTotal();
            satisfied += lot.commuteSatisfied();
            filled += lot.lowWealthJobsFilled();
            routes += lot.commuteRoutes().size();
            complaints += lot.hasLongCommuteComplaint();
            for (const auto &route : lot.commuteRoutes())
                steps += route.morningPathResult.steps.size() + route.eveningPathResult.steps.size();
        }
        if (satisfied != filled)
            throw std::runtime_error("Resident and staffing totals disagree");
        std::cout << "OUTCOME stage=" << stage << " tick=" << runtime.simulationTick_
                  << " lots=" << runtime.lots_.size() << " residents=" << residents << " jobs=" << jobs
                  << " satisfied=" << satisfied << " filled=" << filled << " routes=" << routes
                  << " steps=" << steps << " complaints=" << complaints << std::endl;
        if (verifyPublication) {
            const auto &buffer = runtime.tileBuffers_[runtime.simulationReadBufferIndex_];
            PublicationFingerprint fingerprint;
            fingerprint.fields(buffer.publishedLots, buffer.publishedLotInfos, buffer.publishedCommuteRouteSegments,
                               buffer.publishedLotOccupancy, buffer.lotRenderRevision, buffer.commuteRenderRevision);
            std::cout << "SNAPSHOT stage=" << stage << " hash=" << fingerprint.value << std::endl;
#ifdef CITY_ROUTING_NEW
            verifyRouteQueries(runtime, stage);
#endif
        }
    }

    static void run(const CitySaveState &state, const RuntimeOptions &options, const std::string &mode,
                    int warmup, int samples, const std::string &csvPath) {
        const auto setupStart = Clock::now();
        SimulationRuntime runtime(options);
        const double setupMs = elapsed(setupStart);
        const auto importStart = Clock::now();
        runtime.importCitySaveState(state);
        const double importMs = elapsed(importStart);
        if (runtime.lots_.size() != state.lots.size())
            throw std::runtime_error("Import rejected saved lots; benchmark is not the requested city");
        std::cout << "LOAD setup_ms=" << setupMs << " import_ms=" << importMs << std::endl;
        outcome(runtime, "import");
        std::ofstream csv(csvPath);
        if (!csv)
            throw std::runtime_error("Cannot open output CSV");
        csv << "index,measured,total_ms,routing_ms,neighbor_ms,commands_ms,effects_ms,constructor_ms,local_ms,publish_ms\n";
        csv << std::setprecision(10);
        std::vector<double> totalSamples, routeSamples;
        totalSamples.reserve(samples);
        routeSamples.reserve(samples);
        double cpuStart = 0;
        std::uint64_t cycleStart = 0, searchCount = 0, settledCount = 0;
        if (mode == "full")
            runtime.startWorkers();
        try {
            for (int i = 0; i < warmup + samples; ++i) {
                if (i == warmup) {
                    outcome(runtime, "measured_start");
                    cpuStart = cpuMilliseconds();
                    cycleStart = cycles();
                }
                double neighbor = 0, commands = 0, effects = 0, constructor = 0, local = 0, publish = 0;
                const auto start = Clock::now();
                auto stage = start;
                if (mode == "full") {
                    runtime.copyChunkRevisionsForWriteBuffer();
                    runtime.runNeighborPass(runtime.tileBuffers_[runtime.simulationReadBufferIndex_].tiles,
                                            runtime.tileBuffers_[runtime.simulationWriteBufferIndex_].tiles);
                    neighbor = elapsed(stage);
                    stage = Clock::now();
                    runtime.applyQueuedCommands(runtime.tileBuffers_[runtime.simulationWriteBufferIndex_]);
                    runtime.advanceLotConstruction(runtime.tileBuffers_[runtime.simulationWriteBufferIndex_]);
                    commands = elapsed(stage);
                    stage = Clock::now();
                    runtime.applyLotEffects(runtime.tileBuffers_[runtime.simulationWriteBufferIndex_].tiles);
                    effects = elapsed(stage);
                }
                stage = Clock::now();
                runtime.runCommuteAssignment(runtime.tileBuffers_[mode == "full" ? runtime.simulationWriteBufferIndex_
                                                                                : runtime.simulationReadBufferIndex_]);
                const double routing = elapsed(stage);
                if (mode == "full") {
                    stage = Clock::now();
                    runtime.runRciConstructor(runtime.tileBuffers_[runtime.simulationWriteBufferIndex_]);
                    constructor = elapsed(stage);
                    stage = Clock::now();
                    runtime.runLocalTilePass(runtime.tileBuffers_[runtime.simulationWriteBufferIndex_].tiles);
                    local = elapsed(stage);
                }
                ++runtime.simulationTick_;
                if (mode == "full") {
                    stage = Clock::now();
                    runtime.publishCompletedBuffer();
                    publish = elapsed(stage);
                }
                const double total = elapsed(start);
                if (i >= warmup) {
                    totalSamples.push_back(total);
                    routeSamples.push_back(routing);
#ifdef CITY_ROUTING_NEW
                    searchCount += runtime.timingSnapshot().commuteSearches;
                    settledCount += runtime.timingSnapshot().commuteSettled;
#endif
                }
                csv << i << ',' << (i >= warmup) << ',' << total << ',' << routing << ',' << neighbor << ','
                    << commands << ',' << effects << ',' << constructor << ',' << local << ',' << publish << '\n';
                if ((i + 1) % 100 == 0)
                    std::cout << "PROGRESS ticks=" << i + 1 << std::endl;
            }
            const auto cycleCount = cycles() - cycleStart;
            const double cpuMs = cpuMilliseconds() - cpuStart;
            if (mode == "full")
                runtime.stopWorkers();
            outcome(runtime, "measured_end");
            std::sort(totalSamples.begin(), totalSamples.end());
            std::sort(routeSamples.begin(), routeSamples.end());
            const double mean = std::accumulate(totalSamples.begin(), totalSamples.end(), 0.0) / samples;
            const double routeMean = std::accumulate(routeSamples.begin(), routeSamples.end(), 0.0) / samples;
            PROCESS_MEMORY_COUNTERS_EX memory{};
            memory.cb = sizeof(memory);
            if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory)))
                throw std::runtime_error("GetProcessMemoryInfo failed");
            std::cout << "RESULT mode=" << mode << " warmup=" << warmup << " samples=" << samples
                      << " mean_ms=" << mean << " p50_ms=" << percentile(totalSamples, 0.5)
                      << " p95_ms=" << percentile(totalSamples, 0.95) << " p99_ms=" << percentile(totalSamples, 0.99)
                      << " routing_mean_ms=" << routeMean << " routing_p95_ms=" << percentile(routeSamples, 0.95)
                      << " tps=" << 1000.0 / mean << " cpu_ms=" << cpuMs << " cycles=" << cycleCount
                      << " peak_working_set_bytes=" << memory.PeakWorkingSetSize << " private_bytes=" << memory.PrivateUsage
                      << " searches=" << searchCount << " settled=" << settledCount << std::endl;
        } catch (...) {
            if (mode == "full")
                runtime.stopWorkers();
            throw;
        }
    }
};
bool TransportCommuteTestAccess::verifyPublication = false;
std::string TransportCommuteTestAccess::routeDisplayOutput;

int main(int argc, char **argv) {
    try {
        if (argc < 9)
            throw std::runtime_error("Usage: CityRoutingBenchmark save_directory asset_directory region_x region_y routing|full warmup samples output.csv [--verify-publication] [--main-cpu N] [--route-display-output route.txt]");
        for (int i = 9; i < argc; ++i) {
            if (std::string(argv[i]) == "--verify-publication") {
                TransportCommuteTestAccess::verifyPublication = true;
            } else if (std::string(argv[i]) == "--route-display-output" && i + 1 < argc) {
                TransportCommuteTestAccess::routeDisplayOutput = argv[++i];
                TransportCommuteTestAccess::verifyPublication = true;
            } else if (std::string(argv[i]) == "--main-cpu" && i + 1 < argc) {
                const int cpu = std::stoi(argv[++i]);
                if (cpu < 0 || cpu >= sizeof(DWORD_PTR) * 8 || !SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR(1) << cpu))
                    throw std::runtime_error("Cannot pin benchmark main thread to requested CPU");
                std::cout << "PLACEMENT main_cpu=" << cpu << std::endl;
            } else {
                throw std::runtime_error("Unknown benchmark option");
            }
        }
        const std::string mode = argv[5];
        const int warmup = std::stoi(argv[6]), samples = std::stoi(argv[7]);
        if ((mode != "routing" && mode != "full") || warmup < 0 || samples < 1)
            throw std::runtime_error("Invalid benchmark arguments");
        RuntimeOptions options;
        options.assetDataDirectory = argv[2];
        options.showNonFatalAssetWarningDialogs = false;
        GameSession loader(options);
        loader.setSaveDirectoryOverride(argv[1]);
        City city("Routing benchmark", std::stoi(argv[3]), std::stoi(argv[4]), 1024, 1024);
        if (!loader.requestCityPreviewBuild(city))
            throw std::runtime_error("Could not start saved-city read");
        CitySaveState state = city.takePreviewBuildState();
        if (state.lots.empty())
            throw std::runtime_error("No saved lots loaded; refusing an empty fallback city");
        options.mapWidth = state.width;
        options.mapHeight = state.height;
        std::cout << std::setprecision(10) << "CITY width=" << state.width << " height=" << state.height
                  << " saved_tick=" << state.simulationTick << " saved_lots=" << state.lots.size()
                  << " transport_tiles=" << state.transport.tiles.size() << std::endl;
        TransportCommuteTestAccess::run(state, options, mode, warmup, samples, argv[8]);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << std::endl;
        return 1;
    }
}
