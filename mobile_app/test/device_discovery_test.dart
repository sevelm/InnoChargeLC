import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:innocharge_mobile/device_discovery.dart';

void main() {
  const appHtml = '''<!doctype html><html><body>
    <span id="cpState"></span><span id="phaseMode"></span>
    <span id="targetChargePower"></span><span id="vehicleConnected"></span>
    <span id="chargingActive"></span><input id="setChargePower">
    <script>const APP_CLIENT = 'innocharge-public-app';</script>
    </body></html>''';
  final status = {
    'wallboxName': 'Garage',
    'cpState': 'State B',
    'phaseMode': 'Three-phase',
    'targetChargePower': 4.4,
    'vehicleConnected': true,
    'chargingActive': false,
  };

  test('recognizes the app contract, rejects a generic HTTP page', () {
    expect(DeviceDiscoveryService.isAppPage(appHtml), isTrue);
    expect(
      DeviceDiscoveryService.isAppPage(
        '<html><title>InnoCharge Router</title></html>',
      ),
      isFalse,
    );
    expect(
      DeviceDiscoveryService.isAppPage(
        appHtml.replaceAll('innocharge-public-app', 'other'),
      ),
      isFalse,
    );
  });

  final projectPage = File('../data/app.html');
  test(
    'current firmware page matches discovery',
    () {
      expect(
        DeviceDiscoveryService.isAppPage(projectPage.readAsStringSync()),
        isTrue,
      );
    },
    skip: !projectPage.existsSync()
        ? 'Standalone mobile_app checkout without firmware data'
        : false,
  );

  test('requires the complete typed live status', () {
    expect(DeviceDiscoveryService.parseStatus(jsonEncode(status)), isNotNull);
    expect(DeviceDiscoveryService.parseStatus('{broken'), isNull);
    expect(
      DeviceDiscoveryService.parseStatus(
        jsonEncode({'wallboxName': 'InnoCharge'}),
      ),
      isNull,
    );
    expect(
      DeviceDiscoveryService.parseStatus(
        jsonEncode({...status, 'chargingActive': 'yes'}),
      ),
      isNull,
    );
    expect(
      DeviceDiscoveryService.parseStatus(
        jsonEncode({...status, 'phaseMode': 'unknown'}),
      ),
      isNull,
    );
  });

  test('normalizes manual addresses without preserving unrelated paths', () {
    expect(
      DeviceDiscoveryService.parseAddress('192.168.0.85')?.toString(),
      'http://192.168.0.85/',
    );
    expect(
      DeviceDiscoveryService.parseAddress(
        'http://192.168.0.85:8080/index.html?x=1',
      )?.toString(),
      'http://192.168.0.85:8080/',
    );
    expect(DeviceDiscoveryService.parseAddress('javascript://alert'), isNull);
    expect(DeviceDiscoveryService.parseAddress(''), isNull);
    expect(DeviceDiscoveryService.parseAddress('not a host'), isNull);
  });

  test(
    'probe requires app HTML AND a matching WebSocket, then unsubscribes',
    () async {
      final http = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      final ws = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      final commands = <Map<String, dynamic>>[];
      var serveApp = true;
      var validStatus = true;
      http.listen((request) {
        request.response.headers.contentType = ContentType.html;
        request.response.write(serveApp ? appHtml : '<html>NAS</html>');
        request.response.close();
      });
      ws.listen((request) async {
        final socket = await WebSocketTransformer.upgrade(request);
        socket.listen((message) {
          final command = jsonDecode(message as String) as Map<String, dynamic>;
          commands.add(command);
          if (command['action'] == 'subscribeUpdates') {
            socket.add(jsonEncode(validStatus ? status : {'device': 'NAS'}));
            if (!validStatus) socket.close();
          }
        });
      });
      addTearDown(() async {
        await http.close(force: true);
        await ws.close(force: true);
      });
      final service = DeviceDiscoveryService(webSocketPort: ws.port);
      final uri = Uri.parse('http://127.0.0.1:${http.port}/');
      final device = await service.probe(uri);
      expect(device?.name, 'Garage');
      expect(device?.appUri.path, '/app.html');
      await Future<void>.delayed(const Duration(milliseconds: 50));
      expect(commands.map((c) => c['action']), [
        'subscribeUpdates',
        'unsubscribeUpdates',
      ]);
      expect(
        commands.every((c) => c['client'] == 'innocharge-public-app'),
        isTrue,
      );
      serveApp = false;
      expect(await service.probe(uri), isNull);
      serveApp = true;
      validStatus = false;
      expect(await service.probe(uri), isNull);
      expect(
        commands.any((c) => c['action'] == 'setChargeParameters'),
        isFalse,
      );
    },
  );
}
