import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:innocharge_mobile/device_discovery.dart';
import 'package:innocharge_mobile/main.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:webview_flutter_platform_interface/webview_flutter_platform_interface.dart';

class OfflineDiscovery extends DeviceDiscoveryService {
  int probes = 0;
  bool Function()? active;

  @override
  Future<InnoChargeDevice?> probe(Uri origin) async {
    probes++;
    return null;
  }

  @override
  Future<List<InnoChargeDevice>> discover({
    void Function(InnoChargeDevice)? onDevice,
    bool Function()? shouldContinue,
  }) async {
    active = shouldContinue;
    return [];
  }
}

class DemoPlatform extends WebViewPlatform {
  late DemoController controller;
  late DemoDelegate delegate;

  @override
  PlatformWebViewController createPlatformWebViewController(
    PlatformWebViewControllerCreationParams params,
  ) => controller = DemoController(params);

  @override
  PlatformNavigationDelegate createPlatformNavigationDelegate(
    PlatformNavigationDelegateCreationParams params,
  ) => delegate = DemoDelegate(params);

  @override
  PlatformWebViewWidget createPlatformWebViewWidget(
    PlatformWebViewWidgetCreationParams params,
  ) => DemoWidget(params);
}

class DemoController extends PlatformWebViewController {
  DemoController(super.params) : super.implementation();
  final documents = <String>[];
  late DemoDelegate delegate;

  @override
  Future<void> setBackgroundColor(Color color) async {}
  @override
  Future<void> setJavaScriptMode(JavaScriptMode mode) async {}
  @override
  Future<void> setPlatformNavigationDelegate(
    PlatformNavigationDelegate handler,
  ) async {
    delegate = handler as DemoDelegate;
  }

  @override
  Future<void> loadHtmlString(String html, {String? baseUrl}) async {
    expect(baseUrl, isNull);
    documents.add(html);
    delegate.progress(100);
  }

  // loadRequest and addJavaScriptChannel deliberately remain unimplemented:
  // either would fail the test if the demo used the real device path.
}

class DemoDelegate extends PlatformNavigationDelegate {
  DemoDelegate(super.params) : super.implementation();
  late NavigationRequestCallback navigation;
  late ProgressCallback progress;
  @override
  Future<void> setOnNavigationRequest(
    NavigationRequestCallback callback,
  ) async {
    navigation = callback;
  }

  @override
  Future<void> setOnProgress(ProgressCallback callback) async {
    progress = callback;
  }

  @override
  Future<void> setOnPageStarted(PageEventCallback callback) async {}
  @override
  Future<void> setOnPageFinished(PageEventCallback callback) async {}
  @override
  Future<void> setOnWebResourceError(WebResourceErrorCallback callback) async {}
}

class DemoWidget extends PlatformWebViewWidget {
  DemoWidget(super.params) : super.implementation();
  @override
  Widget build(BuildContext context) => const SizedBox.expand();
}

void main() {
  testWidgets(
    'demo opens offline, blocks remote navigation, resets and closes without changing preferences',
    (tester) async {
      final previous = WebViewPlatform.instance;
      final platform = DemoPlatform();
      WebViewPlatform.instance = platform;
    if (previous != null) {
      addTearDown(() => WebViewPlatform.instance = previous);
    }
      SharedPreferences.setMockInitialValues({
        'last_device_url': 'http://192.168.0.85/',
        'last_device_name': 'Garage',
      });
      final discovery = OfflineDiscovery();
      await tester.pumpWidget(InnoChargeApp(discovery: discovery));
      await tester.pumpAndSettle();
      expect(discovery.probes, 1);
      await tester.tap(find.text('Demo ansehen'));
      await tester.pumpAndSettle();
      expect(find.text('DEMO'), findsOneWidget);
      expect(platform.controller.documents, hasLength(1));
      expect(
        platform.controller.documents.single,
        contains("connect-src 'none'"),
      );
      expect(discovery.active!(), isFalse);
      expect(discovery.probes, 1);
      for (final url in [
        'http://192.168.0.85/',
        'https://example.com/',
        'file:///etc/passwd',
      ]) {
        expect(
          await platform.delegate.navigation(
            NavigationRequest(url: url, isMainFrame: true),
          ),
          NavigationDecision.prevent,
        );
      }
      await tester.tap(find.byTooltip('Demo zuruecksetzen'));
      await tester.pumpAndSettle();
      expect(platform.controller.documents, hasLength(2));
      await tester.tap(find.byTooltip('Demo beenden'));
      await tester.pumpAndSettle();
      expect(find.text('Demo ansehen'), findsOneWidget);
      final prefs = await SharedPreferences.getInstance();
      expect(prefs.getString('last_device_url'), 'http://192.168.0.85/');
      expect(prefs.getString('last_device_name'), 'Garage');
      expect(tester.takeException(), isNull);
    },
  );
}
