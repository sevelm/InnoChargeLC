import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:html/parser.dart';
import 'package:innocharge_mobile/device_discovery.dart';

import '../tool/prepare_demo_assets.dart';

void main() {
  test('bundled demo matches the real UI and simulator sources', () {
    final generated = File('assets/generated/demo.html').readAsStringSync();
    expect(generated, buildDemoDocument(Directory.current));
    final document = parse(generated);
    expect(
      document.head!.children.first.attributes['content'],
      contains("connect-src 'none'"),
    );
    expect(document.querySelectorAll('script[src], link, iframe'), isEmpty);
    expect(document.querySelector('.brand svg #artwork'), isNotNull);
    expect(document.querySelectorAll('use[href]'), isEmpty);
    expect(
      document.querySelectorAll('script').first.text,
      contains("Object.defineProperty(window, 'WebSocket'"),
    );
    expect(document.getElementById('powerRange'), isNotNull);
  });

  test('cancelled discovery does not start network discovery', () async {
    expect(
      await DeviceDiscoveryService().discover(shouldContinue: () => false),
      isEmpty,
    );
  });
}
