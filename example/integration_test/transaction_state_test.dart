// Regression tests for
// https://github.com/pumano/sqflite_sqlcipher_aurora/issues/4
//
// The native plugin keeps a per-database transaction id that must always
// match the actual state of the SQLite handle:
//
// - a regular statement inside a transaction (no "inTransaction" flag) must
//   not clear the state,
// - a failed statement must not clear the state (SQLite keeps the
//   transaction open after an error),
// - a queued command must never be executed inside a transaction it does not
//   belong to.
//
// The two-client tests are the discriminator: with a desynced state the
// second client's statement would run inside the first client's still-open
// transaction and be silently rolled back (or hit SQLITE_BUSY) together with
// it, while the second client believes it committed.

import 'dart:io';

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

  const password = 'review-password';
  late String dbPath;

  setUp(() async {
    dbPath =
        '${await sqlcipher.getDatabasesPath()}/review_txn_${DateTime.now().microsecondsSinceEpoch}.db';
  });

  tearDown(() async {
    try {
      await File(dbPath).delete();
    } catch (_) {}
  });

  Future<SqfliteTransaction> begin(sqlcipher.Database db) =>
      (db as SqfliteDatabase).beginTransaction();

  Future<void> end(sqlcipher.Database db, SqfliteTransaction txn,
          {required bool commit}) async {
    final impl = db as SqfliteDatabase;
    txn.successful = commit;
    await impl.endTransaction(txn);
  }

  Future<sqlcipher.Database> createDb() async {
    final db = await sqlcipher.openDatabase(dbPath,
        password: password, singleInstance: true);
    await db.execute('CREATE TABLE t(x INTEGER UNIQUE NOT NULL)');
    return db;
  }

  Future<List<Map<String, Object?>>> allX(sqlcipher.Database db) async {
    final rows = await db.rawQuery('SELECT x FROM t ORDER BY x');
    return rows.map((row) => row).toList();
  }

  testWidgets('single client: commit persists, rollback discards',
      (tester) async {
    final db = await createDb();
    addTearDown(() async {
      try {
        await db.close();
      } catch (_) {}
    });

    // Commit
    var txn = await begin(db);
    await txn.execute('INSERT INTO t(x) VALUES (5)');
    await end(db, txn, commit: true);
    expect((await allX(db)).map((row) => row['x']), [5]);

    // Rollback after a statement that failed inside the transaction:
    // the failed statement must not break the transaction state, the
    // explicit ROLLBACK must still work.
    txn = await begin(db);
    await txn.execute('INSERT INTO t(x) VALUES (6)');
    var failed = false;
    try {
      await txn.execute('INSERT INTO t(x) VALUES (6)'); // UNIQUE violation
    } catch (_) {
      failed = true;
    }
    expect(failed, isTrue);
    await end(db, txn, commit: false); // ROLLBACK, must not throw
    expect((await allX(db)).map((row) => row['x']), [5]);
  });

  testWidgets('two clients: queued statement survives a foreign rollback',
      (tester) async {
    final dbA = await createDb();
    final dbB = await sqlcipher.openDatabase(dbPath,
        password: password, singleInstance: true);
    addTearDown(() async {
      try {
        await dbA.close();
      } catch (_) {}
      try {
        await dbB.close();
      } catch (_) {}
    });

    final txn = await begin(dbA);
    await txn.execute('INSERT INTO t(x) VALUES (1)');

    // Not awaited yet: it must be queued while A's transaction is open and
    // only run after A ends it.
    final bFuture = dbB.execute('INSERT INTO t(x) VALUES (99)');

    await end(dbA, txn, commit: false); // ROLLBACK
    await bFuture; // must complete after the rollback

    // A's row is rolled back, B's row is committed by itself.
    expect((await allX(dbB)).map((row) => row['x']), [99]);
  });

  testWidgets('two clients: queued statement commits after a foreign commit',
      (tester) async {
    final dbA = await createDb();
    final dbB = await sqlcipher.openDatabase(dbPath,
        password: password, singleInstance: true);
    addTearDown(() async {
      try {
        await dbA.close();
      } catch (_) {}
      try {
        await dbB.close();
      } catch (_) {}
    });

    final txn = await begin(dbA);
    await txn.execute('INSERT INTO t(x) VALUES (1)');
    final bFuture = dbB.execute('INSERT INTO t(x) VALUES (99)');
    await end(dbA, txn, commit: true); // COMMIT
    await bFuture;

    expect((await allX(dbB)).map((row) => row['x']), [1, 99]);
  });
}
