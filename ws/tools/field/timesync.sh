#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "$script_dir/../../.." && pwd)"
build_dir="$project_root/build/s100_timesync"
sdk_dir=/app/timesync_demo/sample_timesync

case "${1:---help}" in
  --prepare|--init-mcu|--run|--debug-on|--debug-off) mode="$1" ;;
  --help)
    cat <<'HELP'
Usage: bash ws/tools/field/timesync.sh MODE
  --prepare    Copy SDK sources/config to build/, apply local patch and compile. No hardware I/O.
  --init-mcu   Once after each fresh MCU1 start: select MCU Ethernet PPS and enable sync via IpcBox.
  --run        Run Acore PHC0 -> MCU RTC synchronization in foreground. Ctrl+C stops it.
  --debug-on   Enable MCU synchronization logs.
  --debug-off  Disable MCU synchronization logs.
Do not repeat --init-mcu in the same MCU1 boot. Restarting only --run does not require it.
HELP
    exit 0 ;;
  *) echo "Unknown mode: $1; use --help" >&2; exit 2 ;;
esac
[[ $# -eq 1 ]] || { echo 'Exactly one mode is required' >&2; exit 2; }

if [[ "$mode" == --prepare ]]; then
  [[ -f "$sdk_dir/timesync_sample.c" ]] || { echo 'S100 SDK sample not found' >&2; exit 1; }
  mkdir -p "$build_dir/sample"
  cp "$sdk_dir/"*.c "$sdk_dir/"*.h "$sdk_dir/Makefile" \
    "$sdk_dir/timesync_ipcf_config.json" "$build_dir/sample/"
  patch --batch -p1 -d "$build_dir/sample" < "$script_dir/timesync_sample.patch"
  make -C "$build_dir/sample" HOBOT_ROOTFS_DIR=/usr/hobot
  g++ -std=c++17 -Wall -Wextra -Wpedantic -I/usr/hobot/include \
    "$script_dir/mcu_timesync_control.cpp" \
    -L/usr/hobot/lib -Wl,-rpath,/usr/hobot/lib -lhbipcfhal -pthread \
    -o "$build_dir/mcu_timesync_control"
  echo 'Prepared; no MCU initialization or synchronization has been executed.'
  exit 0
fi

[[ -x "$build_dir/mcu_timesync_control" && -x "$build_dir/sample/timesync_sample" ]] || {
  echo 'Run --prepare on the S100 first' >&2; exit 1;
}
[[ "$(cat /sys/class/remoteproc/remoteproc_mcu1/alive)" == alive ]] || {
  echo 'MCU1 is not alive; start the matching firmware first' >&2; exit 1;
}

case "$mode" in
  --init-mcu)
    echo 'Initialize once per fresh MCU1 start. Do not repeat while the MCU1 instance is running.'
    exec sudo "$build_dir/mcu_timesync_control" init-mcu ;;
  --debug-on) exec sudo "$build_dir/mcu_timesync_control" debug-on ;;
  --debug-off) exec sudo "$build_dir/mcu_timesync_control" debug-off ;;
  --run)
    cd "$build_dir/sample"
    # Host -P4 and MCU PPS10 both select MCU Ethernet PPS. Do not use RTC PPS0.
    exec sudo env LD_LIBRARY_PATH=/usr/hobot/lib \
      ./timesync_sample -p 0 -r -P 4 -s 0 -l 15 ;;
esac
