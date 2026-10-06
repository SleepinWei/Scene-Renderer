#!/usr/bin/env python3
"""Summarize the fixed underwater gallery's native GPU counters (stdlib only).

Stage intervals overlap; they must not be added to obtain whole-frame cost.
"""
import json
from pathlib import Path
import statistics


def main():
    root = Path(__file__).resolve().parents[1]
    directory = root / "img/diagnostics/water"
    profile = json.loads((directory / "underwater-gallery-profile.json").read_text())
    metrics = json.loads((root / "img/metal/coastal-underwater-water-metrics.json").read_text())
    groups = []
    for sample in profile["samples"]:
        if sample["kind"] == "submission":
            groups.append([])
        elif groups:
            groups[-1].append(sample)
    main_groups = [g for g in groups if any(s["label"] == "water/surface" for s in g)]
    names = ["coastal-underwater", "coastal-underwater-no-fog",
             "coastal-underwater-snell-window", "coastal-underwater-bottom",
             "coastal-underwater-above"]
    if not profile["supported"] or len(main_groups) != 16 * len(names):
        raise RuntimeError("Expected supported counters and five 16-frame gallery views")
    if any(s["kind"] == "diagnostic" for s in profile["samples"]):
        raise RuntimeError("Native GPU profiling data was truncated")
    result = {"size": [1280, 720], "frames_per_view": 16, "warmup_frames": 4,
              "timestamp_sampling": True,
              "scope": "Main rendering submission, excludes separate FFT/SWE and presentation; static synchronized gallery, not editor FPS. Stage intervals overlap.",
              "views": {}}
    for index, name in enumerate(names):
        stages = {}
        for label in ("water/capture", "water/depth-chain", "water/underwater-fog", "water/surface"):
            per_frame = [sum(s["vertex_ms"] + s["fragment_ms"] if s["kind"] == "render"
                             else s["end_ms"] - s["start_ms"]
                             for s in g if s["label"] == label)
                         for g in main_groups[index * 16 + 4:(index + 1) * 16]]
            stages[label] = statistics.median(per_frame)
        result["views"][name] = {"main_gpu_median_ms": metrics[name]["median_gpu_ms"],
                                  "captured_terrain_pixels": metrics[name]["captured_terrain_pixels"],
                                  "stages_median_ms": stages}
        print(name, f'{metrics[name]["median_gpu_ms"]:.2f} ms', stages)
    (directory / "underwater-performance-summary.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
