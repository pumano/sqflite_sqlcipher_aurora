import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sc;
import 'dart:io';

/// Reproduces tainik's double-close scenario:
/// 1. Create DB
/// 2. Close it (lock)
/// 3. Close it AGAIN (dispose) — must not crash
/// 4. Re-open it (unlock) — must work
Future<void> main() async {
  testWidgets('double close then re-open does not crash', (tester) async {
    final dbDir = await sc.getDatabasesPath();
    final dbPath = '$dbDir/double_close_test.db';
    // Clean up from previous runs.
    final file = File(dbPath);
    if (file.existsSync()) file.deleteSync();
    for (final suffix in ['-journal', '-wal', '-shm']) {
      final sidecar = File('$dbPath$suffix');
      if (sidecar.existsSync()) sidecar.deleteSync();
    }

    // 64-char hex key.
    const key =
        'a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2';

    // 1. Create and open the database.
    var db = await sc.openDatabase(dbPath, password: key);
    await db.execute(
        "CREATE TABLE entries(id TEXT PRIMARY KEY, title TEXT NOT NULL)");
    await db
        .insert('entries', {'id': '1', 'title': 'Test Entry'},
            conflictAlgorithm: sc.ConflictAlgorithm.replace);
    debugPrint('Step 1: Created DB');

    // 2. Close it (lock).
    await db.close();
    debugPrint('Step 2: Closed DB (lock)');

    // 3. Close it AGAIN (dispose) — this might crash.
    try {
      await db.close();
      debugPrint('Step 3: Closed DB again (dispose) — no crash');
    } catch (e) {
      debugPrint('Step 3: Second close threw: $e');
    }

    // 4. Re-open it (unlock) — must work.
    db = await sc.openDatabase(dbPath, password: key);
    debugPrint('Step 4: Re-opened DB (unlock)');

    // Verify data is intact.
    final rows = await db.query('entries');
    expect(rows, hasLength(1));
    expect(rows.first['title'], 'Test Entry');
    await db.close();
    debugPrint('Step 5: Verified data and closed DB');

    debugPrint('TEST PASSED: double-close then re-open works');
  });
}
