// Regression tests for: queued statements and close().
//
// With singleInstance, two clients share one native connection. A statement
// issued on client B while client A holds a transaction is QUEUED natively
// (transaction-id based) rather than hitting a SQL "database is locked".
//
// The C++ Database methods guard against a null handle ("database closed") and
// close() drains the pending queue, so a queued statement can never
// dereference a closed sqlite3* (crash) nor be silently lost.
//
// NOTE: the Dart client serializes each database object with a lock it holds
// for the whole call, so it cannot be closed while one of its own writes is in
// flight (that deadlocks on the Dart side). The tests below exercise the
// reachable paths: the queued statement completing, close() handling an open
// transaction without hanging, and a statement that reaches the native queue
// only after close (via raw MethodChannel calls) failing instead of hanging.

import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
// beginTransaction/endTransaction are not part of the public Database API in
// sqflite_common 2.5.6, so the concrete type is used.
// ignore: implementation_imports
import 'package:sqflite_common/src/database.dart' show SqfliteDatabase;
// ignore: implementation_imports
import 'package:sqflite_common/src/transaction.dart' show SqfliteTransaction;
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  late String dbPath;

  setUp(() async {
    dbPath =
        '${await sqlcipher.getDatabasesPath()}/close_pending_'
        '${DateTime.now().microsecondsSinceEpoch}.db';
  });

  tearDown(() async {
    for (final suffix in ['', '-wal', '-shm', '-journal']) {
      try {
        await File('$dbPath$suffix').delete();
      } catch (_) {}
    }
  });

  Future<SqfliteTransaction> begin(sqlcipher.Database db) =>
      (db as SqfliteDatabase).beginTransaction();

  Future<void> end(
    sqlcipher.Database db,
    SqfliteTransaction txn, {
    required bool commit,
  }) async {
    final impl = db as SqfliteDatabase;
    txn.successful = commit;
    await impl.endTransaction(txn);
  }

  Future<sqlcipher.Database> createDb() async {
    final db = await sqlcipher.openDatabase(dbPath, singleInstance: true);
    await db.execute(
      'CREATE TABLE IF NOT EXISTS kv (id INTEGER PRIMARY KEY, v TEXT)',
    );
    return db;
  }

  testWidgets(
    'queued statement completes after the foreign transaction ends; close is clean and idempotent',
    (tester) async {
      final dbA = await createDb();
      final dbB = await sqlcipher.openDatabase(dbPath, singleInstance: true);
      addTearDown(() async {
        for (final db in [dbA, dbB]) {
          try {
            await db.close();
          } catch (_) {}
        }
      });

      final txn = await begin(dbA);
      await txn.execute('INSERT INTO kv (v) VALUES (?)', ['a']);

      // Not awaited yet: it must be queued while A's transaction is open.
      final bFuture = dbB.execute('INSERT INTO kv (v) VALUES (?)', ['b']);

      // A rolls back, unblocking the queue.
      await end(dbA, txn, commit: false);

      // The queued statement ran (not dropped, not crashed).
      await bFuture;
      final rows = await dbB.query('kv');
      expect(rows, hasLength(1));
      expect(rows.single['v'], 'b');

      // Closing the shared connection is clean and idempotent.
      await dbA.close();
      await dbA.close();
    },
  );

  testWidgets(
    'close while a transaction is open completes (rolls back, defers), does not hang',
    (tester) async {
      final dbA = await createDb();
      final dbB = await sqlcipher.openDatabase(dbPath, singleInstance: true);
      addTearDown(() async {
        for (final db in [dbA, dbB]) {
          try {
            await db.close();
          } catch (_) {}
        }
      });

      final txn = await begin(dbA);
      await txn.execute('INSERT INTO kv (v) VALUES (?)', ['x']);
      // Do NOT end the transaction; close the shared connection directly.
      await dbA.close();

      // The uncommitted row was rolled back; the file is intact and reopenable.
      final reopened = await sqlcipher.openDatabase(
        dbPath,
        singleInstance: true,
      );
      final rows = await reopened.query('kv');
      expect(rows, isEmpty);
      await reopened.close();
    },
  );

  // Regression for a statement that reaches processSqlCommand() only AFTER
  // close(): it used to see the stale transaction id and queue itself on the
  // closed database, leaving its Dart future pending forever. Raw
  // MethodChannel calls bypass the Dart object lock, so the worker order is
  // exactly busy -> close -> foreign SQL.
  testWidgets('late statement after close fails instead of hanging', (
    tester,
  ) async {
    const channel = MethodChannel('com.davidmartos96.sqflite_sqlcipher');

    final opened = await channel.invokeMapMethod<String, dynamic>(
      'openDatabase',
      {'path': dbPath, 'password': 'test-key'},
    );
    final id = opened!['id'];
    final txn = await channel.invokeMapMethod<String, dynamic>('execute', {
      'id': id,
      'sql': 'BEGIN',
      'inTransaction': true,
    });

    // Occupy the worker so the two calls below are both accepted (and grab
    // the database) while it is still busy.
    final busy = channel.invokeMethod<Object?>('query', {
      'id': id,
      'transactionId': txn!['transactionId'],
      'sql':
          'WITH RECURSIVE c(x) AS (VALUES(1) UNION ALL '
          'SELECT x + 1 FROM c WHERE x < 10000000) SELECT sum(x) FROM c',
    });
    final closing = channel.invokeMethod<void>('closeDatabase', {'id': id});
    final late = channel
        .invokeMethod<void>('execute', {'id': id, 'sql': 'SELECT 1'})
        .then<String>(
          (_) => 'unexpected success',
          onError: (Object error, StackTrace stack) => 'expected error: $error',
        );

    await busy;
    await closing;

    // Must resolve with a "database closed" error, not hang.
    expect(
      await late.timeout(const Duration(seconds: 10)),
      startsWith('expected error'),
    );
  });
}
