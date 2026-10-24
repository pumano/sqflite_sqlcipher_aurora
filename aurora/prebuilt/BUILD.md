# Prebuilt SQLCipher

`libsqlcipher.so.0` (SQLCipher 4.6.1, SQLite 3.46.1) for the three Aurora OS
architectures. The plugin links against these libraries instead of depending
on a package manager (conan) at build time.

## Properties

- Built from the official SQLCipher **v4.6.1** sources
  (https://github.com/sqlcipher/sqlcipher, tag `v4.6.1`); the amalgamation
  `sqlite3.c` is generated with the project's own `tool/mksqlite3c.tcl`
  (via `./configure ... && make sqlite3.c`).
- Compile definitions: `-O2 -fPIC -DSQLITE_HAS_CODEC=1
  -DSQLCIPHER_CRYPTO_OPENSSL=1 -DSQLITE_TEMP_STORE=2`.
- Linked against **OpenSSL 1.1.1w** (`libssl.so.1.1` / `libcrypto.so.1.1`),
  which is part of the Aurora OS base system, so no additional runtime
  libraries are bundled.
- Stripped. Soname: `libsqlcipher.so.0`.
- `sqlite3.h` (matching the 4.6.1 sources) is vendored in `../include/`.
- **armv7 is hard-float** (`Tag_ABI_VFP_args: VFP registers`,
  `ld-linux-armhf.so.3`), matching the Aurora `armv7hl` target. A
  soft-float library is rejected by the loader on the device.

## Sources

```
https://codeload.github.com/sqlcipher/sqlcipher/tar.gz/refs/tags/v4.6.1
sha256: d8f9afcbc2f4b55e316ca4ada4425daf3d0b4aab25f45e11a802ae422b9f53a3
```

## Rebuilding

### aarch64 and x86_64

Both architectures can be built inside a single `arm64` Debian bookworm
container (glibc 2.36, older than the system's 2.38, so the resulting
binaries run on the device):

```sh
docker run --rm --platform linux/arm64 \
  -v $(pwd)/src:/work/src \
  -v $OPENSSL_1_1_1W_ROOTS:/oss \
  debian:bookworm bash build.sh
```

where `src/` contains the generated `sqlite3.c` and `sqlite3.h`, and
`/oss/<arch>/` contains OpenSSL 1.1.1w headers and shared libraries for
`aarch64` and `x86_64` (any build of 1.1.1w works — the `.so` files are
only used as link inputs; at runtime the system libraries are used). The
cross compilers come from the Debian packages `gcc-arm-linux-gnueabi` and
`gcc-x86-64-linux-gnu`.

Per-architecture build commands:

```sh
# aarch64 (native in the arm64 container)
gcc -O2 -fPIC -c sqlite3.c -o sqlite3.o \
    -DSQLITE_HAS_CODEC=1 -DSQLCIPHER_CRYPTO_OPENSSL=1 -DSQLITE_TEMP_STORE=2 \
    -Ioss/aarch64/include
gcc -shared -o libsqlcipher.so.0.8.6 sqlite3.o \
    -Wl,-soname,libsqlcipher.so.0 -Loss/aarch64/lib -lssl -lcrypto -lpthread -ldl -lm

# x86_64
x86_64-linux-gnu-gcc ... # same flags, oss/x86_64

strip --strip-unneeded libsqlcipher.so.0.8.6
mv libsqlcipher.so.0.8.6 <arch>/libsqlcipher.so.0
```

### armv7 (hard-float)

The `armv7hl` variant must be built with the Aurora SDK cross toolchain
(`armv7hl-meego-linux-gnueabi`, hard-float by default). It is available in
the `aurora-build-tools:5.2.1.200` container under
`/opt/cross/armv7hl-meego-linux-gnueabi`, together with the full target
sysroot (`sys-root/`, glibc 2.38, OpenSSL 1.1.1w), so the library links
directly against the system libraries:

```sh
docker run --rm -v ./src:/src -v ./out:/out aurora-build-tools:5.2.1.200 \
  bash -c '
CC=/opt/cross/bin/armv7hl-meego-linux-gnueabi-gcc
$CC -O2 -fPIC -c /src/sqlite3.c -o /out/sqlite3.o \
    -DSQLITE_HAS_CODEC=1 -DSQLCIPHER_CRYPTO_OPENSSL=1 -DSQLITE_TEMP_STORE=2
$CC -shared -o /out/libsqlcipher.so.0.8.6 /out/sqlite3.o \
    -Wl,-soname,libsqlcipher.so.0 -lssl -lcrypto -lpthread -ldl -lm
/opt/cross/bin/armv7hl-meego-linux-gnueabi-strip --strip-unneeded \
    /out/libsqlcipher.so.0.8.6'
mv out/libsqlcipher.so.0.8.6 armv7/libsqlcipher.so.0
```

The previous armv7 binary was built with Debian's `arm-linux-gnueabi-gcc`
(soft-float ABI) and could not be loaded on the `armv7hl` target
(`error while loading shared libraries: ... internal error`).

## Verification

```sh
readelf -h  armv7/libsqlcipher.so.0   # Flags: 0x5000400, hard-float ABI
readelf -A  armv7/libsqlcipher.so.0   # Tag_ABI_VFP_args: VFP registers
readelf -d  armv7/libsqlcipher.so.0   # SONAME libsqlcipher.so.0,
                                       # NEEDED ld-linux-armhf.so.3
```

Runtime smoke test (qemu-user over the target sysroot): open a database,
`PRAGMA key`, write, close, reopen, read back.

Current armv7 artifact (2026-10-24):

```
sha256: ad916910db114300a79e184b4c50a1e8cee20433d9bb8f0a74aa083561e931b3
```
