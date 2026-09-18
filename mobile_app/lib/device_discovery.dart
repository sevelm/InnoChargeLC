import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:html/parser.dart' as html;
import 'package:multicast_dns/multicast_dns.dart';

class InnoChargeDevice {
  const InnoChargeDevice({required this.name, required this.url});
  final String name;
  final String url;
  Uri get appUri => Uri.parse(url).resolve('/app.html');
}

class DeviceDiscoveryService {
  DeviceDiscoveryService({this.webSocketPort = 81});
  final int webSocketPort;
  static const _client = 'innocharge-public-app';
  static const _timeout = Duration(seconds: 2);

  static Uri? parseAddress(String address) {
    final text = address.trim();
    if (text.isEmpty || text.contains(RegExp(r'\s'))) return null;
    final uri = Uri.tryParse(text.contains('://') ? text : 'http://$text');
    if (uri == null ||
        !['http', 'https'].contains(uri.scheme) ||
        uri.host.isEmpty ||
        uri.host.contains(RegExp(r'[\s%]')) ||
        uri.userInfo.isNotEmpty) {
      return null;
    }
    return Uri(
      scheme: uri.scheme,
      host: uri.host,
      port: uri.hasPort ? uri.port : null,
      path: '/',
    );
  }

  static bool isAppPage(String body) {
    final doc = html.parse(body);
    const ids = [
      'cpState',
      'phaseMode',
      'targetChargePower',
      'vehicleConnected',
      'chargingActive',
      'setChargePower',
    ];
    return ids.every((id) => doc.getElementById(id) != null) &&
        doc
            .querySelectorAll('script')
            .any((script) => script.text.contains(_client));
  }

  static Map<String, dynamic>? parseStatus(dynamic message) {
    if (message is! String || message.length > 16384) return null;
    try {
      final data = jsonDecode(message);
      if (data is! Map<String, dynamic> ||
          data['wallboxName'] is! String ||
          data['cpState'] is! String ||
          !['Single-phase', 'Three-phase'].contains(data['phaseMode']) ||
          data['vehicleConnected'] is! bool ||
          data['chargingActive'] is! bool ||
          data['targetChargePower'] is! num ||
          !(data['targetChargePower'] as num).isFinite) {
        return null;
      }
      return data;
    } on FormatException {
      return null;
    }
  }

  Future<InnoChargeDevice?> probe(Uri origin) async {
    final client = HttpClient()
      ..connectionTimeout = const Duration(milliseconds: 800);
    WebSocket? socket;
    var finished = false;
    try {
      final request = await client
          .getUrl(origin.replace(path: '/app.html'))
          .timeout(_timeout);
      request.followRedirects = false;
      final response = await request.close().timeout(_timeout);
      if (response.statusCode != 200 ||
          response.headers.contentType?.mimeType != 'text/html') {
        return null;
      }
      final bytes = <int>[];
      await response
          .forEach((chunk) {
            if (bytes.length + chunk.length > 131072) {
              throw const FormatException('Page too large');
            }
            bytes.addAll(chunk);
          })
          .timeout(_timeout);
      if (!isAppPage(utf8.decode(bytes, allowMalformed: true))) return null;

      // Both the page and the live app protocol must match; HTTP availability alone is not identity.
      final wsUri = origin.replace(
        scheme: origin.scheme == 'https' ? 'wss' : 'ws',
        port: webSocketPort,
        path: '/',
      );
      socket = await WebSocket.connect(wsUri.toString())
          .then((connection) {
            if (finished) unawaited(connection.close());
            return connection;
          })
          .timeout(_timeout);
      socket.add(
        jsonEncode({
          'client': _client,
          'action': 'subscribeUpdates',
          'page': 'app',
        }),
      );
      final status = await socket
          .map(parseStatus)
          .firstWhere((data) => data != null)
          .timeout(_timeout);
      final name = (status!['wallboxName'] as String).trim();
      return InnoChargeDevice(
        name: name.isEmpty ? 'InnoCharge' : name,
        url: origin.toString(),
      );
    } catch (_) {
      return null;
    } finally {
      finished = true;
      client.close(force: true);
      if (socket != null) {
        if (socket.readyState == WebSocket.open) {
          socket.add(
            jsonEncode({
              'client': _client,
              'action': 'unsubscribeUpdates',
              'page': 'app',
            }),
          );
        }
        unawaited(socket.close());
      }
    }
  }

  Future<List<InnoChargeDevice>> discover({
    void Function(InnoChargeDevice)? onDevice,
    bool Function()? shouldContinue,
  }) async {
    bool active() => shouldContinue?.call() ?? true;
    final found = <String, InnoChargeDevice>{};
    final checked = <String>{};
    Future<void> check(Uri uri) async {
      if (!active()) return;
      if (!checked.add(uri.toString())) return;
      final device = await probe(uri);
      if (device != null && active()) {
        found[device.url] = device;
        onDevice?.call(device);
      }
    }

    if (!active()) return [];
    for (final uri in await _mdnsAddresses(active)) {
      if (!active()) return found.values.toList();
      await check(uri);
    }
    if (!active()) return found.values.toList();
    final interfaces = await NetworkInterface.list(
      includeLoopback: false,
      type: InternetAddressType.IPv4,
    );
    final prefixes = <String>{};
    for (final interface in interfaces) {
      for (final address in interface.addresses) {
        final parts = address.address.split('.');
        if (parts.length == 4 && !['127', '169', '0'].contains(parts.first)) {
          prefixes.add(parts.take(3).join('.'));
        }
      }
    }
    // Keep simultaneous requests bounded on phones and embedded controllers.
    for (final prefix in prefixes.take(2)) {
      for (var first = 1; first <= 254; first += 16) {
        if (!active()) return found.values.toList();
        await Future.wait([
          for (var host = first; host < first + 16 && host <= 254; host++)
            check(Uri.parse('http://$prefix.$host/')),
        ]);
      }
    }
    return found.values.toList();
  }

  Future<List<Uri>> _mdnsAddresses(bool Function() active) async {
    final client = MDnsClient();
    final addresses = <Uri>[];
    try {
      await client.start();
      await for (final ptr in client.lookup<PtrResourceRecord>(
        ResourceRecordQuery.serverPointer('_innocharge._tcp.local'),
        timeout: const Duration(seconds: 2),
      )) {
        if (!active()) break;
        await for (final srv in client.lookup<SrvResourceRecord>(
          ResourceRecordQuery.service(ptr.domainName),
          timeout: _timeout,
        )) {
          if (!active()) break;
          await for (final ip in client.lookup<IPAddressResourceRecord>(
            ResourceRecordQuery.addressIPv4(srv.target),
            timeout: _timeout,
          )) {
            if (!active()) break;
            addresses.add(
              Uri(
                scheme: 'http',
                host: ip.address.address,
                port: srv.port,
                path: '/',
              ),
            );
          }
        }
      }
    } catch (_) {
      // Multicast is not available in every WLAN; the IP scan still works.
    } finally {
      client.stop();
    }
    return addresses;
  }
}
