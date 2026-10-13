# Rendering memory safety

Memory admission is enabled by default for the editor, offscreen captures and cluster tools. A small asset package can expand into several simultaneous CPU and GPU representations: imported vertices, detached snapshots, hierarchy construction maps, indirect indices, diagnostic replay geometry, readback buffers and JSON records. Disk size is measured separately from runtime memory.

## Defaults and controls

The automatic process budget is the smaller of **20% of physical RAM and 3 GiB**. The GPU budget defaults to half of that amount. On the current **16 GiB Apple Silicon machine**, the defaults are **3072 MiB process / 1536 MiB GPU**. Admission also keeps up to 128 MiB of internal headroom. The threaded editor now defaults to **one GPU frame in flight**.

```sh
./build/Scene-Renderer --memory-info
./build/Scene-Renderer --classic cluster-cave --memory-budget-mib 2048 --gpu-resource-budget-mib 1024
```

`--memory-budget-mib` and `--gpu-resource-budget-mib` are parsed before all CLI entry points. **0 selects automatic protection**, rather than unlimited allocation. Explicit process limits must leave at least half of physical RAM, and at least 2 GiB on machines with 4 GiB or more. The GPU quota cannot exceed the process budget. The editor displays the current process footprint, both budgets, pending scratch reservations and system pressure.

## What admission covers

- RHI buffers and textures have a default payload quota; rejection happens before native allocation.
- Metal also checks `currentAllocatedSize` against the GPU budget before allocating buffers, textures, upload/readback staging, ray-tracing indices, acceleration structures and construction scratch. Thus these hidden transient buffers cannot bypass the tracked RHI quota.
- Texture decoders inspect dimensions and reserve decoded pixels plus conversion scratch before loading. DDS validates its complete payload before allocating the output image; a truncated 16384×16384 header is rejected without allocating its 1 GiB output.
- Model imports reserve estimated scratch and abort through Assimp's progress callback when pressure rises. Snapshot copies and PT host/GPU cache copies receive preflight checks; viewport PT preparation also reserves scratch once per unique source mesh.
- Concurrent cluster workers reserve conservative peak scratch before constructing maps, edge sets and QEM candidates. Reservations are shared across workers and released on completion or exception. Construction checks pressure again between hierarchy levels.
- Diagnostic geometry replay has a separate ceiling: the smaller of **512 MiB and a quarter of the process budget**, estimated at 256 bytes per replayed triangle. Offscreen coverage bounds are rejected before copying triangle vertices. Oversized captures fail with a clear budget message.

macOS samples `TASK_VM_INFO.phys_footprint`, including compressed process memory, and observes dispatch memory-pressure notifications. Startup also reads the current pressure flags so a process launched during an existing warning does not temporarily assume normal pressure. On warning/critical pressure, new large allocations and render-frame admission stop. Linux uses RSS plus swap and available-memory pressure; Windows samples working set/private usage and system memory load. [Apple's pressure-notification design](https://github.com/apple-oss-distributions/xnu/blob/main/doc/vm/memorystatus_notify.md).

## Failure and recovery

The threaded editor treats safety admission failures as resource-budget failures: it rolls back the candidate publication, releases idle GPU images, completes candidate retirements and keeps the last successful image. Retries are throttled to 250 ms. The existing optional automatic-quality policy can reduce supported effects. A cold start with no usable frame, or an offscreen capture that cannot fit its budget, reports the reason and exits instead of proceeding with the allocation.

These are **cooperative allocation checks, not an OS-enforced RSS hard cap**. Scratch estimates are deliberately conservative; third-party code and driver internals can still grow between checks. Existing GPU commands cannot be cancelled mid-dispatch, and a memory budget does not impose a GPU-time limit. The limits reduce known memory peaks and refuse unsafe work; they do not promise that every workload will have no desktop impact.

## Validation

`memory-safety-tests` uses synthetic samples, without allocating gigabytes, to cover automatic and explicit limits, process growth, native GPU staging headroom, warning/critical rejection and recovery, overflow, exception-safe reservation release, and concurrent-worker admission. The RHI buffer/texture contract tests also verify rejection before native allocation and retirement accounting.

During implementation, the actual system was already at memory-pressure warning. High-density capture was therefore suspended; integration checks use a small hidden scene and deliberately low budgets to verify controlled refusal.

The 64 MiB process / 32 MiB GPU hidden forward-scene smoke test completed one frame. With a 1 MiB GPU quota, the same scene refused the 3,840,000-byte Forward HDR allocation before native creation and exited with status 1. Extended cluster/engine regression attempts were refused when the real system returned to warning pressure; they are not reported as passes. No protected high-density throughput measurement has been made.
