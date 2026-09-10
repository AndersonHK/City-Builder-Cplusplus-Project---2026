#include "TransportRouter.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <numeric>
#include <stdexcept>

namespace {
const std::uint32_t invalid = std::numeric_limits<std::uint32_t>::max();
const float infinity = std::numeric_limits<float>::infinity();
struct Compare {
    bool operator()(const TransportRoutingScratch::Entry &a, const TransportRoutingScratch::Entry &b) const {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        // Prefer progress on equal f, then a stable compact/dense node order.
        if (a.cost != b.cost)
            return a.cost < b.cost;
        return a.node > b.node;
    }
};
std::size_t timeIndex(CommuteTimeOfDay time) {
    return time == CommuteTimeOfDay::Evening ? 1u : 0u;
}
} // namespace

void TransportRoutingStats::add(const TransportRoutingStats &other) {
    searches += other.searches;
    settled += other.settled;
    relaxed += other.relaxed;
    heapPushes += other.heapPushes;
    candidates += other.candidates;
    reconstructedSteps += other.reconstructedSteps;
}
void TransportRoutingScratch::reset(std::size_t nodes, std::size_t destinations) {
    if (costs.size() != nodes) {
        costs.resize(nodes);
        parents.resize(nodes);
        stamps.assign(nodes, 0);
        goals.assign(nodes, 0);
        stamp = 0;
        destinationStamps.clear();
    }
    destinationStamps.resize(destinations, 0);
    if (++stamp == 0) {
        std::fill(stamps.begin(), stamps.end(), 0);
        std::fill(goals.begin(), goals.end(), 0);
        std::fill(destinationStamps.begin(), destinationStamps.end(), 0);
        stamp = 1;
    }
    heap.clear();
    pendingBegin = pendingEnd = 0;
    stats = TransportRoutingStats();
}
std::size_t TransportRoutingScratch::allocatedBytes() const {
    return costs.capacity() * sizeof(float) +
           (parents.capacity() + stamps.capacity() + goals.capacity() + destinationStamps.capacity()) *
               sizeof(std::uint32_t) +
           heap.capacity() * sizeof(Entry);
}
float TransportRouter::startCost(TransportMode mode) {
    return mode == TransportMode::Car ? 60000.0f : 0.0f;
}
std::uint32_t TransportRouter::compact(std::uint32_t dense) const {
    return dense < denseToCompact_.size() ? denseToCompact_[dense] : invalid;
}
std::size_t TransportRouter::allocatedBytes() const {
    return (denseToCompact_.capacity() + reverseEdges_.capacity()) * sizeof(std::uint32_t) +
           nodes_.capacity() * sizeof(Node) + edges_.capacity() * sizeof(Edge);
}

