# sqflite_sqlcipher_aurora

Aurora OS (Linux) platform implementation for the
[`sqflite_sqlcipher`](https://github.com/davidmartos96/sqflite_sqlcipher)
plugin.

It provides **SQLCipher-encrypted SQLite** on Aurora OS through a native C++
binding to `libsqlcipher`, exposed over the Flutter method channel
`com.davidmartos96.sqflite_sqlcipher` (the same channel the `sqflite_sqlcipher`
Dart package uses on Android/iOS/macOS).

## How it fits together (federated plugin)

```
your app
  └── sqflite_sqlcipher          (Dart API: openDatabase(password: …))
        ├── sqflite_common       (shared, platform-agnostic Dart logic)
        └── [aurora] sqflite_sqlcipher_aurora   ← this package
                └── C++ (libsqlcipher, prebuilt in aurora/prebuilt/)
```

`sqflite_sqlcipher` declares `aurora: default_package: sqflite_sqlcipher_aurora`
in its `pubspec.yaml`, so on Aurora OS the native implementation in this
package is used automatically. The Dart side is unchanged from the upstream
plugin; only the Aurora (Linux) native layer is provided here.

## Requirements

- Aurora OS (Linux) with the `flutter-aurora` SDK.
- No extra dependencies: the plugin vendors prebuilt `libsqlcipher.so.0`
  (SQLCipher 4.6.1, SQLite 3.46.1) for aarch64/armv7/x86_64 in
  `aurora/prebuilt/`, built against the system OpenSSL 1.1.1w that ships
  with Aurora OS. Nothing to install, no conan, nothing to configure —
  just add `sqflite_sqlcipher` to `pubspec.yaml` and build.

## Usage

Depend on `sqflite_sqlcipher` (not on this package directly) — this package is
picked up automatically as the Aurora implementation:

```yaml
dependencies:
  sqflite_sqlcipher: ^3.4.0
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

## Building

From the `example/` directory:

```sh
flutter build aurora --target-platform aurora-arm64   # or aurora-arm
```

The RPM build needs no extra configuration: CMake picks the matching
`aurora/prebuilt/<arch>/libsqlcipher.so.0` for the target architecture and
the spec bundles it with the app. See `aurora/prebuilt/BUILD.md` for how the
prebuilt libraries are produced.

## License

See [LICENSE](LICENSE).
