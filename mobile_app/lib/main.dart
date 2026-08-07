import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:multicast_dns/multicast_dns.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:webview_flutter/webview_flutter.dart';

void main() {
  runApp(const InnoChargeApp());
}

class InnoChargeApp extends StatelessWidget {
  const InnoChargeApp({super.key});

  @override
  Widget build(BuildContext context) {
    const seed = Color(0xff13a085);

    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'InnoCharge',
      themeMode: ThemeMode.system,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: seed,
          brightness: Brightness.light,
        ),
        useMaterial3: true,
      ),
      darkTheme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: seed,
          brightness: Brightness.dark,
        ),
        useMaterial3: true,
      ),
      home: const DeviceDiscoveryPage(),
    );
  }
}

class InnoChargeDevice {
  const InnoChargeDevice({
    required this.name,
    required this.url,
    required this.source,
  });

  final String name;
  final String url;
  final String source;
}

class DeviceDiscoveryPage extends StatefulWidget {
  const DeviceDiscoveryPage({super.key});

  @override
  State<DeviceDiscoveryPage> createState() => _DeviceDiscoveryPageState();
}

class _DeviceDiscoveryPageState extends State<DeviceDiscoveryPage> {
  static const _serviceName = '_innocharge._tcp.local';
  static const _lastUrlKey = 'last_device_url';
  static const _lastNameKey = 'last_device_name';

  final _manualController = TextEditingController();
  final List<InnoChargeDevice> _devices = [];

  bool _isSearching = false;
  String? _status;
  InnoChargeDevice? _lastDevice;

  @override
  void initState() {
    super.initState();
    _loadLastDevice();
    _searchDevices();
  }

  @override
  void dispose() {
    _manualController.dispose();
    super.dispose();
  }

  Future<void> _loadLastDevice() async {
    final prefs = await SharedPreferences.getInstance();
    final url = prefs.getString(_lastUrlKey);
    if (url == null || url.isEmpty || !mounted) {
      return;
    }

    setState(() {
      _lastDevice = InnoChargeDevice(
        name: prefs.getString(_lastNameKey) ?? 'Zuletzt verbunden',
        url: url,
        source: 'Gespeichert',
      );
    });
  }

  Future<void> _searchDevices() async {
    if (_isSearching) {
      return;
    }

    setState(() {
      _isSearching = true;
      _status = 'Suche per Bonjour...';
      _devices.clear();
    });

    final found = <String, InnoChargeDevice>{};
    for (final device in await _discoverWithMdns()) {
      found[device.url] = device;
    }

    if (mounted) {
      setState(() {
        _devices
          ..clear()
          ..addAll(found.values);
        _status = found.isEmpty
            ? 'Kein Bonjour-Treffer. Suche im lokalen Netzwerk...'
            : 'Bonjour-Suche abgeschlossen.';
      });
    }

    if (found.isEmpty) {
      for (final device in await _scanLocalNetwork()) {
        found[device.url] = device;
      }
    }

    if (!mounted) {
      return;
    }

    setState(() {
      _devices
        ..clear()
        ..addAll(found.values);
      _isSearching = false;
      _status = found.isEmpty
          ? 'Keine Wallbox gefunden. IP manuell eingeben.'
          : '${found.length} Geraet(e) gefunden.';
    });
  }

  Future<List<InnoChargeDevice>> _discoverWithMdns() async {
    final client = MDnsClient();
    final devices = <InnoChargeDevice>[];

    try {
      await client.start();
      await for (final ptr in client.lookup<PtrResourceRecord>(
        ResourceRecordQuery.serverPointer(_serviceName),
      ).timeout(const Duration(seconds: 4), onTimeout: (sink) => sink.close())) {
        await for (final srv in client.lookup<SrvResourceRecord>(
          ResourceRecordQuery.service(ptr.domainName),
        ).timeout(const Duration(seconds: 2), onTimeout: (sink) => sink.close())) {
          final addresses = await client
              .lookup<IPAddressResourceRecord>(
                ResourceRecordQuery.addressIPv4(srv.target),
              )
              .timeout(
                const Duration(seconds: 2),
                onTimeout: (sink) => sink.close(),
              )
              .toList();

          for (final address in addresses) {
            devices.add(
              InnoChargeDevice(
                name: _cleanDeviceName(ptr.domainName),
                url: 'http://${address.address.address}:${srv.port}/',
                source: 'Bonjour',
              ),
            );
          }
        }
      }
    } catch (_) {
      // Some networks block multicast discovery; the IP scan below is the fallback.
    } finally {
      client.stop();
    }

    return devices;
  }

