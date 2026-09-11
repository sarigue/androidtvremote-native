#!/usr/bin/env bash
set -u

ok=0
warn=0

check_cmd() {
    local cmd="$1"
    local label="$2"
    if command -v "$cmd" >/dev/null 2>&1; then
        printf 'OK   %-34s %s\n' "$label" "$(command -v "$cmd")"
    else
        printf 'MISS %-34s %s\n' "$label" "$cmd"
        warn=1
    fi
}

check_var() {
    local var="$1"
    local label="$2"
    if [ -n "${!var:-}" ]; then
        printf 'OK   %-34s %s\n' "$label" "${!var}"
    else
        printf 'INFO %-34s not set\n' "$label"
    fi
}

check_cmd cmake CMake
check_cmd ninja Ninja
check_cmd g++ 'Linux x86_64 C++ compiler'
check_cmd aarch64-linux-gnu-g++ 'Linux ARM64 cross compiler'
check_cmd x86_64-w64-mingw32-g++ 'Windows x86_64 MinGW compiler'

if [ -n "${LLVM_MINGW_ROOT:-}" ]; then
    check_cmd "$LLVM_MINGW_ROOT/bin/aarch64-w64-mingw32-clang++" 'Windows ARM64 llvm-mingw compiler'
else
    printf 'INFO %-34s not set (required for Windows ARM64)\n' LLVM_MINGW_ROOT
fi

check_cmd o64-clang++ 'osxcross macOS x86_64 compiler'
check_cmd oa64-clang++ 'osxcross macOS ARM64 compiler'

check_var OPENSSL_LINUX_ARM64_ROOT 'OpenSSL Linux ARM64 root'
check_var OPENSSL_WINDOWS_X86_64_ROOT 'OpenSSL Windows x86_64 root'
check_var OPENSSL_WINDOWS_ARM64_ROOT 'OpenSSL Windows ARM64 root'
check_var OPENSSL_MACOS_X86_64_ROOT 'OpenSSL macOS x86_64 root'
check_var OPENSSL_MACOS_ARM64_ROOT 'OpenSSL macOS ARM64 root'
check_var MACOSX_SDK 'macOS SDK for osxcross'

exit "$ok"
