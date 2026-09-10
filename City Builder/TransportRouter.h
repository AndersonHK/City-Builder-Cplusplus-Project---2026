#pragma once

#include "TransportCostMap.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

class TransportRouter;

struct TransportRoutingStats {
    std::uint64_t searches = 0, settled = 0, relaxed = 0, heapPushes = 0;
    std::uint64_t candidates = 0, reconstructedSteps = 0;
    void add(const TransportRoutingStats &other);
};

// One owner per in-flight query; compact arrays and heap are retained between queries.
struct TransportRoutingScratch {
    struct Entry {
        std::uint32_t node;
        float cost, priority;
    };
    std::vector<float> costs;
    std::vector<std::uint32_t> stamps, parents, goals, destinationStamps;
    std::vector<Entry> heap;
    std::uint32_t stamp = 0, pendingBegin = 0, pendingEnd = 0, pendingNode = 0;
    std::uint64_t snapshot = 0;
    const TransportRouter *owner = nullptr;
    float maximumCost = 0;
    CommuteTimeOfDay time = CommuteTimeOfDay::Morning;
    TransportRoutingStats stats;
    void reset(std::size_t nodes, std::size_t destinations = 0);
    std::size_t allocatedBytes() const;
};

struct TransportRoutingDestination {
    int lotId = -1;
    int capacity = 0;
    std::vector<std::uint32_t> accessNodes;
};

class TransportRouter;
class TransportDestinationIndex {
  public:
    void build(const TransportRouter &router, const std::vector<TransportRoutingDestination> &destinations);
    int remaining(std::size_t destination) const {
        return remaining_[destination];
    }
    void consume(std::size_t destination, int amount);
    void setCapacity(std::size_t destination, int capacity);
    std::int64_t available() const {
        return available_;
    }
    bool hasCapacityFor(const TransportRouter &router, const std::vector<std::uint32_t> &starts) const;

  private:
    friend class TransportRouter;
    std::vector<std::uint32_t> offsets_, entries_;
    std::vector<int> lotIds_, remaining_;
    std::vector<std::vector<std::uint32_t>> components_;
    std::vector<std::int64_t> componentCapacity_;
    std::int64_t available_ = 0;
    std::uint64_t snapshot_ = 0;
    const TransportRouter *owner_ = nullptr;
};

struct TransportDistanceField {
    const TransportRouter *owner = nullptr;
    std::uint64_t snapshot = 0;
    bool towardTargets = true;
    std::vector<float> distances;
    std::vector<std::uint32_t> nearestAccess;
};

// Prepared only by the simulation owner between worker batches; all queries are const.
// Dense tile IDs remain the external route/load identity; scratch uses compact IDs.
class TransportRouter {
  public:
    void prepare(const TransportCostMap &map);
    bool findPath(const TransportPathRequest &request, TransportRoutingScratch &scratch, TransportPathResult &result,
                  bool heuristic = true) const;
    void beginNearest(const TransportPathRequest &request, const TransportDestinationIndex &destinations,
                      TransportRoutingScratch &scratch) const;
    bool nextNearest(const TransportDestinationIndex &destinations, int excludedLotId, TransportRoutingScratch &scratch,
                     std::size_t &destination, TransportPathResult &result) const;
    bool reprice(TransportPathResult &path, CommuteTimeOfDay time) const;
    float pathCost(const TransportPathResult &path, CommuteTimeOfDay time) const;
    void buildField(const std::vector<std::uint32_t> &roots, CommuteTimeOfDay time, bool towardTargets,
                    TransportDistanceField &field) const;
    float fieldCost(const TransportDistanceField &field, std::uint32_t node) const;
    std::uint32_t fieldTarget(const TransportDistanceField &field, std::uint32_t node) const;
    std::size_t nodeCount() const {
        return nodes_.size();
    }
    std::size_t edgeCount() const {
        return edges_.size();
    }
    std::uint64_t snapshot() const {
        return snapshot_;
    }
    std::uint64_t graphSnapshot() const {
        return graphSnapshot_;
    }
    std::size_t allocatedBytes() const;
    std::uint64_t lastMetricEdgesUpdated() const {
        return lastMetricEdgesUpdated_;
    }
    static float startCost(TransportMode mode);

