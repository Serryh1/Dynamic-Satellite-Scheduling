#include "IndependentSet.h"
#include "AdjGraph.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <optional>

#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <thread>
#include <tr2/dynamic_bitset>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;
using tr2::dynamic_bitset;
namespace MIS {

std::set<int> se;

std::string output_file;

std::chrono::duration<double> dgls_time_limit_seconds() {
  const char *env = std::getenv("SATE_SOLVER_TIME_LIMIT_SECONDS");
  if (env) {
    char *end = nullptr;
    const double value = std::strtod(env, &end);
    if (end != env && value > 0.0) {
      return std::chrono::duration<double>(value);
    }
  }
  return std::chrono::duration<double>(900.0);
}

bool disable_greedy_reconstruction() {
  const char *env = std::getenv("SATE_DISABLE_GREEDY_RECONSTRUCTION");
  return env && std::string(env) == "1";
}
struct RandSet {
  int n;
  vector<int> to;
  vector<int> set;

  RandSet(int _n) : n(_n), to(_n) { fill(begin(to), end(to), -1); }

  void clear() {
    fill(begin(to), end(to), -1);
    set.clear();
  }
  bool empty() const { return set.empty(); }
  size_t size() const { return set.size(); }
  int &operator[](const size_t &x) { return set[x]; }
  int &get(int i) { return set[i]; }
  bool is_in(int i) const { return to[i] != -1; }

  void insert(int id) {
    assert(to[id] == -1);
    to[id] = set.size();
    set.emplace_back(id);
  }
  void erase(int id) {
    assert(to[id] != -1);
    int p = to[id];
    int moved =
        set.back(); // MODIFIED: remember the moved value before pop_back().
    swap(set[p], set.back());
    set.pop_back();
    if (p < (int)set.size()) { // MODIFIED: deleting the last element must not
                               // read set[p].
      to[moved] = p;
    }
    to[id] = -1;
  }
};

struct Solve {
  const AdjGraph &g;
  const int n;
  dynamic_bitset<> s, tabu;
  // covers counts selected neighbors of each vertex.
  // cnt1 counts selected neighbors whose cover count is one, i.e. vertices
  // released when u is removed.
  vector<int> covers, cnt1;
  mt19937_64 r;

  // fit_set contains unselected vertices with covers[u] == 0.
  // swap_set contains selected vertices with cnt1[u] >= 2.
  // swap1_set contains selected vertices with cnt1[u] >= 3.
  RandSet fit_set, swap_set;
  vector<unsigned int> greedy_mark;
  unsigned int greedy_epoch = 0;
  vector<pair<int, int>> greedy_order;

  Solve(const AdjGraph &_g, size_t rand_seed = 0)
      : g(_g), n(_g.n), s(_g.n), tabu(_g.n), covers(_g.n), cnt1(_g.n),
        r(rand_seed), fit_set(_g.n), swap_set(_g.n) {
    for (int u = 0; u < n; u++) {
      fit_set.insert(u);
    }
  }

  void inc_cnt1(const int &u) {
    cnt1[u]++;
    if (!tabu.test(u) && s.test(u) && cnt1[u] == 2) {
      swap_set.insert(u);
    }
  }

  void dec_cnt1(const int &u) {
    cnt1[u]--;
    if (!tabu.test(u) && s.test(u) && cnt1[u] == 1) {
      swap_set.erase(u);
    }
  }

  void inc_covers(const int &u) {
    covers[u]++;
    if (!tabu.test(u)) {
      if (covers[u] == 1) {
        if (!s.test(u)) {
          fit_set.erase(u);
        }
        for (const auto &v : g.adj[u]) {
          inc_cnt1(v);
        }
      }
      if (covers[u] == 2) {
        for (const auto &v : g.adj[u]) {
          dec_cnt1(v);
        }
      }
    }
  }

  void dec_covers(const int &u) {
    covers[u]--;
    if (!tabu.test(u)) {
      if (covers[u] == 1) {
        for (const auto &v : g.adj[u]) {
          inc_cnt1(v);
        }
      }
      if (covers[u] == 0) {
        if (!s.test(u)) {
          fit_set.insert(u);
        }
        for (const auto &v : g.adj[u]) {
          dec_cnt1(v);
        }
      }
    }
  }

