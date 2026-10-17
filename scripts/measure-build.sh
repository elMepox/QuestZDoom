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

t 1-clean-full ./gradlew clean assembleDebug --profile
t 2-noop       ./gradlew assembleDebug
touch app/src/main/cpp/xr/xr_vr.cpp
t 3-touch-one-cpp ./gradlew assembleDebug
rm -rf app/.cxx app/build/intermediates/cxx
t 4-reconfigure-warm-ccache ./gradlew assembleDebug

[ -x "$(command -v ccache)" ] && { echo "--- ccache stats"; ccache -s; } | tee -a "$out"
echo "gradle profile reports: build/reports/profile/" | tee -a "$out"
