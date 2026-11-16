#include "AdjGraph.h"
#include "Task.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using std::size_t;
using std::unordered_map;
using std::vector;

// MODIFIED: A candidate belongs to the current frame only if the whole
// communication window is inside [TaskStartTime, TaskEndTime].
inline bool window_in_frame(const TimeWindows &w, const int &TaskStartTime,
                            const int &TaskEndTime) {
  return TaskStartTime <= w.StartTime && w.EndTime <= TaskEndTime;
}

static bool conflicts_with_committed(
    const TimeWindows &candidate,
    const std::vector<TimeWindows> &all_windows,
    const std::vector<int> &committed_window_ids,
    const std::unordered_map<int, int> &device_transfer_time) {
  for (int window_id : committed_window_ids) {
    const TimeWindows &previous = all_windows.at(window_id);
    if (candidate.SatelliteIndex == previous.SatelliteIndex &&
        candidate.StartTime < previous.EndTime &&
        previous.StartTime < candidate.EndTime) {
      return true;
    }
    if (candidate.DeviceIndex == previous.DeviceIndex) {
      const auto transfer = device_transfer_time.find(previous.DeviceIndex);
      const int busy_until = previous.EndTime +
          (transfer == device_transfer_time.end() ? 0 : transfer->second);
      if (candidate.StartTime < busy_until &&
          previous.StartTime < candidate.EndTime) {
        return true;
      }
    }
  }
  return false;
}

