#!/usr/bin/env python3
"""Summarize native water ablations without adding overlapping pass durations."""
import argparse
import collections
import gzip
import json
import math
from pathlib import Path
import statistics


def stats(values):
    values = sorted(values)
    return {"p50_ms": statistics.median(values), "p95_ms": values[math.ceil(.95 * len(values)) - 1], "samples": len(values)}


def union_length(intervals):
    end = -math.inf
    result = 0.
    for low, high in sorted(intervals):
        result += max(0., high - max(low, end))
        end = max(end, high)
    return result


def summarize(case, warmup):
    frames = case["frames"][warmup:]
    if not frames:
        raise ValueError("No measured frames")
    one = {key: value for key, value in case.items() if key != "frames"}
    for key in ("wall_ms", "gpu_submission_ms", "acquire_cpu_ms", "collect_cpu_ms", "record_cpu_ms", "submit_cpu_ms", "wait_cpu_ms"):
        one[key] = stats([frame[key] for frame in frames])
    labels = collections.defaultdict(lambda: collections.defaultdict(list))
    observed_gaps, counts, delays = [], [], []
    for frame in frames:
        totals = collections.defaultdict(lambda: collections.defaultdict(float))
        windows = []
        submissions = [s for s in frame["samples"] if s["kind"] == "submission"]
        counts.append(len(submissions))
        delays.extend(s["queue_delay_ms"] for s in submissions)
        if any(s["label"].startswith("profile-truncated/") for s in frame["samples"]):
            raise ValueError("Truncated GPU profile")
        for sample in frame["samples"]:
            if sample["kind"] == "submission":
                continue
            totals[sample["label"]]["encoder_interval_ms"] += sample["end_ms"] - sample["start_ms"]
            if sample["kind"] == "render":
                totals[sample["label"]]["vertex_window_ms"] += sample["vertex_ms"]
                totals[sample["label"]]["fragment_window_ms"] += sample["fragment_ms"]
                windows.extend([(sample["vertex_start_ms"], sample["vertex_end_ms"]), (sample["fragment_start_ms"], sample["fragment_end_ms"])])
            else:
                windows.append((sample["start_ms"], sample["end_ms"]))
        for label, measures in totals.items():
            for measure, value in measures.items():
                labels[label][measure].append(value)
        if len(submissions) == 1:
            low, high = submissions[0]["start_ms"], submissions[0]["end_ms"]
            clipped = [(max(a, low), min(b, high)) for a, b in windows if min(b, high) >= max(a, low)]
            observed_gaps.append(max(0., high - low - union_length(clipped)))
    one["submission_counts"] = sorted(set(counts))
    one["queue_delay_ms"] = stats(delays) if delays else None
    one["uncovered_counter_windows_ms"] = stats(observed_gaps) if observed_gaps else None
    one["stage_windows"] = {label: {key: stats(values) for key, values in measures.items()} for label, measures in sorted(labels.items())}
    median_gpu = one["gpu_submission_ms"]["p50_ms"]
    representative = min(frames, key=lambda frame: abs(frame["gpu_submission_ms"] - median_gpu))
    one["representative_frame"] = representative
    return one


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    report = {"scope": "Synchronized latency; feature ablations can change several dependent passes. Stage windows may overlap and contain waits; their sums are not whole-frame cost or GPU utilization. Uncovered timestamp windows do not prove hardware idleness.", "runs": []}
    for path in args.inputs:
        opener = gzip.open if path.suffix == ".gz" else open
        with opener(path, "rt") as stream:
            raw = json.load(stream)
        run = {key: value for key, value in raw.items() if key != "cases"}
        run["source"] = str(path)
        run["cases"] = [summarize(case, raw["warmup_frames"]) for case in raw["cases"]]
        report["runs"].append(run)
        print(path)
        print("view | mode | wall p50 | wall p95 | GPU p50 | water fragment | fog fragment")
        for case in run["cases"]:
            def fragment(label):
                return case["stage_windows"].get(label, {}).get("fragment_window_ms", {}).get("p50_ms", 0.)
            print(f'{case["view"]} | {case["mode"]} | {case["wall_ms"]["p50_ms"]:.2f} | {case["wall_ms"]["p95_ms"]:.2f} | {case["gpu_submission_ms"]["p50_ms"]:.2f} | {fragment("water/surface"):.2f} | {fragment("water/underwater-fog"):.2f}')
    args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
