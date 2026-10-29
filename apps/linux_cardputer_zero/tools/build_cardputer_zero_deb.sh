#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../.." && pwd)"

work_root="${TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_WORK:-/tmp/trailmate-cardputer-zero-package}"
source_dir="${work_root}/source"
out_dir="${TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_OUT:-${repo_root}/build/cardputer-zero-deb}"
jobs="${TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_JOBS:-$(nproc 2>/dev/null || echo 4)}"
docker_platform="${TRAIL_MATE_CARDPUTER_ZERO_DOCKER_PLATFORM:-linux/arm64}"
builder_image="${TRAIL_MATE_CARDPUTER_ZERO_BUILDER_IMAGE:-trailmate-cardputer-zero-builder:bookworm-arm64}"
package_version="${TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_VERSION:-$(python3 "${repo_root}/scripts/project_version.py" --debian)}"
package_build_type="${TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_BUILD_TYPE:-Release}"
apt_mirror="${TRAIL_MATE_CARDPUTER_ZERO_APT_MIRROR:-}"
compose_project="${TRAIL_MATE_CARDPUTER_ZERO_COMPOSE_PROJECT:-trailmate-cardputer-zero-package}"
compose_file="${script_dir}/compose.cardputer-zero-builder.yml"
docker_cmd=()
compose_cmd=()
container_id=""

if [ -n "${TRAIL_MATE_DOCKER:-}" ] &&
   command -v "${TRAIL_MATE_DOCKER}" >/dev/null 2>&1 &&
   "${TRAIL_MATE_DOCKER}" info >/dev/null 2>&1; then
    docker_cmd=("${TRAIL_MATE_DOCKER}")
elif command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
    docker_cmd=(docker)
elif command -v docker.exe >/dev/null 2>&1 && docker.exe info >/dev/null 2>&1; then
    docker_cmd=(docker.exe)
else
    printf 'Docker is required for the Cardputer Zero arm64 package build, but the daemon is not reachable.\n' >&2
    exit 1
fi

if "${docker_cmd[@]}" compose version >/dev/null 2>&1; then
    compose_cmd=("${docker_cmd[@]}" compose)
elif command -v docker-compose >/dev/null 2>&1 && docker-compose version >/dev/null 2>&1; then
    compose_cmd=(docker-compose)
else
    printf 'Docker Compose is required for the Cardputer Zero package builder.\n' >&2
    printf 'Install the Docker Compose plugin or docker-compose, then rerun this helper.\n' >&2
    exit 1
fi

cleanup_container()
{
    if [ -n "${container_id}" ]; then
        "${docker_cmd[@]}" rm -f "${container_id}" >/dev/null 2>&1 || true
    fi
}
trap cleanup_container EXIT

rm -rf "${work_root}"
mkdir -p "${source_dir}" "${out_dir}"
rm -f "${out_dir}"/trailmate-cardputer-zero_*.deb

tar -C "${repo_root}" \
    --exclude='./.git' \
    --exclude='./.pio' \
    --exclude='./.platformio' \
    --exclude='./.tmp' \
    --exclude='./.codegraph' \
    --exclude='./.gitnexus' \
    --exclude='./build' \
    --exclude='./builds/linux_cmake/build' \
    --exclude='./out' \
    --exclude='./dist' \
    --exclude='./CMakeFiles' \
    --exclude='./CMakeCache.txt' \
    --exclude='./Makefile' \
    --exclude='./cmake_install.cmake' \
    --exclude='./*.o' \
    --exclude='./*.obj' \
    --exclude='./*.d' \
    --exclude='./*.a' \
    --exclude='./*.so' \
    --exclude='./*.deb' \
    --exclude='./*.changes' \
    --exclude='./*.buildinfo' \
    -cf - . | tar -C "${source_dir}" -xf -

find "${source_dir}" -type d -exec chmod 0755 {} +
find "${source_dir}" -type f -exec chmod 0644 {} +
find "${source_dir}/apps/linux_cardputer_zero/tools" -type f -name '*.sh' -exec chmod 0755 {} +
if [ -n "${TRAIL_MATE_TMAP_PRODUCER_DIR:-}" ]; then
    test -f "${TRAIL_MATE_TMAP_PRODUCER_DIR}/TmapTool"
    test -f "${TRAIL_MATE_TMAP_PRODUCER_DIR}/producer-manifest.json"
    mkdir -p "${source_dir}/.tmap-producer"
    cp -a "${TRAIL_MATE_TMAP_PRODUCER_DIR}/." "${source_dir}/.tmap-producer/"
fi

export TRAIL_MATE_CARDPUTER_ZERO_DOCKER_PLATFORM="${docker_platform}"
export TRAIL_MATE_CARDPUTER_ZERO_BUILDER_IMAGE="${builder_image}"
export TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_JOBS="${jobs}"
export TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_VERSION="${package_version}"
export TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_BUILD_TYPE="${package_build_type}"
export TRAIL_MATE_CARDPUTER_ZERO_APT_MIRROR="${apt_mirror}"

"${compose_cmd[@]}" -f "${compose_file}" -p "${compose_project}" build deb-builder

container_id="$("${compose_cmd[@]}" \
    -f "${compose_file}" \
    -p "${compose_project}" \
    run \
    -d \
    --no-deps \
    deb-builder \
    /bin/sleep infinity | tr -d '\r' | tail -n 1)"

if [ -z "${container_id}" ]; then
    printf 'Docker Compose did not return a builder container id.\n' >&2
    exit 1
fi

"${docker_cmd[@]}" exec "${container_id}" /bin/bash -c \
    'rm -rf /work/source /work/out && mkdir -p /work/source /work/build /work/out'

tar -C "${source_dir}" -cf - . |
    "${docker_cmd[@]}" exec -i "${container_id}" tar -C /work/source -xf -

"${docker_cmd[@]}" exec \
    -e TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_JOBS="${jobs}" \
    -e TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_VERSION="${package_version}" \
    -e GITHUB_REF -e GITHUB_REF_TYPE -e GITHUB_REF_NAME \
    -e TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_BUILD_TYPE="${package_build_type}" \
    "${container_id}" \
    /opt/trailmate/cardputer_zero_deb_package_entrypoint.sh

built_deb_name="$("${docker_cmd[@]}" exec "${container_id}" \
    sh -c 'find /work/out -maxdepth 1 -type f -name "trailmate-cardputer-zero_*.deb" -printf "%f\n" | head -n 1')"
if [ -z "${built_deb_name}" ]; then
    printf 'Docker package build finished without producing a .deb inside the container.\n' >&2
    exit 1
fi

"${docker_cmd[@]}" exec "${container_id}" tar -C /work/out -cf - "${built_deb_name}" |
    tar -C "${out_dir}" -xf -

mapfile -t built_debs < <(find "${out_dir}" -maxdepth 1 -type f -name 'trailmate-cardputer-zero_*.deb' -print)
if [ "${#built_debs[@]}" -lt 1 ]; then
    printf 'Docker package build finished without producing a .deb in %s\n' "${out_dir}" >&2
    exit 1
fi

printf '\nCopied Cardputer Zero arm64 package(s) to:\n'
printf '%s\n' "${built_debs[@]}"
