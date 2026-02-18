# if -compile is provided, use it.
if [[ "${1:-}" == "-compile" ]]; then
    shift
    g++ -std=c++20 $(find src -name "*.cpp" -print | tr '\n' ' ') -Isrc/include -DWINDOWS_UCRT64 -lraylib -llua -o build/Hotones
    exit 0
fi

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
# if meow is not on path, use the local copy and if -- compile wasn't provided, use meow to build the project.
if ! command -v meow &> /dev/null; then
    echo "meow could not be found, using local copy"
    ../../meow/publish/win-x64/meow build
else
    meow build
fi