  private:
    friend class TransportDestinationIndex;
    friend class TransportRoutingOverlay;
    struct Node {
        std::uint32_t dense, begin, end, reverseBegin, reverseEnd, component;
        int x, y;
        TransportMode mode;
    };
    struct Edge {
        std::uint32_t from, to;
        TransportPathStep step;
        float costs[2];
    };
    void build(const TransportCostMap &map);
    void updateNodeCosts(const TransportCostMap &map, std::uint32_t node, CommuteTimeOfDay time);
    void seed(const TransportPathRequest &request, TransportRoutingScratch &scratch) const;
    void expand(std::uint32_t node, TransportRoutingScratch &scratch,
                const std::function<float(std::uint32_t)> &heuristic) const;
    bool pop(TransportRoutingScratch &scratch, std::uint32_t &node) const;
    void reconstruct(std::uint32_t node, TransportRoutingScratch &scratch, TransportPathResult &result) const;
    std::uint32_t compact(std::uint32_t dense) const;
    std::vector<std::uint32_t> denseToCompact_, reverseEdges_;
    std::vector<Node> nodes_;
    std::vector<Edge> edges_;
    const TransportCostMap *owner_ = nullptr;
    std::uint64_t topology_ = 0, metricReset_ = 0, snapshot_ = 0;
    std::uint64_t graphSnapshot_ = 0;
    std::size_t metricCursor_ = 0;
    float minimumCostPerTile_[2] = {0, 0};
    std::uint64_t lastMetricEdgesUpdated_ = 0;
};

// Exact single-level CRP experiment. Bounded storage; stale/oversized overlays fall
// back to compact A*. Kept separate until customization amortizes in city workloads.
class TransportRoutingOverlay {
  public:
    bool build(const TransportRouter &router, CommuteTimeOfDay time, int regionSize = 16,
               std::size_t maximumStoredEdges = 2000000);
    bool findPath(const TransportRouter &router, const TransportPathRequest &request, TransportRoutingScratch &scratch,
                  TransportPathResult &result) const;
    std::size_t storedEdges() const {
        return witnesses_.size();
    }
    std::size_t shortcutCount() const {
        return shortcuts_.size();
    }

  private:
    struct Shortcut {
        std::uint32_t from, to, begin, end;
        float cost;
    };
    const TransportRouter *owner_ = nullptr;
    std::uint64_t snapshot_ = 0;
    CommuteTimeOfDay time_ = CommuteTimeOfDay::Morning;
    std::vector<std::uint32_t> regions_, offsets_, witnesses_;
    std::vector<Shortcut> shortcuts_;
};

// Persistent bounded pool. Jobs own their outputs; the caller reduces in source order.
class TransportRoutingPool {
  public:
    explicit TransportRoutingPool(std::size_t workers = 4);
    ~TransportRoutingPool();
    void run(std::size_t jobs, const std::function<void(std::size_t, TransportRoutingScratch &)> &job);
    TransportRoutingPool(const TransportRoutingPool &) = delete;
    TransportRoutingPool &operator=(const TransportRoutingPool &) = delete;

  private:
    void worker(std::size_t index);
    void drain(TransportRoutingScratch &scratch);
    std::vector<std::thread> threads_;
    std::vector<TransportRoutingScratch> scratch_;
    std::mutex mutex_;
    std::condition_variable ready_, finished_;
    std::function<void(std::size_t, TransportRoutingScratch &)> job_;
    std::atomic<std::size_t> next_{0};
    std::size_t count_ = 0, pending_ = 0;
    std::uint64_t generation_ = 0;
    bool stop_ = false;
    std::exception_ptr error_;
};