void TransportRouter::build(const TransportCostMap &map) {
    denseToCompact_.assign(map.totalNodeCount_, invalid);
    nodes_.clear();
    edges_.clear();
    // Include isolated access states and incoming-only states, not just edge origins.
    for (std::uint32_t dense = 0; dense < map.totalNodeCount_; ++dense) {
        const auto &cell = map.cells_[dense];
        if (cell.buildingAccessMask)
            denseToCompact_[dense] = 0;
        for (int d = 0; d < kRoadDirectionCount; ++d) {
            int neighbor;
            if (cell.costs[d] && map.tryNeighborTile(map.nodeTileIndex(dense), RoadDirectionFromIndex(d), neighbor)) {
                denseToCompact_[dense] = 0;
                denseToCompact_[map.nodeId(map.nodeLayer(dense), map.nodeMode(dense), neighbor)] = 0;
            }
        }
    }
    for (const auto &edge : map.transferEdges_) {
        denseToCompact_[edge.fromNodeId] = 0;
        denseToCompact_[edge.toNodeId] = 0;
    }
    for (std::uint32_t dense = 0; dense < denseToCompact_.size(); ++dense) {
        if (denseToCompact_[dense] == invalid)
            continue;
        denseToCompact_[dense] = static_cast<std::uint32_t>(nodes_.size());
        const int tile = map.nodeTileIndex(dense);
        nodes_.push_back({dense, 0, 0, 0, 0, 0, tile % map.width_, tile / map.width_, map.nodeMode(dense)});
    }
    for (std::uint32_t i = 0; i < nodes_.size(); ++i) {
        auto &node = nodes_[i];
        node.begin = static_cast<std::uint32_t>(edges_.size());
        const auto &cell = map.cells_[node.dense];
        for (int d = 0; d < kRoadDirectionCount; ++d) {
            int neighbor;
            if (!cell.costs[d] ||
                !map.tryNeighborTile(map.nodeTileIndex(node.dense), RoadDirectionFromIndex(d), neighbor))
                continue;
            TransportPathStep step;
            step.fromNodeId = node.dense;
            step.toNodeId = map.nodeId(map.nodeLayer(node.dense), node.mode, neighbor);
            step.roadDirection = RoadDirectionFromIndex(d);
            edges_.push_back({i, compact(step.toNodeId), step, {0, 0}});
        }
        for (auto j = map.transferOffsets_[node.dense]; j < map.transferOffsets_[node.dense + 1]; ++j) {
            const auto &transfer = map.transferEdges_[j];
            TransportPathStep step;
            step.fromNodeId = transfer.fromNodeId;
            step.toNodeId = transfer.toNodeId;
            step.kind = TransportPathStepKind::Transfer;
            step.transferEdgeIndex = j;
            edges_.push_back({i, compact(step.toNodeId), step, {0, 0}});
        }
        node.end = static_cast<std::uint32_t>(edges_.size());
    }
    std::vector<std::uint32_t> counts(nodes_.size(), 0), parents(nodes_.size());
    std::iota(parents.begin(), parents.end(), 0u);
    const auto root = [&parents](std::uint32_t a) {
        while (parents[a] != a) {
            parents[a] = parents[parents[a]];
            a = parents[a];
        }
        return a;
    };
    for (const auto &edge : edges_) {
        ++counts[edge.to];
        const auto a = root(edge.from), b = root(edge.to);
        if (a != b)
            parents[std::max(a, b)] = std::min(a, b);
    }
    std::uint32_t offset = 0;
    for (std::uint32_t i = 0; i < nodes_.size(); ++i) {
        nodes_[i].component = root(i);
        nodes_[i].reverseBegin = offset;
        offset += counts[i];
        nodes_[i].reverseEnd = offset;
        counts[i] = nodes_[i].reverseBegin;
    }
    reverseEdges_.resize(edges_.size());
    for (std::uint32_t i = 0; i < edges_.size(); ++i)
        reverseEdges_[counts[edges_[i].to]++] = i;
}

void TransportRouter::updateNodeCosts(const TransportCostMap &map, std::uint32_t node, CommuteTimeOfDay time) {
    const auto t = timeIndex(time);
    for (auto i = nodes_[node].begin; i < nodes_[node].end; ++i) {
        auto &edge = edges_[i];
        edge.costs[t] = map.routingStepCost(edge.step, time);
        ++lastMetricEdgesUpdated_;
        const int length =
            (std::abs(nodes_[edge.from].x - nodes_[edge.to].x) + std::abs(nodes_[edge.from].y - nodes_[edge.to].y));
        if (length)
            minimumCostPerTile_[t] = std::min(
                minimumCostPerTile_[t], static_cast<float>(std::floor(static_cast<double>(edge.costs[t]) / length)));
    }
}
void TransportRouter::prepare(const TransportCostMap &map) {
    if (map.transferOffsetsDirty_)
        throw std::logic_error("Routing requires finalized transfer edges");
    lastMetricEdgesUpdated_ = 0;
    const bool rebuild = owner_ != &map || topology_ != map.topologyRevision_;
    if (rebuild) {
        ++graphSnapshot_;
        build(map);
        owner_ = &map;
        topology_ = map.topologyRevision_;
    }
    if (rebuild || metricReset_ != map.routingMetricReset_) {
        minimumCostPerTile_[0] = minimumCostPerTile_[1] = infinity;
        for (std::uint32_t i = 0; i < nodes_.size(); ++i) {
            updateNodeCosts(map, i, CommuteTimeOfDay::Morning);
            updateNodeCosts(map, i, CommuteTimeOfDay::Evening);
        }
        for (auto &minimum : minimumCostPerTile_)
            if (!std::isfinite(minimum))
                minimum = 0;
        metricReset_ = map.routingMetricReset_;
        metricCursor_ = map.routingMetricChanges_.size();
        ++snapshot_;
    } else if (metricCursor_ < map.routingMetricChanges_.size()) {
        std::vector<std::pair<std::uint32_t, CommuteTimeOfDay>> changes;
        for (; metricCursor_ < map.routingMetricChanges_.size(); ++metricCursor_) {
            const auto &change = map.routingMetricChanges_[metricCursor_];
            const auto node = compact(change.node);
            if (node != invalid)
                changes.emplace_back(node, change.time);
        }
        std::sort(changes.begin(), changes.end());
        changes.erase(std::unique(changes.begin(), changes.end()), changes.end());
        for (const auto &change : changes)
            updateNodeCosts(map, change.first, change.second);
        ++snapshot_;
    }
}

