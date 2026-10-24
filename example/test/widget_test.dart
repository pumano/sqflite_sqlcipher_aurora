// Widget test for the example app UI.

import 'package:flutter_test/flutter_test.dart';

import 'package:sqflite_sqlcipher_aurora_example/main.dart';

void main() {
  testWidgets('shows the self-test hint', (WidgetTester tester) async {
    await tester.pumpWidget(const MyApp());

    expect(find.text('SQLCipher Aurora Test'), findsOneWidget);
    expect(
      find.textContaining('sqlcipher_test_result.txt'),
      findsOneWidget,
    );
  });
}
