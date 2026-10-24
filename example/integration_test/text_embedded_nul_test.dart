// Embedded NUL in TEXT regression test (issue #7).
//
// Before the fix the read paths copied sqlite3_column_text() through a
// const char* (strlen), so a TEXT value with an embedded NUL byte came
// back truncated at the first NUL. After the fix the copy uses the
// explicit length from sqlite3_column_bytes(), on both the query() and
// the cursor paths.
//
// Run: flutter test integration_test/text_embedded_nul_test.dart -d <device>

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  const password = 'secret-password';
  // "a\0b" - a TEXT value with an embedded NUL byte.
  const value = 'a\u0000b';

  late dynamic db;
  late String dbPath;

  setUp(() async {
    dbPath = '${await sqlcipher.getDatabasesPath()}/'
        'text_nul_${DateTime.now().microsecondsSinceEpoch}.db';
  });

  tearDown(() async {
    try {
      await db.close();
    } catch (_) {}
    try {
      await File(dbPath).delete();
    } catch (_) {}
  });

  Future<void> populate() async {
    db = await sqlcipher.openDatabase(dbPath, password: password);
    await db.execute('CREATE TABLE t(x INTEGER PRIMARY KEY, name TEXT)');
    await db.insert('t', {'name': value});
  }

  testWidgets('query(): a TEXT value with an embedded NUL round-trips',
      (tester) async {
    await populate();

    final rows = await db.query('t', columns: ['name'], orderBy: 'x');

    expect(rows[0]['name'], value,
        reason: 'the embedded NUL must be preserved');
    expect(rows[0]['name'].codeUnits, [97, 0, 98]);
  });

  testWidgets('cursor: a TEXT value with an embedded NUL round-trips',
      (tester) async {
    await populate();

    final cursor = await db.rawQueryCursor('SELECT name FROM t ORDER BY x', null);
    addTearDown(() async {
      try {
        await cursor.close();
      } catch (_) {}
    });

    expect(await cursor.moveNext(), isTrue);
    expect(cursor.current['name'], value,
        reason: 'the embedded NUL must be preserved');
    expect(cursor.current['name'].codeUnits, [97, 0, 98]);

    expect(await cursor.moveNext(), isFalse);
    await cursor.close();
  });
}