void TransportRouter::seed(const TransportPathRequest &request, TransportRoutingScratch &scratch) const {
    scratch.owner = this;
    scratch.snapshot = snapshot_;
    scratch.maximumCost = request.maximumCost;
    scratch.time = request.commuteTimeOfDay;
    ++scratch.stats.searches;
    for (auto dense : request.startNodeIds) {
        const auto node = compact(dense);
        if (node == invalid || scratch.stamps[node] == scratch.stamp)
            continue;
        const float cost = startCost(nodes_[node].mode);
        if (cost > scratch.maximumCost)
            continue;
        scratch.stamps[node] = scratch.stamp;
        scratch.costs[node] = cost;
        scratch.parents[node] = invalid;
        scratch.heap.push_back({node, cost, cost});
        ++scratch.stats.heapPushes;
    }
    std::make_heap(scratch.heap.begin(), scratch.heap.end(), Compare());
}
bool TransportRouter::pop(TransportRoutingScratch &scratch, std::uint32_t &node) const {
    if (scratch.owner != this || scratch.snapshot != snapshot_)
        throw std::logic_error("Cannot resume routing across metric snapshots");
    while (!scratch.heap.empty()) {
        std::pop_heap(scratch.heap.begin(), scratch.heap.end(), Compare());
        const auto entry = scratch.heap.back();
        scratch.heap.pop_back();
        if (entry.cost != scratch.costs[entry.node])
            continue;
        node = entry.node;
        ++scratch.stats.settled;
        return true;
    }
    return false;
}
void TransportRouter::expand(std::uint32_t node, TransportRoutingScratch &scratch,
                             const std::function<float(std::uint32_t)> &heuristic) const {
    for (auto i = nodes_[node].begin; i < nodes_[node].end; ++i) {
        const auto &edge = edges_[i];
        ++scratch.stats.relaxed;
        const float cost = scratch.costs[node] + edge.costs[timeIndex(scratch.time)];
        if (cost > scratch.maximumCost || (scratch.stamps[edge.to] == scratch.stamp && cost >= scratch.costs[edge.to]))
            continue;
        scratch.stamps[edge.to] = scratch.stamp;
        scratch.costs[edge.to] = cost;
        scratch.parents[edge.to] = i;
        scratch.heap.push_back({edge.to, cost, cost + (heuristic ? heuristic(edge.to) : 0)});
        std::push_heap(scratch.heap.begin(), scratch.heap.end(), Compare());
        ++scratch.stats.heapPushes;
    }
}
void TransportRouter::reconstruct(std::uint32_t node, TransportRoutingScratch &scratch,
                                  TransportPathResult &result) const {
    result.success = true;
    result.totalCost = scratch.costs[node];
    result.reachedNodeId = nodes_[node].dense;
    result.steps.clear();
    while (scratch.parents[node] != invalid) {
        const auto &edge = edges_[scratch.parents[node]];
        result.steps.push_back(edge.step);
        node = edge.from;
    }
    std::reverse(result.steps.begin(), result.steps.end());
    scratch.stats.reconstructedSteps += result.steps.size();
}
bool TransportRouter::findPath(const TransportPathRequest &request, TransportRoutingScratch &scratch,
                               TransportPathResult &result, bool heuristic) const {
    result = TransportPathResult();
    scratch.reset(nodes_.size());
    if (request.goalNodeIds.empty() || request.startNodeIds.empty())
        return false;
    if (!request.useCongestion || request.useRouteJitter)
        throw std::invalid_argument("Shared routing requires committed congestion and common weights");
    int minX = std::numeric_limits<int>::max(), minY = minX, maxX = -1, maxY = -1;
    for (auto dense : request.goalNodeIds) {
        const auto goal = compact(dense);
        if (goal == invalid)
            continue;
        scratch.goals[goal] = scratch.stamp;
        minX = std::min(minX, nodes_[goal].x);
        minY = std::min(minY, nodes_[goal].y);
        maxX = std::max(maxX, nodes_[goal].x);
        maxY = std::max(maxY, nodes_[goal].y);
    }
    if (maxX < 0)
        return false;
    const float scale = heuristic ? minimumCostPerTile_[timeIndex(request.commuteTimeOfDay)] : 0;
    const std::function<float(std::uint32_t)> bound = [this, minX, minY, maxX, maxY, scale](std::uint32_t n) {
        const auto &node = nodes_[n];
        return static_cast<float>(
            std::floor(static_cast<double>(scale) * (std::max(0, std::max(minX - node.x, node.x - maxX)) +
                                                     std::max(0, std::max(minY - node.y, node.y - maxY)))));
    };
    seed(request, scratch);
    std::uint32_t node;
    while (pop(scratch, node)) {
        if (scratch.goals[node] == scratch.stamp) {
            reconstruct(node, scratch, result);
            return true;
        }
        expand(node, scratch, bound);
    }
    return false;
}

