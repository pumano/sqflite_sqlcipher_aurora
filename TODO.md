# TODO

## Done

- [x] 0.1.0: Aurora OS (Linux) platform implementation for `sqflite_sqlcipher`
  (native C++ binding over the `com.davidmartos96.sqflite_sqlcipher` method channel).
- [x] SQLCipher dependency without conan at build time: `libsqlcipher.so.0`
  (SQLCipher 4.6.1 / SQLite 3.46.1) is vendored in `aurora/prebuilt/<arch>/`
  for aarch64/armv7/x86_64, built from official sources against the system
  OpenSSL 1.1.1w (see `aurora/prebuilt/BUILD.md`). The RPM therefore depends
  only on the base-system `libcrypto.so.1.1` — no bundled OpenSSL.
- [x] Reason for dropping conan: the `sqlcipher/4.6.1@aurora` prebuilts link
  against different OpenSSL majors per architecture (aarch64 → 1.1.1w,
  armv7/x86_64 → 3.3.3), while the system ships only 1.1.1w; bundling a second
  OpenSSL per arch was undesirable.
- [x] Example app with on-device self-test; verified on the Aurora OS 5.2.1.200
  aarch64 emulator (release RPM): open with key OK, read OK, wrong key
  rejected, no plaintext in the raw file.
- [x] RPM builds for aarch64 and armv7hl; x86_64 prebuilt + CMake mapping in
  place (no x86_64 emulator available for the final on-device run).

## Open

- [ ] Publish to pub.dev (needs the `sqflite_sqlcipher` upstream to reference
      `sqflite_sqlcipher_aurora` as the aurora default_package, or consumers add
      it manually / via dependency_overrides).
- [ ] Optionally: publish the prebuilt `libsqlcipher.so.0` to the Aurora conan
      repository (built against 1.1.1w for all arches) and switch back to conan
      if the maintainers accept it.
- [ ] Re-verify on real aarch64/armv7 hardware when available.
