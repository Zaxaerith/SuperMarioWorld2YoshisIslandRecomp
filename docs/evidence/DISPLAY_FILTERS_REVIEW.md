# Display-filter validation

Windows Release, 19 November 2026: i7-13700KF, Direct3D, VSync on,
Europe Rev2 level 1-1. Live tests used the combined integration build;
the standalone branch uses the same filter kernel.
The contributor verified all five modes, F7-to-F2 rebinding and persistence after restart.

## Same-frame comparisons

Each pair is one F9 frame: raw 342x224 Original and 1026x672 filtered output.
PNG conversion preserves RGB and makes unused alpha opaque; final display aspect
is shown separately below.

| Mode | Original | Filter |
|---|---|---|
| Sharp | ![](display-filters/sharp-before.png) | ![](display-filters/sharp-after.png) |
| Scanlines | ![](display-filters/scanlines-before.png) | ![](display-filters/scanlines-after.png) |
| CRT | ![](display-filters/crt-before.png) | ![](display-filters/crt-after.png) |
| LCD grid | ![](display-filters/lcd-before.png) | ![](display-filters/lcd-after.png) |

Final native-width Sharp output, resized without cropping:

![Native-width final output](display-filters/native-sharp-present.png)

## Runtime cost

Wide input 342x224, output 3840x2160, PAL target ~50 FPS. Each interval is ~5 s.
The first interval after F9 is excluded because capture I/O affects FPS/CPU.
[Raw intervals and exclusions](display-filters/live-wide-intervals.csv).

| Mode | Clean intervals | Median FPS | Filter ms/frame | Process CPU, % of one core |
|---|---:|---:|---:|---:|
| Sharp | 4 |50.010 |0.3489 |31.76 |
| Scanlines |2 |50.007 |0.3519 |36.74 |
| CRT |1 |50.015 |0.4693 |40.16 |
| LCD grid |1 |50.009 |0.3505 |41.40 |

VSync caps FPS. CRT/LCD each have only one clean interval. The Original interval
contains capture I/O, and gameplay activity differs between modes: these samples
do not establish a controlled whole-game CPU/FPS delta versus Original.
100% CPU here means one logical core.

Six stationary native-width Sharp intervals have median 50.007 FPS and ~0.264 ms
filter time. [Log excerpts](display-filters/live-native-excerpts.txt).

An isolated Release/O3 kernel benchmark on captured pixels used five rotating-order
batches per mode/width, 100 warm-up and 1000 measured frames each.
Median cost is 0.260-0.350 ms at 256x224 and 0.348-0.476 ms at 342x224.
Kernel CPU time projects to ~1.25-2.27% of one core at 50 FPS; texture upload,
GPU presentation and emulation are excluded. [Raw benchmark](display-filters/kernel-benchmark.csv).

## Build and integration

Updated onto main 1922348, including VSync #8. Filters do not force VSync or
change regional pacing. Nine standalone and 16 combined local contracts pass;
Windows Release CI passes the ROM-free contracts.

The shared launcher helper accumulates/deduplicates callbacks per target, with
six registration orders and target isolation tested. Combined presentation is
native/wide source -> optional 3x filter -> aspect-correct output -> RA overlay.
Merging #5/#7 must retain this order and allocate textures for the selected width;
the standalone presenter uses 256x224. RA/PAL/widescreen remain separate features.
F9 full-output/opaque-border readback is regression-tested without changing rendering.

A controlled Original-versus-filter runtime comparison remains outstanding.