void TransportDestinationIndex::build(const TransportRouter &router,
                                      const std::vector<TransportRoutingDestination> &destinations) {
    owner_ = &router;
    snapshot_ = router.graphSnapshot();
    offsets_.assign(router.nodeCount() + 1, 0);
    lotIds_.clear();
    remaining_.clear();
    components_.assign(destinations.size(), {});
    componentCapacity_.assign(router.nodeCount(), 0);
    available_ = 0;
    std::vector<std::vector<std::uint32_t>> access(destinations.size());
    for (std::size_t i = 0; i < destinations.size(); ++i) {
        const auto &destination = destinations[i];
        lotIds_.push_back(destination.lotId);
        for (auto dense : destination.accessNodes) {
            const auto node = router.compact(dense);
            if (node != invalid)
                access[i].push_back(node);
        }
        std::sort(access[i].begin(), access[i].end());
        access[i].erase(std::unique(access[i].begin(), access[i].end()), access[i].end());
        const int capacity = access[i].empty() ? 0 : std::max(0, destination.capacity);
        remaining_.push_back(capacity);
        available_ += capacity;
        for (auto node : access[i]) {
            ++offsets_[node + 1];
            components_[i].push_back(router.nodes_[node].component);
        }
        auto &components = components_[i];
        std::sort(components.begin(), components.end());
        components.erase(std::unique(components.begin(), components.end()), components.end());
        for (auto component : components)
            componentCapacity_[component] += capacity;
    }
    std::partial_sum(offsets_.begin(), offsets_.end(), offsets_.begin());
    entries_.resize(offsets_.back());
    auto cursor = offsets_;
    for (std::uint32_t i = 0; i < access.size(); ++i)
        for (auto node : access[i])
            entries_[cursor[node]++] = i;
}
void TransportDestinationIndex::consume(std::size_t destination, int amount) {
    if (destination >= remaining_.size() || amount < 0 || amount > remaining_[destination])
        throw std::logic_error("Invalid destination allocation");
    remaining_[destination] -= amount;
    available_ -= amount;
    for (auto component : components_[destination])
        componentCapacity_[component] -= amount;
}
bool TransportDestinationIndex::hasCapacityFor(const TransportRouter &router,
                                               const std::vector<std::uint32_t> &starts) const {
    if (owner_ != &router || snapshot_ != router.graphSnapshot())
        throw std::logic_error("Stale destination index");
    if (!available_)
        return false;
    for (auto dense : starts) {
        const auto node = router.compact(dense);
        if (node != invalid && componentCapacity_[router.nodes_[node].component] > 0)
            return true;
    }
    return false;
}
void TransportRouter::beginNearest(const TransportPathRequest &request, const TransportDestinationIndex &destinations,
                                   TransportRoutingScratch &scratch) const {
    scratch.reset(nodes_.size(), destinations.remaining_.size());
    scratch.owner = this;
    scratch.snapshot = snapshot_;
    if (!request.useCongestion || request.useRouteJitter)
        throw std::invalid_argument("Shared routing requires common weights");
    if (destinations.hasCapacityFor(*this, request.startNodeIds))
        seed(request, scratch);
}
bool TransportRouter::nextNearest(const TransportDestinationIndex &destinations, int excludedLotId,
                                  TransportRoutingScratch &scratch, std::size_t &destination,
                                  TransportPathResult &result) const {
    result = TransportPathResult();
    if (scratch.owner != this || scratch.snapshot != snapshot_ || destinations.owner_ != this ||
        destinations.snapshot_ != graphSnapshot_)
        throw std::logic_error("Stale nearest query");
    while (destinations.available() > 0) {
        while (scratch.pendingBegin < scratch.pendingEnd) {
            const auto id = destinations.entries_[scratch.pendingBegin++];
            if (destinations.remaining_[id] <= 0 || destinations.lotIds_[id] == excludedLotId ||
                scratch.destinationStamps[id] == scratch.stamp)
                continue;
            scratch.destinationStamps[id] = scratch.stamp;
            destination = id;
            ++scratch.stats.candidates;
            reconstruct(scratch.pendingNode, scratch, result);
            return true;
        }
        std::uint32_t node;
        if (!pop(scratch, node))
            return false;
        // Expand before yielding so continuation never drops the reached node's edges.
        expand(node, scratch, {});
        scratch.pendingNode = node;
        scratch.pendingBegin = destinations.offsets_[node];
        scratch.pendingEnd = destinations.offsets_[node + 1];
    }
    return false;
}
float TransportRouter::pathCost(const TransportPathResult &path, CommuteTimeOfDay time,
                                std::vector<float>* elapsedSeconds) const {
    if (elapsedSeconds) elapsedSeconds->clear();
    if (!path.success)
        return infinity;
    const auto first = compact(path.steps.empty() ? path.reachedNodeId : path.steps.front().fromNodeId);
    if (first == invalid)
        return infinity;
    float cost = startCost(nodes_[first].mode);
    const float departureCost = cost;
    if (elapsedSeconds) {
        elapsedSeconds->reserve(path.steps.size() + 1);
        elapsedSeconds->push_back(0.0f);
    }
    auto node = first;
    for (const auto &step : path.steps) {
        if (nodes_[node].dense != step.fromNodeId)
            return infinity;
        bool found = false;
        for (auto i = nodes_[node].begin; i < nodes_[node].end; ++i) {
            const auto &edge = edges_[i];
            if (edge.step.toNodeId == step.toNodeId && edge.step.kind == step.kind &&
                (step.kind == TransportPathStepKind::Movement
                     ? edge.step.roadDirection == step.roadDirection
                     : edge.step.transferEdgeIndex == step.transferEdgeIndex)) {
                cost += edge.costs[timeIndex(time)];
                if (elapsedSeconds) elapsedSeconds->push_back((cost - departureCost) / 1000.0f);
                node = edge.to;
                found = true;
                break;
            }
        }
        if (!found)
            return infinity;
    }
    if (nodes_[node].dense != path.reachedNodeId)
        return infinity;
    return cost;
}