  Future<List<InnoChargeDevice>> _scanLocalNetwork() async {
    final prefixes = await _localIpv4Prefixes();
    final devices = <InnoChargeDevice>[];

    for (final prefix in prefixes.take(2)) {
      final futures = <Future<InnoChargeDevice?>>[];
      for (var host = 1; host <= 254; host++) {
        futures.add(_probeHost('$prefix.$host'));
      }

      final results = await Future.wait(futures);
      devices.addAll(results.whereType<InnoChargeDevice>());
    }

    return devices;
  }

  Future<Set<String>> _localIpv4Prefixes() async {
    final prefixes = <String>{};
    final interfaces = await NetworkInterface.list(
      includeLoopback: false,
      type: InternetAddressType.IPv4,
    );

    for (final interface in interfaces) {
      for (final address in interface.addresses) {
        final parts = address.address.split('.');
        if (parts.length == 4 &&
            parts.first != '127' &&
            parts.first != '169' &&
            parts.first != '0') {
          prefixes.add('${parts[0]}.${parts[1]}.${parts[2]}');
        }
      }
    }

    return prefixes;
  }

  Future<InnoChargeDevice?> _probeHost(String host) async {
    final client = HttpClient()..connectionTimeout = const Duration(milliseconds: 450);
    try {
      final request = await client
          .getUrl(Uri.parse('http://$host/'))
          .timeout(const Duration(milliseconds: 700));
      final response = await request.close().timeout(const Duration(milliseconds: 700));
      final server = response.headers.value(HttpHeaders.serverHeader) ?? '';
      await response.drain<void>();

      if (response.statusCode < 500) {
        return InnoChargeDevice(
          name: server.toLowerCase().contains('innocharge')
              ? 'InnoCharge $host'
              : 'Weboberflaeche $host',
          url: 'http://$host/',
          source: 'IP-Scan',
        );
      }
    } catch (_) {
      return null;
    } finally {
      client.close(force: true);
    }

    return null;
  }

  String _cleanDeviceName(String domainName) {
    return domainName
        .replaceAll('._innocharge._tcp.local', '')
        .replaceAll('.local', '')
        .replaceAll(r'\032', ' ')
        .trim()
        .isEmpty
        ? 'InnoCharge'
        : domainName
            .replaceAll('._innocharge._tcp.local', '')
            .replaceAll('.local', '')
            .replaceAll(r'\032', ' ')
            .trim();
  }

