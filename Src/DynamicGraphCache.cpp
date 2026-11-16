#include "DynamicGraphCache.h"

#include <algorithm>

namespace {
void add_resource_edges(
    AdjGraph &graph, std::unordered_map<int, std::vector<int>> &buckets,
    const std::vector<std::pair<int, int>> &intervals) {
  for (auto &[resource, bucket] : buckets) {
    std::sort(bucket.begin(), bucket.end(), [&](int a, int b) {
      if (intervals[a].second != intervals[b].second) {
        return intervals[a].second < intervals[b].second;
      }
      return intervals[a].first < intervals[b].first;
    });
    for (size_t i = 0; i < bucket.size(); ++i) {
      const int node = bucket[i];
      const auto first = std::partition_point(
          bucket.begin(), bucket.begin() + i, [&](int other) {
            return intervals[other].second <= intervals[node].first;
          });
      for (auto it = first; it != bucket.begin() + i; ++it) {
        graph.adj[node].push_back(*it);
        graph.adj[*it].push_back(node);
      }
    }
  }
}
}  // namespace

DynamicGraphCache::DynamicGraphCache(
    std::vector<std::pair<int, int>> frame_bounds,
    const std::unordered_map<int, int> &device_transfer_time)
    : bounds_(std::move(frame_bounds)),
      transfer_time_(device_transfer_time),
      staged_(bounds_.size()) {}

void DynamicGraphCache::add_new_windows(
    const std::vector<Tasks> &new_tasks,
    const std::vector<TimeWindows> &all_windows,
    size_t current_frame_index) {
  for (const Tasks &task : new_tasks) {
    for (int window_id : task.TWs) {
      const TimeWindows &window = all_windows.at(window_id);
      auto bound = std::upper_bound(
          bounds_.begin(), bounds_.end(), window.StartTime,
          [](int start, const std::pair<int, int> &frame) {
            return start < frame.second;
          });
      const size_t frame_index = bound - bounds_.begin();
      if (bound == bounds_.end() || frame_index < current_frame_index ||
          window.StartTime < bound->first || window.StartTime >= window.EndTime) {
        continue;
      }
      int transfer = 0;
      auto it = transfer_time_.find(window.DeviceIndex);
      if (it != transfer_time_.end()) transfer = it->second;
      staged_[frame_index].push_back(
          Candidate{task.TaskId, window_id, window.SatelliteIndex,
                    window.DeviceIndex, window.StartTime, window.EndTime,
                    window.EndTime + transfer});
    }
  }
}

AdjGraph DynamicGraphCache::active_graph(
    size_t frame_index, const std::set<int> &finished, int task_num,
    const ResourceReservations &reservations) const {
  std::vector<const Candidate *> selected;
  for (const Candidate &candidate : staged_.at(frame_index)) {
    if (!finished.count(candidate.task_id) &&
        !reservations.blocks(candidate.satellite_id, candidate.device_id,
                             candidate.start)) {
      selected.push_back(&candidate);
    }
  }
  std::sort(selected.begin(), selected.end(),
            [](const Candidate *a, const Candidate *b) {
              if (a->task_id != b->task_id) return a->task_id < b->task_id;
              return a->window_id < b->window_id;
            });

  AdjGraph graph;
  graph.task_num = task_num;
  graph.n = static_cast<int>(selected.size());
  graph.adj.resize(selected.size());
  size_t same_task_begin = 0;
  for (size_t index = 0; index < selected.size(); ++index) {
    const Candidate &candidate = *selected[index];
    if (index && candidate.task_id != selected[index - 1]->task_id) {
      same_task_begin = index;
    }
    graph.edge_to_id[{candidate.task_id, candidate.window_id}] = index;
    graph.id_to_edge.emplace_back(candidate.task_id, candidate.window_id);
    graph.id_to_device.push_back(candidate.device_id);
    graph.device_to_forbidding_time.emplace_back(candidate.start,
                                                 candidate.device_end);
    graph.satellite_to_forbidding_time.emplace_back(candidate.start,
                                                    candidate.end);
    graph.device_buckets[candidate.device_id].push_back(index);
    graph.satellite_buckets[candidate.satellite_id].push_back(index);
    graph.adj[index].push_back(index);
    for (size_t other = same_task_begin; other < index; ++other) {
      graph.adj[index].push_back(other);
      graph.adj[other].push_back(index);
    }
  }
  add_resource_edges(graph, graph.satellite_buckets,
                     graph.satellite_to_forbidding_time);
  add_resource_edges(graph, graph.device_buckets,
                     graph.device_to_forbidding_time);
  for (auto &neighbors : graph.adj) {
    std::sort(neighbors.begin(), neighbors.end());
    neighbors.erase(std::unique(neighbors.begin(), neighbors.end()),
                    neighbors.end());
  }
  return graph;
}

void DynamicGraphCache::discard_frame(size_t frame_index) {
  std::vector<Candidate>().swap(staged_.at(frame_index));
}
