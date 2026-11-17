#!/usr/bin/env bash
# Source this file, do not execute it. ROS distribution must already be sourced.
s100_project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ ! -f "$s100_project_root/install/local_setup.bash" ]]; then
  echo '项目尚未构建，请先运行 bash ws/build_workspace.sh' >&2
  return 1
fi
source "$s100_project_root/install/local_setup.bash"
unset s100_project_root
