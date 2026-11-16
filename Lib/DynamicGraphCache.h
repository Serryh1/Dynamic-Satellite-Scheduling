#pragma once

#include "AdjGraph.h"

#include <set>
#include <unordered_map>
#include <vector>

class DynamicGraphCache {
 public:
  DynamicGraphCache(std::vector<std::pair<int, int>> frame_bounds,
                    const std::unordered_map<int, int> &device_transfer_time);

  void add_new_windows(const std::vector<Tasks> &new_tasks,
                       const std::vector<TimeWindows> &all_windows,
                       size_t current_frame_index);
  AdjGraph active_graph(size_t frame_index, const std::set<int> &finished,
                        int task_num,
                        const ResourceReservations &reservations) const;
  void discard_frame(size_t frame_index);

 private:
  struct Candidate {
    int task_id;
    int window_id;
    int satellite_id;
    int device_id;
    int start;
    int end;
    int device_end;
  };

  std::vector<std::pair<int, int>> bounds_;
  std::unordered_map<int, int> transfer_time_;
  std::vector<std::vector<Candidate>> staged_;
};
