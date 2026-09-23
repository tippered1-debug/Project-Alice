#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/build/runtime-verification"
if [[ -n "${CMAKE_BUILD_PARALLEL_LEVEL:-}" ]]; then
	parallel_jobs="${CMAKE_BUILD_PARALLEL_LEVEL}"
elif [[ "${GITHUB_ACTIONS:-}" == "true" ]]; then
	parallel_jobs=2
else
	parallel_jobs=1
fi

# The developer's interactive machine should stay responsive during the large
# Alice translation unit. GitHub runners use the regular scheduler priority.
low_priority="false"
if [[ "$(uname -s)" == "Darwin" && "${GITHUB_ACTIONS:-}" != "true" ]]; then
	low_priority="true"
fi

build_target() {
	if [[ "${low_priority}" == "true" ]]; then
		nice -n 15 cmake --build "${build_dir}" --parallel "${parallel_jobs}" --target "$1"
	else
		cmake --build "${build_dir}" --parallel "${parallel_jobs}" --target "$1"
	fi
}

run_ctest() {
	if [[ "${low_priority}" == "true" ]]; then
		nice -n 15 ctest --test-dir "${build_dir}" --output-on-failure --no-tests=error
	else
		ctest --test-dir "${build_dir}" --output-on-failure --no-tests=error
	fi
}

# Keep this path reproducible: never reuse a binary or generated build graph
# from an earlier checkout/run.
rm -rf -- "${build_dir}"

cmake -S "${repo_root}" -B "${build_dir}" \
	-DCMAKE_BUILD_TYPE=Debug \
	-DBUILD_TESTING=ON

build_target Alice
build_target tests_project
run_ctest
