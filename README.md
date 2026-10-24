# sqflite_sqlcipher_aurora

Aurora OS (Linux) platform implementation for the
[`sqflite_sqlcipher`](https://github.com/davidmartos96/sqflite_sqlcipher)
plugin.

It provides **SQLCipher-encrypted SQLite** on Aurora OS through a native C++
binding to `libsqlcipher`, exposed over the Flutter method channel
`com.davidmartos96.sqflite_sqlcipher` (the same channel the `sqflite_sqlcipher`
Dart package uses on Android/iOS/macOS).

## How it fits together

```
your app
  ├── sqflite_sqlcipher          (Dart API: openDatabase(password: …))
  │     └── sqflite_common       (shared, platform-agnostic Dart logic)
  └── [aurora] sqflite_sqlcipher_aurora   ← this package
            └── C++ (libsqlcipher, prebuilt in aurora/prebuilt/)
```

The Dart side of `sqflite_sqlcipher` is self-contained: on every platform it
talks to the method channel `com.davidmartos96.sqflite_sqlcipher` directly.
This package only provides the **native handler for that channel on Aurora OS**
(Linux). The Dart API is unchanged from the upstream plugin; only the Aurora
native layer is provided here.

The upstream `sqflite_sqlcipher` has no `aurora` platform entry yet, so the app
depends on this package **directly** (see [Usage](#usage)). The `flutter-aurora`
tooling picks up any package with a `plugin.platforms.aurora` section in the
dependency graph, builds its C++ part and registers the plugin — no fork of
`sqflite_sqlcipher` is required.

An alternative wiring — a fork of `sqflite_sqlcipher` declaring
`aurora: default_package: sqflite_sqlcipher_aurora` — is what `example/` uses
for local development (path dependencies) and the shape a future upstream merge
would take. It is not needed for real apps.

## Requirements

- Aurora OS (Linux) with the `flutter-aurora` SDK.
- No extra runtime dependencies: the plugin vendors prebuilt `libsqlcipher.so.0`
  (SQLCipher 4.6.1, SQLite 3.46.1) for aarch64/armv7/x86_64 in
  `aurora/prebuilt/`, built against the system OpenSSL 1.1.1w that ships
  with Aurora OS. Nothing to install, no conan, nothing to configure.

## Usage

Add both the API package and this package (git dependency until it is
published / adopted upstream):

```yaml
dependencies:
  sqflite_sqlcipher: ^3.4.1
  # Aurora OS (Linux) native implementation of the plugin above:
  # the app depends on it directly, the tooling registers it.
  sqflite_sqlcipher_aurora:
    git:
      url: https://github.com/pumano/sqflite_sqlcipher_aurora.git
```

```dart
import 'package:sqflite_sqlcipher/sqflite.dart';

Future<Database> open() async {
  final path = (await getDatabasesPath()) + '/secret.db';
  return openDatabase(path,
      version: 1,
      password: 'my-secret-password', // enables SQLCipher encryption
      onCreate: (db, v) =>
          db.execute('CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT)'));
}
```

Omitting `password` gives a plain (unencrypted) SQLite database.

## Integrating an existing app

Lessons from integrating into a production app (kept generic on purpose):

- **Two SDKs, two pub worlds.** The Aurora SDK is a separate Flutter
  installation: its own `bin/`, its own pub cache (`~/.aurora-pub-cache`) and
  a pub mirror that lags behind pub.dev. Because the two SDKs are different
  Flutter/Dart versions, dependency resolution can differ between them:
  widen version constraints so the (older) Aurora mirror can resolve them, and
  pin the transitive dependencies the Aurora SDK itself pins (it ships its own
  pinned `flutter_localizations`, which in turn pins `intl`). Use an SDK
  constraint both SDKs accept (the Aurora fork is on Dart 3.11.x).
- **Two lockfiles, one package config.** The main SDK writes `pubspec.lock`,
  the Aurora SDK writes `aurora_pubspec.lock` — they do not clobber each
  other. But `.dart_tool/package_config.json` is shared: run the pub get of
  the SDK you are about to build with.
- **Build with an explicit target platform:**
  `flutter-aurora build aurora --target-platform aurora-arm64` (or
  `aurora-arm`). With no device connected the default is `aurora-arm`
  (armv7hl), and deploying that RPM to an aarch64 device/emulator fails with
  `BadPackageArchitecture`.
- **Docker** must be running: the RPM is produced in the `aurora-build-tools`
  container.
- **`organization` in `pubspec.yaml`** (e.g. `organization: com.example`) plus
  the package `name` form the RPM package name (`com.example.my_app`).
- **Emulator specifics** (details live in the SDK documentation):
  - grant the app's sailjail permissions before the first launch — a first run
    blocks on the permission prompt, which kills unattended `flutter test`;
  - hot reload/restart doesn't work; the edit cycle is build → `adt deploy`
    → restart the app;
  - the device id for `-d` comes from `flutter-aurora doctor -v` (the
    emulator is not listed by `flutter-aurora devices`).

## Building

From the `example/` directory:

```sh
flutter-aurora build aurora --target-platform aurora-arm64   # or aurora-arm
```

The RPM build needs no extra configuration: CMake picks the matching
`aurora/prebuilt/<arch>/libsqlcipher.so.0` for the target architecture and
the spec bundles it with the app. See `aurora/prebuilt/BUILD.md` for how the
prebuilt libraries are produced.

## License

See [LICENSE](LICENSE).
