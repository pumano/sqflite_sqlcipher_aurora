## 0.2.0

The first release after the initial template: a batch of correctness and
reliability fixes, each covered by an integration test on the Aurora
emulator.

### Data safety

* A failed open (wrong or missing password, path problems) never deletes or
  modifies the existing database file: the error is surfaced and the file is
  left untouched.
* The database key is applied with `sqlite3_key_v2` on a binary-safe buffer
  that is wiped after use, so keys with special characters, non-ASCII text,
  or embedded/trailing NULs work.
* SQL `NULL` and zero-length `BLOB` are kept distinct in query results.
* Embedded NUL bytes are preserved in `TEXT` values.
* `insert()` and `batch()` return full 64-bit rowids.

### Transactions

* The transaction state stays in sync with the SQLite handle: a failed
  COMMIT/ROLLBACK is rolled back natively, and `ROLLBACK TO` (in all of its
  spelling variants) no longer terminates the transaction.
* The open and transaction state is read lock-free (atomics), closing the
  data race between the method-channel thread and the worker thread
  (ThreadSanitizer-clean).

### Lifecycle

* `close()` finalizes open cursors, so the database file is actually
  unlocked.
* Statements queued behind an active transaction, or arriving after
  `close()`, fail with an error instead of leaving their Dart future pending
  forever.
* Exceptions in asynchronous database work are reported to the caller instead
  of being swallowed: a failing call (for example, `openDatabase` under a
  path component that is a regular file) gets an error instead of a future
  that never completes.
* `deleteDatabase` removes the `-wal`, `-shm`, and `-journal` sidecar files
  and reports an error if any of them cannot be removed, instead of claiming
  success.

### Memory

* The expanded SQL is freed after use and is no longer allocated at all when
  SQL logging is off (previously ~33 KB leaked per parameterized query).

### Platform and packaging

* The vendored `libsqlcipher.so.0` for armv7 is rebuilt with the hard-float
  ABI.
* The RPM no longer declares a `Requires` on the bundled `libsqlcipher`
  (`requires_exclude`).

### Tests

* The template unit tests were rewritten for the real plugin surface, the
  example app is reproducible, and every fix above has an integration test
  that runs on the Aurora emulator.
