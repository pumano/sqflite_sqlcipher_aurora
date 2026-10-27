# Plan — fix the v0.2.0 key-handling regression (segfault on passphrase keys)

## The bug

v0.2.0 changed `Database::setKey()` (`aurora/database.cpp`) from a
`PRAGMA key = '…'` statement to a direct `sqlite3_key_v2()` call:

```cpp
// v0.1.0
sqlite3_exec(db_, "PRAGMA key = '<escaped password>'", ...);
// v0.2.0
sqlite3_key_v2(db_, "main", key.data(), (int)key.size());
```

These two are **not equivalent**:

- `PRAGMA key` applies a **password phrase** — SQLCipher runs it through its
  KDF (PBKDF2) to derive the real encryption key.
- `sqlite3_key_v2()` applies a **raw binary key** — the bytes are used directly
  as the AES key, no KDF.

The `sqflite_sqlcipher` `openDatabase(password:)` parameter is a **password
phrase** (a Dart `String`). The SQLCipher docs are explicit about the split:

> **`sqlite3_key_v2()`** — when you have a *raw binary key* (random bytes) or
> you use `ATTACH DATABASE`.
> **`PRAGMA key`** — when you use a *standard password phrase* (human-readable
> text).

So v0.2.0 feeds a **phrase** through the **raw-key** API. For the short phrases
in this repo's own tests (10–18 chars) SQLCipher happens to absorb them, but a
**64-character hex passphrase** — e.g. the consuming **tainik** app's `hex(K)`,
a hex-encoded 32-byte key — becomes a 64-byte "raw key", an invalid AES key
length, and the open **segfaults** (SIGSEGV) inside libsqlcipher.

## Evidence

- `libsqlcipher.so.0` (SQLCipher 4.6.1, SQLite 3.46.1) is byte-identical for
  aarch64 between v0.1.0 and v0.2.0 — the crash is the code path, not the lib.
- The consuming **tainik** app opens its vault with a 64-char hex passphrase:
  - v0.1.0 (`PRAGMA key`): all 11 integration scenarios pass; the app is stable.
  - v0.2.0 (`sqlite3_key_v2`): the app dies with SIGSEGV a few seconds after the
    first `openDatabase` (journalctl: `ANOM_ABEND … sig=11`).
- This repo's own tests use short phrases, so they never hit the long-hex case.

## The fix

In `aurora/database.cpp`, branch `Database::setKey()` on whether the key
contains a NUL byte:

- **No NUL** → it is a password **phrase** → apply it with `PRAGMA key = '…'`
  (single quotes escaped), exactly as v0.1.0 did.
- **NUL present** → a phrase cannot contain a NUL, so it is a **raw binary key**
  → apply it with `sqlite3_key_v2()` on the binary-safe buffer (v0.2.0's intent,
  kept for the embedded-NUL test).

This restores phrase semantics (fixing the tainik segfault) while preserving the
binary-safety for genuinely raw keys. The `SQLITE_HAS_CODEC=1` compile
definition stays (both APIs need the codec).

```cpp
utils::error Database::setKey(const std::string &password) {
  if (password.empty()) return utils::error::none();

  // A NUL cannot appear in a password phrase, so a key with one is a raw
  // binary key: apply it with sqlite3_key_v2 on a binary-safe buffer.
  if (password.find('\0') != std::string::npos) {
    std::string key(password);
    const int rc = sqlite3_key_v2(db_, "main", key.data(),
                                  static_cast<int>(key.size()));
    secureWipe(key.data(), key.size());
    if (rc != SQLITE_OK) return utils::error(sqlite3_errmsg(db_), rc);
    return utils::error::none();
  }

  // Otherwise it is a phrase: PRAGMA key (SQLCipher runs its KDF). A phrase
  // must NOT go through sqlite3_key_v2 — that would treat it as a raw key and,
  // for a long hex phrase, crash. Escape single quotes as v0.1.0 did.
  std::string sql;
  sql.reserve(password.size() * 2 + 16);
  sql += "PRAGMA key = '";
  for (const char c : password) {
    sql += c;
    if (c == '\'') sql += '\'';
  }
  sql += '\'';
  char *errmsg = nullptr;
  const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    const auto message = errmsg ? errmsg : sqlite3_errmsg(db_);
    return utils::error(message, rc);
  }
  return utils::error::none();
}
```

## Reproduction test

