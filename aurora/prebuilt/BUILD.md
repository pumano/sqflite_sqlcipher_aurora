# Prebuilt SQLCipher

`libsqlcipher.so.0` (SQLCipher 4.6.1, SQLite 3.46.1) for the three Aurora OS
architectures. The plugin links against these libraries instead of depending
on a package manager (conan) at build time.

## Properties

- Built from the official sqlcipher 4.6.1 sources (amalgamation `sqlite3.c`
  generated with the project's own `tool/mksqlite3c.tcl`).
- Compile definitions: `-DSQLITE_HAS_CODEC=1 -DSQLCIPHER_CRYPTO_OPENSSL=1
  -DSQLITE_TEMP_STORE=2 -O2 -fPIC`.
- Linked against **OpenSSL 1.1.1w** (`libssl.so.1.1` / `libcrypto.so.1.1`),
  which is part of the Aurora OS base system, so no additional runtime
  libraries are bundled.
- Stripped. Soname: `libsqlcipher.so.0`.
- `sqlite3.h` (matching the 4.6.1 sources) is vendored in `../include/`.

## Rebuilding

All three architectures can be built inside a single `arm64` Debian
bookworm container (glibc 2.36, older than the system's 2.38, so the
resulting binaries run on the device):

```sh
docker run --rm --platform linux/arm64 \
  -v $(pwd)/src:/work/src \
  -v $OPENSSL_1_1_1W_ROOTS:/oss \
  debian:bookworm bash build.sh
```

where `src/` contains the generated `sqlite3.c` and `sqlite3.h`, and
`/oss/<arch>/` contains OpenSSL 1.1.1w headers and shared libraries for
`aarch64`, `armv7` and `x86_64` (any build of 1.1.1w works — the `.so`
files are only used as link inputs; at runtime the system libraries are
used). The cross compilers come from the Debian packages
`gcc-arm-linux-gnueabi` and `gcc-x86-64-linux-gnu`.

Per-architecture build commands:

```sh
# aarch64 (native in the arm64 container)
gcc -O2 -fPIC -c sqlite3.c -o sqlite3.o \
    -DSQLITE_HAS_CODEC=1 -DSQLCIPHER_CRYPTO_OPENSSL=1 -DSQLITE_TEMP_STORE=2 \
    -Ioss/aarch64/include
gcc -shared -o libsqlcipher.so.0.8.6 sqlite3.o \
    -Wl,-soname,libsqlcipher.so.0 -Loss/aarch64/lib -lssl -lcrypto -lpthread -ldl -lm

# armv7
arm-linux-gnueabi-gcc  ... # same flags, oss/armv7
# x86_64
x86_64-linux-gnu-gcc    ... # same flags, oss/x86_64

strip --strip-unneeded libsqlcipher.so.0.8.6
mv libsqlcipher.so.0.8.6 <arch>/libsqlcipher.so.0
```
