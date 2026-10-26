// A database key may contain characters that are special in SQL (quotes,
// unicode). The key is applied via PRAGMA key (passphrase semantics, matching
// the Dart API contract). Single quotes are escaped; unicode passes through.
// NUL bytes are not supported (PRAGMA key is a C-string) and must be rejected
// with a clear error rather than silently truncating the key.
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:path/path.dart' as p;
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  // (key, shouldSucceed) — the value stored is always 'v'.
  final cases = <List<Object>>[
    ["it's a key", true], // single quote → escaped
    ['pässword_ключ', true], // unicode → passes through
    [String.fromCharCodes([105, 116, 0, 115]), false], // embedded NUL → error
  ];

  for (final c in cases) {
    final key = c[0] as String;
    final shouldSucceed = c[1] as bool;
    final idx = cases.indexOf(c);

    testWidgets('key special chars (case $idx)', (tester) async {
      final dbDir = await sqlcipher.getDatabasesPath();
      final path = p.join(dbDir, 'keytest_$idx.db');
      for (final suffix in <String>['', '-journal', '-wal', '-shm']) {
        final f = File('$path$suffix');
        if (f.existsSync()) f.deleteSync();
      }

      if (!shouldSucceed) {
        // NUL in the key must be rejected with an error.
        expect(
          () => sqlcipher.openDatabase(path, password: key),
          throwsA(isA<Exception>()),
        );
        // Clean up any partial file.
        for (final suffix in <String>['', '-journal', '-wal', '-shm']) {
          final f = File('$path$suffix');
          if (f.existsSync()) f.deleteSync();
        }
        return;
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
