#!/usr/bin/env bash
# Builds the library, runs every unit test, end-to-end test and benchmark,
# and stores the raw results under docs/results/<platform>/ so that
# tools/gen_docs.py can regenerate the per-algorithm documentation.
#
# Usage: tools/run_all.sh [native|wasm|asan|noexc|all]   (default: native)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:-native}"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
BENCH_TIME="${SM_BENCH_MIN_TIME:-0.25}"

run_platform() {
  local name="$1" build="$2" runner="$3"
  shift 3
  local out="$ROOT/docs/results/$name"
  mkdir -p "$out/raw"
  echo "== [$name] configure + build"
  if [[ "$name" == wasm ]]; then
    emcmake cmake -S "$ROOT" -B "$build" -DCMAKE_BUILD_TYPE=Release "$@" > "$out/raw/configure.txt" 2>&1
  else
    cmake -S "$ROOT" -B "$build" "$@" > "$out/raw/configure.txt" 2>&1
  fi
  cmake --build "$build" -j "$JOBS" > "$out/raw/build.txt" 2>&1
  echo "== [$name] tests"
  (cd "$build" && ctest -j "$JOBS" --output-on-failure > "$out/raw/ctest.txt" 2>&1) || true
  for exe in "$build"/tests/sm_unit_* "$build"/tests/sm_e2e_*; do
    [[ -f "$exe" ]] || continue
    [[ "$exe" == *.wasm ]] && continue
    local base
    base="$(basename "$exe" .js)"
    if [[ -n "$runner" ]]; then
      "$runner" "$exe" > "$out/raw/$base.txt" 2>&1 || true
    else
      "$exe" > "$out/raw/$base.txt" 2>&1 || true
    fi
  done
  if [[ -f "$build/bench/sm_bench" || -f "$build/bench/sm_bench.js" ]]; then
    echo "== [$name] benchmarks"
    local bench="$build/bench/sm_bench"
    [[ -f "$bench.js" ]] && bench="$bench.js"
    if [[ -n "$runner" ]]; then
      "$runner" "$bench" --min-time="$BENCH_TIME" --json="$out/bench.json" > "$out/raw/bench.txt" 2>&1 || true
    else
      "$bench" --min-time="$BENCH_TIME" --json="$out/bench.json" > "$out/raw/bench.txt" 2>&1 || true
    fi
  fi
  # Environment record.
  {
    echo "{"
    echo "  \"platform\": \"$name\","
    echo "  \"date\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
    echo "  \"host\": \"$(uname -sm)\","
    if [[ "$(uname -s)" == Darwin ]]; then
      echo "  \"cpu\": \"$(sysctl -n machdep.cpu.brand_string 2>/dev/null)\","
    else
      echo "  \"cpu\": \"$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2 | xargs)\","
    fi
    if [[ "$name" == wasm ]]; then
      echo "  \"compiler\": \"$(emcc --version | head -1)\","
      echo "  \"runtime\": \"node $(node --version)\","
    else
      echo "  \"compiler\": \"$(${CXX:-c++} --version | head -1)\","
      echo "  \"runtime\": \"native\","
    fi
    echo "  \"ctest\": \"$(grep -E 'tests passed' "$out/raw/ctest.txt" | tail -1)\""
    echo "}"
  } > "$out/environment.json"
  grep -E "tests passed" "$out/raw/ctest.txt" | tail -1
}

case "$TARGET" in
  native) run_platform native "$ROOT/build" "" ;;
  wasm) run_platform wasm "$ROOT/build-wasm" node ;;
  asan) run_platform asan "$ROOT/build-asan" "" -DSM_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug -DSM_BUILD_BENCH=OFF ;;
  noexc) run_platform noexc "$ROOT/build-noexc" "" -DSM_NO_EXCEPTIONS=ON -DSM_BUILD_BENCH=OFF ;;
  all)
    run_platform native "$ROOT/build" ""
    command -v emcc > /dev/null && run_platform wasm "$ROOT/build-wasm" node
    run_platform asan "$ROOT/build-asan" "" -DSM_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug -DSM_BUILD_BENCH=OFF
    run_platform noexc "$ROOT/build-noexc" "" -DSM_NO_EXCEPTIONS=ON -DSM_BUILD_BENCH=OFF
    ;;
  *) echo "unknown target $TARGET" >&2; exit 2 ;;
esac

python3 "$ROOT/tools/gen_docs.py"
