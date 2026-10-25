import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

// Regression guard for the open()/close() threading of the native Database.
//
// isOpen() is called from the method-channel thread (the single-instance
// re-open check) while open()/close() mutate the sqlite3* handle on the async
// worker thread. The handle is now mirrored by an atomic open flag, so the
// channel-thread read is race-free.
//
// A data race is not deterministically reproducible, so this test hammers the
// database with concurrent single-instance re-opens, writes, and closes and
// asserts only that nothing crashes and the file is left intact and
// re-openable. On the arm64 emulator a torn pointer read would not crash
// anyway (word-sized reads are atomic), so this guards against future
// regressions (e.g. dereferencing the handle off the worker thread) rather
// than catching the original benign race.
void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('concurrent open/write/close leaves the database intact', (
    widget,
  ) async {
    final path = await sqlcipher.getDatabasesPath();
    final dbPath =
        '$path/concurrent_${DateTime.now().microsecondsSinceEpoch}.db';
    const password = 'concurrent-pw';

    final db = await sqlcipher.openDatabase(
      dbPath,
      password: password,
      version: 1,
      onCreate: (d, _) async {
        await d.execute('CREATE TABLE t (v INTEGER)');
      },
    );

    // A burst of concurrent single-instance re-opens, writes, and closes.
    // Closing a single-instance handle closes the shared native database, so
    // some in-flight writes may surface a "closed" error; that is expected
    // and tolerated here. The point is that the process does not crash.
    final tasks = <Future<void>>[];
    for (var i = 0; i < 24; i++) {
      tasks.add(() async {
        try {
          await db.execute('INSERT INTO t (v) VALUES (?)', [i]);
        } catch (_) {
          // The shared handle may have been closed by a sibling task.
        }
        if (i.isEven) {
          try {
            final re = await sqlcipher.openDatabase(
              dbPath,
              password: password,
              singleInstance: true,
            );
            await re.close();
          } catch (_) {
            // Re-open or close of a concurrently-closed handle.
          }
        }
      }());
    }
    await Future.wait(tasks);
    await db.close();

    // The file survives: it re-opens with the key and the table is queryable.
    final db2 = await sqlcipher.openDatabase(dbPath, password: password);
    final rows = await db2.rawQuery('SELECT count(*) AS c FROM t');
    expect(rows.single['c'], isA<int>());
    await db2.close();
  });

  // The reviewer's ThreadSanitizer scenario: the channel thread calls
  // isInTransaction() (single-instance re-open check) while the worker runs
  // BEGIN/ROLLBACK churn. Before the fix, TSan flagged the plain int
  // currentTransactionID_ read; it is atomic now. The single-instance dedup
  // must always return the same database id.
  testWidgets('concurrent BEGIN/ROLLBACK and single-instance re-open', (
    widget,
  ) async {
    final path = await sqlcipher.getDatabasesPath();
    final dbPath = '$path/race_${DateTime.now().microsecondsSinceEpoch}.db';
    const password = 'race-pw';
    const channel = MethodChannel('com.davidmartos96.sqflite_sqlcipher');

    Future<int> open() async {
      final result = await channel.invokeMethod<Map<Object?, Object?>>(
        'openDatabase',
        {'path': dbPath, 'password': password, 'singleInstance': true},
      );
      return result!['id']! as int;
    }

    final db = await open();

    // 1000 x BEGIN/ROLLBACK on the worker, using the same protocol as
    // sqflite_common: BEGIN carries inTransaction: true and no id, the
    // plugin answers with the new transaction id, and the ROLLBACK carries
    // inTransaction: false plus that id. (A ROLLBACK without the id would be
    // queued behind the active transaction and never run.)
    final churn = Future<void>(() async {
      for (var i = 0; i < 1000; i++) {
        final begun = await channel.invokeMethod<Map<Object?, Object?>>(
          'execute',
          {'id': db, 'sql': 'BEGIN', 'inTransaction': true},
        );
        expect(begun?['transactionId'], isA<int>());
        await channel.invokeMethod('execute', {
          'id': db,
          'sql': 'ROLLBACK',
          'inTransaction': false,
          'transactionId': begun!['transactionId'],
        });
      }
    });

    // ... while 2000 single-instance re-opens hit the channel thread.
    final reopens = Future<void>(() async {
      for (var i = 0; i < 2000; i++) {
        expect(
          await open(),
          db,
          reason: 'single-instance dedup must return the same database',
        );
      }
    });

    await Future.wait([churn, reopens]);
    await channel.invokeMethod('closeDatabase', {'id': db});
    await channel.invokeMethod('deleteDatabase', {'path': dbPath});
  });
}
