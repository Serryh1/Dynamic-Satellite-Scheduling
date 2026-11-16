# DGLS source package

This directory contains a standalone snapshot of the DGLS implementation.
`Src/IndependentSet.cpp` implements the local search and greedy reconstruction;
`Src/DynamicGraphCache.cpp` implements incremental graph updates. The `main.cpp`
entry point runs DGLS only. KaMIS baselines, experiment data, and plotting code
are not included.

The `daily_dataset/` directory contains the five DailyDataset instance
generators and their output-directory details.

## Layout

```text
final_code/
|-- CMakeLists.txt       Build configuration
|-- main.cpp             DGLS command-line entry point
|-- Lib/                 C++ headers
|-- Src/                 Graph construction and local-search implementation
|-- daily_dataset/       D1-D5 data generators and their README
|-- build/               Generated build files (ignored by Git)
`-- results/             Generated solver output (ignored by Git)
```

## Build

From the repository root:

```sh
cmake -S final_code -B final_code/build -DCMAKE_BUILD_TYPE=Release
cmake --build final_code/build -j 8
```

## Run

The input directory needs an `*_info.csv` (or `info.csv`) and numbered
`*_batch_*.csv` files, as in `data/poisson_batches_20/group_1`.

```sh
SATE_GRAPH_MODE=update \
SATE_WARM_START=1 \
SATE_REPORT_GRAPH_METRICS=1 \
SATE_SOLVER_TIME_LIMIT_SECONDS=600 \
./final_code/build/dgls data/poisson_batches_20/group_1 final_code/results
```

Results are written to `final_code/results/dgls/<frame>_MIS_result.txt`.
The time limit is per frame and includes graph construction by default.
Greedy reconstruction is enabled by default; `SATE_GRAPH_MODE=update` selects
the dynamic graph implementation. The solver's default limit is 900 seconds
when `SATE_SOLVER_TIME_LIMIT_SECONDS` is unset.

Useful optional settings:

- `SATE_STOP_AFTER_FRAME=1` runs only the first frame.
- `SATE_TARGET_FRAMES=5-10` resumes selected frames from existing results.
- `SATE_GRAPH_MODE=scratch` rebuilds the graph at each frame.
- `SATE_HEURISTIC_ONLY_LIMIT=1` starts the time limit after graph construction.
