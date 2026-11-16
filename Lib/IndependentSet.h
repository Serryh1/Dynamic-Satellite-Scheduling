#pragma once
#include <vector>
#include <set>
#include <chrono>
#include <optional>
#include "Task.h"
#include "AdjGraph.h"

namespace MIS {
std::vector<Tasks> independent_set_solver(const std::vector<TimeWindows> &AllTimewindows, const std::vector<Tasks> &AllTasks,
                   const std::unordered_set<int> &AccessibleSatellite, std::unordered_map<int, int> &DeviceTransferTime,  const int &TaskStartTime, const int &TaskEndTime, 
                   const std::string &str, std::set<int> &is_finish,
                   std::optional<AdjGraph> prepared_graph = std::nullopt,
                   std::optional<std::chrono::steady_clock::time_point> batch_start = std::nullopt,
                   const std::set<int> &carried_task_ids = {},
                   std::vector<int> *selected_window_ids = nullptr,
                   double graph_construction_seconds = 0.0,
                   double *hitting_time_seconds = nullptr);
};
