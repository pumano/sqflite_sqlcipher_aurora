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
                └── C++ (libsqlcipher via conan)
```

`sqflite_sqlcipher` declares `aurora: default_package: sqflite_sqlcipher_aurora`
in its `pubspec.yaml`, so on Aurora OS the native implementation in this
package is used automatically. The Dart side is unchanged from the upstream
plugin; only the Aurora (Linux) native layer is provided here.

## Requirements

- Aurora OS (Linux) with the `flutter-aurora` SDK.
- The native build pulls **SQLCipher** from the Aurora conan repository
  (`sqlcipher/4.6.1@aurora`), together with its OpenSSL dependency, and bundles
  the shared libraries next to the plugin (`$ORIGIN` rpath). No manual SQLCipher
  setup is required.

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

The example's `aurora/conanfile.py` + `aurora/rpm/*` (spec, `load-conan.sh`,
`version.config`) wire SQLCipher into the RPM build via conan.

## License

See [LICENSE](LICENSE).