  Future<void> _openDevice(InnoChargeDevice device) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_lastUrlKey, device.url);
    await prefs.setString(_lastNameKey, device.name);

    if (!mounted) {
      return;
    }

    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) => WebAppPage(device: device),
      ),
    );
  }

  void _openManualAddress() {
    final text = _manualController.text.trim();
    if (text.isEmpty) {
      return;
    }

    final url = text.startsWith('http://') || text.startsWith('https://')
        ? text
        : 'http://$text/';

    _openDevice(
      InnoChargeDevice(
        name: 'Manuelle Adresse',
        url: url,
        source: 'Manuell',
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;

    return Scaffold(
      body: SafeArea(
        child: RefreshIndicator(
          onRefresh: _searchDevices,
          child: ListView(
            padding: const EdgeInsets.fromLTRB(20, 24, 20, 32),
            children: [
              Row(
                children: [
                  Container(
                    width: 52,
                    height: 52,
                    decoration: BoxDecoration(
                      color: colorScheme.primaryContainer,
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: Icon(
                      Icons.ev_station,
                      color: colorScheme.onPrimaryContainer,
                      size: 30,
                    ),
                  ),
                  const SizedBox(width: 14),
                  const Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          'InnoCharge',
                          style: TextStyle(
                            fontSize: 28,
                            fontWeight: FontWeight.w700,
                          ),
                        ),
                        SizedBox(height: 2),
                        Text('Wallbox verbinden'),
                      ],
                    ),
                  ),
                  IconButton(
                    tooltip: 'Aktualisieren',
                    onPressed: _isSearching ? null : _searchDevices,
                    icon: const Icon(Icons.refresh),
                  ),
                ],
              ),
              const SizedBox(height: 28),
              if (_isSearching) ...[
                const LinearProgressIndicator(),
                const SizedBox(height: 12),
              ],
              Text(
                _status ?? 'Bereit.',
                style: TextStyle(color: colorScheme.onSurfaceVariant),
              ),
              const SizedBox(height: 24),
              if (_lastDevice != null) ...[
                _DeviceTile(
                  device: _lastDevice!,
                  icon: Icons.history,
                  onTap: () => _openDevice(_lastDevice!),
                ),
                const SizedBox(height: 12),
              ],
              for (final device in _devices) ...[
                _DeviceTile(
                  device: device,
                  icon: Icons.electrical_services,
                  onTap: () => _openDevice(device),
                ),
                const SizedBox(height: 12),
              ],
              const SizedBox(height: 12),
              TextField(
                controller: _manualController,
                keyboardType: TextInputType.url,
                textInputAction: TextInputAction.go,
                onSubmitted: (_) => _openManualAddress(),
                decoration: const InputDecoration(
                  labelText: 'IP oder Adresse',
                  hintText: '192.168.1.34',
                  border: OutlineInputBorder(),
                  prefixIcon: Icon(Icons.link),
                ),
              ),
              const SizedBox(height: 12),
              FilledButton.icon(
                onPressed: _openManualAddress,
                icon: const Icon(Icons.login),
                label: const Text('Verbinden'),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _DeviceTile extends StatelessWidget {
  const _DeviceTile({
    required this.device,
    required this.icon,
    required this.onTap,
  });

  final InnoChargeDevice device;
  final IconData icon;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;

    return Material(
      color: colorScheme.surfaceContainerHighest,
      borderRadius: BorderRadius.circular(8),
      child: InkWell(
        borderRadius: BorderRadius.circular(8),
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.all(14),
          child: Row(
            children: [
              Icon(icon, color: colorScheme.primary),
              const SizedBox(width: 14),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      device.name,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(fontWeight: FontWeight.w700),
                    ),
                    const SizedBox(height: 3),
                    Text(
                      '${device.url}  ${device.source}',
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(color: colorScheme.onSurfaceVariant),
                    ),
                  ],
                ),
              ),
              const Icon(Icons.chevron_right),
            ],
          ),
        ),
      ),
    );
  }
}

class WebAppPage extends StatefulWidget {
  const WebAppPage({super.key, required this.device});

  final InnoChargeDevice device;

  @override
  State<WebAppPage> createState() => _WebAppPageState();
}

class _WebAppPageState extends State<WebAppPage> {
  late final WebViewController _controller;
  var _progress = 0;

  @override
  void initState() {
    super.initState();
    _controller = WebViewController()
      ..setJavaScriptMode(JavaScriptMode.unrestricted)
      ..setNavigationDelegate(
        NavigationDelegate(
          onProgress: (progress) => setState(() => _progress = progress),
        ),
      )
      ..loadRequest(Uri.parse(widget.device.url));
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(widget.device.name),
        actions: [
          IconButton(
            tooltip: 'Neu laden',
            onPressed: _controller.reload,
            icon: const Icon(Icons.refresh),
          ),
        ],
        bottom: _progress < 100
            ? PreferredSize(
                preferredSize: const Size.fromHeight(3),
                child: LinearProgressIndicator(value: _progress / 100),
              )
            : null,
      ),
      body: WebViewWidget(controller: _controller),
    );
  }
}
