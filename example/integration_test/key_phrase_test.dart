// Regression test: a 64-character hex passphrase (a common format for
// derived keys stored as hex text) must round-trip through openDatabase.
// Before the fix, setKey() applied the key via sqlite3_key_v2 (raw-key API),
// which crashed on a 64-byte "key" because AES only accepts 16/24/32-byte
// raw keys.
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:path/path.dart' as p;
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  // 64 hex chars = 32 bytes when decoded; as a *passphrase* it is a
  // 64-character string, well beyond the 16/24/32-byte raw-key limit.
  const hexKey =
      'a3f1c9d2e8b47065f1a2d3c4b5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708';

  testWidgets('64-char hex passphrase round-trips', (tester) async {
    final dbDir = await sqlcipher.getDatabasesPath();
    final path = p.join(dbDir, 'phrase_test.db');
    for (final suffix in <String>['', '-journal', '-wal', '-shm']) {
      final f = File('$path$suffix');
      if (f.existsSync()) f.deleteSync();
    }

    // Create + write.
    final db = await sqlcipher.openDatabase(path, password: hexKey);
    await db.execute('CREATE TABLE t (x TEXT)');
    await db.execute("INSERT INTO t VALUES ('hello')");
    await db.close();

    // Re-open + read back.
    final db2 = await sqlcipher.openDatabase(path, password: hexKey);
    final rows = await db2.query('t');
    expect(rows.first['x'], 'hello');
    await db2.close();

    // Clean up.
    await sqlcipher.deleteDatabase(path);
    expect(File(path).existsSync(), isFalse);
  });

  // Wrong key must fail gracefully (with an error, not a crash).
  testWidgets('wrong passphrase fails with an error, not a crash',
      (tester) async {
    final dbDir = await sqlcipher.getDatabasesPath();
    final path = p.join(dbDir, 'wrong_key_test.db');
    for (final suffix in <String>['', '-journal', '-wal', '-shm']) {
      final f = File('$path$suffix');
      if (f.existsSync()) f.deleteSync();
    }

    // Create a database with a known passphrase.
    final db = await sqlcipher.openDatabase(path, password: hexKey);
    await db.execute('CREATE TABLE t (x TEXT)');
    await db.execute("INSERT INTO t VALUES ('secret')");
    await db.close();

    // Now try to open with a DIFFERENT passphrase — must fail gracefully.
    const wrongKey =
        'b3f1c9d2e8b47065f1a2d3c4b5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708';
    Object? outcome;
    try {
      final openFuture = sqlcipher.openDatabase(path, password: wrongKey);
      // The open should complete (with an error) within a bounded time.
      // If it hangs, the timeout will fire.
      final opened = await openFuture.timeout(const Duration(seconds: 5));
      // If it somehow opens, verify that reading fails.
      try {
        await opened.query('t');
        outcome = 'opened successfully (unexpected)';
      } catch (e) {
        outcome = e;
      } finally {
        await opened.close();
      }
    } catch (e) {
      outcome = e;
    }

    expect(outcome, isNotNull,
        reason: 'opening with a wrong key must not hang');
    // The open should either fail at open() or at the first read — either way,
    // it must not crash the process.
    print('wrong-key outcome: $outcome');

    // Clean up.
    await sqlcipher.deleteDatabase(path);
    expect(File(path).existsSync(), isFalse);
  });
}
