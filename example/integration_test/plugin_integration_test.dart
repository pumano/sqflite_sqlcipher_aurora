// Integration test for the sqflite_sqlcipher_aurora plugin: real
// open / encrypt / read / wrong-password checks against the native
// libsqlcipher binding.
//
// Run: flutter test integration_test/plugin_integration_test.dart -d <device>

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  const password = 'integration-test-password';
  const marker = 'INTEGRATION_TEST_PLAINTEXT_MARKER_98765';
  late String dbPath;

  setUp(() async {
    dbPath = '${await sqlcipher.getDatabasesPath()}/'
        'plugin_${DateTime.now().microsecondsSinceEpoch}.db';
  });

  tearDown(() async {
    try {
      await File(dbPath).delete();
    } catch (_) {}
  });

  testWidgets('encrypted round-trip, wrong key rejected, no plaintext on disk',
      (tester) async {
    // 1. Open (creates) with a password; create the table and insert a row.
    var db = await sqlcipher.openDatabase(dbPath,
        password: password, singleInstance: false);
    await db
        .execute('CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT)');
    await db.insert('t', {'v': marker});

    // 2. Read back with the correct password.
    final rows = await db.query('t');
    expect(rows.single['v'], marker);
    await db.close();

    // 3. Re-open with a wrong password: the open (or the first read) must
    // fail — the file is encrypted.
    var wrongKeyFailed = false;
    try {
      final db2 = await sqlcipher.openDatabase(dbPath,
          password: 'definitely-wrong', singleInstance: false);
      try {
        await db2.query('t');
      } catch (_) {
        wrongKeyFailed = true;
      }
      await db2.close();
    } catch (_) {
      wrongKeyFailed = true;
    }
    expect(wrongKeyFailed, isTrue,
        reason: 'a wrong password must not yield readable data');

    // 4. The plaintext marker must not appear in the raw file bytes.
    final raw =
        String.fromCharCodes(File(dbPath).readAsBytesSync());
    expect(raw.contains(marker), isFalse,
        reason: 'the database file must be encrypted at rest');
  });
}
