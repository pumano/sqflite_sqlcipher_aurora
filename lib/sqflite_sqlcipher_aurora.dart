/// Aurora (Linux) platform implementation of the `sqflite_sqlcipher` plugin.
///
/// The actual work is done by the native C++ plugin (see `aurora/`), which
/// registers a MethodChannel handler for `com.davidmartos96.sqflite_sqlcipher`.
/// The Dart side of the `sqflite_sqlcipher` package is self-contained and talks
/// to that channel directly, so this library only needs to exist to satisfy
/// the plugin's `default_package` declaration. It intentionally has no Dart
/// logic of its own.
library;

/// Placeholder so the package has a public entry point.
class SqfliteSqlcipherAurora {
  /// The MethodChannel name used by the native implementation.
  static const String channelName = 'com.davidmartos96.sqflite_sqlcipher';
}
