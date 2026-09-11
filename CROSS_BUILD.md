# Cross-compilation depuis Linux x86_64

Le `Makefile` pilote CMake et produit les bibliothèques demandées dans `dist/`.
Dans ce projet, les alias `x86` et `arm` signifient respectivement **x86_64** et
**ARM64/AArch64**.

> macOS utilise l'extension standard `.dylib` (et non `.dynlib`).

## Matrice de sortie

| Cible | Statique | Dynamique |
|---|---|---|
| Linux x86_64 | `libandroidtvremote-x86_64.lin.a` | `libandroidtvremote-x86_64.so` |
| Linux ARM64 | `libandroidtvremote-arm64.lin.a` | `libandroidtvremote-arm64.so` |
| Windows x86_64 | `libandroidtvremote-x86_64.win.a` | `androidtvremote-x86_64.dll` |
| Windows ARM64 | `libandroidtvremote-arm64.win.a` | `androidtvremote-arm64.dll` |
| macOS Intel | `libandroidtvremote-x86_64.mac.a` | `libandroidtvremote-x86_64.dylib` |
| macOS Apple Silicon | `libandroidtvremote-arm64.mac.a` | `libandroidtvremote-arm64.dylib` |

Les builds DLL génèrent également, lorsque le toolchain le fournit, la
bibliothèque d'import `libandroidtvremote-<arch>.win.dll.a`.

## Commandes

```bash
make help
make doctor

make linux-x86
make linux-arm
make windows-x86
make windows-arm
make mac-x86
make mac-arm

# ou toute la matrice :
make all
```

Chaque groupe construit la variante statique et la variante dynamique. Les
cibles individuelles sont également disponibles, par exemple :

```bash
make windows-x86-static
make mac-arm-shared
```

## Dépendances communes

La bibliothèque utilise C++20, Boost.Asio (headers) et OpenSSL. Il faut au
minimum :

```bash
sudo apt install build-essential cmake ninja-build libboost-dev libssl-dev
```

`make native-test` compile et exécute les tests sur l'hôte Linux x86_64.

## Linux ARM64

Installer le cross-compilateur :

```bash
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

Il faut aussi OpenSSL compilé pour ARM64. Indiquer son préfixe :

```bash
export OPENSSL_LINUX_ARM64_ROOT=/opt/cross/openssl/linux-arm64
make linux-arm
```

Ce préfixe doit contenir les headers et bibliothèques OpenSSL de la **cible**,
pas celles de l'hôte.

## Windows x86_64

Deux toolchains sont supportés :

1. MinGW-w64 classique (`x86_64-w64-mingw32-g++`) ;
2. llvm-mingw via `LLVM_MINGW_ROOT`.

Exemple MinGW :

```bash
sudo apt install mingw-w64
export OPENSSL_WINDOWS_X86_64_ROOT=/opt/cross/openssl/windows-x86_64
make windows-x86
```

La bibliothèque statique `.win.a` est une archive au format GNU/MinGW. Pour un
consommateur construit avec MSVC, utiliser plutôt le build natif Windows qui
produit un `.lib`.

## Windows ARM64

Le build ARM64 utilise **llvm-mingw** :

```bash
export LLVM_MINGW_ROOT=/opt/llvm-mingw
export OPENSSL_WINDOWS_ARM64_ROOT=/opt/cross/openssl/windows-arm64
make windows-arm
```

Le répertoire llvm-mingw doit notamment contenir :

```text
bin/aarch64-w64-mingw32-clang
bin/aarch64-w64-mingw32-clang++
```

## macOS x86_64 et ARM64 depuis Linux

Le projet supporte `osxcross`, mais il ne fournit volontairement **aucun SDK
Apple**. Il faut préparer osxcross soi-même avec un SDK macOS obtenu dans le
respect de la licence Apple.

Les wrappers attendus sont :

```text
o64-clang++    # Intel x86_64
oa64-clang++   # Apple Silicon ARM64
```

Ils peuvent être remplacés par des chemins explicites :

```bash
export OSXCROSS_X86_64_CC=/opt/osxcross/target/bin/o64-clang
export OSXCROSS_X86_64_CXX=/opt/osxcross/target/bin/o64-clang++
export OSXCROSS_ARM64_CC=/opt/osxcross/target/bin/oa64-clang
export OSXCROSS_ARM64_CXX=/opt/osxcross/target/bin/oa64-clang++
```

Puis :

```bash
export MACOSX_SDK=/opt/osxcross/target/SDK/MacOSX15.5.sdk
export MACOSX_DEPLOYMENT_TARGET=13.0
export OPENSSL_MACOS_X86_64_ROOT=/opt/cross/openssl/macos-x86_64
export OPENSSL_MACOS_ARM64_ROOT=/opt/cross/openssl/macos-arm64

make macos
```

OpenSSL doit être construit séparément pour chaque architecture macOS.

## Variables OpenSSL reconnues

```text
OPENSSL_LINUX_ARM64_ROOT
OPENSSL_WINDOWS_X86_64_ROOT
OPENSSL_WINDOWS_ARM64_ROOT
OPENSSL_MACOS_X86_64_ROOT
OPENSSL_MACOS_ARM64_ROOT
```

Le build Linux x86_64 natif utilise directement OpenSSL du système, sauf si
`OPENSSL_LINUX_X86_64_ROOT` est défini.

## Pourquoi les dépendances doivent être cross-compilées elles aussi ?

Un compilateur ARM64 ne peut pas lier une bibliothèque OpenSSL x86_64, et un
linker Windows ne peut pas utiliser une bibliothèque ELF Linux. Le code source
C++ est portable, mais toutes les bibliothèques natives liées au résultat
doivent cibler le même OS, la même architecture et une ABI compatible.

Boost.Asio est utilisé ici en mode header-only, ce qui simplifie fortement ce
point. OpenSSL reste la principale dépendance native à fournir pour chaque
cible.
