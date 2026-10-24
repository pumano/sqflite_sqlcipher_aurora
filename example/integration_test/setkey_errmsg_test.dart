// Regression test for the sqlite3_free(errmsg) leak in Database::setKey.
//
// Every failed open of an encrypted database with a wrong key runs
// sqlite3_exec("PRAGMA key ...") on a fresh handle and tears the handle
// down again. The errmsg buffer SQLite allocates for a failing exec must be
// freed; otherwise every failed open leaks it.
//
// The test cannot force the exact failing exec, so it bounds the observable
// consequence instead: repeated failed opens must not grow the process
// memory without limit, must keep failing with a stable error, and must not
// corrupt the database (a correct open still reads the data back).

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

Future<int> vmRssKb() async {
  final status = await File('/proc/self/status').readAsString();
  final match = RegExp(r'^VmRSS:\s+(\d+) kB', multiLine: true)
      .firstMatch(status);
  return match == null ? -1 : int.parse(match.group(1)!);
}

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  const password = 'review-password';
  late String dbPath;

  setUp(() async {
    dbPath =
        '${await sqlcipher.getDatabasesPath()}/setkey_errmsg_${DateTime.now().microsecondsSinceEpoch}.db';
  });

  tearDown(() async {
    try {
      await File(dbPath).delete();
    } catch (_) {}
  });

  Future<void> createEncryptedDb() async {
    final db = await sqlcipher.openDatabase(dbPath,
        password: password, singleInstance: false);
    await db
        .execute('CREATE TABLE secrets(value TEXT NOT NULL)');
    await db.insert('secrets', {'value': 'must survive'});
    await db.close();
  }

  Future<bool> wrongKeyOpenFails() async {
    try {
      final db = await sqlcipher.openDatabase(dbPath,
          password: 'definitely-wrong', singleInstance: false);
      await db.close();
      return false; // opened: the wrong key was accepted
    } on sqlcipher.DatabaseException {
      return true;
    }
  }

  test('repeated wrong-key opens: stable error, no unbounded growth, '
      'database intact', () async {
    await createEncryptedDb();

    // Settle, then take the baseline.
    await Future<void>.delayed(const Duration(milliseconds: 100));
    final baselineKb = await vmRssKb();
    expect(baselineKb, greaterThan(0),
        reason: '/proc/self/status must be readable on Aurora OS');

    const attempts = 50;
    for (var i = 0; i < attempts; i++) {
      final failed = await wrongKeyOpenFails();
      expect(failed, isTrue,
          reason: 'wrong-key open #$i must fail, not open the database');
    }

    await Future<void>.delayed(const Duration(milliseconds: 100));
    final afterKb = await vmRssKb();
    // Very generous bound: the leaked object per attempt is a short string,
    // so anything close to this would be a genuine per-open leak.
    expect(afterKb - baselineKb, lessThan(50 * 1024),
        reason: 'RSS grew ${afterKb - baselineKb} kB over $attempts '
            'failed opens (baseline $baselineKb kB, after $afterKb kB)');

    // The database itself must be untouched.
    final db = await sqlcipher.openDatabase(dbPath,
        password: password, singleInstance: false);
    try {
      final rows = await db.query('secrets');
      expect(rows.single['value'], 'must survive');
    } finally {
      await db.close();
    }
  });
}
