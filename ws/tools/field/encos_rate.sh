#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "$script_dir/../../.." && pwd)"
build_dir="$project_root/build/encos_rate"
if [[ "${1:-}" == --prepare ]]; then
  [[ $# -eq 1 ]] || exit 2
  mkdir -p "$build_dir/config"
  cp "$project_root/ws/src/bringup/s100_bringup/config/canhal/config/"*.json "$build_dir/config/"
  python3 - "$build_dir/config/ipcf_channel.json" <<'PY'
import json,sys
p=sys.argv[1]
d=json.load(open(p))
d['channels'][0]['channel']['recv_timeout']=0
with open(p,'w') as f:json.dump(d,f,indent=2)
PY
  g++ -O2 -std=c++17 -Wall -Wextra -Wpedantic -DS100_RATE_CANHAL \
    -I/usr/hobot/include/canhal "$script_dir/encos_rate.cpp" \
    -L/usr/hobot/lib -Wl,-rpath,/usr/hobot/lib -lhbcanhal -pthread -o "$build_dir/encos_rate"
  echo 'Prepared only; no hardware I/O. Dedicated CAN5 RX timeout=0 (nonblocking).'
  exit 0
fi
[[ -x "$build_dir/encos_rate" ]] || { echo 'Run --prepare on S100 first' >&2; exit 1; }
cd "$build_dir"
profile=rt
previous=
for arg in "$@"; do
  if [[ "$previous" == --profile ]]; then profile="$arg"; fi
  previous="$arg"
done
sdk_level=5
[[ "$profile" != baseline ]] || sdk_level=2
for arg in "$@"; do
  if [[ "$arg" == --hardware ]]; then
    exec sudo env CAN_HAL_DEBUG_LEVEL="$sdk_level" CANHAL_LOG_LEVEL="$sdk_level" CAN_LOG_LEVEL="$sdk_level" "$build_dir/encos_rate" "$@"
  fi
done
exec "$build_dir/encos_rate" "$@"
