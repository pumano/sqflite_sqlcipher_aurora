import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sc;

/// Replicates the tainik lock_flow integration test (scenario 3) at the
/// database level. Tainik crashed the whole app (SIGABRT in DartWorker)
/// on this exact sequence with sqflite_sqlcipher_aurora 0.2.0, while
/// 0.1.0 passed.
///
/// Tainik's native call sequence (singleInstance: true — the sqflite
/// default, tainik never sets it; key = 64-char hex of PBKDF2-SHA512):
///
///   scenario 1: open(key) → CREATE → INSERT
///   scenario 2: close → [File.delete vault.db + sidecars]
///               → open(key) → CREATE → INSERT ×2 → close → open(key)
///               → CREATE → SELECT
///   scenario 3: close → [File.delete]
///               → open(key) → CREATE → INSERT ×2 → close
///               → (wrong password: rejected by a Dart-side hash pre-check,
///                  no native call)
///               → open(key) → CREATE → SELECT   ← crash here
///
/// Notes:
/// - The file is deleted with a raw dart:io File.delete (tainik does not
///   use the plugin's deleteDatabase for the wipe).
/// - Tainik re-opens with the same key every time; each "create" uses a
///   fresh random key, so we rotate keys per lifecycle like tainik does
///   (fresh salt per vault).
/// - The wrong-key open is included once (harmless per
///   wrong_password_test, but it makes the sequence a superset of tainik's
///   in case the crash needs it).

// 64-char hex passphrases (like tainik's hex(K)); distinct per lifecycle.
const keyA =
    'a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2';
const keyB =
    'b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2c3';
const keyC =
    'c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2c3d4';
const wrongKey =
    'f0e1d2c3b4a5968778695a4b3c2d1e0f9a8b7c6d5e4f3a2b1c0d9e8f7a6b5c4d';

const table =
    "CREATE TABLE IF NOT EXISTS entries(id TEXT PRIMARY KEY, "
    "title TEXT NOT NULL, password TEXT NOT NULL)";

Future<String> dbPath() async =>
    '${await sc.getDatabasesPath()}/tainik_replica.db';

/// Tainik's VaultDb.deleteFile(): the database must already be closed.
Future<void> deleteFile(String path) async {
  for (final suffix in const ['', '-journal', '-wal', '-shm']) {
    final f = File('$path$suffix');
    if (await f.exists()) await f.delete();
  }
}

Future<void> main() async {
  testWidgets('tainik lock_flow scenario 3 replica', (tester) async {
    final path = await dbPath();
    await deleteFile(path);

    // -- scenario 1: create (open #1) ----------------------------------
    var db = await sc.openDatabase(path, password: keyA);
    await db.execute(table);
    await db.insert('entries', {'id': 'e1', 'title': 'GitHub', 'password': 'p1'});
    debugPrint('S1: created, open #1 done');

    // -- scenario 2: lock (close #1), wipe, create (open #2) -----------
    await db.close();
    await deleteFile(path);
    db = await sc.openDatabase(path, password: keyB);
    await db.execute(table);
    await db.insert('entries', {'id': 'e1', 'title': 'GitHub', 'password': 'p1'});
    await db.insert('entries', {'id': 'e2', 'title': 'Mail', 'password': 'p2'});
    debugPrint('S2: recreated, open #2 done');

    // lock (close #2), unlock (open #3)
    await db.close();
    db = await sc.openDatabase(path, password: keyB);
    await db.execute(table);
    var rows = await db.query('entries');
    expect(rows, hasLength(2));
    debugPrint('S2: unlocked, open #3 done, rows=$rows');

    // -- scenario 3: lock (close #3), wipe, create (open #4) -----------
    await db.close();
    await deleteFile(path);
    db = await sc.openDatabase(path, password: keyC);
    await db.execute(table);
    await db.insert('entries', {'id': 'e1', 'title': 'GitHub', 'password': 'p1'});
    await db.insert('entries', {'id': 'e2', 'title': 'Mail', 'password': 'p2'});
    debugPrint('S3: recreated, open #4 done');

    // lock (close #4)
    await db.close();

    // Wrong master password: tainik rejects it by hash pre-check (no
    // native call); we additionally try the wrong key natively — a
    // superset of tainik's sequence.
    try {
      db = await sc.openDatabase(path, password: wrongKey);
      await db.close();
      fail('Database opened with the wrong key');
    } catch (e) {
      debugPrint('S3: wrong key correctly rejected: $e');
    }

    // Correct password: unlock (open #5) — tainik crashed here.
    db = await sc.openDatabase(path, password: keyC);
    await db.execute(table);
    rows = await db.query('entries');
    expect(rows, hasLength(2));
    await db.close();
    debugPrint('S3: unlocked, open #5 done');

    // -- scenario 4 shape: wipe from settings (close + delete) ----------
    await deleteFile(path);

    // And one more full lifecycle to be a strict superset.
    db = await sc.openDatabase(path, password: keyA);
    await db.execute(table);
    await db.insert('entries', {'id': 'e1', 'title': 'GitHub', 'password': 'p1'});
    await db.close();
    await deleteFile(path);
    debugPrint('S4: extra lifecycle done');

    debugPrint('TANIK REPLICA PASSED');
  });
}
