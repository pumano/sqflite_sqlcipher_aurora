import 'package:flutter_test/flutter_test.dart';
import 'package:sqflite_sqlcipher_aurora/sqflite_sqlcipher_aurora.dart';
import 'package:sqflite_sqlcipher_aurora/sqflite_sqlcipher_aurora_platform_interface.dart';
import 'package:sqflite_sqlcipher_aurora/sqflite_sqlcipher_aurora_method_channel.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';

class MockSqfliteSqlcipherAuroraPlatform
    with MockPlatformInterfaceMixin
    implements SqfliteSqlcipherAuroraPlatform {
  @override
  Future<String?> getPlatformVersion() => Future.value('42');
}

void main() {
  final SqfliteSqlcipherAuroraPlatform initialPlatform = SqfliteSqlcipherAuroraPlatform.instance;

  test('$MethodChannelSqfliteSqlcipherAurora is the default instance', () {
    expect(initialPlatform, isInstanceOf<MethodChannelSqfliteSqlcipherAurora>());
  });

  test('getPlatformVersion', () async {
    SqfliteSqlcipherAurora sqfliteSqlcipherAuroraPlugin = SqfliteSqlcipherAurora();
    MockSqfliteSqlcipherAuroraPlatform fakePlatform = MockSqfliteSqlcipherAuroraPlatform();
    SqfliteSqlcipherAuroraPlatform.instance = fakePlatform;

    expect(await sqfliteSqlcipherAuroraPlugin.getPlatformVersion(), '42');
  });
}
