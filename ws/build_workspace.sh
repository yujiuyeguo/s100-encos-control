#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"
if [[ -z "${AMENT_PREFIX_PATH:-}" ]]; then
  echo '请先 source /opt/ros/<发行版>/setup.bash，再运行此脚本。' >&2
  exit 1
fi
colcon build --base-paths ws/src \
  --build-base build --install-base install \
  --event-handlers console_direct+ "$@"
