// Regression test for
// https://github.com/pumano/sqflite_sqlcipher_aurora/issues/1
//
// Opening an encrypted database with a missing or wrong password must fail
// with an error and must NOT delete or modify the original file.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

Future<List<int>> _readBytes(String path) async {
  final file = File(path);
  if (!await file.exists()) {
    return const [];
  }
  return file.readAsBytesSync();
}

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  const password = 'review-password';
  late String dbPath;

  setUp(() async {
    dbPath =
        '${await sqlcipher.getDatabasesPath()}/review_missing_key_${DateTime.now().microsecondsSinceEpoch}.db';
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

  Future<sqlcipher.Database?> tryOpen(
      {String? password, bool? passPassword}) async {
    try {
      return passPassword ?? (password != null)
          ? await sqlcipher.openDatabase(dbPath,
              password: password, singleInstance: false)
          : await sqlcipher.openDatabase(dbPath, singleInstance: false);
    } catch (_) {
      return null;
    }
  }

  test('wrong password: open fails, file bytes preserved', () async {
    await createEncryptedDb();
    final before = await _readBytes(dbPath);
    expect(before, isNotEmpty);

    final reopened = await tryOpen(password: 'definitely-wrong');
    expect(reopened, isNull,
        reason: 'opening an encrypted db with a wrong key must fail');

    final after = await _readBytes(dbPath);
    expect(after, before,
        reason: 'a failed open must not modify the database file');

    // The original database is still readable with the correct password.
    final db = await sqlcipher.openDatabase(dbPath,
        password: password, singleInstance: false);
    try {
      final rows = await db.query('secrets');
      expect(rows.single['value'], 'must survive');
    } finally {
      await db.close();
    }
  });

  test('missing password: open fails, file bytes preserved', () async {
    await createEncryptedDb();
    final before = await _readBytes(dbPath);
    expect(before, isNotEmpty);

    final reopened = await tryOpen(passPassword: false);
    expect(reopened, isNull,
        reason: 'opening an encrypted db without a key must fail');

    final after = await _readBytes(dbPath);
    expect(after, before,
        reason: 'a failed open must not modify the database file');

    final db = await sqlcipher.openDatabase(dbPath,
        password: password, singleInstance: false);
    try {
      final rows = await db.query('secrets');
      expect(rows.single['value'], 'must survive');
    } finally {
      await db.close();
    }
  });

  test('empty password: open fails, file bytes preserved', () async {
    await createEncryptedDb();
    final before = await _readBytes(dbPath);
    expect(before, isNotEmpty);

    final reopened = await tryOpen(password: '');
    expect(reopened, isNull,
        reason: 'opening an encrypted db with an empty key must fail');

    final after = await _readBytes(dbPath);
    expect(after, before,
        reason: 'a failed open must not modify the database file');

    final db = await sqlcipher.openDatabase(dbPath,
        password: password, singleInstance: false);
    try {
      final rows = await db.query('secrets');
      expect(rows.single['value'], 'must survive');
    } finally {
      await db.close();
    }
  });

  test('plain database with a password still opens and is recreated-free',
      () async {
    // A database created without encryption opens fine without a password
    // and must survive a failed (passworded) open attempt as well.
    var db = await sqlcipher.openDatabase(dbPath, singleInstance: false);
    await db.execute('CREATE TABLE plain(value TEXT)');
    await db.insert('plain', {'value': 'plain data'});
    await db.close();
    final before = await _readBytes(dbPath);
    expect(before, isNotEmpty);

    // Opening a plain database with a password: the key does not match,
    // the open must fail, but the file must be preserved.
    final reopened = await tryOpen(password: password);
    expect(reopened, isNull);

    final after = await _readBytes(dbPath);
    expect(after, before,
        reason: 'a failed open must not modify the database file');

    db = await sqlcipher.openDatabase(dbPath, singleInstance: false);
    try {
      final rows = await db.query('plain');
      expect(rows.single['value'], 'plain data');
    } finally {
      await db.close();
    }
  });
}
