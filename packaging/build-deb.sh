#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "${script_dir}/.." && pwd)"
build_dir="${BUILD_DIR:-${project_dir}/build}"
build_type="${BUILD_TYPE:-Release}"

if command -v nproc >/dev/null 2>&1; then
    jobs="$(nproc)"
else
    jobs=1
fi

echo "Configuring ${build_type} build in ${build_dir}"
cmake -S "${project_dir}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE="${build_type}"

echo "Building ChessGui"
cmake --build "${build_dir}" --parallel "${jobs}"

echo "Generating Debian package"
cpack --config "${build_dir}/CPackConfig.cmake" \
    --working-directory "${project_dir}" \
    -G DEB

echo "Debian package generated in ${project_dir}"