bool TransportRouter::reprice(TransportPathResult &path, CommuteTimeOfDay time) const {
    const float cost = pathCost(path, time);
    if (!std::isfinite(cost))
        return false;
    path.totalCost = cost;
    return true;
}
void TransportRouter::buildField(const std::vector<std::uint32_t> &roots, CommuteTimeOfDay time, bool towardTargets,
                                 TransportDistanceField &field) const {
    field.owner = this;
    field.snapshot = snapshot_;
    field.towardTargets = towardTargets;
    field.distances.assign(nodes_.size(), infinity);
    field.nearestAccess.assign(nodes_.size(), invalid);
    std::vector<TransportRoutingScratch::Entry> heap;
    for (auto dense : roots) {
        const auto node = compact(dense);
        if (node == invalid)
            continue;
        const float cost = towardTargets ? 0 : startCost(nodes_[node].mode);
        if (cost >= field.distances[node])
            continue;
        field.distances[node] = cost;
        field.nearestAccess[node] = dense;
        heap.push_back({node, cost, cost});
    }
    std::make_heap(heap.begin(), heap.end(), Compare());
    while (!heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), Compare());
        const auto entry = heap.back();
        heap.pop_back();
        if (entry.cost != field.distances[entry.node])
            continue;
        const auto &node = nodes_[entry.node];
        const auto begin = towardTargets ? node.reverseBegin : node.begin,
                   end = towardTargets ? node.reverseEnd : node.end;
        for (auto i = begin; i < end; ++i) {
            const auto &edge = edges_[towardTargets ? reverseEdges_[i] : i];
            const auto next = towardTargets ? edge.from : edge.to;
            const float cost = entry.cost + edge.costs[timeIndex(time)];
            if (cost >= field.distances[next])
                continue;
            field.distances[next] = cost;
            field.nearestAccess[next] = field.nearestAccess[entry.node];
            heap.push_back({next, cost, cost});
            std::push_heap(heap.begin(), heap.end(), Compare());
        }
    }
}
float TransportRouter::fieldCost(const TransportDistanceField &field, std::uint32_t dense) const {
    if (field.owner != this || field.snapshot != snapshot_)
        return infinity;
    const auto node = compact(dense);
    if (node == invalid || node >= field.distances.size())
        return infinity;
    return field.distances[node] + (field.towardTargets ? startCost(nodes_[node].mode) : 0);
}

