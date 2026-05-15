#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
IMAGE_TAG="${DEV_CONTAINER_IMAGE:-trailmate-cardputer-zero/dev-simulator:trixie}"
BUILD_TYPE="${DEV_CONTAINER_BUILD_TYPE:-Debug}"
BUILD_VOLUME_NAME="${DEV_CONTAINER_BUILD_VOLUME:-trailmate-cardputer-zero-dev-build}"
CONTAINER_BUILD_ROOT="${DEV_CONTAINER_BUILD_ROOT:-/container-build}"
CONTAINER_BUILD_DIR="${DEV_CONTAINER_BUILD_DIR:-$CONTAINER_BUILD_ROOT/simulator}"
MODE="${DEV_CONTAINER_MODE:-simulator}"
SKIP_IMAGE_BUILD="${DEV_CONTAINER_SKIP_BUILD:-0}"
PLATFORM="${DEV_CONTAINER_PLATFORM:-}"
CONTAINER_SD_ROOT="${DEV_CONTAINER_CONTAINER_SD_ROOT:-/trailmate-sdroot}"
CONTAINER_SETTINGS_ROOT="${DEV_CONTAINER_CONTAINER_SETTINGS_ROOT:-/trailmate-settings-root}"
CONTAINER_GPS_NMEA_FILE="${DEV_CONTAINER_CONTAINER_GPS_NMEA_FILE:-/trailmate-gps-input.nmea}"

require_command() {
  local command_name="$1"
  if command -v "$command_name" >/dev/null 2>&1; then
    return 0
  fi

  printf 'Missing required command: %s\n' "$command_name" >&2
  if [[ "$command_name" == "docker" ]]; then
    printf 'This workflow expects Docker to be available inside WSL.\n' >&2
    printf 'Enable Docker Desktop WSL integration for your distro, then rerun the script.\n' >&2
  fi
  exit 1
}

ensure_docker_available() {
  if docker version >/dev/null 2>&1; then
    return 0
  fi

  printf 'Docker is installed but not usable from the current environment.\n' >&2
  printf 'On Windows + WSL, enable Docker Desktop WSL integration for your distro first.\n' >&2
  exit 1
}

print_step() {
  local message="$1"
  printf '\n==> %s\n' "$message"
}

