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

    // Wrong key must fail (not crash).
    final wrongKey =
        'b3f1c9d2e8b47065f1a2d3c4b5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708';
    expect(
      () => sqlcipher.openDatabase(path, password: wrongKey),
      throwsA(isA<Exception>()),
    );

    // Clean up.
    await sqlcipher.deleteDatabase(path);
    expect(File(path).existsSync(), isFalse);
  });
}
