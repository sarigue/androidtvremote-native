# Migration depuis `androidtvremote2` Python

Correspondance principale pour porter Freebox Pop Remote vers la bibliothèque native :

| Python `androidtvremote2` | C++ `libandroidtvremote` |
|---|---|
| `AndroidTVRemote(...)` | `AndroidTvRemote(Options{...})` |
| `async_generate_cert_if_missing()` | `generateCertificateIfMissing()` |
| `async_start_pairing()` | `startPairing()` |
| `async_finish_pairing(code)` | `finishPairing(code)` |
| `async_connect()` | `connect()` + callback `onConnectionState` |
| `keep_reconnecting()` | `Options::autoReconnect = true` |
| `disconnect()` | `disconnect()` |
| `async_get_name_and_mac()` | `getNameAndMac()` |
| `send_key_command("HOME")` | `sendKey("HOME")` ou `sendKey(KeyCode::Home)` |
| `send_launch_app_command(url)` | `launchApp(url)` |
| `send_text(text)` | `sendText(text)` |
| `start_voice()` | `startVoice()` + `onVoiceStarted` |
| `VoiceStream.send_chunk()` | `sendVoiceData()` |
| `VoiceStream.end()` | `stopVoice()` |
| `InvalidAuth` | callback `onAuthenticationRequired` pour la connexion |

## Modèle de threads

La connexion Remote v2 possède son propre thread I/O interne. `connect()`,
`sendKey()`, `launchApp()`, `sendText()`, `startVoice()`, `sendVoiceData()` et
`stopVoice()` ne bloquent pas l'appelant.

Les callbacks sont exécutés depuis le thread I/O interne. Une interface Qt doit
les rapatrier vers le thread GUI, par exemple avec un signal Qt ou
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.

Les opérations d'appairage sont synchrones afin de garder une API simple et
prédictible. Elles doivent être lancées dans un worker depuis une GUI.

## Données audio

Le format reste identique à celui utilisé par `androidtvremote2` : PCM 16 bits,
mono, 8000 Hz. La bibliothèque se charge du découpage à 20 KiB et du padding à
8 KiB minimum.

## Certificats existants

La bibliothèque lit des certificats et clés PEM OpenSSL standards. Les fichiers
`cert.pem` / `key.pem` déjà créés par la version Python sont donc destinés à être
réutilisables, ce qui évite en principe un nouvel appairage lors de la migration.
Cette compatibilité doit néanmoins être validée sur un Player physique avant de
supprimer l'ancien backend Python.
