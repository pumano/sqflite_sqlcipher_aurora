// Regression test for: deleteDatabase leaving behind -wal/-shm/-journal files.
//
// A WAL-mode database has sidecar files (-wal, -shm) and a rollback journal
// has -journal. A clean close merges/removes them, but a crash or unclean
// close can leave them behind. deleteDatabase must remove the database file
// AND every sidecar, leaving no trace, and must report a real error if the
// main file cannot be removed.

import 'dart:async';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

const suffixes = ['', '-journal', '-wal', '-shm'];

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  late String dbPath;

  setUp(() async {
    dbPath = '${await sqlcipher.getDatabasesPath()}/delete_sidecar_'
        '${DateTime.now().microsecondsSinceEpoch}.db';
  });

  tearDown(() async {
    for (final suffix in suffixes) {
      try {
        final f = File('$dbPath$suffix');
        if (f.existsSync()) f.deleteSync();
      } catch (_) {}
    }
  });

  testWidgets(
      'deleteDatabase removes the database file and every -wal/-shm/-journal sidecar',
      (tester) async {
    // Create a real WAL-mode database and close it cleanly (a clean close
    // merges the WAL and removes -wal/-shm).
    final db = await sqlcipher.openDatabase(dbPath, singleInstance: true);
    await db.rawQuery('PRAGMA journal_mode = WAL');
    await db.execute('CREATE TABLE t(x INTEGER)');
    await db.execute('INSERT INTO t VALUES (1)');
    await db.close();

    // Simulate the sidecar files a crash (unclean close) would leave behind.
    for (final suffix in ['-journal', '-wal', '-shm']) {
      File('$dbPath$suffix').writeAsStringSync('leftover');
    }
    for (final suffix in suffixes) {
      expect(File('$dbPath$suffix').existsSync(), isTrue,
          reason: 'precondition: $dbPath$suffix should exist');
    }

    await sqlcipher.deleteDatabase(dbPath);

    for (final suffix in suffixes) {
      expect(File('$dbPath$suffix').existsSync(), isFalse,
          reason: 'deleteDatabase left behind $dbPath$suffix');
    }
  });

  testWidgets('deleteDatabase on an absent database is a no-op success',
      (tester) async {
    // Nothing to delete; must not throw.
    await sqlcipher.deleteDatabase(dbPath);
    expect(File(dbPath).existsSync(), isFalse);
  });

  testWidgets('an unremovable sidecar yields a controlled error, not a hang',
      (tester) async {
    // The main database is absent, but a leftover "-wal" exists as a
    // non-empty directory: std::filesystem::remove fails on it. The call
    // must complete with an error (or a documented best-effort result) and
    // never lose its response (a swallowed native exception used to hang
    // the Dart Future).
    final walDir = Directory('$dbPath-wal');
    walDir.createSync();
    File('${walDir.path}/entry').writeAsStringSync('leftover');

    try {
      await sqlcipher.deleteDatabase(dbPath).timeout(const Duration(seconds: 5));
    } on TimeoutException {
      fail('deleteDatabase lost its response for an unremovable sidecar');
    } catch (error) {
      // A controlled PlatformException is the expected outcome.
      expect(walDir.existsSync(), isTrue, reason: 'the directory is unremovable');
    }

    // Clean up so the tearDown / next runs are not affected.
    File('${walDir.path}/entry').deleteSync();
    walDir.deleteSync();
  });

  testWidgets('an unremovable database path yields a controlled error, not a hang or a false success',
      (tester) async {
    // The main database path exists as a non-empty directory:
    // std::filesystem::remove fails on it (ENOTEMPTY). The call must
    // complete with an error and leave the directory intact; it must not
    // hang (a swallowed native exception used to) nor report a false
    // success (the pre-fix behaviour).
    final dbDir = Directory(dbPath);
    dbDir.createSync();
    File('${dbDir.path}/entry').writeAsStringSync('leftover');

    Object? outcome;
    try {
      await sqlcipher.deleteDatabase(dbPath)
          .timeout(const Duration(seconds: 5));
    } on TimeoutException {
      fail('deleteDatabase lost its response for an unremovable path');
    } catch (error) {
      outcome = error;
    }

    expect(outcome, isNotNull,
        reason: 'deleteDatabase must fail for a non-empty directory path, '
            'a success would be a lie');
    expect(dbDir.existsSync(), isTrue,
        reason: 'the unremovable directory must be left intact');

    // Clean up so the tearDown / next runs are not affected.
    File('${dbDir.path}/entry').deleteSync();
    dbDir.deleteSync();
  });
}