  void add(const int &u) {
    assert(!tabu.test(u) && !s.test(u));
    s.set(u);
    // Adding a vertex must preserve the independent-set property.
    fit_set.erase(u);
    if (cnt1[u] >= 2) {
      swap_set.insert(u);
    }
    for (const auto &v : g.adj[u]) {
      inc_covers(v);
    }
  }

  void del(const int &u) {
    assert(!tabu.test(u) && s.test(u));
    s.reset(u);
    if (covers[u] == 0) {
      fit_set.insert(u);
    }
    if (swap_set.is_in(u)) {
      swap_set.erase(u);
    }
    for (const auto &v : g.adj[u]) {
      dec_covers(v);
    }
  }

  // Tabu prevents additions from fit_set and swaps involving swap1_set.
  void add_tabu(const int &u) {
    assert(!tabu.test(u));
    tabu.set(u);
    if (s.test(u)) {
      if (cnt1[u] >= 2) {
        swap_set.erase(u);
      }
    } else {
      if (covers[u] == 0) {
        fit_set.erase(u);
      }
    }
    if (covers[u] == 1) {
      for (const auto &v : g.adj[u]) {
        dec_cnt1(v);
      }
    }
  }

  void del_tabu(const int &u) {
    assert(tabu.test(u));
    tabu.reset(u);
    if (s.test(u)) {
      if (cnt1[u] >= 2) {
        swap_set.insert(u);
      }
    } else {
      if (covers[u] == 0) {
        fit_set.insert(u);
      }
    }
    if (covers[u] == 1) {
      for (const auto &v : g.adj[u]) {
        inc_cnt1(v);
      }
    }
  }

  int get_add() {
    int c = r() % size(fit_set);
    return fit_set[c];
  }

