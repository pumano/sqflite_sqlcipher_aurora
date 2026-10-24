// Regression test for
// https://github.com/pumano/sqflite_sqlcipher_aurora/issues/5
//
// Rowids above 2^32-1 (e.g. 4294967297) must be returned in full by both
// insert() and batch(). The native side used a 32-bit int, so
// 4294967297 came back as 1.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  const password = 'review-password';
  late String dbPath;

  setUp(() async {
    dbPath =
        '${await sqlcipher.getDatabasesPath()}/review_rowid_${DateTime.now().microsecondsSinceEpoch}.db';
  });

  tearDown(() async {
    try {
      await File(dbPath).delete();
    } catch (_) {}
  });

  testWidgets('rowid above 2^32 survives insert() and batch()',
      (tester) async {
    final db = await sqlcipher.openDatabase(dbPath, password: password);
    addTearDown(() async {
      try {
        await db.close();
      } catch (_) {}
    });

    await db.execute('CREATE TABLE t(x INTEGER PRIMARY KEY, v TEXT)');
    await db.execute('CREATE TABLE s(x INTEGER PRIMARY KEY, v TEXT)');

    // 2^32 + 1: with a 32-bit int this wrapped to 1.
    const bigId = 4294967297;
    final id = await db.insert('t', {'x': bigId, 'v': 'big'});
    expect(id, bigId);

    // Batch path: the batch result list carries the same value.
    const bigId2 = 4294967298;
    final batch = db.batch();
    batch.insert('t', {'x': bigId2, 'v': 'batch'});
    final results = await batch.commit();
    expect(results.single, bigId2);

    // Small auto-assigned ids are unchanged.
    final smallId = await db.insert('s', {'v': 'small'});
    expect(smallId, 1);
  });
}
