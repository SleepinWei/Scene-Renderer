#!/usr/bin/env python3
"""Compare real PT outputs, including photon preparation; no denoising/clamping.

Runs serially so GPU timings are not contaminated by simultaneous variants.
"""
import argparse
import array
import json
import math
from pathlib import Path
import subprocess
import sys
import time


def pfm(path):
    with open(path, "rb") as stream:
        if stream.readline().strip() != b"PF":
            raise ValueError("Expected RGB PFM")
        width, height = map(int, stream.readline().split())
        scale = float(stream.readline())
        values = array.array("f")
        values.frombytes(stream.read())
        if (scale < 0) != (sys.byteorder == "little"):
            values.byteswap()
        if len(values) != width * height * 3:
            raise ValueError("Invalid PFM payload")
        return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderer", type=Path, default=Path("build/pt/pt-package-render"))
    parser.add_argument("--backend", choices=("CPU", "Metal", "Vulkan"), default="Metal")
    parser.add_argument("--scene", type=Path)
    parser.add_argument("--skip-photons", action="store_true", help="Profile ordinary PT on scenes outside photon support")
    parser.add_argument("--size", default="128x96")
    parser.add_argument("--samples", type=int, default=64)
    parser.add_argument("--reference-samples", type=int, default=4096)
    parser.add_argument("--photon-samples", type=int, default=16)
    parser.add_argument("--photon-paths", type=int, default=1000000)
    parser.add_argument("--photon-radius", type=float, default=.08)
    parser.add_argument("--bounces", type=int, default=8)
    parser.add_argument("--output", type=Path, default=Path("build/path-tracing/pool-benchmark"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    base = [str(args.renderer.resolve()), "--backend", args.backend, "--size", args.size,
            "--bounces", str(args.bounces), "--threads", "8"]
    base += ["--scene", str(args.scene)] if args.scene else ["--fixture", "pool-caustics"]
    reports = {}
    images = {}

    def render(name, samples, flags=(), seed=1):
        prefix = args.output / name
        command = base + ["--output", str(prefix), "--samples", str(samples), "--seed", str(seed)] + list(flags)
        started = time.monotonic()
        with open(str(prefix) + ".log", "w") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        report = json.loads(Path(str(prefix) + ".json").read_text())
        report["process_seconds"] = time.monotonic() - started
        # GPU setup includes photon generation. CPU generation is outside render_seconds.
        report["render_prepare_seconds"] = report["render_seconds"] + report["setup_seconds"] + report["training_seconds"]
        if args.backend == "CPU":
            report["render_prepare_seconds"] += report["photon_seconds"]
        reports[name] = report
        images[name] = pfm(str(prefix) + ".pfm")
        print(name, report["render_prepare_seconds"], flush=True)

    render("reference", args.reference_samples, seed=77)
    render("closest", args.samples, ("--no-shadow-any-hit", "--gpu-batch-samples", "4"))
    render("any-hit", args.samples, ("--gpu-batch-samples", "4"))
    if args.backend != "CPU":
        render("batch8", args.samples, ("--gpu-batch-samples", "8"))
        render("batch16", args.samples, ("--gpu-batch-samples", "16"))
    for name in ("any-hit", "batch8", "batch16"):
        if name in images and images[name] != images["closest"]:
            raise ValueError(f"{name} changed deterministic PT samples")
    for seed in (() if args.skip_photons else (1, 2)):
        render(f"photon-seed{seed}", args.photon_samples,
               ("--photons", "--photon-paths", str(args.photon_paths), "--photon-radius", str(args.photon_radius)), seed)
    matched_samples = None
    if not args.skip_photons:
        photon_cost = max(reports[f"photon-seed{s}"]["render_prepare_seconds"] for s in (1, 2))
        baseline_cost = reports.get("batch8", reports["any-hit"])["render_prepare_seconds"]
        matched_samples = max(32, min(1048576, round(args.samples * photon_cost / max(baseline_cost, 1e-9) / 32) * 32))
        for seed in (1, 2):
            render(f"matched-pt-seed{seed}", matched_samples, seed=seed)
    reference = images["reference"]
    metrics = {}
    for name, values in images.items():
        if any(not math.isfinite(v) for v in values):
            raise ValueError("Nonfinite radiance")
        error = sum((a - b) ** 2 for a, b in zip(values, reference)) / len(reference)
        metrics[name] = {"rmse": math.sqrt(error),
                         "relative_l1": sum(abs(a - b) for a, b in zip(values, reference)) / max(sum(reference), 1e-30),
                         "mean_ratio": sum(values) / max(sum(reference), 1e-30),
                         "render_prepare_seconds": reports[name]["render_prepare_seconds"],
                         "process_seconds": reports[name]["process_seconds"],
                         "rays": reports[name]["rays"],
                         "photon_rays": reports[name]["photon_rays"],
                         "photon_seconds": reports[name]["photon_seconds"],
                         "stored_photons": reports[name]["stored_photons"],
                         "caustic_photons": reports[name]["caustic_photons"],
                         "dispatches": reports[name]["gpu_dispatches"]}
    result = {"backend": args.backend, "size": args.size, "bounces": args.bounces,
              "reference": "pure PT seed 77, no OIDN or clamping; finite-sample reference, not exact GT",
              "settings": {"pt_samples": args.samples, "reference_samples": args.reference_samples,
                           "photon_samples": args.photon_samples, "photon_paths": args.photon_paths,
                           "photon_radius": args.photon_radius, "approximately_matched_pt_samples": matched_samples},
              "bias": None if args.skip_photons else "fixed photon count/radius; camera spp cannot eliminate photon density bias/noise",
              "deterministic_any_hit_and_batch_match": True, "variants": metrics}
    (args.output / "benchmark.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