TransportRoutingPool::TransportRoutingPool(std::size_t workers) : scratch_(std::max<std::size_t>(1, workers)) {
    try {
        for (std::size_t i = 1; i < scratch_.size(); ++i)
            threads_.emplace_back(&TransportRoutingPool::worker, this, i);
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        ready_.notify_all();
        for (auto &thread : threads_)
            thread.join();
        throw;
    }
}
TransportRoutingPool::~TransportRoutingPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    ready_.notify_all();
    for (auto &thread : threads_)
        thread.join();
}
void TransportRoutingPool::drain(TransportRoutingScratch &scratch) {
    try {
        for (;;) {
            const auto i = next_.fetch_add(1, std::memory_order_relaxed);
            if (i >= count_)
                break;
            job_(i, scratch);
        }
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!error_)
            error_ = std::current_exception();
    }
}
void TransportRoutingPool::worker(std::size_t index) {
    std::uint64_t seen = 0;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [&] { return stop_ || generation_ != seen; });
            if (stop_)
                return;
            seen = generation_;
        }
        drain(scratch_[index]);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (--pending_ == 0)
                finished_.notify_one();
        }
    }
}
void TransportRoutingPool::run(std::size_t jobs,
                               const std::function<void(std::size_t, TransportRoutingScratch &)> &job) {
    if (!jobs)
        return;
    if (jobs == 1 || threads_.empty()) {
        for (std::size_t i = 0; i < jobs; ++i)
            job(i, scratch_[0]);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job_ = job;
        count_ = jobs;
        next_ = 0;
        pending_ = threads_.size();
        error_ = nullptr;
        ++generation_;
    }
    ready_.notify_all();
    drain(scratch_[0]);
    std::unique_lock<std::mutex> lock(mutex_);
    finished_.wait(lock, [&] { return pending_ == 0; });
    job_ = {};
    if (error_)
        std::rethrow_exception(error_);
}

