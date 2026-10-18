#!/usr/bin/env bash
# Times the three builds that matter and prints where the time goes.
#   usage: scripts/measure-build.sh            (from the repo root; needs an Android SDK/NDK like a normal build)
# Output goes to build-timings.txt; paste it back into the thread.
set -uo pipefail
cd "$(dirname "$0")/.."
out=build-timings.txt
: > "$out"
t() { # t <label> <command...>
  local label="$1"; shift
  local s=$SECONDS
  "$@" > "build-timings-$label.log" 2>&1
  echo "$label: $((SECONDS - s))s (exit $?)" | tee -a "$out"
}
{ echo "date: $(date)"; echo "ccache: $(command -v ccache || echo none)"; ./gradlew --version | sed -n 1,6p; } | tee -a "$out"
[ -x "$(command -v ccache)" ] && ccache -z >/dev/null

# `gradlew clean` runs CMake configure, which needs the patched UZDoom sources, so wipe the outputs directly.
rm -rf app/.cxx app/build
t 1-clean-full ./gradlew assembleDebug --profile
t 2-noop       ./gradlew assembleDebug
touch app/src/main/cpp/xr/xr_vr.cpp
t 3-touch-one-cpp ./gradlew assembleDebug
rm -rf app/.cxx app/build/intermediates/cxx
t 4-reconfigure-warm-ccache ./gradlew assembleDebug
# The profile flag changes the CMake arguments (and so the build dir), so it gets its own step.
rm -rf app/.cxx app/build/intermediates/cxx
t 5-reconfigure-with-cmake-trace ./gradlew assembleDebug -PdoomxrCmakeProfile="$PWD/build-cmake-configure-trace.json"

[ -x "$(command -v ccache)" ] && { echo "--- ccache stats"; ccache -s; } | tee -a "$out"
echo "cmake configure trace of step 5: build-cmake-configure-trace.json (ui.perfetto.dev)" | tee -a "$out"
echo "gradle profile reports: build/reports/profile/" | tee -a "$out"
