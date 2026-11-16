#pragma once

#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fstream>
#include <functional>
#include <string>
#include <set>
#include "Task.h"

struct Result {
    int task_id, forecast_id;
};
struct ResourceReservations {
    std::unordered_map<int, int> device_busy_until;
    std::unordered_map<int, int> satellite_busy_until;

    bool blocks(int satellite_id, int device_id, int start_time) const {
        auto device = device_busy_until.find(device_id);
        if (device != device_busy_until.end() && start_time < device->second) return true;
        auto satellite = satellite_busy_until.find(satellite_id);
        return satellite != satellite_busy_until.end() && start_time < satellite->second;
    }

    bool blocks(const TimeWindows &window) const {
        return blocks(window.SatelliteIndex, window.DeviceIndex, window.StartTime);
    }
};
template <>
struct std::hash<std::pair<int, int>> {
    size_t operator()(const std::pair<int, int>& o) const { return ((size_t)o.first << 32) + o.second; }
};


struct AdjGraph{
    int task_num = 0; // Number of tasks.
    int n = 0;
    std::unordered_map<std::pair<int, int>, int> edge_to_id; // Maps each task-window pair to its vertex ID.
    std::vector<std::pair<int, int>> id_to_edge; 

    std::vector<int> id_to_device;  // Device used by each vertex.
    std::vector<std::pair<int, int>> device_to_forbidding_time; // Ground-station device occupancy intervals.
    std::vector<std::pair<int,int>> satellite_to_forbidding_time; // Satellite occupancy intervals.
    std::unordered_map<int, std::vector<int>> device_buckets;  // 
    std::unordered_map<int,std::vector<int>> satellite_buckets;

    std::vector<std::vector<int>> adj;  // Conflict graph for the current scheduling horizon.
    std::vector<std::vector<int>> rev_adj; // Vertices deferred to a later scheduling horizon.

    AdjGraph() = default;
    AdjGraph(const std::vector<TimeWindows> &AllTimewindows, const std::vector<Tasks> &AllTasks,
             const std::unordered_set<int> &AccessibleSatellite,std::unordered_map<int, int> &DeviceTransferTime,const int &TaskStartTime,const int &TaskEndTime, 
             const std::set<int> &is_finish,
             const ResourceReservations &reservations = {},
             bool include_cross_boundary = false,
             const std::vector<int> &committed_window_ids = {});

    bool is_neighbor(const int& u, const int& v) const { return std::binary_search(begin(adj[u]), end(adj[u]), v); }

    void write_graph(std::string path = "./independent_set.graph") {
        std::ofstream out_file(path, std::ios::out);
        int m = 0;
        for (int u = 0; u < n; u++) {
            m += size(adj[u]) - 1;
        }
        m /= 2;
        out_file << n << ' ' << m << '\n';
        for (int u = 0; u < n; u++) {
            std::sort(begin(adj[u]), end(adj[u]));
            for (const auto& v : adj[u]) {
                if (v != u) {
                    out_file << v + 1 << ' ';
                }
            }
            out_file << '\n';
        }
    }
    
};
