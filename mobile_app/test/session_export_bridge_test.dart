import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:innocharge_mobile/device_discovery.dart';
import 'package:innocharge_mobile/main.dart';
import 'package:webview_flutter_platform_interface/webview_flutter_platform_interface.dart';

import 'demo_navigation_test.dart' as fake;

class ExportController extends fake.DeviceController {
  ExportController(super.params);
  late JavaScriptChannelParams channel;
  final scripts = <String>[];

  @override
  Future<void> addJavaScriptChannel(JavaScriptChannelParams params) async {
    channel = params;
  }

  @override
  Future<void> runJavaScript(String javaScript) async {
    scripts.add(javaScript);
  }

  @override
  Future<Object> runJavaScriptReturningResult(String javaScript) async => true;
}

class ExportDelegate extends fake.DemoDelegate {
  ExportDelegate(super.params);
  late PageEventCallback finished;

  @override
  Future<void> setOnPageFinished(PageEventCallback callback) async {
    finished = callback;
  }
}

class ExportPlatform extends fake.DevicePlatform {
  @override
  PlatformWebViewController createPlatformWebViewController(
    PlatformWebViewControllerCreationParams params,
  ) => controller = ExportController(params);

  @override
  PlatformNavigationDelegate createPlatformNavigationDelegate(
    PlatformNavigationDelegateCreationParams params,
  ) => delegate = ExportDelegate(params);
}

void main() {
  testWidgets(
    'the native bridge advertises file export, offers all formats, and reports cancellation',
    (tester) async {
      final previous = WebViewPlatform.instance;
      final platform = ExportPlatform();
      WebViewPlatform.instance = platform;
      if (previous != null) {
        addTearDown(() => WebViewPlatform.instance = previous);
      }
      await tester.pumpWidget(
        const MaterialApp(
          home: WebAppPage(
            device: InnoChargeDevice(
              name: 'Garage',
              url: 'http://10.10.10.11/',
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();
      final controller = platform.controller as ExportController;
      (platform.delegate as ExportDelegate).finished(
        'http://10.10.10.11/app.html',
      );
      await tester.pumpAndSettle();
      expect(
        controller.scripts.join(),
        contains('InnoChargeExportSupported = true'),
      );
      expect(
        controller.scripts.join(),
        contains('InnoChargeNavigationSupported = true'),
      );
      controller.channel.onMessageReceived(
        JavaScriptMessage(
          message: jsonEncode({
            'action': 'exportSessions',
            'wallboxName': 'Garage',
            'selection': 'All sessions',
            'sessions': [
              {'transactionId': 1, 'userName': 'Anna', 'energyWh': 12000},
            ],
          }),
        ),
      );
      await tester.pumpAndSettle();
      for (final label in [
        'Save PDF',
        'Share PDF / Send by email',
        'Save CSV',
        'Share CSV / Send by email',
      ]) {
        expect(find.text(label), findsOneWidget);
      }
      Navigator.of(tester.element(find.text('Save PDF'))).pop();
      await tester.pumpAndSettle();
      expect(controller.scripts.join(), contains('Export cancelled.'));
      expect(tester.takeException(), isNull);
      await tester.pumpWidget(const SizedBox.shrink());
    },
  );

  testWidgets(
    'settings navigation offers icons and the back bridge leaves the wallbox',
    (tester) async {
      final previous = WebViewPlatform.instance;
      final platform = ExportPlatform();
      WebViewPlatform.instance = platform;
      if (previous != null) {
        addTearDown(() => WebViewPlatform.instance = previous);
      }
      await tester.pumpWidget(
        MaterialApp(
          home: Builder(
            builder: (context) => Scaffold(
              body: TextButton(
                onPressed: () => Navigator.of(context).push(
                  MaterialPageRoute<void>(
                    builder: (_) => const WebAppPage(
                      device: InnoChargeDevice(
                        name: 'Garage',
                        url: 'http://10.10.10.11/',
                      ),
                    ),
                  ),
                ),
                child: const Text('Open wallbox'),
              ),
            ),
          ),
        ),
      );
      await tester.tap(find.text('Open wallbox'));
      await tester.pumpAndSettle();
      await tester.tap(find.byIcon(Icons.settings_outlined));
      await tester.pumpAndSettle();
      expect(find.byIcon(Icons.refresh), findsOneWidget);
      expect(find.byIcon(Icons.swap_horiz), findsOneWidget);
      expect(find.text('Neu laden'), findsNothing);
      expect(find.text('Wallbox wechseln'), findsNothing);
      Navigator.of(tester.element(find.byIcon(Icons.refresh))).pop();
      await tester.pumpAndSettle();
      (platform.controller as ExportController).channel.onMessageReceived(
        JavaScriptMessage(message: 'back'),
      );
      await tester.pumpAndSettle();
      expect(find.text('Open wallbox'), findsOneWidget);
      expect(find.byType(WebAppPage), findsNothing);
      expect(tester.takeException(), isNull);
      await tester.pumpWidget(const SizedBox.shrink());
    },
  );
}
