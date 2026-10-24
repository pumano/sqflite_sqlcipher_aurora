// close()-and-cursors regression test (issue #9).
//
// Before the fix Database::close() called sqlite3_close_v2() without
// finalizing the still-open cursor statements. close_v2() then only
// *defers* the real close until those statements are finalized, so the
// database file stayed locked: a new connection to the same file got
// SQLITE_BUSY (5) on the first write.
//
// Run: flutter test integration_test/close_finalizes_cursors_test.dart -d <device>

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  const password = 'secret-password';
  late String dbPath;

  setUp(() async {
    dbPath = '${await sqlcipher.getDatabasesPath()}/'
        'close_cursors_${DateTime.now().microsecondsSinceEpoch}.db';
  });

  tearDown(() async {
    try {
      await File(dbPath).delete();
    } catch (_) {}
  });

  Future<dynamic> open() =>
      sqlcipher.openDatabase(dbPath, password: password);

  testWidgets('close() releases cursor statements so the file reopens',
      (tester) async {
    var db = await open();
    await db.execute('CREATE TABLE t (v TEXT)');
    await db.execute('INSERT INTO t VALUES (?)', ['a']);
    await db.execute('INSERT INTO t VALUES (?)', ['b']);
    await db.execute('INSERT INTO t VALUES (?)', ['c']);

    // Open a cursor and consume only the first page: the statement is
    // live, positioned mid-result, holding a read transaction.
    final cursor =
        await db.rawQueryCursor('SELECT v FROM t', null, bufferSize: 1);
    expect(cursor.resultList, hasLength(1));
    expect(await cursor.moveNext(), isTrue); // second row, still mid-result

    await db.close();

    // The old code left the cursor statement live, so close_v2() deferred
    // the real close and the file stayed locked: the first write through
    // a new connection failed with SQLITE_BUSY (5).
    db = await open();
    await db
        .execute('INSERT INTO t VALUES (?)', ['d']) // must not be busy
        .timeout(const Duration(seconds: 10));
    final rows = await db.rawQuery('SELECT v FROM t');
    expect(rows.map((row) => row['v']).toList(), ['a', 'b', 'c', 'd']);
    await db.close();
  });

  testWidgets('a QueryCursor used after close() gets a clear error',
      (tester) async {
    final db = await open();
    await db.execute('CREATE TABLE t (v TEXT)');
    await db.execute('INSERT INTO t VALUES (?)', ['a']);
    await db.execute('INSERT INTO t VALUES (?)', ['b']);

    final cursor =
        await db.rawQueryCursor('SELECT v FROM t', null, bufferSize: 1);
    expect(await cursor.moveNext(), isTrue); // consume the buffered row

    await db.close();

    // The plugin drops the database object on close, so the next fetch
    // must fail with a clear "database closed" error.
    await expectLater(
        cursor.moveNext(),
        throwsA(isA<Object>().having(
            (e) => e.toString(), 'message', contains('closed'))));
  });
}
