import 'package:flutter_test/flutter_test.dart';
import 'package:flutter/services.dart';
import 'package:innocharge_mobile/main.dart';
import 'package:innocharge_mobile/device_discovery.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:url_launcher_platform_interface/url_launcher_platform_interface.dart';

class PrivacyLauncher extends UrlLauncherPlatform {
  PrivacyLauncher(this.outcome);
  final String outcome;
  final urls = <String>[];
  LaunchOptions? options;

  @override
  get linkDelegate => null;

  @override
  Future<bool> launchUrl(String url, LaunchOptions options) async {
    urls.add(url);
    this.options = options;
    if (outcome == 'exception') throw PlatformException(code: 'unavailable');
    return outcome == 'success';
  }
}

class EmptyDiscovery extends DeviceDiscoveryService {
  @override
  Future<List<InnoChargeDevice>> discover({
    void Function(InnoChargeDevice)? onDevice,
    bool Function()? shouldContinue,
  }) async => [];
}

class FakeDiscovery extends DeviceDiscoveryService {
  @override
  Future<List<InnoChargeDevice>> discover({
    void Function(InnoChargeDevice)? onDevice,
    bool Function()? shouldContinue,
  }) async {
    const device = InnoChargeDevice(
      name: 'Garage',
      url: 'http://192.168.0.85/',
    );
    onDevice?.call(device);
    return [device];
  }
}

void main() {
  testWidgets('shows the InnoCharge discovery screen', (tester) async {
    SharedPreferences.setMockInitialValues({});
    await tester.pumpWidget(InnoChargeApp(discovery: FakeDiscovery()));
    await tester.pumpAndSettle();

    expect(find.bySemanticsLabel('InnoCharge'), findsOneWidget);
    expect(find.text('Manuell verbinden'), findsOneWidget);
    expect(find.byTooltip('Netzwerk erneut durchsuchen'), findsOneWidget);
    expect(find.text('Verbinden'), findsOneWidget);
    expect(find.text('Demo ansehen'), findsOneWidget);
    expect(find.text('Datenschutz'), findsOneWidget);
    expect(find.text('Garage'), findsOneWidget);
    expect(find.text('1 InnoCharge gefunden.'), findsOneWidget);
  });

  testWidgets('discovery fits a narrow display with enlarged text', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    tester.view.physicalSize = const Size(320, 640);
    tester.view.devicePixelRatio = 1;
    tester.platformDispatcher.textScaleFactorTestValue = 1.4;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    addTearDown(tester.platformDispatcher.clearTextScaleFactorTestValue);
    await tester.pumpWidget(InnoChargeApp(discovery: FakeDiscovery()));
    await tester.pumpAndSettle();
    await tester.ensureVisible(find.text('Datenschutz'));
    await tester.pumpAndSettle();
    expect(find.text('Datenschutz').hitTestable(), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  for (final outcome in ['success', 'unavailable', 'exception']) {
    testWidgets('privacy link without a wallbox: $outcome', (tester) async {
      final previous = UrlLauncherPlatform.instance;
      final launcher = PrivacyLauncher(outcome);
      UrlLauncherPlatform.instance = launcher;
      addTearDown(() => UrlLauncherPlatform.instance = previous);
      SharedPreferences.setMockInitialValues({});
      await tester.pumpWidget(InnoChargeApp(discovery: EmptyDiscovery()));
      await tester.pumpAndSettle();
      expect(launcher.urls, isEmpty);
      await tester.ensureVisible(find.text('Datenschutz'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Datenschutz'));
      await tester.pumpAndSettle();
      expect(launcher.urls, ['https://www.innocharge.at/kontakt/']);
      expect(launcher.options!.mode, PreferredLaunchMode.externalApplication);
      expect(
        find.text('Datenschutzseite konnte nicht geoeffnet werden.'),
        outcome == 'success' ? findsNothing : findsOneWidget,
      );
      expect(find.text('Manuell verbinden'), findsOneWidget);
      expect(tester.takeException(), isNull);
    });
  }
}