  // Is covers[u] always one for a selected removal candidate?
  int get_del() {
    int c = r() % size(swap_set);
    return swap_set[c];
  }
};
int get_TaskID(const Solve &solve, const int &node) {
  const auto &[task, forecast] = solve.g.id_to_edge[node];
  return task;
}
void random_add(Solve &solve) {
  while (!solve.fit_set.empty()) {
    int c = solve.get_add();
    solve.add(c);
    se.insert(get_TaskID(solve, c));
  }
}

void random_add_1(Solve &solve, vector<int> &added) {
  while (!solve.fit_set.empty()) {
    int c = solve.get_add();
    solve.add(c);
    added.push_back(c);
  }
}

vector<int> random_add_1(Solve &solve) {
  vector<int> r;
  random_add_1(solve, r);
  return r;
}

void greedy_reconstruct(Solve &solve, const int &u, vector<int> &selected) {
  if (solve.greedy_mark.empty()) {
    solve.greedy_mark.resize(solve.n, 0);
  }
  ++solve.greedy_epoch;
  if (solve.greedy_epoch == 0) {
    fill(solve.greedy_mark.begin(), solve.greedy_mark.end(), 0);
    solve.greedy_epoch = 1;
  }
  auto &order = solve.greedy_order;
  order.clear();
  for (const auto &v : solve.g.adj[u]) {
    if (v != u && solve.fit_set.is_in(v) &&
        solve.greedy_mark[v] != solve.greedy_epoch) {
      solve.greedy_mark[v] = solve.greedy_epoch;
      order.emplace_back(0, v);
    }
  }
  for (auto &[degree, v] : order) {
    for (const int neighbor : solve.g.adj[v]) {
      if (neighbor != v && solve.greedy_mark[neighbor] == solve.greedy_epoch) {
        ++degree;
      }
    }
  }
  sort(order.begin(), order.end());

  for (const auto &[degree, v] : order) {
    if (solve.fit_set.is_in(v)) {
      solve.add(v);
      selected.push_back(v);
    }
  }
}

std::vector<int> Greedy_search(Solve &solve, const int &u) {
  static vector<int> vec;
  vec.clear();
  vec.insert(end(vec), begin(solve.swap_set.set), end(solve.swap_set.set));
  shuffle(begin(vec), end(vec), solve.r);
  std::vector<int> temp_freq;
  for (const auto &u : vec) {
    static vector<int> temp;
    if (solve.cnt1[u] > 2 && solve.swap_set.is_in(u)) {
      temp.clear();
      for (const auto &v : solve.g.adj[u]) {
        if (v != u && solve.covers[v] == 1) {
          temp.emplace_back(v);
        }
      }
      shuffle(begin(temp), end(temp), solve.r);
      int n = size(temp);
      bool suc = false;
      for (int i = 0; i < n && !suc; i++) {
        for (int j = i + 1; j < n; j++) {
          if (!solve.g.is_neighbor(temp[i], temp[j])) {
            suc = true;
            solve.del(u);
            int node_info = get_TaskID(solve, u);
            if (se.count(node_info)) {
              se.erase(node_info);
            }
            solve.add(temp[i]);
            int x = get_TaskID(solve, temp[i]);
            se.insert(x);

            solve.add(temp[j]);
            x = get_TaskID(solve, temp[j]);
            se.insert(x);

            temp_freq.push_back(temp[i]);
            temp_freq.push_back(temp[j]);
            temp_freq.push_back(u);
            break;
          }
        }
      }
    }
    auto it = random_add_1(solve);
    for (const auto &ii : it) {
      temp_freq.push_back(ii);
    }
  }
  return temp_freq;
}

void random_add_onetimeframe(Solve &solve) {
  while (!solve.fit_set.empty()) {
    int c = solve.get_add();
    solve.add(c);
  }
}

void swap_1_2(Solve &solve) {
  static vector<int> vec;
  vec.clear();
  vec.insert(end(vec), begin(solve.swap_set.set), end(solve.swap_set.set));
  shuffle(begin(vec), end(vec), solve.r);
  for (const auto &u : vec) {
    static vector<int> temp;
    if (solve.cnt1[u] > 2 && solve.swap_set.is_in(u)) {
      temp.clear();
      for (const auto &v : solve.g.adj[u]) {
        if (v != u && solve.covers[v] == 1) {
          temp.emplace_back(v);
        }
      }
      shuffle(begin(temp), end(temp), solve.r);
      int n = size(temp);
      bool suc = false;
      for (int i = 0; i < n && !suc; i++) {
        for (int j = i + 1; j < n; j++) {
          if (!solve.g.is_neighbor(temp[i], temp[j])) {
            suc = true;
            solve.del(u);
            solve.add(temp[i]);
            solve.add(temp[j]);
            break;
          }
        }
      }
    }
    random_add_onetimeframe(solve);
  }
}

template <typename T, typename R>
void local_search_1(Solve &solve, const chrono::duration<T, R> &max_time,
                    const string &output_file) {
  random_add(solve);
  size_t iter = 0;
  size_t cnt = 0;
  size_t last_ans = solve.s.count();
  dynamic_bitset<> best_s =
      solve.s; // MODIFIED: keep the actual best solution, not only its size.
  auto s = chrono::system_clock::now();
  auto t = chrono::system_clock::now();

  std::ofstream File;
  File.open(output_file, std::ios::out | std::ios::trunc);

  File << last_ans << endl;
  while (t - s < max_time) {
    iter++;
    cnt++;
    if (solve.s.count() > last_ans) {
      last_ans = solve.s.count();
      t = chrono::system_clock::now();
      std::chrono::duration<double> duration = t - s;
      File << "time: " << duration.count() << "s ";
      File << "solve_s: " << last_ans << endl;
      if (last_ans == solve.g.task_num) {
        File << last_ans << " is the best solution." << endl;
        File << "early stop." << endl;
        return;
      }
    }

    if (solve.swap_set.empty()) {
      File << last_ans << " is the best solution." << endl;
      File << "early stop." << endl;
      return;
    }
    int c = solve.get_del();
    solve.del(c);

    int x = get_TaskID(solve, c);
    if (se.count(x))
      se.erase(x);

    solve.add_tabu(c);
    random_add(solve);
    // random_add_onetimeframe(solve);
    t = chrono::system_clock::now();
    solve.del_tabu(c);
    // 1-2 swap

    if (cnt >= solve.swap_set.size()) {
      cnt = 0;
      swap_1_2(solve);
    }
  }
}

double local_search_2(Solve &solve, const chrono::steady_clock::time_point frame_start,
                    const chrono::duration<double> max_time,
                    const string &output_file,
                    const std::set<int> &carried_task_ids = {},
                    const std::vector<TimeWindows> *all_windows = nullptr,
                    double graph_construction_seconds = 0.0) {
  const char *solver_only_env = std::getenv("SATE_HEURISTIC_ONLY_LIMIT");
  const bool solver_only = solver_only_env && std::string(solver_only_env) == "1";
  const auto heuristic_start = chrono::steady_clock::now();
  const auto search_start = solver_only ? heuristic_start : frame_start;
  if (solver_only && std::getenv("SATE_EMIT_HEURISTIC_MARKERS")) {
    std::cerr << "SATE_HEURISTIC_START" << std::endl;
  }
  size_t warm_seed_size = 0;
  const char *warm_env = std::getenv("SATE_WARM_START");
  if (warm_env && std::string(warm_env) == "1") {
    vector<int> candidates;
    for (int node = 0; node < solve.n; ++node) {
      if (carried_task_ids.count(solve.g.id_to_edge[node].first)) {
        candidates.push_back(node);
      }
    }
    sort(candidates.begin(), candidates.end(), [&](int a, int b) {
      if (solve.g.adj[a].size() != solve.g.adj[b].size()) {
        return solve.g.adj[a].size() < solve.g.adj[b].size();
      }
      return a < b;
    });
    for (int node : candidates) {
      if (solve.fit_set.is_in(node)) {
        solve.add(node);
        ++warm_seed_size;
      }
    }
  }
  random_add_onetimeframe(solve);
  size_t cnt = 0;
  size_t last_ans = solve.s.count();
  dynamic_bitset<> best_s = solve.s;
  double hitting_seconds = chrono::duration<double>(
      chrono::steady_clock::now() - search_start).count();
  const bool checkpoint_enabled = all_windows &&
      std::getenv("SATE_REPORT_GRAPH_METRICS") && !solver_only;
  const string checkpoint_path = output_file + ".incumbent";
  auto save_incumbent = [&]() {
    if (!checkpoint_enabled || hitting_seconds > max_time.count()) return;
    const string temporary_path = checkpoint_path + ".tmp";
    std::ofstream checkpoint(temporary_path, std::ios::trunc);
    std::set<int> task_ids;
    std::vector<int> node_ids, window_ids;
    checkpoint << best_s.count() << '\n'
               << "Runtime seconds: " << hitting_seconds << '\n'
               << "Graph construction seconds: " << graph_construction_seconds << '\n'
               << "Heuristic start offset seconds: "
               << chrono::duration<double>(heuristic_start - frame_start).count() << '\n'
               << "Selected tasks in current frame:\n"
               << "node_id,task_id,window_id,satellite_id,device_id,start_time,end_time\n";
    for (int node = best_s.find_first(); node != best_s.size();
         node = best_s.find_next(node)) {
      const auto [task_id, window_id] = solve.g.id_to_edge[node];
      const auto &window = all_windows->at(window_id);
      task_ids.insert(task_id);
      node_ids.push_back(node);
      window_ids.push_back(window_id);
      checkpoint << node << ',' << task_id << ',' << window_id << ','
                 << window.SatelliteIndex << ',' << window.DeviceIndex << ','
                 << window.StartTime << ',' << window.EndTime << '\n';
    }
    checkpoint << "Solution set node_ids: {";
    for (size_t i = 0; i < node_ids.size(); ++i) {
      if (i) checkpoint << ',';
      checkpoint << node_ids[i];
    }
    checkpoint << "}\nSolution set task_ids: {";
    size_t index = 0;
    for (int task_id : task_ids) {
      if (index++) checkpoint << ',';
      checkpoint << task_id;
    }
    checkpoint << "}\nSolution set window_ids: {";
    for (size_t i = 0; i < window_ids.size(); ++i) {
      if (i) checkpoint << ',';
      checkpoint << window_ids[i];
    }
    checkpoint << "}\n";
    checkpoint.close();
    if (checkpoint) std::rename(temporary_path.c_str(), checkpoint_path.c_str());
  };
  save_incumbent();
  const auto deadline = search_start + max_time;
  const int maxIter = std::min(10'000, solve.n / 2);
  vector<pair<double, size_t>> improvements;
  const bool no_greedy_reconstruction = disable_greedy_reconstruction();
  size_t one_for_one_swaps = 0;
  size_t direct_additions = 0;

  int last_result = solve.s.count();
  std::unordered_map<int, int> freq;
  std::vector<int> add_fre;
  while (chrono::steady_clock::now() < deadline) {
    cnt = 0;
    while (cnt < maxIter &&
           (!solve.swap_set.empty() ||
            (no_greedy_reconstruction && !solve.fit_set.empty()))) {
      if ((cnt & 255) == 0 && chrono::steady_clock::now() >= deadline) {
        break;
      }
      if (solve.s.count() > last_ans) {
        last_ans = solve.s.count();
        best_s = solve.s;
        hitting_seconds = chrono::duration<double>(
            chrono::steady_clock::now() - search_start).count();
        improvements.emplace_back(hitting_seconds, last_ans);
        save_incumbent();
        if (last_ans == solve.g.task_num) {
          break;
        }
      }

      if (no_greedy_reconstruction && !solve.fit_set.empty()) {
        const int v = solve.get_add();
        solve.add(v);
        ++direct_additions;
        freq[v]++;
        ++cnt;
        continue;
      }
      if (solve.swap_set.empty()) {
        break;
      }
      int c = solve.get_del();
      solve.del(c);

      freq[c]++;
      if (freq[c] > last_result) {
        freq[c] = 0;
      }
      // solve.add_tabu(c);
      add_fre.clear();
      size_t reconstruction_size = 0;
      if (no_greedy_reconstruction) {
        int replacement = -1;
        size_t choices = 0;
        for (const int v : solve.g.adj[c]) {
          if (v != c && solve.fit_set.is_in(v) &&
              solve.r() % ++choices == 0) {
            replacement = v;
          }
        }
        if (replacement == -1) {
          solve.add(c);
        } else {
          solve.add(replacement);
          add_fre.push_back(replacement);
          ++one_for_one_swaps;
        }
      } else {
        greedy_reconstruct(solve, c, add_fre);
        reconstruction_size = add_fre.size();
        random_add_1(solve, add_fre);
      }

      for (const auto &i : add_fre) {
        freq[i]++;
        if (freq[i] > last_result) {
          freq[i] = 0;
        }
      }
      // solve.del_tabu(c);
      cnt = reconstruction_size > 1 ? 0 : cnt + 1;
    }
    if (chrono::steady_clock::now() >= deadline) break;
    if (last_ans == solve.g.task_num) {
      std::this_thread::sleep_until(deadline);
      break;
    }
    if (solve.s.count() > last_result) {
      last_result = solve.s.count();
      if (solve.s.count() > last_ans) { // MODIFIED: do not overwrite a better
                                        // historical solution.
        last_ans = solve.s.count();
        best_s = solve.s;
        hitting_seconds = chrono::duration<double>(
            chrono::steady_clock::now() - search_start).count();
        improvements.emplace_back(hitting_seconds, last_ans);
        save_incumbent();
      }
      continue;
    }
    int sum = 0;
    vector<int> candidate, random_del;
    for (const auto &[x, y] : freq) {
      if (solve.s.test(x)) {
        sum += y;
        candidate.push_back(x);
      }
    }
    if (candidate.empty()) {
      for (int node = solve.s.find_first(); node != solve.s.size();
           node = solve.s.find_next(node)) {
        candidate.push_back(node);
      }
    }
    const int freq_avg = last_result > 0 ? sum / last_result + 1 : 1;

    for (const auto &i : candidate) {
      if (freq[i] < freq_avg) {
        random_del.push_back(i);
      }
    }
    if (random_del.empty()) random_del = candidate;
    if (random_del.empty()) {
      if (!solve.fit_set.empty()) random_add_onetimeframe(solve);
      else std::this_thread::sleep_until(deadline);
      continue;
    }

    int siz = random_del.size();
    int delete_num = ceil(siz * 0.01 + 1);

    shuffle(random_del.begin(), random_del.end(), solve.r);

    for (int i = 0; i < min(delete_num, siz); ++i) {
      solve.del(random_del[i]);
    }
  }
  if (solve.s.count() > last_ans) {
    last_ans = solve.s.count();
    best_s = solve.s;
    hitting_seconds = chrono::duration<double>(
        chrono::steady_clock::now() - search_start).count();
    improvements.emplace_back(hitting_seconds, last_ans);
    save_incumbent();
  }
  solve.s = best_s;
  std::ofstream File(output_file, std::ios::out | std::ios::trunc);
  File << last_ans << '\n';
  File << "Warm-start seed size: " << warm_seed_size << '\n';
  if (no_greedy_reconstruction) {
    File << "Neighborhood: 1-1 swap\n"
         << "One-for-one swaps: " << one_for_one_swaps << '\n'
         << "Direct additions: " << direct_additions << '\n';
  }
  for (const auto &[seconds, size] : improvements) {
    File << "time: " << seconds << "s solve_s: " << size << '\n';
  }
  File << "Runtime seconds: " << std::min(hitting_seconds, max_time.count()) << '\n';
  File << "Heuristic solve time seconds: " << chrono::duration<double>(
      chrono::steady_clock::now() - heuristic_start).count() << '\n';
  File << "Wall time seconds: " << chrono::duration<double>(
      chrono::steady_clock::now() - frame_start).count() << '\n';
  return std::min(hitting_seconds, max_time.count());
}

struct CSolve {
  AdjGraph g_pre;
  Solve solve_pre;
  CSolve(const std::vector<TimeWindows> &AllTimewindows,
         const std::vector<Tasks> &AllTasks,
         const std::unordered_set<int> &AccessibleSatellite,
         std::unordered_map<int, int> &DeviceTransferTime,
         const int &TaskStartTime, const int &TaskEndTime,
         std::set<int> &is_finish, const string &output_file,
         std::optional<AdjGraph> prepared_graph = std::nullopt)
      : g_pre(prepared_graph
                  ? std::move(*prepared_graph)
                  : AdjGraph(AllTimewindows, AllTasks, AccessibleSatellite,
                             DeviceTransferTime, TaskStartTime, TaskEndTime,
                             is_finish)),
        solve_pre(g_pre, 3407) {}
};

std::vector<Tasks>
independent_set_solver(const std::vector<TimeWindows> &AllTimewindows,
                       const std::vector<Tasks> &AllTasks,
                       const std::unordered_set<int> &AccessibleSatellite,
                       std::unordered_map<int, int> &DeviceTransferTime,
                       const int &TaskStartTime, const int &TaskEndTime,
                       const std::string &output, std::set<int> &is_finish,
                       std::optional<AdjGraph> prepared_graph,
                       std::optional<chrono::steady_clock::time_point> batch_start,
                       const std::set<int> &carried_task_ids,
                       std::vector<int> *selected_window_ids,
                       double graph_construction_seconds,
                       double *hitting_time_seconds) {
  const auto frame_start = batch_start.value_or(chrono::steady_clock::now());
  { // MODIFIED: authoritative multi-frame solver path.
    std::set<int> previous_finished = is_finish;
    se.clear();
    for (const auto &task_id : previous_finished) {
      se.insert(task_id);
    }

    CSolve csolve(AllTimewindows, AllTasks, AccessibleSatellite,
                  DeviceTransferTime, TaskStartTime, TaskEndTime, is_finish,
                  output, std::move(prepared_graph));

    const double hitting_seconds = local_search_2(
        csolve.solve_pre, frame_start, dgls_time_limit_seconds(),
        output, carried_task_ids, &AllTimewindows, graph_construction_seconds);
    if (hitting_time_seconds) *hitting_time_seconds = hitting_seconds;

    std::vector<Tasks> Res;
    std::set<int> selected_this_frame;
    std::vector<int> selected_nodes;
    // MODIFIED: solution set in graph-node space.
    std::vector<int> selected_windows;
    // MODIFIED: solution set in time-window space.

    std::ofstream File;
    File.open(output, std::ios::app);
    File << "Selected tasks in current frame:" << endl;
    File << "node_id,task_id,window_id,satellite_id,device_id,start_time,end_"
            "time"
         << endl; // MODIFIED

    const auto &selected = csolve.solve_pre.s;
    for (int node = selected.find_first(); node != selected.size();
         node = selected.find_next(node)) {
      const auto &[task_id, window_id] = csolve.solve_pre.g.id_to_edge[node];
      const TimeWindows &w = AllTimewindows[window_id];
      selected_this_frame.insert(task_id);
      selected_nodes.push_back(node);        // MODIFIED
      selected_windows.push_back(window_id); // MODIFIED
      File << node << "," << task_id << "," << window_id << ","
           << w.SatelliteIndex << "," << w.DeviceIndex << "," << w.StartTime
           << "," << w.EndTime << endl;
    }

    if (selected_window_ids) *selected_window_ids = selected_windows;

    // MODIFIED: explicit solution sets for this round/frame.
    File << "Solution set node_ids: {";
    for (size_t idx = 0; idx < selected_nodes.size(); ++idx) {
      if (idx)
        File << ",";
      File << selected_nodes[idx];
    }
    File << "}" << endl;

    File << "Solution set task_ids: {";
    size_t task_pos = 0;
    for (const auto &task_id : selected_this_frame) {
      if (task_pos++)
        File << ",";
      File << task_id;
    }
    File << "}" << endl;

    File << "Solution set window_ids: {";
    for (size_t idx = 0; idx < selected_windows.size(); ++idx) {
      if (idx)
        File << ",";
      File << selected_windows[idx];
    }
    File << "}" << endl;

    // MODIFIED: one task can be finished only once across frames.
    is_finish = previous_finished;
    for (const auto &task_id : selected_this_frame) {
      is_finish.insert(task_id);
    }

    // MODIFIED: rebuild the global tracker from the authoritative finished set.
    se.clear();
    for (const auto &task_id : is_finish) {
      se.insert(task_id);
    }

    // MODIFIED: carry unfinished tasks that still have future candidate
    // windows.
    for (const auto &task : AllTasks) {
      if (task.TWs.empty())
        continue;
      if (is_finish.count(task.TaskId))
        continue;

      Tasks carry_task;
      carry_task.TaskId = task.TaskId;
      for (const auto &window_id : task.TWs) {
        if (AllTimewindows[window_id].EndTime > TaskEndTime) {
          carry_task.TWs.push_back(window_id);
        }
      }

      if (!carry_task.TWs.empty()) {
        while ((int)Res.size() <= carry_task.TaskId) {
          Res.push_back(Tasks{});
        }
        Res[carry_task.TaskId] = carry_task;
      }
    }

    return Res;
  }

  CSolve csolve(AllTimewindows, AllTasks, AccessibleSatellite,
                DeviceTransferTime, TaskStartTime, TaskEndTime, is_finish,
                output);

  std::vector<Tasks> Res;
  // local_search_1(csolve.solve_pre, 30min, output); //
  // First solve tasks whose execution windows fall within the time range.
  for (const auto &it : is_finish) {
    se.insert(it);
  }
  local_search_2(csolve.solve_pre, frame_start, dgls_time_limit_seconds(),
                 output); // Solve tasks within the current time range.

  // std::ofstream File;
  // File.open(output, std::ios::out | std::ios::trunc);

  // File << "Pre_Tasks: " << (double)csolve.solve_pre.s.count() /
  // csolve.solve_pre.g.task_num * 100 << "%" << endl;

  // const auto& s_pre = csolve.solve_pre.s;
  // for (int i = s_pre.find_first(); i != s_pre.size(); i = s_pre.find_next(i))
  // {
  //     for (const auto& v : csolve.adj[i]) {
  //         csolve.solve_lst.inc_covers(v);
  //     }
  // }
  // pre
  // Previously completed tasks can affect the later graph; their future
  // windows must remain covered. Merge them without deleting covered vertices,
  // since those vertices cannot be selected.

  // AdjGraph g(AllTimewindows, AllTasks, AccessibleSatellite,
  // DeviceTransferTime, TaskEndTime, INT32_MAX, output);

  // for(int i = 0; i < csolve.g_lst.adj.size(); ++ i){
  //     const auto &vec = csolve.g_lst.adj[i];
  //     std::vector<int> temp;
  //     for(const auto &v: vec){
  //         if(!csolve.solve_lst.covers[v]){
  //             temp.emplace_back(v);
  //             auto [x, y] = csolve.g_lst.id_to_edge[v];
  //             Tasks task;
  //             while(x >= Res.size()) {
  //                 Res.push_back(task);
  //             }
  //             Res[x].TWs.push_back(y);
  //             Res[x].TaskId = x;
  //         }
  //     }
  // }

  is_finish.clear();
  for (const auto &it : se) {
    is_finish.insert(it);
  }

  return Res;
}

} // namespace MIS
