// A database key may contain characters that are special in SQL (quotes,
// unicode) or that cannot be represented in a SQL string literal at all (NUL).
// The key is applied via sqlite3_key_v2() on a binary-safe buffer, never as a
// constructed PRAGMA statement, so none of these keys can be truncated or
// mis-escaped. Each key must round-trip: open with it, write, close, re-open
// with the same key, read back.
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:path/path.dart' as p;
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  // (key, expected readable back) — the value stored is always 'v'.
  final cases = <List<Object>>[
    ["it's a key", 0], // single quote
    ['pässword_ключ', 1], // unicode
    [String.fromCharCodes([105, 116, 0, 115]), 2], // embedded NUL
  ];

  for (final c in cases) {
    final key = c[0] as String;
    final idx = c[1] as int;
    testWidgets('key round-trips (case $idx)', (tester) async {
      final dbDir = await sqlcipher.getDatabasesPath();
      final path = p.join(dbDir, 'keytest_$idx.db');
      for (final suffix in <String>['', '-journal', '-wal', '-shm']) {
        final f = File('$path$suffix');
        if (f.existsSync()) f.deleteSync();
      }

      final db =
          await sqlcipher.openDatabase(path, password: key);
      await db.execute('CREATE TABLE t (x TEXT)');
      await db.execute("INSERT INTO t VALUES ('v')");
      await db.close();

      final db2 =
          await sqlcipher.openDatabase(path, password: key);
      final rows = await db2.query('t');
      expect(rows.first['x'], 'v');
      await db2.close();

      await sqlcipher.deleteDatabase(path);
      expect(File(path).existsSync(), isFalse);
      expect(File('$path-journal').existsSync(), isFalse);
      expect(File('$path-wal').existsSync(), isFalse);
      expect(File('$path-shm').existsSync(), isFalse);
    });
  }
}
