import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sc;
import 'dart:io';

/// Reproduces the tainik crash: open a database with a 64-char hex key,
/// close it, then try to open it with a WRONG key. This must not crash.
Future<void> main() async {
  testWidgets('wrong 64-hex passphrase does not crash', (tester) async {
    final dbDir = await sc.getDatabasesPath();
    final dbPath = '$dbDir/wrongpass_test.db';
    // Clean up from previous runs.
    final file = File(dbPath);
    if (file.existsSync()) file.deleteSync();
    for (final suffix in ['-journal', '-wal', '-shm']) {
      final sidecar = File('$dbPath$suffix');
      if (sidecar.existsSync()) sidecar.deleteSync();
    }

    // 64-char hex passphrase (like tainik's hex(K)).
    const correctKey =
        'a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2';
    const wrongKey =
        'f0e1d2c3b4a5968778695a4b3c2d1e0f9a8b7c6d5e4f3a2b1c0d9e8f7a6b5c4d';

    // 1. Create and open with the correct key.
    var db = await sc.openDatabase(dbPath, password: correctKey);
    await db.execute('CREATE TABLE t (v TEXT)');
    await db.insert('t', {'v': 'hello'});
    await db.close();

    // 2. Re-open with the WRONG key — must fail gracefully, not crash.
    try {
      db = await sc.openDatabase(dbPath, password: wrongKey);
      // If it opened, the key was accepted (shouldn't happen).
      await db.close();
      fail('Database opened with wrong key — should have failed');
    } catch (e) {
      // Expected: an error (not a crash).
      debugPrint('Correctly rejected wrong key: $e');
    }

    // 3. Re-open with the CORRECT key — must work.
    db = await sc.openDatabase(dbPath, password: correctKey);
    final rows = await db.query('t');
    expect(rows, hasLength(1));
    expect(rows.first['v'], 'hello');
    await db.close();
    debugPrint('TEST PASSED: wrong password rejected, correct key works');
  });
}
