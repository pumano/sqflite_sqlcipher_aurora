// NULL vs empty BLOB regression test (issue #6).
//
// Before the fix:
//  * a NULL value in a BLOB-declared column came back as an empty
//    Uint8List (the native side special-cased SQLITE_NULL + BLOB
//    decltype), indistinguishable from a real zero-length BLOB;
//  * writing an empty Uint8List bound SQL NULL instead of a
//    zero-length BLOB (sqlite3_bind_blob with a null buffer binds
//    NULL), so the two were silently swapped.
//
// After the fix a NULL stays null and an empty BLOB stays an empty
// (non-null) byte list, on both the query() and the cursor paths.
//
// Run: flutter test integration_test/null_vs_empty_blob_test.dart -d <device>

import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  const password = 'secret-password';
  late dynamic db;
  late String dbPath;

  setUp(() async {
    dbPath = '${await sqlcipher.getDatabasesPath()}/'
        'null_blob_${DateTime.now().microsecondsSinceEpoch}.db';
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
    await db.execute(
        'CREATE TABLE t(x INTEGER PRIMARY KEY, data BLOB, name TEXT)');
    // x = 1: explicit NULLs (must stay null in a BLOB column).
    await db.insert('t', {'data': null, 'name': null});
    // x = 2: zero-length BLOB + empty string (must stay non-null).
    await db.insert('t', {'data': Uint8List(0), 'name': ''});
    // x = 3: non-empty values (sanity, must be unchanged).
    await db.insert(
        't', {'data': Uint8List.fromList([1, 2, 3]), 'name': 'x'});
  }

  testWidgets('query(): NULL stays null, empty BLOB stays empty',
      (tester) async {
    await populate();

    final rows =
        await db.query('t', columns: ['data', 'name'], orderBy: 'x');

    expect(rows[0]['data'], isNull,
        reason: 'NULL in a BLOB column must come back as null');
    expect(rows[0]['name'], isNull);

    expect(rows[1]['data'], isA<Uint8List>(),
        reason: 'a zero-length BLOB must come back as an empty Uint8List');
    expect(rows[1]['data'], isEmpty);
    expect(rows[1]['name'], isEmpty);

    expect(rows[2]['data'], [1, 2, 3]);
    expect(rows[2]['name'], 'x');
  });

  testWidgets('cursor: NULL stays null, empty BLOB stays empty',
      (tester) async {
    await populate();

    final cursor =
        await db.rawQueryCursor('SELECT data, name FROM t ORDER BY x', null);
    addTearDown(() async {
      try {
        await cursor.close();
      } catch (_) {}
    });

    expect(await cursor.moveNext(), isTrue);
    expect(cursor.current['data'], isNull);
    expect(cursor.current['name'], isNull);

    expect(await cursor.moveNext(), isTrue);
    expect(cursor.current['data'], isA<Uint8List>());
    expect(cursor.current['data'], isEmpty);
    expect(cursor.current['name'], isEmpty);

    expect(await cursor.moveNext(), isTrue);
    expect(cursor.current['data'], [1, 2, 3]);
    expect(cursor.current['name'], 'x');

    expect(await cursor.moveNext(), isFalse);
    await cursor.close();
  });

  testWidgets('typeof(): an empty BLOB is stored as a BLOB, not NULL',
      (tester) async {
    await populate();

    final rows = await db.query('t', columns: ['typeof(data)'], orderBy: 'x');

    expect(rows[0]['typeof(data)'], 'null');
    expect(rows[1]['typeof(data)'], 'blob',
        reason: 'a zero-length BLOB must be stored as a BLOB, not NULL');
    expect(rows[2]['typeof(data)'], 'blob');
  });
}
