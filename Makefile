SHELL := /usr/bin/env bash

.PHONY: help doctor all linux windows macos \
        linux-x86 linux-arm windows-x86 windows-arm mac-x86 mac-arm \
        linux-x86-static linux-x86-shared linux-arm-static linux-arm-shared \
        windows-x86-static windows-x86-shared windows-arm-static windows-arm-shared \
        mac-x86-static mac-x86-shared mac-arm-static mac-arm-shared \
        native-test clean distclean

JOBS ?= 2
export JOBS

help:
	@printf '%s\n' \
	  'libandroidtvremote build targets:' \
	  '' \
	  '  make native-test           Build + run tests on the current Linux host' \
	  '  make linux                 Linux x86_64 + ARM64, static + shared' \
	  '  make windows               Windows x86_64 + ARM64, static + DLL' \
	  '  make macos                 macOS x86_64 + ARM64, static + dylib' \
	  '  make all                   All 12 requested libraries' \
	  '' \
	  '  make linux-x86-static      -> dist/libandroidtvremote-x86_64.lin.a' \
	  '  make linux-x86-shared      -> dist/libandroidtvremote-x86_64.so' \
	  '  make linux-arm-static      -> dist/libandroidtvremote-arm64.lin.a' \
	  '  make linux-arm-shared      -> dist/libandroidtvremote-arm64.so' \
	  '  make windows-x86-static    -> dist/libandroidtvremote-x86_64.win.a' \
	  '  make windows-x86-shared    -> dist/androidtvremote-x86_64.dll + .dll.a' \
	  '  make windows-arm-static    -> dist/libandroidtvremote-arm64.win.a' \
	  '  make windows-arm-shared    -> dist/androidtvremote-arm64.dll + .dll.a' \
	  '  make mac-x86-static        -> dist/libandroidtvremote-x86_64.mac.a' \
	  '  make mac-x86-shared        -> dist/libandroidtvremote-x86_64.dylib' \
	  '  make mac-arm-static        -> dist/libandroidtvremote-arm64.mac.a' \
	  '  make mac-arm-shared        -> dist/libandroidtvremote-arm64.dylib' \
	  '' \
	  'Aliases x86 = x86_64 and arm = ARM64.' \
	  'Run make doctor to see which cross toolchains/dependency roots are available.'

doctor:
	@./scripts/doctor.sh

all: linux windows macos

linux: linux-x86 linux-arm
windows: windows-x86 windows-arm
macos: mac-x86 mac-arm

linux-x86: linux-x86-static linux-x86-shared
linux-arm: linux-arm-static linux-arm-shared
windows-x86: windows-x86-static windows-x86-shared
windows-arm: windows-arm-static windows-arm-shared
mac-x86: mac-x86-static mac-x86-shared
mac-arm: mac-arm-static mac-arm-shared

linux-x86-static:
	@./scripts/build-one.sh linux x86_64 static
linux-x86-shared:
	@./scripts/build-one.sh linux x86_64 shared
linux-arm-static:
	@./scripts/build-one.sh linux arm64 static
linux-arm-shared:
	@./scripts/build-one.sh linux arm64 shared

windows-x86-static:
	@./scripts/build-one.sh windows x86_64 static
windows-x86-shared:
	@./scripts/build-one.sh windows x86_64 shared
windows-arm-static:
	@./scripts/build-one.sh windows arm64 static
windows-arm-shared:
	@./scripts/build-one.sh windows arm64 shared

mac-x86-static:
	@./scripts/build-one.sh macos x86_64 static
mac-x86-shared:
	@./scripts/build-one.sh macos x86_64 shared
mac-arm-static:
	@./scripts/build-one.sh macos arm64 static
mac-arm-shared:
	@./scripts/build-one.sh macos arm64 shared

native-test:
	@rm -rf build-native-test
	@cmake -S . -B build-native-test -G Ninja \
	  -DCMAKE_BUILD_TYPE=Release \
	  -DBUILD_SHARED_LIBS=OFF \
	  -DANDROIDTVREMOTE_BUILD_EXAMPLES=ON \
	  -DANDROIDTVREMOTE_BUILD_TESTS=ON
	@cmake --build build-native-test --parallel $(JOBS)
	@ctest --test-dir build-native-test --output-on-failure

clean:
	@rm -rf build-cross build-native-test

distclean: clean
	@rm -rf dist
