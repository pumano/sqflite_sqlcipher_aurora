// sqlite3_expanded_sql() leak regression test (issue #8).
//
// Before the fix every prepared statement with bound arguments made
// sqlite3_expanded_sql() allocate a new copy of the SQL (arguments
// inlined) which was streamed to a null stream and never freed, even
// when SQL logging was off. 100 queries with a 4 KiB argument leaked
// ~412 KB.
//
// The test measures the process RSS around two identical bursts of such
// queries: the first burst absorbs one-time allocator growth, the second
// must be flat. The old code leaked ~33 KB per query, so the second
// burst grew the RSS by several MB; the fixed code does not grow.
//
// Run: flutter test integration_test/expanded_sql_leak_test.dart -d <device>

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:sqflite_sqlcipher/sqflite.dart' as sqlcipher;

int _vmRssKb() {
  for (final line in File('/proc/self/status').readAsLinesSync()) {
    if (line.startsWith('VmRSS:')) {
      return int.parse(line.split(RegExp(r'\s+'))[1]);
    }
  }
  return -1;
}

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  const password = 'secret-password';
  // A 4 KiB argument: the expanded SQL (argument inlined) is ~4 KiB per
  // statement, so the burst below leaks ~800 KB on the old code.
  final bigArgument = 'x' * 4096;
  const burst = 200;

  late dynamic db;
  late String dbPath;

  setUp(() async {
    dbPath = '${await sqlcipher.getDatabasesPath()}/'
        'expanded_sql_leak_${DateTime.now().microsecondsSinceEpoch}.db';
  });

  tearDown(() async {
    try {
      await db.close();
    } catch (_) {}
    try {
      await File(dbPath).delete();
    } catch (_) {}
  });

  Future<void> runBigQuery() async {
    await db.execute('SELECT ?', [bigArgument]);
  }

  testWidgets('expanded SQL is not leaked when logging is off',
      (tester) async {
    db = await sqlcipher.openDatabase(dbPath, password: password);

    // Let the allocator reach a steady state before measuring: the first
    // burst absorbs one-time allocator/VM growth that is unrelated to
    // the leak.
    for (int i = 0; i < 50; i++) {
      await runBigQuery();
    }
    await tester.pump();
    for (int i = 0; i < burst; i++) {
      await runBigQuery();
    }
    await tester.pump();

    final rssBeforeSecondBurst = _vmRssKb();
    expect(rssBeforeSecondBurst, greaterThan(0),
        reason: 'VmRSS must be readable on this platform');

    for (int i = 0; i < burst; i++) {
      await runBigQuery();
    }
    await tester.pump();

    final rssAfterSecondBurst = _vmRssKb();
    // The old code leaked ~33 KB per query (~6.5 MB for the burst);
    // allow generous headroom for allocator noise.
    expect(rssAfterSecondBurst - rssBeforeSecondBurst, lessThan(512),
        reason: 'RSS grew by '
            '${rssAfterSecondBurst - rssBeforeSecondBurst} kB over '
            '$burst queries with a 4 KiB argument - the expanded SQL is '
            'leaking');
  });
}
