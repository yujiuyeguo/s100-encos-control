#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "$script_dir/../../.." && pwd)"
build_dir="$project_root/build/encos_jog"
if [[ "${1:-}" == --prepare ]]; then
  [[ $# -eq 1 ]] || exit 2
  mkdir -p "$build_dir/config"
  cp "$project_root/ws/src/bringup/s100_bringup/config/canhal/config/"*.json "$build_dir/config/"
  g++ -std=c++17 -Wall -Wextra -Wpedantic -DS100_ID_CANHAL \
    -I/usr/hobot/include/canhal "$script_dir/encos_jog.cpp" \
    -L/usr/hobot/lib -Wl,-rpath,/usr/hobot/lib -lhbcanhal -pthread -o "$build_dir/encos_jog"
  echo 'Prepared only; no hardware I/O.'
  exit 0
fi
[[ -x "$build_dir/encos_jog" ]] || { echo 'Run --prepare on S100 first' >&2; exit 1; }
cd "$build_dir"
for arg in "$@"; do
  if [[ "$arg" == --hardware ]]; then
    [[ "$(cat /sys/class/remoteproc/remoteproc_mcu1/alive)" == alive ]] || { echo 'MCU1 is not alive' >&2; exit 1; }
    exec sudo "$build_dir/encos_jog" "$@"
  fi
done
exec "$build_dir/encos_jog" "$@"