void TransportDestinationIndex::setCapacity(std::size_t destination, int capacity) {
    if (destination >= remaining_.size() || capacity < 0)
        throw std::logic_error("Invalid vacancy capacity");
    const std::int64_t delta = static_cast<std::int64_t>(capacity) - remaining_[destination];
    remaining_[destination] = capacity;
    available_ += delta;
    for (auto component : components_[destination])
        componentCapacity_[component] += delta;
}

bool TransportRoutingOverlay::build(const TransportRouter &router, CommuteTimeOfDay time, int regionSize,
                                    std::size_t maximumStoredEdges) {
    owner_ = nullptr;
    snapshot_ = 0;
    regions_.resize(router.nodeCount());
    offsets_.assign(router.nodeCount() + 1, 0);
    shortcuts_.clear();
    witnesses_.clear();
    regionSize = std::max(1, regionSize);
    int width = 0;
    for (const auto &node : router.nodes_)
        width = std::max(width, node.x + 1);
    const int columns = (width + regionSize - 1) / regionSize;
    std::vector<bool> boundary(router.nodeCount(), false);
    for (std::size_t i = 0; i < router.nodeCount(); ++i)
        regions_[i] = (router.nodes_[i].y / regionSize) * columns + router.nodes_[i].x / regionSize;
    for (const auto &edge : router.edges_)
        if (regions_[edge.from] != regions_[edge.to])
            boundary[edge.from] = boundary[edge.to] = true;
    TransportRoutingScratch scratch;
    for (std::uint32_t source = 0; source < router.nodeCount(); ++source) {
        offsets_[source] = static_cast<std::uint32_t>(shortcuts_.size());
        if (!boundary[source])
            continue;
        scratch.reset(router.nodeCount());
        scratch.owner = &router;
        scratch.snapshot = router.snapshot();
        scratch.stamps[source] = scratch.stamp;
        scratch.costs[source] = 0;
        scratch.parents[source] = invalid;
        scratch.heap.push_back({source, 0, 0});
        std::uint32_t current;
        while (router.pop(scratch, current)) {
            if (current != source && boundary[current]) {
                const auto begin = static_cast<std::uint32_t>(witnesses_.size());
                auto n = current;
                while (n != source) {
                    if (witnesses_.size() >= maximumStoredEdges) {
                        shortcuts_.clear();
                        witnesses_.clear();
                        return false;
                    }
                    const auto edge = scratch.parents[n];
                    witnesses_.push_back(edge);
                    n = router.edges_[edge].from;
                }
                std::reverse(witnesses_.begin() + begin, witnesses_.end());
                shortcuts_.push_back(
                    {source, current, begin, static_cast<std::uint32_t>(witnesses_.size()), scratch.costs[current]});
            }
            for (auto i = router.nodes_[current].begin; i < router.nodes_[current].end; ++i) {
                const auto &edge = router.edges_[i];
                if (regions_[edge.to] != regions_[source])
                    continue;
                const float cost = scratch.costs[current] + edge.costs[timeIndex(time)];
                if (scratch.stamps[edge.to] == scratch.stamp && cost >= scratch.costs[edge.to])
                    continue;
                scratch.stamps[edge.to] = scratch.stamp;
                scratch.costs[edge.to] = cost;
                scratch.parents[edge.to] = i;
                scratch.heap.push_back({edge.to, cost, cost});
                std::push_heap(scratch.heap.begin(), scratch.heap.end(), Compare());
            }
        }
    }
    offsets_.back() = static_cast<std::uint32_t>(shortcuts_.size());
    owner_ = &router;
    snapshot_ = router.snapshot();
    time_ = time;
    return true;
}

