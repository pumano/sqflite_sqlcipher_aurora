import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sc;
import 'dart:io';

/// Reproduces tainik's lock/unlock scenario:
/// 1. Create DB with a key
/// 2. Close it (lock)
/// 3. Open it again with the same key (unlock) — must not crash
Future<void> main() async {
  testWidgets('lock then unlock does not crash', (tester) async {
    final dbDir = await sc.getDatabasesPath();
    final dbPath = '$dbDir/lock_unlock_test.db';
    // Clean up from previous runs.
    final file = File(dbPath);
    if (file.existsSync()) file.deleteSync();
    for (final suffix in ['-journal', '-wal', '-shm']) {
      final sidecar = File('$dbPath$suffix');
      if (sidecar.existsSync()) sidecar.deleteSync();
    }

    // 64-char hex key (like tainik's hex(K)).
    const key =
        'a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2';

    // 1. Create and open the database (scenario 1: welcome → setup → list).
    var db = await sc.openDatabase(dbPath, password: key);
    await db.execute(
        "CREATE TABLE entries(id TEXT PRIMARY KEY, title TEXT NOT NULL)");
    await db
        .insert('entries', {'id': '1', 'title': 'Test Entry'},
            conflictAlgorithm: sc.ConflictAlgorithm.replace);
    await db.close();
    debugPrint('Step 1: Created and closed DB');

    // 2. Lock: close the database (it's already closed, but tainik calls close()
    //    which is a no-op if already closed).
    //    Actually, let me re-open it first to simulate the "unlocked" state.
    db = await sc.openDatabase(dbPath, password: key);
    debugPrint('Step 2: Re-opened DB (unlocked state)');

    // 3. Lock: close the database.
    await db.close();
    debugPrint('Step 3: Closed DB (locked state)');

    // 4. Unlock: open the database again with the same key.
    //    THIS IS WHERE TAINIK CRASHES.
    db = await sc.openDatabase(dbPath, password: key);
    debugPrint('Step 4: Re-opened DB (unlocked again)');

    // Verify data is intact.
    final rows = await db.query('entries');
    expect(rows, hasLength(1));
    expect(rows.first['title'], 'Test Entry');
    await db.close();
    debugPrint('Step 5: Verified data and closed DB');

    debugPrint('TEST PASSED: lock/unlock cycle works');
  });
}
