#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "${script_dir}/.." && pwd)"
repo_root="$(cd -- "${project_dir}/../.." && pwd)"
lvgl_commit="85aa60d18b3d5e5588d7b247abf90198f07c8a63"

if command -v getent >/dev/null 2>&1 && command -v id >/dev/null 2>&1; then
    detected_home="$(getent passwd "$(id -un)" | cut -d: -f6 || true)"
    if [[ -n "${detected_home}" && -d "${detected_home}" ]]; then
        export HOME="${detected_home}"
    fi
fi

if [[ -n "${HOME:-}" && -d "${HOME}/.local/bin" ]]; then
    export PATH="${HOME}/.local/bin:${PATH}"
fi

if [[ -z "${TRAIL_MATE_M5STACK_LINUX_LIBS_PATH:-}" && -d "${repo_root}/.tmp/M5Stack_Linux_Libs" ]]; then
    export TRAIL_MATE_M5STACK_LINUX_LIBS_PATH="${repo_root}/.tmp/M5Stack_Linux_Libs"
fi

if ! command -v scons >/dev/null 2>&1; then
    echo "scons not found in PATH. Install it first, for example with pip3 install --user scons." >&2
    exit 1
fi

prepare_lvgl_sdk_layout() {
    if [[ -z "${TRAIL_MATE_M5STACK_LINUX_LIBS_PATH:-}" ]]; then
        return 0
    fi

    local sdk_root="${TRAIL_MATE_M5STACK_LINUX_LIBS_PATH}"
    local repo_cache="${sdk_root}/github_source"
    local archive_path="${repo_cache}/lvgl-${lvgl_commit}.zip"
    local extracted_path="${repo_cache}/lvgl-${lvgl_commit}"
    local sdk_lvgl_root="${repo_cache}/lvgl/lvgl_9_5"
    local sdk_lvgl_path="${sdk_lvgl_root}/lvgl"

    if [[ -d "${sdk_lvgl_path}" ]]; then
        return 0
    fi

    mkdir -p "${sdk_lvgl_root}"

    if [[ ! -d "${extracted_path}" && -f "${archive_path}" ]]; then
        python3 - <<'PY' "${archive_path}" "${repo_cache}"
import sys
import zipfile

archive_path = sys.argv[1]
destination = sys.argv[2]
with zipfile.ZipFile(archive_path, "r") as archive:
    archive.extractall(destination)
PY
    fi

    if [[ -d "${extracted_path}" ]]; then
        mv "${extracted_path}" "${sdk_lvgl_path}"
        return 0
    fi

    if [[ -d "${sdk_lvgl_root}" && ! -d "${sdk_lvgl_path}" ]]; then
        shopt -s nullglob dotglob
        local existing=("${sdk_lvgl_root}"/*)
        shopt -u nullglob dotglob
        if (( ${#existing[@]} > 0 )); then
            mkdir -p "${sdk_lvgl_path}"
            for entry in "${existing[@]}"; do
                if [[ "${entry}" != "${sdk_lvgl_path}" ]]; then
                    mv "${entry}" "${sdk_lvgl_path}/"
                fi
            done
        fi
    fi
}

prepare_lvgl_sdk_layout

cd "${project_dir}"
scons "$@"
