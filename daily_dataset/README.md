# DailyDataset generators

These scripts reproduce the five DailyDataset instance-generation variants.
Each creates four seeded groups of task-window CSV files, an info file, and a
batch summary. They require Python 3 and NumPy.

Run from the repository root, for example:

```sh
python3 final_code/daily_dataset/D1.py
```

| Script | Frames | Duration | Output directory |
| --- | ---: | ---: | --- |
| `D1.py` | 20 | 1 day | `data/poisson_batches` |
| `D2.py` | 40 | 2 days | `data/poisson_batches_40` |
| `D3.py` | 60 | 3 days | `data/poisson_batches_60_2days_gs200` |
| `D4.py` | 80 | 4 days | `data/poisson_batches_80_2.5days_gs200` |
| `D5.py` | 100 | 5 days | `data/poisson_batches_100_3days_gs200` |

Output directory names are retained from the original scripts, including the
older duration labels. Running a generator overwrites its output files. These
scripts generate scheduling instances; the DGLS conflict-graph construction is
in `../Src/AdjGraph.cpp` and `../Src/DynamicGraphCache.cpp`.
