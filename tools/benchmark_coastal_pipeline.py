#!/usr/bin/env python3
"""Run or summarize the native coastal benchmark; uses only the Python stdlib.

Each native run keeps its original encoder boundaries, drains after presentation,
and excludes the first eight frames. It measures synchronized frame latency.
GPU stage durations overlap and must not be added to obtain frame time.
"""
import argparse
import collections
import json
import os
from pathlib import Path
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CASES = {
    "720": ({}, "coastal-performance"),
    "moving": ({"SCENERENDERER_BENCH_MOVE": "1"}, "coastal-performance"),
    "water": ({}, "coastal-performance-water"),
    "1440": ({"SCENERENDERER_BENCH_WIDTH": "2560"}, "coastal-performance"),
    "effects-off": ({"SCENERENDERER_BENCH_BASELINE": "1"}, "coastal-performance"),
}


def intervals(sample):
    if sample["kind"] == "render":
        return [(sample["vertex_start_ms"], sample["vertex_end_ms"]),
                (sample["fragment_start_ms"], sample["fragment_end_ms"])]
    if sample["kind"] in ("compute", "blit"):
        return [(sample["start_ms"], sample["end_ms"])]
    return []


def summarize(data):
    frames = data["frames"][data["warmup_frames"]:]
    result = {k: {"median": statistics.median(f[k] for f in frames),
                  "p95": sorted(f[k] for f in frames)[int(.95 * (len(frames) - 1))]}
              for k in ("wall_ms", "gpu_submission_ms", "acquire_cpu_ms",
                        "collect_cpu_ms", "record_cpu_ms", "submit_cpu_ms", "wait_cpu_ms")}
    stages = collections.defaultdict(list)
    uncovered, queue_delay, counts = [], [], []
    for frame in frames:
        values = collections.defaultdict(float)
        ocean_draw = 0
        for sample in frame["samples"]:
            name = sample["label"]
            # The baseline graph used a single "ocean" name for these stages.
            if name == "ocean":
                if sample["kind"] == "compute":
                    name = "water/depth-chain"
                elif sample["kind"] == "render":
                    name = "water/capture" if ocean_draw == 0 else "water/surface"
                    ocean_draw += 1
            if name.startswith("fft") or name.startswith("shore/") or name.startswith("terrain/"):
                name = name.split("/")[0]
            if sample["kind"] == "render":
                values[name] += sample["vertex_ms"] + sample["fragment_ms"]
            elif sample["kind"] in ("compute", "blit"):
                values[name] += sample["end_ms"] - sample["start_ms"]
            elif sample["kind"] == "diagnostic":
                raise RuntimeError("Truncated native GPU profiling data")
            elif sample["kind"] == "submission":
                queue_delay.append(sample["queue_delay_ms"])
        for name, duration in values.items():
            stages[name].append(duration)
        ranges = sorted((max(0, a), min(frame["gpu_submission_ms"], b))
                        for sample in frame["samples"] for a, b in intervals(sample)
                        if b > a and b > 0 and a < frame["gpu_submission_ms"])
        covered, end = 0, 0
        for a, b in ranges:
            covered += max(0, b - max(end, a))
            end = max(end, b)
        if ranges:
            uncovered.append(frame["gpu_submission_ms"] - covered)
        counts.append(len(frame["samples"]) - 1)
    result["stages_ms"] = {k: statistics.median(v) for k, v in stages.items()}
    result["uncovered_timestamp_interval_ms"] = statistics.median(uncovered) if uncovered else None
    result["commit_to_gpu_start_ms"] = statistics.median(queue_delay) if queue_delay else None
    result["timestamp_records_per_frame"] = statistics.median(counts)
    result["measured_frames"] = len(frames)
    return result


def trace_frame(data, pid, title):
    frames = data["frames"][data["warmup_frames"]:]
    median = statistics.median(f["gpu_submission_ms"] for f in frames)
    frame = min(frames, key=lambda f: abs(f["gpu_submission_ms"] - median))
    events = [{"ph": "M", "name": "process_name", "pid": pid, "args": {"name": title}}]
    lanes = {"submission": 0, "compute": 1, "vertex": 2, "fragment": 3, "blit": 4}
    for name, tid in lanes.items():
        events.append({"ph": "M", "name": "thread_name", "pid": pid, "tid": tid, "args": {"name": name}})
    for sample in frame["samples"]:
        if sample["kind"] == "submission":
            ranges = [("submission", sample["start_ms"], sample["end_ms"])]
        elif sample["kind"] == "render":
            ranges = [("vertex", sample["vertex_start_ms"], sample["vertex_end_ms"]),
                      ("fragment", sample["fragment_start_ms"], sample["fragment_end_ms"])]
        elif sample["kind"] in lanes:
            ranges = [(sample["kind"], sample["start_ms"], sample["end_ms"])]
        else:
            ranges = []
        for lane, start, end in ranges:
            if end > start:
                events.append({"ph": "X", "name": sample["label"], "pid": pid,
                               "tid": lanes[lane], "ts": start * 1000, "dur": (end - start) * 1000,
                               "args": {"native_frame": frame["frame"]}})
    return events


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--summarize", action="store_true")
    parser.add_argument("--binary", type=Path, default=ROOT / "build/Scene-Renderer")
    parser.add_argument("--output", type=Path, default=ROOT / "img/diagnostics/water")
    parser.add_argument("--tag", default="after")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.run:
        for name, (extra, selection) in CASES.items():
            directory = args.output / ("performance-" + args.tag + "-" + name)
            env = os.environ.copy()
            for key in ("SCENERENDERER_BENCH_MOVE", "SCENERENDERER_BENCH_WIDTH", "SCENERENDERER_BENCH_BASELINE", "SCENERENDERER_BENCH_EDGE"):
                env.pop(key, None)
            env.update(SCENERENDERER_DISABLE_PIPELINE_DISK_CACHE="1", SCENERENDERER_GPU_PROFILE="1")
            env.update(extra)
            with (args.output / ("performance-" + args.tag + "-" + name + ".log")).open("w") as log:
                subprocess.run([str(args.binary.resolve()), "--render-gallery", str(directory.resolve()), selection],
                               cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            print(args.tag, name, "complete", flush=True)
    if args.summarize:
        summary, events = {}, []
        for name in CASES:
            summary[name] = {}
            for phase in ("before", "after"):
                path = args.output / ("performance-" + phase + "-" + name) / "coastal-performance.json"
                data = json.loads(path.read_text())
                summary[name][phase] = summarize(data)
                events.extend(trace_frame(data, len(summary) * 2 + (phase == "after"), name + " - " + phase))
            before = summary[name]["before"]["wall_ms"]["median"]
            after = summary[name]["after"]["wall_ms"]["median"]
            summary[name]["latency_reduction_percent"] = 100 * (1 - after / before)
            print(name, f"{before:.2f} -> {after:.2f} ms; {100 * (1 - after / before):.1f}% reduction")
        (args.output / "coastal-performance-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
        (args.output / "coastal-performance-trace.json").write_text(json.dumps({"traceEvents": events}, separators=(",", ":")) + "\n")


if __name__ == "__main__":
    main()
