#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <linux|windows|macos> <x86_64|arm64> <static|shared>" >&2
    exit 2
fi

OS="$1"
ARCH="$2"
KIND="$3"

case "$OS:$ARCH:$KIND" in
    linux:x86_64:static|linux:x86_64:shared|linux:arm64:static|linux:arm64:shared|\
    windows:x86_64:static|windows:x86_64:shared|windows:arm64:static|windows:arm64:shared|\
    macos:x86_64:static|macos:x86_64:shared|macos:arm64:static|macos:arm64:shared) ;;
    *) echo "Unsupported target: $OS/$ARCH/$KIND" >&2; exit 2 ;;
esac

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${BUILD_ROOT:-$ROOT/build-cross}"
DIST="${DIST:-$ROOT/dist}"
CMAKE_BIN="${CMAKE:-cmake}"
GENERATOR="${GENERATOR:-Ninja}"
BUILD_DIR="$BUILD_ROOT/$OS-$ARCH-$KIND"

mkdir -p "$DIST"
rm -rf "$BUILD_DIR"

shared=OFF
if [ "$KIND" = shared ]; then
    shared=ON
fi

args=(
    -S "$ROOT"
    -B "$BUILD_DIR"
    -G "$GENERATOR"
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS="$shared"
    -DANDROIDTVREMOTE_BUILD_EXAMPLES=OFF
    -DANDROIDTVREMOTE_BUILD_TESTS=OFF
)

case "$OS:$ARCH" in
    linux:x86_64)
        ;;
    linux:arm64)
        args+=( -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/linux-arm64.cmake" )
        ;;
    windows:x86_64)
        args+=( -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/windows-x86_64.cmake" )
        ;;
    windows:arm64)
        args+=( -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/windows-arm64.cmake" )
        ;;
    macos:x86_64)
        args+=( -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/macos-x86_64.cmake" )
        ;;
    macos:arm64)
        args+=( -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/macos-arm64.cmake" )
        ;;
esac

upper_os="$(printf '%s' "$OS" | tr '[:lower:]' '[:upper:]')"
upper_arch="$(printf '%s' "$ARCH" | tr '[:lower:]' '[:upper:]')"
root_var="OPENSSL_${upper_os}_${upper_arch}_ROOT"
openssl_root="${!root_var:-}"
if [ "$OS:$ARCH" != "linux:x86_64" ] && [ -z "$openssl_root" ]; then
    echo "$root_var must point to an OpenSSL installation built for $OS/$ARCH" >&2
    exit 2
fi
if [ -n "$openssl_root" ]; then
    args+=( -DOPENSSL_ROOT_DIR="$openssl_root" )
fi

if [ "$OS" = macos ] && [ -z "${MACOSX_SDK:-}" ]; then
    echo "MACOSX_SDK must point to the macOS SDK used by osxcross" >&2
    exit 2
fi

if [ -n "${BOOST_ROOT:-}" ]; then
    args+=( -DBOOST_ROOT="$BOOST_ROOT" )
fi
if [ -n "${BOOST_INCLUDEDIR:-}" ]; then
    args+=( -DBOOST_INCLUDEDIR="$BOOST_INCLUDEDIR" )
fi

"$CMAKE_BIN" "${args[@]}"
"$CMAKE_BIN" --build "$BUILD_DIR" --parallel "${JOBS:-2}"

find_one() {
    local pattern="$1"
    find "$BUILD_DIR" -type f -name "$pattern" -print | head -n1
}

copy_required() {
    local src="$1"
    local dst="$2"
    if [ -z "$src" ] || [ ! -e "$src" ]; then
        echo "Expected build artifact not found for $OS/$ARCH/$KIND" >&2
        exit 1
    fi
    cp -L "$src" "$dst"
    printf '%s\n' "$dst"
}

case "$OS:$KIND" in
    linux:static)
        src="$(find_one 'libandroidtvremote.a')"
        copy_required "$src" "$DIST/libandroidtvremote-${ARCH}.lin.a"
        ;;
    linux:shared)
        src="$(find_one 'libandroidtvremote.so')"
        if [ -z "$src" ]; then
            src="$(find "$BUILD_DIR" -type f -name 'libandroidtvremote.so.*' -print | sort -V | tail -n1)"
        fi
        copy_required "$src" "$DIST/libandroidtvremote-${ARCH}.so"
        ;;
    windows:static)
        src="$(find_one 'libandroidtvremote.a')"
        if [ -z "$src" ]; then
            src="$(find_one 'androidtvremote.lib')"
        fi
        copy_required "$src" "$DIST/libandroidtvremote-${ARCH}.win.a"
        ;;
    windows:shared)
        src="$(find "$BUILD_DIR" -type f \( -name 'androidtvremote.dll' -o -name 'libandroidtvremote.dll' \) -print | head -n1)"
        copy_required "$src" "$DIST/androidtvremote-${ARCH}.dll"
        import_lib="$(find_one 'libandroidtvremote.dll.a')"
        copy_required "$import_lib" "$DIST/libandroidtvremote-${ARCH}.win.dll.a"
        ;;
    macos:static)
        src="$(find_one 'libandroidtvremote.a')"
        copy_required "$src" "$DIST/libandroidtvremote-${ARCH}.mac.a"
        ;;
    macos:shared)
        src="$(find_one 'libandroidtvremote.dylib')"
        if [ -z "$src" ]; then
            src="$(find "$BUILD_DIR" -type f -name 'libandroidtvremote.*.dylib' -print | sort -V | tail -n1)"
        fi
        copy_required "$src" "$DIST/libandroidtvremote-${ARCH}.dylib"
        ;;
esac