AdjGraph::AdjGraph(const std::vector<TimeWindows> &AllTimewindows,
                   const std::vector<Tasks> &AllTasks,
                   const std::unordered_set<int> &AccessibleSatellite,
                   std::unordered_map<int, int> &DeviceTransferTime,
                   const int &TaskStartTime, const int &TaskEndTime,
                   const std::set<int> &is_finish,
                   const ResourceReservations &reservations,
                   bool include_cross_boundary,
                   const std::vector<int> &committed_window_ids)
    : n(0), task_num(size(AllTasks)) {

  size_t edge_num = 0;
  auto s = std::chrono::system_clock::now();

  for (int i = 0; i < size(AllTasks); ++i) {
    const auto &TWs = AllTasks[i].TWs;
    if (TWs.empty())
      continue; // MODIFIED: skip placeholder tasks.

    int task_id = AllTasks[i].TaskId;
    if (is_finish.count(task_id))
      continue; // MODIFIED: finished tasks never enter later frames.

    for (const auto &window_id : TWs) {
      const TimeWindows &w = AllTimewindows[window_id];
      const bool in_frame = include_cross_boundary
          ? TaskStartTime <= w.StartTime && w.StartTime < TaskEndTime && w.StartTime < w.EndTime
          : window_in_frame(w, TaskStartTime, TaskEndTime);
      if (!in_frame || reservations.blocks(w) ||
          conflicts_with_committed(w, AllTimewindows, committed_window_ids,
                                   DeviceTransferTime))
        continue; // MODIFIED

      const int id = size(id_to_edge);
      edge_to_id[{task_id, window_id}] = id;
      id_to_edge.emplace_back(task_id, window_id);
      id_to_device.emplace_back(w.DeviceIndex);

      int switch_time = 0;
      auto switch_it = DeviceTransferTime.find(w.DeviceIndex);
      if (switch_it != DeviceTransferTime.end()) {
        switch_time = switch_it->second;
      }

      device_to_forbidding_time.emplace_back(w.StartTime,
                                             w.EndTime + switch_time);
      device_buckets[w.DeviceIndex].emplace_back(id);

      satellite_to_forbidding_time.emplace_back(w.StartTime, w.EndTime);
      satellite_buckets[w.SatelliteIndex].emplace_back(id);
      n++;
    }
  }

  std::cout << "Total candidates: " << n << std::endl;
  adj.resize(n);

  // Constraint 1: windows belonging to the same task conflict with each other.
  for (int i = 0; i < size(AllTasks); ++i) {
    const auto &TWs = AllTasks[i].TWs;
    if (TWs.empty())
      continue; // MODIFIED

    int task_id = AllTasks[i].TaskId;
    if (is_finish.count(task_id))
      continue; // MODIFIED

    vector<int> same_task_nodes;
    for (const auto &window_id : TWs) {
      auto id_it = edge_to_id.find({task_id, window_id});
      if (id_it !=
          edge_to_id
              .end()) { // MODIFIED: avoid default-inserting missing candidates.
        same_task_nodes.emplace_back(id_it->second);
      }
    }

    for (int x = 0; x < size(same_task_nodes); ++x) {
      const int &u = same_task_nodes[x];
      for (int y = 0; y < x; ++y) {
        const int &v = same_task_nodes[y];
        adj[u].emplace_back(v);
        adj[v].emplace_back(u);
        edge_num++;
      }
    }
  }

  std::cout << "Task edges number: " << edge_num << std::endl;

  // Constraint 2: overlapping windows on the same satellite conflict.
  for (auto &[_, bucket] : satellite_buckets) {
    std::sort(begin(bucket), end(bucket), [&](const int &x, const int &y) {
      if (satellite_to_forbidding_time[x].second ==
          satellite_to_forbidding_time[y].second) {
        return satellite_to_forbidding_time[x].first <
               satellite_to_forbidding_time[y].first;
      }
      return satellite_to_forbidding_time[x].second <
             satellite_to_forbidding_time[y].second;
    });

    for (int i = 0; i < size(bucket); i++) {
      const auto &id = bucket[i];
      const int lbegin = std::partition_point(
                             begin(bucket), begin(bucket) + i,
                             [&](const int &x) {
                               return satellite_to_forbidding_time[x].second <=
                                      satellite_to_forbidding_time[id].first;
                             }) -
                         begin(bucket);
      for (int j = lbegin; j < i; j++) {
        adj[bucket[i]].emplace_back(bucket[j]);
        adj[bucket[j]].emplace_back(bucket[i]);
        edge_num++;
      }
    }
  }

  std::cout << "Task+Satellite edges number: " << edge_num << std::endl;

  // Constraint 3: overlapping windows on the same device conflict, including
  // switch time.
  for (auto &[_, bucket] : device_buckets) {
    std::sort(begin(bucket), end(bucket), [&](const int &x, const int &y) {
      if (device_to_forbidding_time[x].second ==
          device_to_forbidding_time[y].second) {
        return device_to_forbidding_time[x].first <
               device_to_forbidding_time[y].first;
      }
      return device_to_forbidding_time[x].second <
             device_to_forbidding_time[y].second;
    });

    for (int i = 0; i < size(bucket); i++) {
      const auto &id = bucket[i];
      const int lbegin =
          std::partition_point(begin(bucket), begin(bucket) + i,
                               [&](const int &x) {
                                 return device_to_forbidding_time[x].second <=
                                        device_to_forbidding_time[id].first;
                               }) -
          begin(bucket);
      for (int j = lbegin; j < i; j++) {
        adj[bucket[i]].emplace_back(bucket[j]);
        adj[bucket[j]].emplace_back(bucket[i]);
        edge_num++;
      }
    }
  }

  std::cout << "Task+Satellite+Device edges number: " << edge_num << std::endl;

  std::map<int, int> mp;
  for (int i = 0; i < size(adj); i++) {
    adj[i].emplace_back(i);
    std::sort(begin(adj[i]), end(adj[i]));
    adj[i].erase(std::unique(begin(adj[i]), end(adj[i])), end(adj[i]));
    int c = size(adj[i]);
    mp[c]++;
  }

  auto t = std::chrono::system_clock::now();
  std::chrono::duration<double> duration = t - s;
  std::cout << "Time :" << duration.count() << "s" << std::endl;
};