resolve_host_path_env() {
  local converted_name="$1"
  local direct_name="$2"
  local value="${!converted_name:-}"
  if [[ -n "$value" ]]; then
    printf '%s' "$value"
    return 0
  fi

  value="${!direct_name:-}"
  if [[ "$value" == /* ]]; then
    printf '%s' "$value"
    return 0
  fi

  return 1
}

append_env_if_present() {
  local name="$1"
  if [[ -n "${!name:-}" ]]; then
    RUN_ARGS+=(-e "${name}=${!name}")
  fi
}

simulator_args=()
while (( $# > 0 )); do
  case "$1" in
    --shell)
      MODE="shell"
      shift
      ;;
    --skip-image-build)
      SKIP_IMAGE_BUILD=1
      shift
      ;;
    --)
      shift
      simulator_args=("$@")
      break
      ;;
    *)
      simulator_args+=("$1")
      shift
      ;;
  esac
done

require_command docker
ensure_docker_available

if (( ! SKIP_IMAGE_BUILD )); then
  "$ROOT_DIR/scripts/build-dev-container.sh"
fi

GUI_ARGS=()

if [[ -d /mnt/wslg ]]; then
  GUI_ARGS+=(-v /mnt/wslg:/mnt/wslg)
fi

if [[ -d /tmp/.X11-unix ]]; then
  GUI_ARGS+=(-v /tmp/.X11-unix:/tmp/.X11-unix)
fi

for environment_name in DISPLAY WAYLAND_DISPLAY XDG_RUNTIME_DIR PULSE_SERVER SDL_AUDIODRIVER SDL_VIDEODRIVER; do
  if [[ -n "${!environment_name:-}" ]]; then
    GUI_ARGS+=(-e "${environment_name}=${!environment_name}")
  fi
done

if [[ -d /mnt/wslg && -z "${XDG_RUNTIME_DIR:-}" ]]; then
  GUI_ARGS+=(-e XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir)
fi

RUN_ARGS=(
  run
  --rm
  -v "$REPO_ROOT:/workspace/trail-mate"
  -v "$BUILD_VOLUME_NAME:$CONTAINER_BUILD_ROOT"
  -w /workspace/trail-mate/apps/linux_sim
  -e "TRAIL_MATE_CONTAINER_BUILD_TYPE=$BUILD_TYPE"
  -e "TRAIL_MATE_CONTAINER_BUILD_DIR=$CONTAINER_BUILD_DIR"
)

for env_name in \
  TRAIL_MATE_GPS_VALID \
  TRAIL_MATE_GPS_ENABLED \
  TRAIL_MATE_GPS_POWERED \
  TRAIL_MATE_GPS_LAT \
  TRAIL_MATE_GPS_LNG \
  TRAIL_MATE_GPS_ALT_M \
  TRAIL_MATE_GPS_SPEED_MPS \
  TRAIL_MATE_GPS_COURSE_DEG \
  TRAIL_MATE_GPS_SATS \
  TRAIL_MATE_GPS_HDOP \
  TRAIL_MATE_GPS_FIX \
  TRAIL_MATE_GPS_BAUD; do
  append_env_if_present "$env_name"
done

if host_sd_root="$(resolve_host_path_env DEV_CONTAINER_TRAIL_MATE_SD_ROOT_HOST TRAIL_MATE_SD_ROOT)"; then
  if [[ ! -d "$host_sd_root" ]]; then
    printf 'Configured host SD root does not exist: %s\n' "$host_sd_root" >&2
    exit 1
  fi
  RUN_ARGS+=(-v "$host_sd_root:$CONTAINER_SD_ROOT:ro")
  RUN_ARGS+=(-e "TRAIL_MATE_SD_ROOT=$CONTAINER_SD_ROOT")
fi

if host_settings_root="$(resolve_host_path_env DEV_CONTAINER_TRAIL_MATE_SETTINGS_ROOT_HOST TRAIL_MATE_SETTINGS_ROOT)"; then
  mkdir -p "$host_settings_root"
  RUN_ARGS+=(-v "$host_settings_root:$CONTAINER_SETTINGS_ROOT")
  RUN_ARGS+=(-e "TRAIL_MATE_SETTINGS_ROOT=$CONTAINER_SETTINGS_ROOT")
fi

if host_nmea_file="$(resolve_host_path_env DEV_CONTAINER_TRAIL_MATE_GPS_NMEA_FILE_HOST TRAIL_MATE_GPS_NMEA_FILE)"; then
  if [[ ! -f "$host_nmea_file" ]]; then
    printf 'Configured host GPS NMEA file does not exist: %s\n' "$host_nmea_file" >&2
    exit 1
  fi
  RUN_ARGS+=(-v "$host_nmea_file:$CONTAINER_GPS_NMEA_FILE:ro")
  RUN_ARGS+=(-e "TRAIL_MATE_GPS_NMEA_FILE=$CONTAINER_GPS_NMEA_FILE")
fi

if [[ -n "${TRAIL_MATE_GPS_DEVICE:-}" ]]; then
  RUN_ARGS+=(-e "TRAIL_MATE_GPS_DEVICE=${TRAIL_MATE_GPS_DEVICE}")
  if [[ "${TRAIL_MATE_GPS_DEVICE}" == /dev/* && -e "${TRAIL_MATE_GPS_DEVICE}" ]]; then
    RUN_ARGS+=(--device "${TRAIL_MATE_GPS_DEVICE}:${TRAIL_MATE_GPS_DEVICE}")
  fi
fi

if [[ "$MODE" == "shell" ]]; then
  RUN_ARGS+=(-it)
elif [[ -t 0 && -t 1 ]]; then
  RUN_ARGS+=(-it)
fi

if command -v id >/dev/null 2>&1 && [[ "$REPO_ROOT" != /mnt/* ]]; then
  RUN_ARGS+=(--user "$(id -u):$(id -g)")
fi

if [[ -n "$PLATFORM" ]]; then
  RUN_ARGS+=(--platform "$PLATFORM")
fi

RUN_ARGS+=("${GUI_ARGS[@]}")

print_step "Launching development container in $MODE mode"

if [[ "$MODE" == "shell" ]]; then
  docker "${RUN_ARGS[@]}" "$IMAGE_TAG" bash
  exit 0
fi

docker "${RUN_ARGS[@]}" "$IMAGE_TAG" bash -lc '
  set -euo pipefail
  cmake -S . -B "$TRAIL_MATE_CONTAINER_BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$TRAIL_MATE_CONTAINER_BUILD_TYPE" \
    -DBUILD_TESTING=ON
  cmake --build "$TRAIL_MATE_CONTAINER_BUILD_DIR" --target trailmate_cardputer_zero_simulator --config "$TRAIL_MATE_CONTAINER_BUILD_TYPE"
  exec "$TRAIL_MATE_CONTAINER_BUILD_DIR/trailmate_cardputer_zero_simulator" "$@"
' bash "${simulator_args[@]}"
