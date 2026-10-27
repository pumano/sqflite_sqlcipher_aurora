# Code review findings — sqflite_sqlcipher_aurora

Review of the native plugin (`aurora/*.cpp`, `aurora/src/*.h`) and the Dart
wrapper (`lib/sqflite_sqlcipher_aurora.dart`). Overall the architecture is
sound: a single worker thread, one-shot `Reply`, atomics for cross-thread
reads, NUL-safe column copies, `SQLITE_TRANSIENT` everywhere, PRAGMA-key wipe,
WAL merge and cursor finalization on close, draining of pending commands on
close with an error instead of a silent drop. The issues below, in priority
order.

## Critical

### 1. `Logger::level_` / `tag_` — data race (UB) — `src/logger.h`

`onOptionsCall` (channel thread) calls `logger_.setLogLevel()`, while the
worker thread concurrently reads `level_` in `verb()`/`sql()`/`error()`.
It is a plain `int`, not atomic — formally UB.
Fix: `std::atomic<int>` (loads with `memory_order_relaxed` are fine), and a
mutex or an immutable copy for `tag_`.

### 2. All commands deadlock after a failed `BEGIN` — `database.cpp`

`enterInTransaction()` is called **before** `execute()` in the execute
handler, so `currentTransactionID_` is bumped even when `BEGIN` fails
(e.g. "cannot start a transaction within a transaction"). In the error path
`resyncTransactionState()` checks `sqlite3_get_autocommit()`; if the handle
is already inside *another* transaction, autocommit is 0, and the **stale,
invalid id is kept**. The Dart client still believes its *old* transaction
id is open, so every subsequent command (`transactionID != current &&
!= Force`) is queued in `pendingSqlCallbacks_` and **never runs** — all
futures hang until `close()`.

Fix: only bump `currentTransactionID_` if the handle was actually outside a
transaction before `BEGIN` (`sqlite3_get_autocommit() != 0`), or restore the
previous id when `BEGIN` fails.

### 3. `deleteDatabase` deletes arbitrary files — `plugin.cpp`

`dbPath` comes from Dart with no validation. `removeFile` happily removes
any file the process can see, including sidecars next to it. Even an honest
app bug (`deleteDatabase('../important.db')` or an absolute path) destroys
data. Constrain the path to the app data directory (canonicalize and compare
against `getDatabasesPath()`). For `open` this is acceptable (sqlite will
not delete), but for `remove` it is not.

## Serious, but narrow

### 4. `close()` error path corrupts the single-instance registry

`close()` sets `open_ = false` first. If `sqlite3_close_v2` fails (e.g.
SQLITE_BUSY), the method returns an error, `databaseRemove` is **not**
called, but `open_` is already false. A subsequent single-instance open of
the same path creates a new `Database`; `databaseAdd` uses
`singleInstanceDatabases_.emplace(path, db)`, which does **not** replace the
existing (dead) entry — `databaseByPath` then returns the dead object
forever. Use `insert_or_assign` (or erase + emplace).

### 5. `sqlite3_stmt` leak on exception (bad_alloc)

In `query()`, `execute()` and `resultFromCursor()` rows are built with
`emplace_back`; a thrown `bad_alloc` (or any exception) skips
`sqlite3_finalize`: in `query()` the stmt leaks outright; in
`queryWithPageSize()` the cursor stays in `cursors_` with its stmt until
`close()`. The `Reply` guard in `plugin.cpp` catches the exception and
answers the call, but never cleans up the stmt/cursor.
Fix: an RAII wrapper over `sqlite3_stmt` (`std::unique_ptr` with a finalize
deleter) closes every throw path at once.

### 6. `AsyncQueue::~AsyncQueue` drops unexecuted tasks

The destructor joins the worker thread, but the remaining `tasks_` are simply
destroyed — their `Reply` is never answered and the corresponding Dart
futures hang forever. Today the plugin lives until process exit, so this is
latent; but any future plugin unload/recreation becomes a silent hang.
Minimum fix: in the destructor run the leftover tasks through
`Reply->Fail("plugin destroyed")` (or answer all pending replies with an
error).

## Minor / code smell

### 7. `execute()` does not check `isReadOnly_`

`insert()`/`update()` reject writes to a read-only handle, `execute()` does
not — a write via `execute` fails inside sqlite with a less clear error.
Add the same check.

### 8. `deleteDatabase` with an empty path

`removeFile("")` is treated as "missing = success", then
`removeFile("-journal")` etc. are evaluated **relative to the process CWD**
— files with those names could be deleted. An empty path must be an error.

### 9. `secureWipe` coverage is partial

The PRAGMA string is wiped, but the `password` copy captured in the task
lambda lives until the end of the worker loop iteration without a wipe, and
`sqlite3_errmsg` output may land in logs. Usually acceptable for user
passphrases, but worth a comment stating the wipe is not complete.

### 10. Log lines written from two threads

The channel thread logs (`verb()` in `onOpenDatabaseCall`) concurrently with
the worker. Individual `<<` calls into `std::cout` are internally
synchronized, but one log line is a chain of `<<` — lines can interleave.
Cosmetic, but painful for debugging. Emit one `<<` per line, or add a mutex.

### 11. `onOptionsCall` trusts the level value

`static_cast<Logger::Level>(*level)` accepts any int from Dart (e.g. 999).
Behavior ("always log") is harmless, but clamp to the known range.

### 12. `query()` materializes the whole result set

No memory bound for large tables (`queryWithPageSize` exists, but plain
`query` is unbounded). Matches upstream sqflite, so this is a note, not a
bug.

### 13. Inconsistent error codes for a closed database

`onCloseDatabaseCall` / `onDeleteDatabaseCall` report a closed database with
code `sqlite_error`, while `onExecuteCall` etc. use the dedicated
`database_closed`. The Dart side may discriminate on the code.

### 14. `cursors_[cursorID]` after `count()`

`queryCursorNext` uses `operator[]` after a `count()` check; `operator[]`
inserts a default-constructed `Cursor` (nullptr stmt) if the entry vanished
in between. Safe today (single worker thread) but fragile — use `find()`.

### 15. `AsyncQueue::push` can throw out of the channel handler

If the `std::thread` constructor throws, the `promise` is destroyed without
`set_value`, `future.wait()` throws `broken_promise`, and the channel call
gets no answer. Wrap in try/catch and answer with an error.
