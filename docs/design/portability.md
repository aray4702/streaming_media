# Portability

## Targets

| Device class | Typical platform | CMake preset | Status |
|---|---|---|---|
| Server, SFU | Linux x86-64 / ARM64, GCC or Clang | `native` | CI job (GCC 13, Clang 18) |
| Desktop | macOS (Apple Silicon), Linux, Windows | `native` | **Verified locally**: macOS arm64, AppleClang 21 |
| Browser | WebAssembly (Emscripten) | `wasm` | **Verified locally**: Emscripten 6.0, Node 26 |
| Phone, tablet | iOS arm64 | `ios`, `ios-simulator` | CI cross-build |
| Phone, wearable | Android arm64-v8a, armeabi-v7a | `android-arm64`, `android-armv7` | CI cross-build |
| Robot, camera, SBC | Embedded Linux aarch64 | `linux-aarch64` | CI cross-build, tests under QEMU |
| Wearable, low-end camera | Embedded Linux ARMv7 hard-float | `linux-armv7` (size-optimized) | Toolchain provided |

"Verified locally" means the complete unit, end-to-end and benchmark
suites pass and the results are in [docs/results](../results/README.md).
iOS, Android and the Linux ARM toolchains were not available on the
development machine, so they are covered by the CI workflow
(`.github/workflows/ci.yml`) rather than a local run. MSVC/Windows has
no preset yet; the code avoids GCC/Clang-only constructs except an
inline-asm optimization barrier in the benchmark harness, which has an
MSVC fallback.

## Requirements

- C++20: `<span>`, `<numbers>`, `<bit>`, defaulted `operator<=>`,
  `std::erase_if`. Expected minimum compilers: GCC 11, Clang 14,
  AppleClang 14, Emscripten 3.x.
- CMake 3.20 (3.25 for the presets).
- A hardware FPU: audio DSP uses `float`. Cortex-M0-class MCUs without an
  FPU are not a target. Cortex-M4F/M7 could run the
  packet-level algorithms (sections 3, 4.1-4.2, 5, 8) but would need an
  RTOS-specific build setup.
- No third-party dependencies, no threads, no exceptions or RTTI
  required, no file or network I/O, no clock access.

## Checks performed

| Check | How | Result |
|---|---|---|
| 32-bit target | WebAssembly (wasm32) | all tests pass |
| `-fno-exceptions -fno-rtti` | `SM_NO_EXCEPTIONS=ON` | all tests pass |
| Memory and UB safety | ASan + UBSan, Debug | all tests pass, no reports |
| Feature subsets | cc-only, audio-only, loss-only, loss+timing+video | all section tests pass |
| Size build | `embedded` preset (MinSizeRel, metrics/events compiled out) | builds warning-free |
| Standard-library portability | include-what-you-use scan (every `std::` facility includes its header directly, rather than relying on libc++'s transitive includes) | 19 missing includes added |
| Cross-platform numerics | measured values native vs. Wasm | identical |
| Packaging | `cmake --install` + `find_package(streaming_media)` from a separate project | works |

Portability defects found and fixed during verification:

- **`long double` on WebAssembly** is a software-emulated 128-bit float. The
  capture-time interpolation used it and missed its speed budget there
  (347 ns vs. a 200 ns budget). It now uses exact integer arithmetic.
- **Wasm heap.** Emscripten's default fixed 16 MB heap is too small for
  test executables that hold 40 s of audio; test and benchmark
  executables now enable memory growth. The library itself allocates only
  small, bounded buffers.
- **Benchmark JSON under Node** needs `-sNODERAWFS=1` to reach the real
  file system.
- **Timing gates under contention.** Benchmark CTest entries run serially
  (`RUN_SERIAL`); speed budgets were flaky when sharing the CPU with
  parallel tests.

## Performance across hardware

All code is portable scalar C++ (no SIMD), so it runs anywhere and
leaves room for platform-specific acceleration. On the reference machine
(Apple M4), WebAssembly runs at a median of 1.14x native time.

| Workload | Native (M4) | Wasm (Node) |
|---|---|---|
| Full audio capture chain (HPF+AEC+NS+VAD+TS+AGC), 10 ms at 16 kHz | 97 us, 103x real time | 140 us, 71x real time |
| Echo canceller alone (80 ms tail) | 102 us, 98x real time | 147 us, 68x real time |
| Noise suppressor | 3.5 us, 2 800x real time | 5.2 us, 1 900x real time |
| NetEq-lite pull (10 ms) | 0.16 us | 0.16 us |
| GCC, per acknowledged packet | 128 ns | 113 ns |
| 720p -> 360p scale | 1.7 ms (20x at 30 fps) | 2.3 ms (14x) |

Rule of thumb for planning: a Cortex-A53 at 1.2 GHz (Raspberry Pi 3,
many cameras and wearables) is roughly 8-15x slower than an M4 core on
scalar float code. That leaves the full audio chain at about 7-13x real
time, comfortably real-time on one core. Video scaling at 720p becomes
the bottleneck there; use the SoC's scaler or a SIMD library (libyuv) for
pixel work. The benchmark harness takes `--budget-scale` (or
`SM_BENCH_BUDGET_SCALE`) so the same speed gates can run on slower
hardware.

## Footprint

Code size per section (native Release, `size` text segment) is listed in
[docs/results](../results/README.md#footprint-native-release-code-size-per-section):
about 350 KiB for all six sections plus core, against roughly 6-12 MB
per ABI for libwebrtc. Unused sections can be disabled at configure
time, and the linker drops unused objects within a section.

Runtime memory is dominated by explicit, configurable buffers: the RTX
history (1 024 packets by default), the jitter buffer (up to 100
packets), the pacer queue (bounded by queue time) and the echo
canceller's filter (16 partitions x 129 complex bins x 2 filters, about
33 KB at 16 kHz).
