# DailyDataset D1 generator for 20 time frames.
# Task windows may extend into later frames.

from pathlib import Path
import csv
import numpy as np

def generate_dataset() -> None:

    satellite_num = 300
    ground_station_num = 100

    expected_total_task_num = 100_000

    batch_num = 20

    group_num = 4

    end_time = 86_400

    batch_duration = end_time // batch_num

    min_time_window_num = 5
    max_time_window_num = 20

    min_window_length = 180
    max_window_length = 360

    base_lambda = expected_total_task_num / batch_num

    group_seeds = [
        3407,
        5419,
        7823,
        9967,
    ]

    load_factors = np.array([
        0.5,
        1.0,
        2.0,
        3.0,
        4.0,
    ])

    load_names = [
        "very_low",
        "low",
        "normal",
        "high",
        "burst",
    ]

    load_probabilities = np.array([
        0.10,
        0.20,
        0.40,
        0.20,
        0.10,
    ])

    root_output_directory = Path("./data/poisson_batches")
    root_output_directory.mkdir(
        parents=True,
        exist_ok=True,
    )

    for group_id in range(group_num):
        rng = np.random.default_rng(
            group_seeds[group_id]
        )

        group_directory = (
            root_output_directory
            / f"group_{group_id + 1}"
        )

        group_directory.mkdir(
            parents=True,
            exist_ok=True,
        )

        global_task_id = 0

        group_total_tasks = 0
        group_total_time_windows = 0

        summary_file_path = (
            group_directory
            / "batch_summary.csv"
        )

        with summary_file_path.open(
            mode="w",
            newline="",
            encoding="utf-8-sig",
        ) as summary_file:

            summary_writer = csv.writer(
                summary_file
            )

            summary_writer.writerow([
                "group_id",
                "batch_id",
                "frame_start",
                "frame_end",
                "load_state",
                "lambda",
                "task_num",
                "time_window_num",
                "first_task_id",
                "last_task_id",
            ])

            for batch_id in range(batch_num):
                frame_start = (
                    batch_id * batch_duration
                )

                if batch_id == batch_num - 1:
                    frame_end = end_time
                else:
                    frame_end = (
                        (batch_id + 1)
                        * batch_duration
                    )

                load_state = int(
                    rng.choice(
                        len(load_factors),
                        p=load_probabilities,
                    )
                )

                current_lambda = (
                    base_lambda
                    * load_factors[load_state]
                )

                current_batch_task_num = int(
                    rng.poisson(current_lambda)
                )

                current_batch_task_num = max(
                    1,
                    current_batch_task_num,
                )

                first_task_id = global_task_id

                current_batch_time_window_num = 0

                batch_file_name = (
                    f"{satellite_num}_"
                    f"{min_time_window_num}-"
                    f"{max_time_window_num}_"
                    f"batch_{batch_id + 1:02d}.csv"
                )

                batch_file_path = (
                    group_directory
                    / batch_file_name
                )

                with batch_file_path.open(
                    mode="w",
                    newline="",
                    encoding="utf-8",
                    buffering=1024 * 1024,
                ) as batch_file:

                    batch_writer = csv.writer(
                        batch_file
                    )

                    batch_writer.writerow([
                        frame_end
                    ])

                    for _ in range(
                        current_batch_task_num
                    ):
                        num_time_windows = int(
                            rng.integers(
                                min_time_window_num,
                                max_time_window_num + 1,
                            )
                        )

                        satellite_id = int(
                            rng.integers(
                                0,
                                satellite_num,
                            )
                        )

                        for _ in range(
                            num_time_windows
                        ):
                            ground_station_id = int(
                                rng.integers(
                                    0,
                                    ground_station_num,
                                )
                            )

                            window_length = int(
                                rng.integers(
                                    min_window_length,
                                    max_window_length + 1,
                                )
                            )

                            latest_start_time = (
                                end_time
                                - window_length
                            )

                            start_time = int(
                                rng.integers(
                                    frame_start,
                                    latest_start_time + 1,
                                )
                            )

                            window_end_time = (
                                start_time
                                + window_length
                            )

                            batch_writer.writerow([
                                global_task_id,
                                satellite_id,
                                ground_station_id,
                                start_time,
                                window_end_time,
                            ])

                            current_batch_time_window_num += 1

                        global_task_id += 1

                last_task_id = (
                    global_task_id - 1
                )

                group_total_tasks += (
                    current_batch_task_num
                )

                group_total_time_windows += (
                    current_batch_time_window_num
                )

                summary_writer.writerow([
                    group_id + 1,
                    batch_id + 1,
                    frame_start,
                    frame_end,
                    load_names[load_state],
                    f"{current_lambda:.2f}",
                    current_batch_task_num,
                    current_batch_time_window_num,
                    first_task_id,
                    last_task_id,
                ])

                print(
                    f"Group {group_id + 1}, "
                    f"Batch {batch_id + 1:02d}, "
                    f"Frame=[{frame_start}, {frame_end}), "
                    f"load={load_names[load_state]}, "
                    f"lambda={current_lambda:.2f}, "
                    f"tasks={current_batch_task_num}, "
                    f"windows="
                    f"{current_batch_time_window_num}"
                )

        info_file_path = (
            group_directory
            / (
                f"{satellite_num}_"
                f"{min_time_window_num}-"
                f"{max_time_window_num}_info.csv"
            )
        )

        with info_file_path.open(
            mode="w",
            newline="",
            encoding="utf-8",
        ) as info_file:

            info_writer = csv.writer(
                info_file
            )

            info_writer.writerow([
                satellite_num,
                ground_station_num,
            ])

            for satellite_id in range(
                satellite_num
            ):
                info_writer.writerow([
                    satellite_id
                ])

            for ground_station_id in range(
                ground_station_num
            ):
                setup_time = int(
                    rng.integers(
                        20,
                        101,
                    )
                )

                info_writer.writerow([
                    ground_station_id,
                    setup_time,
                ])

        print("-" * 70)
        print(
            f"Group {group_id + 1} complete"
        )
        print(
            f"Actual tasks: "
            f"{group_total_tasks}"
        )
        print(
            f"Actual windows: "
            f"{group_total_time_windows}"
        )
        print(
            f"Average windows per task: "
            f"{group_total_time_windows / group_total_tasks:.2f}"
        )
        print(
            f"Output directory: {group_directory}"
        )
        print("-" * 70)

    print(
        "All 4 groups and 80 frame files generated."
    )

if __name__ == "__main__":
    generate_dataset()
