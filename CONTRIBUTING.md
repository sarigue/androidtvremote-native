# Contributing

Contributions are welcome. Bug fixes should include a regression test when practical, and public API changes should be discussed in an issue before implementation.

## Development setup

Install a C++20 compiler, CMake 3.21 or newer, Ninja, OpenSSL 1.1.1 or newer, and Boost 1.74 or newer. On Debian or Ubuntu:

```bash
sudo apt install build-essential cmake ninja-build libssl-dev libboost-dev
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DANDROIDTVREMOTE_BUILD_EXAMPLES=ON \
  -DANDROIDTVREMOTE_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

See [CROSS_BUILD.md](CROSS_BUILD.md) for cross-compilation instructions.

## Pull requests

- Keep changes focused and explain their motivation.
- Follow the existing C++ style and enable compiler warnings.
- Add or update tests and documentation as appropriate.
- Never commit generated certificates, private keys, build directories, or device identifiers.
- Ensure the GitHub Actions jobs pass on Linux, macOS, and Windows.

By submitting a contribution, you agree that it is licensed under the Apache License 2.0, as described in [LICENSE](LICENSE).

---

# Contribuer

Les contributions sont bienvenues. Ajoutez si possible un test de non-régression pour toute correction et ouvrez une discussion avant de modifier l’API publique.

La procédure de compilation et de test est identique à celle présentée ci-dessus. Consultez également [README.fr.md](README.fr.md) et [CROSS_BUILD.md](CROSS_BUILD.md). Une contribution ne doit jamais contenir de certificat, clé privée, adresse d’un appareil ou autre donnée sensible.

Toute contribution soumise est distribuée sous licence Apache 2.0, conformément au fichier [LICENSE](LICENSE).