bool TransportRoutingOverlay::findPath(const TransportRouter &router, const TransportPathRequest &request,
                                       TransportRoutingScratch &scratch, TransportPathResult &result) const {
    if (owner_ != &router || snapshot_ != router.snapshot() || time_ != request.commuteTimeOfDay)
        return router.findPath(request, scratch, result);
    if (!request.useCongestion || request.useRouteJitter)
        throw std::invalid_argument("Overlay requires shared committed weights");
    result = TransportPathResult();
    scratch.reset(router.nodeCount());
    std::vector<std::uint32_t> localRegions;
    for (auto dense : request.startNodeIds) {
        const auto n = router.compact(dense);
        if (n != invalid)
            localRegions.push_back(regions_[n]);
    }
    for (auto dense : request.goalNodeIds) {
        const auto n = router.compact(dense);
        if (n != invalid) {
            localRegions.push_back(regions_[n]);
            scratch.goals[n] = scratch.stamp;
        }
    }
    std::sort(localRegions.begin(), localRegions.end());
    localRegions.erase(std::unique(localRegions.begin(), localRegions.end()), localRegions.end());
    router.seed(request, scratch);
    const std::uint32_t shortcutFlag = 0x80000000u;
    const auto relax = [&](std::uint32_t from, std::uint32_t to, float weight, std::uint32_t parent) {
        ++scratch.stats.relaxed;
        const float cost = scratch.costs[from] + weight;
        if (cost > request.maximumCost || (scratch.stamps[to] == scratch.stamp && cost >= scratch.costs[to]))
            return;
        scratch.stamps[to] = scratch.stamp;
        scratch.costs[to] = cost;
        scratch.parents[to] = parent;
        scratch.heap.push_back({to, cost, cost});
        std::push_heap(scratch.heap.begin(), scratch.heap.end(), Compare());
        ++scratch.stats.heapPushes;
    };
    std::uint32_t current;
    while (router.pop(scratch, current)) {
        if (scratch.goals[current] == scratch.stamp) {
            result.success = true;
            result.reachedNodeId = router.nodes_[current].dense;
            result.totalCost = scratch.costs[current];
            while (scratch.parents[current] != invalid) {
                const auto parent = scratch.parents[current];
                if (parent & shortcutFlag) {
                    const auto &shortcut = shortcuts_[parent & ~shortcutFlag];
                    for (auto i = shortcut.end; i > shortcut.begin; --i)
                        result.steps.push_back(router.edges_[witnesses_[i - 1]].step);
                    current = shortcut.from;
                } else {
                    const auto &edge = router.edges_[parent];
                    result.steps.push_back(edge.step);
                    current = edge.from;
                }
            }
            std::reverse(result.steps.begin(), result.steps.end());
            scratch.stats.reconstructedSteps += result.steps.size();
            return true;
        }
        const bool local = std::binary_search(localRegions.begin(), localRegions.end(), regions_[current]);
        for (auto i = router.nodes_[current].begin; i < router.nodes_[current].end; ++i) {
            const auto &edge = router.edges_[i];
            if (local || regions_[edge.to] != regions_[current])
                relax(current, edge.to, edge.costs[timeIndex(time_)], i);
        }
        if (!local)
            for (auto i = offsets_[current]; i < offsets_[current + 1]; ++i) {
                const auto &shortcut = shortcuts_[i];
                relax(current, shortcut.to, shortcut.cost, shortcutFlag | i);
            }
    }
    return false;
}

std::uint32_t TransportRouter::fieldTarget(const TransportDistanceField &field, std::uint32_t dense) const {
    if (field.owner != this || field.snapshot != snapshot_)
        return invalid;
    const auto node = compact(dense);
    return node == invalid || node >= field.nearestAccess.size() ? invalid : field.nearestAccess[node];
}
