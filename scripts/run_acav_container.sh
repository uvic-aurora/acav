#!/usr/bin/env bash

#$!{
# Aurora Clang AST Viewer (ACAV)
# 
# Copyright (c) 2026 Min Liu
# Copyright (c) 2026 Michael David Adams
# 
# SPDX-License-Identifier: GPL-2.0-or-later
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <https://www.gnu.org/licenses/>.
#}$!

set -euo pipefail

# Simple, cross-platform runner for the ACAV container.

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
desktop_script="$script_dir/print_desktop_options"

panic() {
  echo "ERROR: $1" >&2
  exit "${2:-1}"
}

# Resolve an image name, handling podman's localhost/ prefix.
# Tries the given name, then with localhost/ prepended, then with it stripped.
resolve_image() {
  local name="$1"
  local bare="${name#localhost/}"
  if "$runtime" image inspect "$bare" >/dev/null 2>&1; then
    echo "$bare"
  elif "$runtime" image inspect "localhost/$bare" >/dev/null 2>&1; then
    echo "localhost/$bare"
  fi
}

usage() {
  cat <<'USAGE'
Usage: scripts/run_acav_container.sh [options] [-- command...]

Options:
  -r, --runtime docker|podman  Container runtime (default: auto-detect)
  -i, --image IMAGE            Image tag (default: $ACAV_IMAGE or acav:latest)
  -t, --tar PATH               Load image from tar if missing
  --shell                      Start an interactive bash shell in the container
  --no-desktop                 Skip X11/Wayland desktop options
  --workspace [PATH]           Mount PATH at the same path and set workdir (default: cwd)
  --user                       Run as the current user (id -u:id -g)
  --run-opt TOKEN              Extra runtime option (repeatable, one token each)
  --dry-run                    Print the final command and exit
  -h, --help                   Show this help

Examples:
  scripts/run_acav_container.sh -- acav -c /opt/acav/examples/compile_commands.json
  scripts/run_acav_container.sh --workspace --user -- make-ast --help
  scripts/run_acav_container.sh -i acav:v1.0.0 -t acav-v1.0.0.tar.gz -- acav --help
  scripts/run_acav_container.sh --shell
USAGE
}

# ------------------------------------------------------------
# Defaults
# ------------------------------------------------------------

runtime="${ACAV_RUNTIME:-}"
image="${ACAV_IMAGE:-acav:latest}"
tar_path=""
shell_mode=0
no_desktop=0
mount_workspace=0
workspace_path=""
as_user=0
dry_run=0
run_opts=()

# ------------------------------------------------------------
# Parse args
# ------------------------------------------------------------

while [[ $# -gt 0 ]]; do
  case "$1" in
    -r|--runtime)
      runtime="${2:-}"
      shift 2
      ;;
    -i|--image)
      image="${2:-}"
      shift 2
      ;;
    -t|--tar|--load)
      tar_path="${2:-}"
      shift 2
      ;;
    --shell)
      shell_mode=1
      shift
      ;;
    --no-desktop)
      no_desktop=1
      shift
      ;;
    --workspace)
      mount_workspace=1
      if [[ $# -ge 2 && "${2:-}" != "--" && "${2:-}" != -* ]]; then
        workspace_path="${2:-}"
        shift 2
      else
        shift
      fi
      ;;
    --user)
      as_user=1
      shift
      ;;
    --run-opt)
      run_opts+=("${2:-}")
      shift 2
      ;;
    --dry-run)
      dry_run=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    *)
      break
      ;;
  esac
done

cmd=("$@")

# ------------------------------------------------------------
# Runtime + image checks
# ------------------------------------------------------------

detect_runtime() {
  if [[ -n "$runtime" ]]; then
    echo "$runtime"
    return
  fi
  if command -v docker >/dev/null 2>&1; then
    echo docker
    return
  fi
  if command -v podman >/dev/null 2>&1; then
    echo podman
    return
  fi
  echo docker
}

runtime="$(detect_runtime)"
command -v "$runtime" >/dev/null 2>&1 || panic "container runtime not found: $runtime"

resolved="$(resolve_image "$image")"
if [[ -z "$resolved" ]]; then
  if [[ -n "$tar_path" ]]; then
    "$runtime" load -i "$tar_path"
    resolved="$(resolve_image "$image")"
    [[ -n "$resolved" ]] || panic "failed to find image $image after loading $tar_path"
  else
    panic "image not found: $image (use --tar to load a local archive)"
  fi
fi
image="$resolved"

# ------------------------------------------------------------
# Build runtime options
# ------------------------------------------------------------

# Interactive when running in a TTY
if [[ -t 0 && -t 1 ]]; then
  run_opts+=(-it)
fi
run_opts+=(--rm)

# SELinux (Fedora/RHEL) workaround
if command -v getenforce >/dev/null 2>&1; then
  selinux_state="$(getenforce 2>/dev/null || true)"
  if [[ -n "$selinux_state" && "$selinux_state" != "Disabled" ]]; then
    run_opts+=(--security-opt label=disable)
  fi
fi

# Mount workspace at the same path inside the container
if [[ "$mount_workspace" -eq 1 ]]; then
  if [[ -n "$workspace_path" ]]; then
    [[ -d "$workspace_path" ]] || panic "workspace path not found: $workspace_path"
    workspace="$(cd "$workspace_path" && pwd)"
  else
    workspace="$(pwd)"
  fi
  run_opts+=(--volume "$workspace:$workspace:rw" --workdir "$workspace")
fi

# Run as the current user
if [[ "$as_user" -eq 1 ]]; then
  run_opts+=(--user "$(id -u):$(id -g)")
fi

# Desktop options (X11/Wayland)
if [[ "$no_desktop" -eq 0 && -x "$desktop_script" ]]; then
  while IFS= read -r token; do
    [[ -n "$token" ]] && run_opts+=("$token")
  done < <("$desktop_script")
fi

# Default to bash when --shell is used and no command is provided
if [[ "${#cmd[@]}" -eq 0 && "$shell_mode" -eq 1 ]]; then
  cmd=(bash)
fi

# ------------------------------------------------------------
# Execute
# ------------------------------------------------------------

if [[ "$dry_run" -eq 1 ]]; then
  printf '%q ' "$runtime" run "${run_opts[@]}" "$image" "${cmd[@]}"
  printf '\n'
  exit 0
fi

exec "$runtime" run "${run_opts[@]}" "$image" "${cmd[@]}"
