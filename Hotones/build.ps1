#!/usr/bin/env bash
set -euo pipefail

libs=(
    "/ucrt64/bin/libstdc++-6.dll"
    "/ucrt64/bin/libgcc_s_seh-1.dll"
    "/ucrt64/bin/libwinpthread-1.dll"
    "/ucrt64/bin/libraylib.dll"
    "/ucrt64/bin/lua54.dll"
    "/ucrt64/bin/glfw3.dll"
)

mkdir -p build

if [[ "${OS:-}" == "Windows_NT" ]]; then
    for lib in "${libs[@]}"; do
        if [[ ! -e "$lib" ]]; then
            printf 'Required library not found: %s\n' "$lib" >&2
            exit 1
        fi
        printf 'Copying %s to build directory...\n' "$lib"
        cp -f "$lib" build/
    done
fi

mkdir -p build/assets
cp -r -f ../assets/. build/assets/