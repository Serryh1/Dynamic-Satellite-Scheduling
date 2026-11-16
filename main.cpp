
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <set>
#include <iostream>
#include <fstream>
#include <sstream>
#include <utility>
#include <memory>
#include <optional>

#include "Device.h"
#include "Task.h"
#include "IndependentSet.h"
#include "DynamicGraphCache.h"

using std::string;
using std::unordered_map;
using std::unordered_set;
using std::vector;
namespace fs = std::filesystem;

using namespace MIS;

static string WithTrailingSlash(string path) {
    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
        path += "/";
    }
    return path;
}

static bool EndsWith(const string& value, const string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static int ExtractBatchIndex(const string& fileName) {
    const string marker = "_batch_";
    size_t pos = fileName.find(marker);
    if (pos == string::npos) return -1;

    pos += marker.size();
    size_t end = pos;
    while (end < fileName.size() && std::isdigit(static_cast<unsigned char>(fileName[end]))) {
        ++end;
    }
    if (end == pos) return -1;

    return std::stoi(fileName.substr(pos, end - pos));
}

static string FindInfoFile(const string& inputFolder) {
    if (!fs::exists(inputFolder) || !fs::is_directory(inputFolder)) return "";

    vector<string> infoFiles;
    for (const auto& entry : fs::directory_iterator(inputFolder)) {
        if (!entry.is_regular_file()) continue;

        string name = entry.path().filename().string();
        if (EndsWith(name, "_info.csv")) {
            infoFiles.push_back(entry.path().string());
        }
    }

    if (!infoFiles.empty()) {
        std::sort(infoFiles.begin(), infoFiles.end());
        return infoFiles.front();
    }

    string oldInfoFile = inputFolder + "info.csv";
    if (fs::exists(oldInfoFile)) return oldInfoFile;

    return "";
}

static vector<string> FindBatchFiles(const string& inputFolder) {
    if (!fs::exists(inputFolder) || !fs::is_directory(inputFolder)) return {};

    vector<std::pair<int, string>> indexedFiles;
    for (const auto& entry : fs::directory_iterator(inputFolder)) {
        if (!entry.is_regular_file()) continue;

        string name = entry.path().filename().string();
        int batchIndex = ExtractBatchIndex(name);
        if (batchIndex >= 0 && EndsWith(name, ".csv")) {
            indexedFiles.push_back({batchIndex, entry.path().string()});
        }
    }

    std::sort(indexedFiles.begin(), indexedFiles.end(),
              [](const auto& lhs, const auto& rhs) {
                  if (lhs.first != rhs.first) return lhs.first < rhs.first;
                  return lhs.second < rhs.second;
              });

    vector<string> files;
    files.reserve(indexedFiles.size());
    for (const auto& item : indexedFiles) {
        files.push_back(item.second);
    }
    return files;
}

static void MergeTasks(vector<Tasks>& dst, const vector<Tasks>& src) {
    for (const auto& task : src) {
        if (task.TWs.empty()) continue;
        while ((int)dst.size() <= task.TaskId) {
            dst.push_back(Tasks{});
        }
        if (dst[task.TaskId].TWs.empty()) {
            dst[task.TaskId].TaskId = task.TaskId;
        }
        dst[task.TaskId].TWs.insert(dst[task.TaskId].TWs.end(),
                                    task.TWs.begin(), task.TWs.end());
    }
}

static std::set<int> ParseTargetFrames() {
    std::set<int> frames;
    const char* env = std::getenv("SATE_TARGET_FRAMES");
    if (env == nullptr) return frames;

    std::stringstream ss(env);
    string token;
    while (std::getline(ss, token, ',')) {
        token.erase(std::remove_if(token.begin(), token.end(), ::isspace), token.end());
        if (token.empty()) continue;

        size_t dash = token.find('-');
        if (dash == string::npos) {
            frames.insert(std::stoi(token));
            continue;
        }

        int start = std::stoi(token.substr(0, dash));
        int end = std::stoi(token.substr(dash + 1));
        if (start > end) std::swap(start, end);
        for (int frame = start; frame <= end; ++frame) {
            frames.insert(frame);
        }
    }
    return frames;
}

static bool ShouldRunFrame(const std::set<int>& target_frames, int frame) {
    return target_frames.empty() || target_frames.count(frame) > 0;
}

static vector<int> ReadResultIds(const string& output_file, const string& prefix) {
    vector<int> ids;
    std::ifstream file(output_file);
    if (!file.is_open()) return ids;

    string line;
    while (std::getline(file, line)) {
        size_t pos = line.find(prefix);
        if (pos == string::npos) continue;

        pos += prefix.size();
        size_t end = line.find('}', pos);
        string body = line.substr(pos, end == string::npos ? string::npos : end - pos);
        std::stringstream ss(body);
        string item;
        while (std::getline(ss, item, ',')) {
            item.erase(std::remove_if(item.begin(), item.end(), ::isspace), item.end());
            if (!item.empty()) {
                ids.push_back(std::stoi(item));
            }
        }
        break;
    }
    return ids;
}

static std::set<int> ReadFinishedTasksFromResult(const string& output_file) {
    const auto ids = ReadResultIds(output_file, "Solution set task_ids: {");
    return {ids.begin(), ids.end()};
}

static void ReserveSelectedWindows(ResourceReservations& reservations,
                                   const vector<int>& selected_window_ids,
                                   const vector<TimeWindows>& all_windows,
                                   const unordered_map<int, int>& device_transfer_time) {
    for (int window_id : selected_window_ids) {
        const auto& window = all_windows.at(window_id);
        const auto transfer = device_transfer_time.find(window.DeviceIndex);
        const int device_end = window.EndTime +
            (transfer == device_transfer_time.end() ? 0 : transfer->second);
        auto& device_busy = reservations.device_busy_until[window.DeviceIndex];
        device_busy = std::max(device_busy, device_end);
        auto& satellite_busy = reservations.satellite_busy_until[window.SatelliteIndex];
        satellite_busy = std::max(satellite_busy, window.EndTime);
    }
}

static vector<Tasks> CarryUnfinishedTasksForMain(
    const vector<TimeWindows>& all_timewindows,
    const vector<Tasks>& all_tasks,
    const int task_end_time,
    const std::set<int>& finished) {
    vector<Tasks> carried;
    for (const auto& task : all_tasks) {
        if (task.TWs.empty() || finished.count(task.TaskId)) continue;

        Tasks carried_task;
        carried_task.TaskId = task.TaskId;
        for (const int window_id : task.TWs) {
            if (all_timewindows[window_id].EndTime > task_end_time) {
                carried_task.TWs.push_back(window_id);
            }
        }

        if (!carried_task.TWs.empty()) {
            while ((int)carried.size() <= carried_task.TaskId) {
                carried.push_back(Tasks{});
            }
            carried[carried_task.TaskId] = carried_task;
        }
    }
    return carried;
}

static vector<Tasks> AdvanceWithExistingResult(
    const vector<TimeWindows>& all_timewindows,
    const vector<Tasks>& all_tasks,
    const int task_end_time,
    const string& output_file,
    std::set<int>& finished) {
    std::set<int> selected = ReadFinishedTasksFromResult(output_file);
    finished.insert(selected.begin(), selected.end());
    return CarryUnfinishedTasksForMain(
        all_timewindows, all_tasks, task_end_time, finished);
}

int main(int argc, char* argv[]) {
    unordered_map<int, int> DeviceTransferTime;
    unordered_set<int> AccessibleSatellite;
    std::vector<TimeWindows> AllTimewindows;
    std::vector<Tasks> DGLSTasks;
    int TaskEndTime;
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <input_folder> [output_folder] [dgls]\n";
        return 1;
    }

    string input_folder = WithTrailingSlash(argv[1]);
    string output_folder = (argc >= 3) ? WithTrailingSlash(argv[2]) : input_folder;
    if (argc >= 4 && string(argv[3]) != "dgls") {
        std::cerr << "Error: only dgls is available in this package\n";
        return 1;
    }
    string fileName1 = FindInfoFile(input_folder);
    if (fileName1.empty()) {
        std::cerr << "Error: cannot find info file in " << input_folder
                  << " (expected *_info.csv or info.csv)\n";
        return 1;
    }

    vector<string> task_files = FindBatchFiles(input_folder);
    if (task_files.empty()) {
        task_files = {
            input_folder + "1.csv",
            input_folder + "2.csv"
        };
    }
    
    int t = 0;
    int TaskStartTime = 0;
    std::set<int> dgls_finished;
    std::set<int> target_frames = ParseTargetFrames();
    const char* stop_after_env = std::getenv("SATE_STOP_AFTER_FRAME");
    const int stop_after_frame = stop_after_env ? std::stoi(stop_after_env) : 0;
    DeviceProcess(DeviceTransferTime, fileName1, AccessibleSatellite);
    std::unique_ptr<DynamicGraphCache> graph_cache;
    const char* graph_mode = std::getenv("SATE_GRAPH_MODE");
    const bool report_graph_metrics = std::getenv("SATE_REPORT_GRAPH_METRICS") != nullptr;
    const bool rebuild_graph = graph_mode && string(graph_mode) == "scratch";
    ResourceReservations committed_reservations;
    vector<int> committed_window_ids;
    if (graph_mode && string(graph_mode) == "update") {
        std::vector<std::pair<int, int>> bounds;
        int start = 0;
        for (const string& path : task_files) {
            std::ifstream input(path);
            string line;
            if (!std::getline(input, line)) {
                std::cerr << "Cannot read frame boundary from " << path << '\n';
                return 1;
            }
            int end = std::stoi(line);
            if (end <= start) {
                std::cerr << "Invalid frame boundary in " << path << '\n';
                return 1;
            }
            bounds.emplace_back(start, end);
            start = end;
        }
        graph_cache = std::make_unique<DynamicGraphCache>(bounds, DeviceTransferTime);
    } else if (graph_mode && string(graph_mode) != "scratch") {
        std::cerr << "Unknown SATE_GRAPH_MODE: " << graph_mode << '\n';
        return 1;
    }
    for(size_t i = 0; i < task_files.size(); ++ i){
        string frame_id = std::to_string(i + 1);
        const bool run_this_frame = ShouldRunFrame(target_frames, (int)i + 1);
        auto batch_start = std::chrono::steady_clock::now();
        auto mark_graph_start = [&]() {
            batch_start = std::chrono::steady_clock::now();
            if (run_this_frame && std::getenv("SATE_EMIT_FRAME_MARKERS")) {
                std::cerr << "SATE_FRAME_START " << frame_id << std::endl;
            }
        };
        if (!graph_cache && !rebuild_graph) mark_graph_start();
        double graph_seconds = 0.0;
        double graph_ingest_seconds = 0.0;
        double graph_snapshot_seconds = 0.0;
        double graph_cleanup_seconds = 0.0;
        bool graph_discarded = false;
        vector<Tasks> NewTasks;
        TimeWindowsProcess(AllTimewindows, task_files[i], NewTasks, TaskEndTime, t);
        if (graph_cache) {
            if (run_this_frame) mark_graph_start();
            const auto graph_start = std::chrono::steady_clock::now();
            graph_cache->add_new_windows(NewTasks, AllTimewindows, i);
            graph_ingest_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - graph_start).count();
            graph_seconds += graph_ingest_seconds;
        }

        {
            std::set<int> carried_task_ids;
            for (const auto& task : DGLSTasks) {
                if (!task.TWs.empty()) carried_task_ids.insert(task.TaskId);
            }
            MergeTasks(DGLSTasks, NewTasks);
            fs::create_directories(output_folder + "dgls");
            string dgls_output = output_folder + "dgls/" + frame_id + "_MIS_result.txt";
            if (run_this_frame) {
                const size_t finished_before = dgls_finished.size();
                std::optional<AdjGraph> prepared_graph;
                if (graph_cache) {
                    const auto graph_start = std::chrono::steady_clock::now();
                    prepared_graph.emplace(graph_cache->active_graph(
                        i, dgls_finished, static_cast<int>(DGLSTasks.size()),
                        committed_reservations));
                    graph_snapshot_seconds = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - graph_start).count();
                    graph_seconds += graph_snapshot_seconds;
                    if (std::getenv("SATE_VERIFY_GRAPH_UPDATE")) {
                        AdjGraph fresh(AllTimewindows, DGLSTasks, AccessibleSatellite,
                                       DeviceTransferTime, TaskStartTime, TaskEndTime,
                                       dgls_finished, {}, true,
                                       committed_window_ids);
                        if (prepared_graph->id_to_edge != fresh.id_to_edge ||
                            prepared_graph->adj != fresh.adj) {
                            std::cerr << "Graph mismatch at frame " << frame_id << '\n';
                            return 2;
                        }
                    }
                } else if (rebuild_graph) {
                    mark_graph_start();
                    const auto graph_start = std::chrono::steady_clock::now();
                    prepared_graph.emplace(AllTimewindows, DGLSTasks,
                                           AccessibleSatellite, DeviceTransferTime,
                                           TaskStartTime, TaskEndTime, dgls_finished,
                                           ResourceReservations{}, true,
                                           committed_window_ids);
                    graph_seconds = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - graph_start).count();
                }
                std::vector<int> selected_window_ids;
                double hitting_time_seconds = 0.0;
                DGLSTasks = MIS::independent_set_solver(
                    AllTimewindows, DGLSTasks, AccessibleSatellite,
                    DeviceTransferTime, TaskStartTime, TaskEndTime,
                    dgls_output, dgls_finished, std::move(prepared_graph),
                    batch_start, carried_task_ids,
                    (rebuild_graph || graph_cache) ? &selected_window_ids : nullptr,
                    graph_seconds, &hitting_time_seconds);
                if (graph_cache) {
                    ReserveSelectedWindows(committed_reservations, selected_window_ids,
                                           AllTimewindows, DeviceTransferTime);
                }
                if (rebuild_graph || graph_cache) {
                    committed_window_ids.insert(committed_window_ids.end(),
                                                selected_window_ids.begin(),
                                                selected_window_ids.end());
                }
                if (graph_cache) {
                    const auto graph_start = std::chrono::steady_clock::now();
                    graph_cache->discard_frame(i);
                    graph_cleanup_seconds = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - graph_start).count();
                    graph_discarded = true;
                }
                if (report_graph_metrics) {
                    const double total_seconds = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - batch_start).count();
                    std::ofstream metrics(dgls_output, std::ios::app);
                    metrics << "Graph update seconds: " << graph_seconds << '\n'
                            << "Graph construction seconds: " << graph_seconds << '\n'
                            << "Graph cleanup seconds: " << graph_cleanup_seconds << '\n'
                            << "Time budget includes graph: "
                            << !(std::getenv("SATE_HEURISTIC_ONLY_LIMIT") &&
                                 string(std::getenv("SATE_HEURISTIC_ONLY_LIMIT")) == "1") << '\n'
                            << "Total solve time seconds: " << hitting_time_seconds << '\n'
                            << "Frame wall time seconds: " << total_seconds << '\n'
                            << "Solution size: " << dgls_finished.size() - finished_before << '\n'
                            << "Cumulative solution size: " << dgls_finished.size() << '\n';
                    if (graph_cache) {
                        metrics << "Graph ingestion seconds: " << graph_ingest_seconds << '\n'
                                << "Graph snapshot seconds: " << graph_snapshot_seconds << '\n';
                    }
                    metrics.close();
                }
            } else {
                if (rebuild_graph || graph_cache) {
                    const auto selected_windows = ReadResultIds(dgls_output, "Solution set window_ids: {");
                    const auto selected_tasks = ReadFinishedTasksFromResult(dgls_output);
                    if (!selected_tasks.empty() && selected_windows.empty()) {
                        std::cerr << "Cannot restore occupied windows from " << dgls_output << '\n';
                        return 2;
                    }
                    if (graph_cache) {
                        ReserveSelectedWindows(committed_reservations, selected_windows,
                                               AllTimewindows, DeviceTransferTime);
                    }
                    committed_window_ids.insert(committed_window_ids.end(),
                                                selected_windows.begin(),
                                                selected_windows.end());
                }
                DGLSTasks = AdvanceWithExistingResult(AllTimewindows, DGLSTasks, TaskEndTime, dgls_output, dgls_finished);
            }
        }

        TaskStartTime = TaskEndTime;
        if (graph_cache && !graph_discarded) graph_cache->discard_frame(i);
        if (stop_after_frame > 0 && static_cast<int>(i + 1) >= stop_after_frame) {
            break;
        }
        if (run_this_frame && std::getenv("SATE_STOP_AFTER_TARGET_FRAME")) {
            break;
        }
    }

    
    return 0;
}
