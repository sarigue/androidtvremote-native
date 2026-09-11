# libandroidtvremote

[English](README.md) · **Français**

Bibliothèque cliente **C++20 native** pour le protocole **Android TV Remote v2**, avec le protocole d’appairage utilisé par les appareils Android TV et Google TV, notamment Freebox Player Pop / Player TV Free 4K.

Le projet est indépendant, sans dépendance d’exécution à Python ni Protocol Buffers, et peut être intégré à une application Qt, GTK, Win32, Cocoa, en ligne de commande ou sous forme de service.

> Version actuelle : **0.1.0**. L’implémentation du protocole est couverte par des tests TLS simulés en local, mais sa compatibilité doit encore être validée sur chaque appareil physique et version d’Android TV.

## Fonctionnalités

- génération d’un certificat client RSA 2048 / X.509 avec OpenSSL ;
- appairage Android TV sur le port `6467` ;
- connexion Remote v2 TLS sur le port `6466` ;
- framing Protocol Buffers intégré, sans dépendance à `libprotobuf` ;
- touches Android, D-pad, volume, programmes et chiffres ;
- App Links et saisie de texte / IME ;
- callbacks pour l’alimentation, l’application courante et le volume ;
- réponse automatique aux pings et reconnexion avec backoff ;
- commande vocale PCM 16 bits, mono, 8 kHz ;
- API asynchrone pour la connexion et les commandes, API synchrone pour l’appairage ;
- bibliothèques statiques et dynamiques.

## Prérequis

- compilateur C++20 ;
- CMake 3.21 ou plus récent ;
- OpenSSL 1.1.1 ou plus récent ;
- Boost 1.74 ou plus récent (en-têtes Asio uniquement) ;
- threads natifs du système.

## Démarrage rapide

Sous Debian ou Ubuntu :

```bash
sudo apt install build-essential cmake ninja-build libssl-dev libboost-dev
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DANDROIDTVREMOTE_BUILD_EXAMPLES=ON \
  -DANDROIDTVREMOTE_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

La bibliothèque statique est produite sous `build/libandroidtvremote.a`. Utilisez `BUILD_SHARED_LIBS=ON` pour une bibliothèque dynamique.

GitHub Actions exécute d'abord une compilation avec les avertissements traités comme des erreurs, ainsi que les tests. Les variantes natives x86_64 et ARM64 sont ensuite compilées et testées pour Linux, macOS et Windows. Les tranches macOS sont fusionnées en bibliothèques universelles statique et dynamique. Une compilation réussie fournit les artefacts temporaires suivants :

- Linux : `libandroidtvremote-linux-x86_64.a`, `libandroidtvremote-linux-arm64.a`, `libandroidtvremote-linux-x86_64.so` et `libandroidtvremote-linux-arm64.so` ;
- macOS universel (x86_64 + ARM64) : `libandroidtvremote-macos.a` et `libandroidtvremote-macos.dylib` ;
- bibliothèques statiques Windows : `libandroidtvremote-win-x86_64_static.lib` et `libandroidtvremote-win-arm64_static.lib` ;
- DLL Windows et bibliothèques d’import : `libandroidtvremote-win-x86_64.dll`, `libandroidtvremote-win-arm64.dll`, `libandroidtvremote-win-x86_64.lib`, `libandroidtvremote-win-arm64.lib`, `libandroidtvremote-win-x86_64.dll.a` et `libandroidtvremote-win-arm64.dll.a`.

La publication d'un tag nommé `v*` crée ou met à jour une release GitHub contenant directement ces fichiers téléchargeables.

### Windows

Le dépôt contient un manifeste `vcpkg.json`. Depuis un terminal développeur Visual Studio avec vcpkg disponible :

```powershell
cmake -S . -B build -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure
```

Avec MSVC, le build statique produit `androidtvremote_static.lib`. Le build
partagé produit `androidtvremote.dll` et sa bibliothèque d’import MSVC
`androidtvremote.lib`.

### macOS

```bash
brew install cmake ninja boost openssl@3
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Le `Makefile` fournit également `make native-test` et les cibles de compilation croisée. Consultez [CROSS_BUILD.md](CROSS_BUILD.md) pour toute la matrice Linux, Windows et macOS.

## Intégration CMake

Installez le projet dans un préfixe :

```bash
cmake --install build --prefix /chemin/du/prefixe
```

Puis utilisez-le dans un autre projet CMake :

```cmake
find_package(androidtvremote CONFIG REQUIRED)
target_link_libraries(mon_application PRIVATE androidtvremote::androidtvremote)
```

## Exemple d’utilisation

```cpp
#include <androidtvremote/android_tv_remote.hpp>

using namespace androidtvremote;

AndroidTvRemote remote({
    .clientName = "Télécommande du salon",
    .certificateFile = "cert.pem",
    .privateKeyFile = "key.pem",
    .host = "192.168.1.42",
    .enableVoice = true,
});

remote.setCallbacks({
    .onConnectionState = [](ConnectionState state) {
        // Transférer vers le thread principal dans une application graphique.
    },
    .onAuthenticationRequired = [] {
        // Démarrer le parcours utilisateur d’appairage.
    },
    .onError = [](const std::string& error) {
        // Journaliser ou afficher l’erreur.
    },
});

if (remote.generateCertificateIfMissing()) {
    remote.startPairing();
    // Demander le code à six caractères affiché par la TV.
    remote.finishPairing("A1B2C3");
}

remote.connect();
remote.sendKey(KeyCode::Home);
remote.launchApp("https://example.com/");
```

Les callbacks sont exécutés dans le thread I/O interne de la bibliothèque. Une application graphique doit les transférer vers son thread principal. Les opérations d’appairage sont volontairement synchrones et doivent être lancées depuis un worker dans une GUI.

Les certificats et clés privées générés sont des identifiants : gardez-les hors du gestionnaire de versions et protégez-les comme des secrets applicatifs.

## Commande vocale

`startVoice()` envoie `KEYCODE_SEARCH`, attend `remote_voice_begin`, puis renvoie le `session_id` via `onVoiceStarted`. `sendVoiceData()` accepte du PCM 16 bits mono 8 kHz. Les blocs sont limités à 20 Kio et complétés à au moins 8 Kio pour reproduire le comportement attendu par Android TV.

## Intégration continue

GitHub Actions compile et teste les variantes statique et dynamique sous Linux, macOS et Windows. Chaque job réussi publie une archive de son arborescence d’installation CMake comme artefact temporaire. Le workflow se lance pour les pull requests, les pushes et manuellement.

## Documentation du projet

- [English README](README.md)
- [Guide de compilation croisée](CROSS_BUILD.md)
- [Migration depuis l’implémentation Python](MIGRATION_FROM_PYTHON.md)
- [Guide de contribution](CONTRIBUTING.md)
- [Code de conduite](CODE_OF_CONDUCT.md)
- [Politique de sécurité](SECURITY.md)
- [Historique des versions](CHANGELOG.md)

## Références du protocole

Cette implémentation propre et minimale du codec réseau s’appuie sur le comportement public d’Android TV Remote v2 et sur des ressources publiques, notamment `tronikos/androidtvremote2` (Apache-2.0) et les documents du protocole d’appairage Google TV publiés dans AOSP.

Android et Google TV sont des marques de leurs propriétaires respectifs. Ce projet n’est ni affilié à Google, ni approuvé par Google.

## Licence

Distribué sous [licence Apache 2.0](LICENSE). Les informations d’attribution figurent dans [NOTICE](NOTICE).
