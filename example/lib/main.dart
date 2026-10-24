import 'dart:io';

import 'package:flutter/material.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

const _marker = 'SECRET_PLAINTEXT_MARKER_XYZ_12345';
const _password = 'my-secret-password';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const MyApp());
  // Run the SQLCipher self-test shortly after the engine is up.
  Future.delayed(const Duration(seconds: 3), _runTest);
}

Future<void> _runTest() async {
  final out = StringBuffer();
  String dbDir = '';
  String dbFile = '';
  try {
    dbDir = await sqlcipher.getDatabasesPath();
    dbFile = '$dbDir/test.db';
    out.writeln('DB_PATH=$dbFile');

    // 1. Open (creates) with a password; create table + insert a row.
    var db = await sqlcipher.openDatabase(dbFile,
        version: 1,
        password: _password,
        onCreate: (sqlcipher.Database d, int v) async {
          await d
              .execute('CREATE TABLE IF NOT EXISTS t(id INTEGER PRIMARY KEY, v TEXT)');
          await d.insert('t', {'v': _marker});
        });
    out.writeln('STEP1_OPEN_WITH_KEY=OK');

    // 2. Read back with the correct password.
    var rows = await db.query('t');
    out.writeln('STEP2_READ_CORRECT_KEY rows=${rows.length} val=${rows.first['v']}');
    await db.close();
  } catch (e, st) {
    out.writeln('ERROR in step 1-2: $e\n$st');
  }

  // 3. Re-open with a WRONG password -> the read must fail (file is encrypted).
  try {
    var db2 = await sqlcipher.openDatabase(dbFile,
        password: 'totally-wrong', singleInstance: false);
    try {
      var rows2 = await db2.query('t');
      out.writeln('STEP3_WRONG_KEY rows=${rows2.length} (UNEXPECTED — should have failed)');
    } catch (e) {
      out.writeln('STEP3_WRONG_KEY=REJECTED (expected): $e');
    }
    await db2.close();
  } catch (e) {
    out.writeln('STEP3_WRONG_KEY=REJECTED (expected, at open): $e');
  }

  // 4. Inspect the raw file bytes: the plaintext marker must NOT be present.
  try {
    final bytes = File(dbFile).readAsBytesSync();
    final raw = String.fromCharCodes(bytes);
    final contains = raw.contains(_marker);
    out.writeln('STEP4_ENCRYPTION_CHECK file_bytes=${bytes.length} '
        'plaintext_in_file=$contains (MUST be false)');
  } catch (e) {
    out.writeln('STEP4_ERROR: $e');
  }

  final summary = out.toString();
  // Persist for inspection: write next to the DB (app-writable in sandbox too)
  // and to a fixed path (works for direct/non-sandbox runs).
  try {
    await File('$dbDir/test_result.txt').writeAsString(summary);
  } catch (_) {}
  try {
    await File('/home/defaultuser/sqlcipher_test_result.txt')
        .writeAsString(summary);
  } catch (_) {}
  debugPrint('=== SQLCIPHER SELF-TEST ===\n$summary');
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'SQLCipher Aurora Test',
      home: Scaffold(
        appBar: AppBar(title: const Text('SQLCipher Aurora Test')),
        body: const Center(
          child: Text(
            'Aurora + SQLCipher\n(see logs / sqlcipher_test_result.txt)',
            textAlign: TextAlign.center,
          ),
        ),
      ),
    );
  }
}
