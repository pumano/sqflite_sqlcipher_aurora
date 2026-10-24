## 0.1.0

* Initial release.
* Aurora OS (Linux) platform implementation for `sqflite_sqlcipher`.
* Native C++ binding to `libsqlcipher` (SQLCipher 4.x), pulled from the Aurora
  conan repository (`sqlcipher/4.6.1@aurora`) and bundled with the plugin.
* Encryption is enabled per-database via the `password` open option
  (`PRAGMA key`), over the `com.davidmartos96.sqflite_sqlcipher` method channel.
