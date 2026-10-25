// Regression test for the per-callback exception guard.
//
// A SQL command callback runs either immediately or later, from the drain
// of the pending commands that another call (a COMMIT / ROLLBACK / close)
// triggers. An exception thrown while a DEFERRED callback runs (for
// example a bad_alloc building the response) must answer that callback's
// own call - not the call that triggered the drain, whose reply was already
// sent - and must not interrupt the drain, so the remaining queued commands
// still complete.
//
// The plugin throws on the sentinel SQL below (a test hook); the queue
// order here is: BEGIN -> foreign query (sentinel, deferred) -> foreign
// query (deferred) -> COMMIT. The drain then runs both deferred queries.

import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  const channel = MethodChannel('com.davidmartos96.sqflite_sqlcipher');

  late String dbPath;

  setUp(() async {
    dbPath =
        '${await sqlcipher.getDatabasesPath()}/deferred_exc_'
        '${DateTime.now().microsecondsSinceEpoch}.db';
  });

  tearDown(() async {
    for (final suffix in ['', '-wal', '-shm', '-journal']) {
      try {
        await File('$dbPath$suffix').delete();
      } catch (_) {}
    }
  });

  testWidgets(
    'exception in a deferred query errors that call; the drain continues',
    (tester) async {
      final opened = await channel.invokeMapMethod<String, dynamic>(
        'openDatabase',
        {'path': dbPath, 'password': 'test-key'},
      );
      final id = opened!['id'];
      await channel.invokeMethod<void>('execute', {
        'id': id,
        'sql': 'CREATE TABLE kv (id INTEGER PRIMARY KEY, v TEXT)',
      });
      final txn = await channel.invokeMapMethod<String, dynamic>('execute', {
        'id': id,
        'sql': 'BEGIN',
        'inTransaction': true,
      });
      final txnId = txn!['transactionId'];

      // Both carry no transaction id, so while the transaction is open they
      // are deferred natively. The first one throws when it runs.
      final throwing = channel
          .invokeMethod<Object?>('query', {
            'id': id,
            'sql': 'THROW_STD_EXCEPTION',
          })
          .then<Object?>(
            (value) => 'unexpected success: $value',
            onError: (Object error, StackTrace stack) => error,
          );
      final following = channel
          .invokeMethod<Object?>('query', {
            'id': id,
            'sql': 'SELECT count(*) AS n FROM kv',
          })
          .then<Object?>(
            (value) => value,
            onError: (Object error, StackTrace stack) => error,
          );

      // Ends the transaction, triggering the drain of the two deferred
      // queries.
      await channel.invokeMethod<void>('execute', {
        'id': id,
        'sql': 'COMMIT',
        'inTransaction': false,
        'transactionId': txnId,
      });

      // The deferred query that threw got an error of its own (before the
      // guard it never answered: the exception escaped into the COMMIT's
      // task, whose reply was already sent).
      final first = await throwing.timeout(const Duration(seconds: 5));
      expect(first, isA<PlatformException>());
      expect((first as PlatformException).message, contains('injected'));

      // The drain was not interrupted: the next queued command completed.
      final second = await following.timeout(const Duration(seconds: 5));
      expect(second, isA<Map>());
      expect((second as Map)['rows'], hasLength(1));

      // The database is healthy afterwards.
      await channel.invokeMethod<void>('execute', {
        'id': id,
        'sql': 'INSERT INTO kv (v) VALUES (?)',
        'arguments': ['ok'],
      });
      await channel.invokeMethod<void>('closeDatabase', {'id': id});
    },
  );
}
