import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:innocharge_mobile/main.dart' as app;

void main() {
  testWidgets('startup requests portrait only before displaying the app', (
    tester,
  ) async {
    final orientations = <dynamic>[];
    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
      SystemChannels.platform,
      (call) async {
        if (call.method == 'SystemChrome.setPreferredOrientations') {
          orientations.add(call.arguments);
        }
        return null;
      },
    );
    addTearDown(() {
      tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
        SystemChannels.platform,
        null,
      );
    });
    await app.main();
    expect(orientations, [
      ['DeviceOrientation.portraitUp'],
    ]);
    // Replace the root before discovery starts; this test covers startup only.
    await tester.pumpWidget(const SizedBox.shrink());
  });
}
