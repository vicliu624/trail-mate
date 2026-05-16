#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_TAG="${DEV_CONTAINER_IMAGE:-trailmate-cardputer-zero/dev-simulator:trixie}"
DOCKERFILE_PATH="${DEV_CONTAINER_DOCKERFILE:-$ROOT_DIR/docker/dev-simulator.Dockerfile}"
PLATFORM="${DEV_CONTAINER_PLATFORM:-}"

require_command() {
  local command_name="$1"
  if command -v "$command_name" >/dev/null 2>&1; then
    return 0
  fi

  printf 'Missing required command: %s\n' "$command_name" >&2
  if [[ "$command_name" == "docker" ]]; then
    printf 'Install Docker Desktop and enable Linux containers before rerunning this script.\n' >&2
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

require_command docker
ensure_docker_available

BUILD_ARGS=(
  build
  -f "$DOCKERFILE_PATH"
  -t "$IMAGE_TAG"
)

if [[ -n "$PLATFORM" ]]; then
  BUILD_ARGS+=(--platform "$PLATFORM")
fi

BUILD_ARGS+=("$ROOT_DIR")

print_step "Building development container image $IMAGE_TAG"
docker "${BUILD_ARGS[@]}"

printf '\nContainer image build completed successfully.\n'
printf '  Image tag: %s\n' "$IMAGE_TAG"
if [[ -n "$PLATFORM" ]]; then
  printf '  Requested platform: %s\n' "$PLATFORM"
fi