Add `example/integration_test/key_phrase_test.dart` — a **64-character hex
passphrase** (a stand-in for tainik's `hex(K)`): open → write → close →
re-open with the same phrase → read back. It **crashes the app** under the
v0.2.0 code and must pass after the fix. Keep `key_special_chars_test.dart`
(quotes / unicode / NUL) green.

## Verification

1. Run the library's integration tests on the Aurora emulator (build the
   example RPM, deploy, grant the sailjail permissions — see the tainik repo's
   `docs/flutter-aurora.md`): the new `key_phrase_test` + the existing
   `key_special_chars_test` must both pass.
2. Consumer check: the **tainik** app's `lock_flow_test` (11 scenarios) must
   pass against the patched plugin (this is the real-world reproduction).

## Steps

- [ ] `aurora/database.cpp`: branch `setKey()` on `password.find('\0')`.
- [ ] `example/integration_test/key_phrase_test.dart`: 64-hex-char passphrase round-trip.
- [ ] Build the example RPM (aarch64), deploy, grant the sailjail permissions.
- [ ] Run `key_phrase_test` + `key_special_chars_test` — both green.
- [ ] Bump the version to 0.2.1; note the regression + fix in `CHANGELOG.md`.
- [ ] Consumer check: tainik `lock_flow_test` green against the patched plugin.

## Debug: tainik still crashes after the fix

The library's own example passes 39/39 tests with the `PRAGMA key` fix.
But tainik's `lock_flow_test` still crashes with `free(): invalid pointer`
(scenarios 3+ fail at ~00:11, after scenarios 1–2 pass).

### What we know (from fprintf debug in `setKey`/`open`/`close`)

```
[DEBUG open]   path=.../vault.db  db_=(nil)          ← first open, handle is null
[DEBUG setKey] db_=0xffff640024a0 sql=PRAGMA key='…' rc=0   ← success
[DEBUG close]  db_=0xffff640024a0 open_=0           ← close, handle valid
[DEBUG open]   path=.../vault.db  db_=(nil)          ← second open, handle null again
[DEBUG setKey] db_=0xffff640024a0 sql=PRAGMA key='…' rc=0   ← same handle addr, new key
[DEBUG close]  db_=0xffff640024a0 open_=0
… (repeats)
free(): invalid pointer                                ← crash after the last setKey
```

- `close()` correctly sets `db_` to null (verified: next `open` sees nil).
- The allocator reuses the same address for each new handle.
- Every `PRAGMA key` returns rc=0 (SQLITE_OK).
- The `sql` buffer is **not** corrupted during `sqlite3_exec` (verified by copy-compare).
- The app opens the same `vault.db` path multiple times (lock/unlock cycles in the test).

### Hypotheses eliminated

| Hypothesis | Result |
|---|---|
| `secureWipe` corrupts heap | ❌ Disabled → still crashes |
| `PRAGMA key` itself corrupts heap | ❌ Replaced with no-op → still crashes |
| `SQLITE_HAS_CODEC=1` define | ❌ Removed → still crashes |
| `sqflite` (plain) dependency / `sqflite_aurora` plugin | ❌ Removed → different failure ("No tests found") but app does launch |
| Stale user data | ❌ Cleaned → still crashes |
| Buffer corruption in `sqlite3_exec` | ❌ Verified: buffer unchanged after call |

### Remaining suspects

1. **`validateOpenDatabase()`** — runs after `setKey()` in `open()`. It executes
   `SELECT count(*) FROM sqlite_master` which triggers SQLCipher's page-decryption
   code path. If the key is wrong (or the DB was created with a different key),
   the decrypt callback might corrupt heap. But rc=0 suggests the key matched.

2. **Multiple open/close cycles** — the test does lock→unlock→lock→unlock, each
   cycle calls `close()` then `openDatabase()` again. Maybe the second or third
   `sqlite3_open_v2` reuses the freed handle's memory and the SQLCipher codec
   (linked into `libsqlcipher.so.0`) has a use-after-free in its page cache or
   cipher context.

3. **`libsqlcipher.so.0` internal bug** — the prebuilt SQLCipher 4.6.1 library
   (OpenSSL 1.1.1w backend) might have a bug in its key-change or re-open path
   that only manifests with certain key values or allocation patterns.

### Next steps

- [ ] Add debug logging to `validateOpenDatabase()` to confirm it runs without error.
- [ ] Try `sqlite3_close_v2` → `sqlite3_open_v2` on the **same** `Database` object
  (instead of destroying and recreating) to see if the issue is in the codec's
  static/global state.
- [ ] Test with a **fixed** key (same key every open) to rule out key-change as trigger.
- [ ] If the above doesn't help: try building `libsqlcipher.so.0` from source with
  ASAN to get a real stack trace of the corruption.
