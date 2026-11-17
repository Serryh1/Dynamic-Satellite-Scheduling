# Dynamic Scheduling for Large-Scale Satellite Constellation: A Graph-based Approach

This directory contains the source code and DailyDataset generators for
the paper. DGLS handles tasks arriving over successive time frames by building
a conflict graph of feasible communication opportunities. It then searches for
an independent set while preserving decisions made in earlier frames.

## Contents

| Path | Description |
| --- | --- |
| `main.cpp` | Reads the time-frame batches and runs DGLS sequentially. |
| `Lib/`, `Src/` | Conflict-graph construction, dynamic updates, and independent-set search. |
| `daily_dataset/` | Generators for DailyDataset instances D1-D5. |
| `CMakeLists.txt` | C++17 build configuration. |

## Datasets

DailyDataset has five scales, D1-D5, with 20, 40, 60, 80, and 100 time
frames, respectively. Each generator creates four groups of task-window CSV
files and a resource information file. See
[`daily_dataset/README.md`](daily_dataset/README.md) for generation commands
and output directories. The generators require Python 3 and NumPy.

MTFDataset: [download](https://pan.baidu.com/s/1aEFW-838GZLgQL2G_FRfbA?pwd=rb6b).

## Environment

1. C++17 support；
2. CMake VERSION 3.16 ;

## Run instructions

Create build folder

```sh
mkdir  build
```

Then use the following commands to generate the executable:

```sh
cd build
cmake ..
make
```

Run DGLS with an input dataset directory and an optional output directory:

```sh
./dgls <input_folder> [output_folder]
```

The input directory must contain an `*_info.csv` (or `info.csv`) resource
file and numbered `*_batch_*.csv` files. For example, if a D1 group is at
`data/poisson_batches/group_1` relative to this directory, run from `build`:

```sh
SATE_GRAPH_MODE=update SATE_WARM_START=1 \
SATE_SOLVER_TIME_LIMIT_SECONDS=600 \
./dgls ../data/poisson_batches/group_1 ../results
```

Results are written to `results/dgls/<frame>_MIS_result.txt`.
The time limit includes graph construction and defaults to 900 seconds per
frame. Greedy reconstruction is enabled by default. If the output directory
is omitted, results are written under the input directory instead.

Optional settings:

- `SATE_GRAPH_MODE=scratch` rebuilds the graph at each frame instead of updating it.
- `SATE_REPORT_GRAPH_METRICS=1` appends graph and timing metrics to each result.
- `SATE_HEURISTIC_ONLY_LIMIT=1` starts the time limit after graph construction.
- `SATE_STOP_AFTER_FRAME=1` stops after the first frame.
- `SATE_TARGET_FRAMES=5-10` runs selected frames using preceding results to restore state.
