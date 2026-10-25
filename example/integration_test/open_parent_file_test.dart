// Regression test for the open-database hang: when a component of the
// database path's parent is a regular file, the native side used to throw
// filesystem_error in the queue worker; the exception was swallowed by the
// async queue and the Dart future never completed. Opening such a path must
// now complete (within a bounded time) with a DatabaseException.

import 'dart:async';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  test('openDatabase under a regular file fails with an error, not a hang',
      () async {
    final blocker = File(
        '${await sqlcipher.getDatabasesPath()}/blocker_${DateTime.now().microsecondsSinceEpoch}');
    blocker.writeAsStringSync('regular file, not a directory');

    sqlcipher.Database? opened;
    Object? outcome;
    try {
      final open = sqlcipher.openDatabase('${blocker.path}/child/db.sqlite',
          password: 'test-key', singleInstance: false);
      // The timeout guards against the original bug (a swallowed exception
      // left the future pending forever); an open that completes with an
      // error returns well before it.
      opened = await open.timeout(const Duration(seconds: 5));
    } catch (e) {
      outcome = e;
    } finally {
      try {
        await opened?.close();
      } catch (_) {}
      if (blocker.existsSync()) {
        blocker.deleteSync();
      }
    }

    expect(outcome, isNotNull,
        reason: 'the open must fail (neither succeed nor hang), '
            'got: $outcome');
    expect(outcome, isA<sqlcipher.DatabaseException>(),
        reason: 'a failed open must surface as a DatabaseException, '
            'got: $outcome');
    expect('$outcome', contains('parent directory'),
        reason: 'the error must come from the parent-directory check, '
            'got: $outcome');
  });
}
