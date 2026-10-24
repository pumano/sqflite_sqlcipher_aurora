import 'package:flutter_test/flutter_test.dart';
import 'package:sqflite_sqlcipher_aurora/sqflite_sqlcipher_aurora.dart';

// The Dart side of this package is intentionally a placeholder: all the
// work is done by the native C++ plugin (see `aurora/`), which registers a
// handler for the `sqflite_sqlcipher` method channel. The only Dart-side
// contract worth pinning is the channel name the two sides agree on.
void main() {
  test('native plugin listens on the sqflite_sqlcipher channel', () {
    expect(SqfliteSqlcipherAurora.channelName,
        'com.davidmartos96.sqflite_sqlcipher');
  });
}
