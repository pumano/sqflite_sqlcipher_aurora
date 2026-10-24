## 0.1.0

* Initial release.
* Aurora OS (Linux) platform implementation for `sqflite_sqlcipher`.
* Native C++ binding to `libsqlcipher` (SQLCipher 4.6.1). Prebuilt
  `libsqlcipher.so.0` for aarch64/armv7/x86_64 is vendored in `aurora/prebuilt/`
  and linked against the system OpenSSL 1.1.1w, so the plugin has no
  build-time dependency on conan or any other package manager.
* Encryption is enabled per-database via the `password` open option
  (`PRAGMA key`), over the `com.davidmartos96.sqflite_sqlcipher` method channel.
