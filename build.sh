#!/bin/bash
# Build wrapper for StremioNX (run from MSYS2).
# Fixes a CMake bug where the "Unix Makefiles" generator prepends the object
# directory to absolute Windows paths (e.g. "CMakeFiles/X.dir/C:/Users/...")
# in compiler_depend.make, which GNU make rejects. Run from MSYS2 bash:
#   bash build.sh          # full build
#   bash build.sh StremioNX.nro   # only the .nro package
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build"
cd "$BUILD"

# make regenerates compiler_depend.make while compiling objects, and CMake's
# post-processing prepends the object dir to drive-letter paths again. Retry
# the sed fix + make until no regeneration is left.
for attempt in 1 2 3; do
    find . -name 'compiler_depend.make' -exec sed -i -E 's#(^|[[:space:]])[^[:space:]]*/([A-Za-z]):#\1\2:#g' {} +

    # The .nro custom target has no sources, so CMake never writes its dep file.
    : > CMakeFiles/StremioNX.nro.dir/compiler_depend.make

    if make "$@"; then
        exit 0
    fi
    echo "compiler_depend.make regenerated with mangled paths; retrying (${attempt})..."
done
echo "build.sh: giving up after 3 attempts"
exit 1