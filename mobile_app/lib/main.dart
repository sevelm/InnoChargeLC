import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:url_launcher/url_launcher.dart';
import 'package:webview_flutter/webview_flutter.dart';

import 'device_discovery.dart';

const background = Color(0xff263238);
const surface = Color(0xff37474f);
const accent = Color(0xff588fc7);
const muted = Color(0xffb0bec5);

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await SystemChrome.setPreferredOrientations([DeviceOrientation.portraitUp]);
  SystemChrome.setSystemUIOverlayStyle(
    const SystemUiOverlayStyle(
      statusBarColor: Colors.transparent,
      systemNavigationBarColor: background,
      statusBarIconBrightness: Brightness.light,
      systemNavigationBarIconBrightness: Brightness.light,
    ),
  );
  runApp(const InnoChargeApp());
}

class InnoChargeApp extends StatelessWidget {
  const InnoChargeApp({super.key, this.discovery});
  final DeviceDiscoveryService? discovery;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'InnoCharge',
      theme: ThemeData(
        brightness: Brightness.dark,
        scaffoldBackgroundColor: background,
        colorScheme: const ColorScheme.dark(
          primary: accent,
          onPrimary: Colors.white,
          surface: background,
          onSurface: Color(0xfff4f4f9),
          onSurfaceVariant: muted,
          surfaceContainerHighest: surface,
        ),
        inputDecorationTheme: InputDecorationTheme(
          filled: true,
          fillColor: surface,
          border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
        ),
        filledButtonTheme: FilledButtonThemeData(
          style: FilledButton.styleFrom(
            minimumSize: const Size(0, 50),
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(6),
            ),
          ),
        ),
        useMaterial3: true,
      ),
      home: DeviceDiscoveryPage(
        discovery: discovery ?? DeviceDiscoveryService(),
      ),
    );
  }
}

class DeviceDiscoveryPage extends StatefulWidget {
  const DeviceDiscoveryPage({super.key, required this.discovery});
  final DeviceDiscoveryService discovery;

  @override
  State<DeviceDiscoveryPage> createState() => _DeviceDiscoveryPageState();
}

class _DeviceDiscoveryPageState extends State<DeviceDiscoveryPage> {
  final _manualController = TextEditingController();
  final Map<String, InnoChargeDevice> _devices = {};
  bool _searching = false;
  bool _opening = false;
  String? _lastUrl;
  String? _error;
  int _searchGeneration = 0;

  @override
  void initState() {
    super.initState();
    _search();
  }

  @override
  void dispose() {
    _manualController.dispose();
    super.dispose();
  }

  Future<void> _search() async {
    if (_searching) return;
    final generation = ++_searchGeneration;
    bool current() => mounted && generation == _searchGeneration;
    setState(() {
      _searching = true;
      _error = null;
      _devices.clear();
    });
    try {
      final prefs = await SharedPreferences.getInstance();
      if (!current()) return;
      _lastUrl = prefs.getString('last_device_url');
      // Previously saved generic HTTP results must pass the same identity check.
      final last = _lastUrl == null
          ? null
          : DeviceDiscoveryService.parseAddress(_lastUrl!);
      if (last != null) {
        final device = await widget.discovery.probe(last);
        if (!current()) return;
        if (device != null) {
          setState(() => _devices[device.url] = device);
        }
      }
      await widget.discovery.discover(
        shouldContinue: current,
        onDevice: (device) {
          if (current()) setState(() => _devices[device.url] = device);
        },
      );
    } catch (_) {
      if (current()) {
        setState(
          () =>
              _error = 'Netzwerksuche nicht moeglich. WLAN-Verbindung pruefen.',
        );
      }
    } finally {
      if (current()) setState(() => _searching = false);
    }
  }

  Future<void> _openDemo() async {
    if (_opening) return;
    FocusScope.of(context).unfocus();
    setState(() {
      ++_searchGeneration;
      _searching = false;
      _opening = true;
    });
    try {
      await Navigator.of(
        context,
      ).push(MaterialPageRoute<void>(builder: (_) => const WebAppPage.demo()));
    } finally {
      if (mounted) setState(() => _opening = false);
    }
  }

  Future<void> _openPrivacyPolicy() async {
    try {
      if (await launchUrl(
        Uri.parse('https://www.innocharge.at/kontakt/'),
        mode: LaunchMode.externalApplication,
      )) {
        return;
      }
    } on Exception {
      // Show the same recoverable error when the platform cannot open a browser.
    }
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(
        content: Text('Datenschutzseite konnte nicht geoeffnet werden.'),
      ),
    );
  }

  Future<void> _open(Uri? address) async {
    if (_opening) return;
    if (address == null) {
      setState(
        () => _error = 'Bitte eine gueltige IP oder HTTP-Adresse eingeben.',
      );
      return;
    }
    FocusScope.of(context).unfocus();
    setState(() {
      _opening = true;
      _error = null;
    });
    try {
      final device = await widget.discovery.probe(address);
      if (!mounted) return;
      if (device == null) {
        setState(
          () => _error =
              'Keine erreichbare InnoCharge-Wallbox an dieser Adresse.',
        );
        return;
      }
      final prefs = await SharedPreferences.getInstance();
      await prefs.setString('last_device_url', device.url);
      await prefs.setString('last_device_name', device.name);
      if (!mounted) return;
      _lastUrl = device.url;
      await Navigator.of(context).push(
        MaterialPageRoute<void>(builder: (_) => WebAppPage(device: device)),
      );
    } catch (_) {
      if (mounted) {
        setState(
          () => _error = 'Verbindung fehlgeschlagen. Bitte erneut versuchen.',
        );
      }
    } finally {
      if (mounted) setState(() => _opening = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: RefreshIndicator(
          onRefresh: _search,
          child: CustomScrollView(
            physics: const AlwaysScrollableScrollPhysics(),
            slivers: [
              SliverToBoxAdapter(
                child: ColoredBox(
                  color: background,
                  child: Padding(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 28,
                      vertical: 20,
                    ),
                    child: Image.asset(
                      'assets/generated/innocharge.png',
                      height: 64,
                      fit: BoxFit.contain,
                      semanticLabel: 'InnoCharge',
                    ),
                  ),
                ),
              ),
              SliverToBoxAdapter(
                child: Center(
                  child: ConstrainedBox(
                    constraints: const BoxConstraints(maxWidth: 560),
                    child: Padding(
                      padding: const EdgeInsets.fromLTRB(24, 24, 24, 16),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: [
                          const Text(
                            'Manuell verbinden',
                            style: TextStyle(
                              fontSize: 18,
                              fontWeight: FontWeight.w600,
                            ),
                          ),
                          const SizedBox(height: 12),
                          TextField(
                            controller: _manualController,
                            keyboardType: TextInputType.url,
                            textInputAction: TextInputAction.go,
                            autocorrect: false,
                            onSubmitted: (_) => _open(
                              DeviceDiscoveryService.parseAddress(
                                _manualController.text,
                              ),
                            ),
                            decoration: const InputDecoration(
                              labelText: 'IP oder Adresse',
                              hintText: '192.168.0.85',
                              prefixIcon: Icon(Icons.link),
                            ),
                          ),
                          const SizedBox(height: 12),
                          FilledButton.icon(
                            onPressed: _opening
                                ? null
                                : () => _open(
                                    DeviceDiscoveryService.parseAddress(
                                      _manualController.text,
                                    ),
                                  ),
                            icon: _opening
                                ? const SizedBox.square(
                                    dimension: 18,
                                    child: CircularProgressIndicator(
                                      strokeWidth: 2,
                                    ),
                                  )
                                : const Icon(Icons.login),
                            label: Text(
                              _opening ? 'Verbinde ...' : 'Verbinden',
                            ),
                          ),
                          if (_error != null)
                            Padding(
                              padding: const EdgeInsets.only(top: 14),
                              child: Text(
                                _error!,
                                style: TextStyle(
                                  color: Theme.of(context).colorScheme.error,
                                ),
                              ),
                            ),
                          const SizedBox(height: 8),
                          TextButton.icon(
                            onPressed: _opening ? null : _openDemo,
                            icon: const Icon(Icons.play_circle_outline),
                            label: const Text('Demo ansehen'),
                          ),
                          const SizedBox(height: 28),
                          Row(
                            children: [
                              const Expanded(
                                child: Text(
                                  'Wallboxen im Netzwerk',
                                  style: TextStyle(fontWeight: FontWeight.w600),
                                ),
                              ),
                              if (_searching)
                                const SizedBox.square(
                                  dimension: 16,
                                  child: CircularProgressIndicator(
                                    strokeWidth: 2,
                                  ),
                                ),
                              IconButton(
                                tooltip: 'Netzwerk erneut durchsuchen',
                                onPressed: _searching ? null : _search,
                                icon: const Icon(Icons.refresh),
                              ),
                            ],
                          ),
                          const SizedBox(height: 10),
                          Text(
                            _searching
                                ? 'Suche InnoCharge ...'
                                : _devices.isEmpty
                                ? 'Keine Wallbox gefunden.'
                                : '${_devices.length} InnoCharge gefunden.',
                            style: const TextStyle(color: muted),
                          ),
                          const SizedBox(height: 14),
                          for (final device in _devices.values)
                            ListTile(
                              contentPadding: const EdgeInsets.symmetric(
                                vertical: 8,
                              ),
                              leading: const Icon(
                                Icons.ev_station_outlined,
                                color: accent,
                              ),
                              title: Text(
                                device.name,
                                style: const TextStyle(
                                  fontWeight: FontWeight.w600,
                                ),
                              ),
                              subtitle: Text(
                                '${Uri.parse(device.url).authority}${device.url == _lastUrl ? '  |  Zuletzt verbunden' : ''}',
                              ),
                              trailing: const Icon(Icons.chevron_right),
                              onTap: _opening
                                  ? null
                                  : () => _open(Uri.parse(device.url)),
                              shape: const Border(
                                bottom: BorderSide(color: surface),
                              ),
                            ),
                          const SizedBox(height: 24),
                          Align(
                            alignment: Alignment.centerLeft,
                            child: TextButton(
                              onPressed: _openPrivacyPolicy,
                              style: TextButton.styleFrom(
                                foregroundColor: muted,
                                textStyle: const TextStyle(fontSize: 12),
                              ),
                              child: const Text('Datenschutz'),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class WebAppPage extends StatefulWidget {
  const WebAppPage({super.key, required this.device});
  const WebAppPage.demo({super.key}) : device = null;
  final InnoChargeDevice? device;
  bool get isDemo => device == null;
  @override
  State<WebAppPage> createState() => _WebAppPageState();
}

class _WebAppPageState extends State<WebAppPage> with WidgetsBindingObserver {
  static const _display = MethodChannel('at.innocharge/display');
  late final WebViewController _controller;
  bool _failed = false;
  bool _menuInPage = false;
  int _progress = 0;

  Future<void> _fullscreen(bool enabled) async {
    if (Platform.isAndroid) {
      await _display.invokeMethod<void>('fullscreen', enabled);
    } else {
      await SystemChrome.setEnabledSystemUIMode(
        enabled ? SystemUiMode.immersiveSticky : SystemUiMode.edgeToEdge,
      );
    }
  }

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    unawaited(_fullscreen(true));
    _controller = WebViewController()
      ..setBackgroundColor(background)
      ..setJavaScriptMode(JavaScriptMode.unrestricted)
      ..setNavigationDelegate(
        NavigationDelegate(
          onNavigationRequest: (request) {
            if (widget.isDemo) {
              return request.url == 'about:blank'
                  ? NavigationDecision.navigate
                  : NavigationDecision.prevent;
            }
            final uri = Uri.tryParse(request.url);
            return uri != null &&
                    ['http', 'https'].contains(uri.scheme) &&
                    uri.origin == widget.device!.appUri.origin
                ? NavigationDecision.navigate
                : NavigationDecision.prevent;
          },
          onProgress: (progress) {
            if (mounted) setState(() => _progress = progress);
          },
          onPageStarted: (_) {
            if (mounted) {
              setState(() {
                _failed = false;
                _menuInPage = false;
              });
            }
          },
          onPageFinished: (_) async {
            if (!mounted) return;
            if (widget.isDemo) return;
            // Older wallbox UIs have no app menu yet; retain a native menu for those.
            try {
              final result = await _controller.runJavaScriptReturningResult(
                "Boolean(document.getElementById('nativeMenu') && window.InnoChargeHost)",
              );
              if (mounted) {
                setState(
                  () => _menuInPage = result == true || result == 'true',
                );
              }
            } on PlatformException {
              // Keep the native menu available if the page is no longer loaded.
            }
          },
          onWebResourceError: (error) {
            if (error.isForMainFrame == true && mounted) {
              setState(() => _failed = true);
            }
          },
        ),
      );
    if (!widget.isDemo) {
      _controller.addJavaScriptChannel(
        'InnoChargeHost',
        onMessageReceived: (message) {
          if (message.message == 'menu' && mounted) _showMenu();
        },
      );
    }
    unawaited(_loadContent());
  }

  Future<void> _loadContent() async {
    if (mounted) setState(() => _failed = false);
    try {
      await _controller.setOverScrollMode(WebViewOverScrollMode.never);
      if (!mounted) return;
      if (widget.isDemo) {
        final html = await rootBundle.loadString('assets/generated/demo.html');
        if (mounted) await _controller.loadHtmlString(html);
      } else {
        await _controller.loadRequest(widget.device!.appUri);
      }
    } catch (_) {
      if (mounted) setState(() => _failed = true);
    }
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) unawaited(_fullscreen(true));
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    unawaited(_fullscreen(false));
    super.dispose();
  }

  Future<void> _showMenu() async {
    if (widget.isDemo) return;
    final action = await showModalBottomSheet<String>(
      context: context,
      builder: (context) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ListTile(
              title: Text(widget.device!.name),
              subtitle: Text(widget.device!.url),
            ),
            ListTile(
              leading: const Icon(Icons.refresh),
              title: const Text('Neu laden'),
              onTap: () => Navigator.pop(context, 'reload'),
            ),
            ListTile(
              leading: const Icon(Icons.swap_horiz),
              title: const Text('Wallbox wechseln'),
              onTap: () => Navigator.pop(context, 'back'),
            ),
          ],
        ),
      ),
    );
    if (!mounted) return;
    if (action == 'reload') await _controller.reload();
    if (action == 'back' && mounted) Navigator.pop(context);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: Column(
          children: [
            if (widget.isDemo)
              Row(
                children: [
                  IconButton(
                    tooltip: 'Demo beenden',
                    onPressed: () => Navigator.of(context).pop(),
                    icon: const Icon(Icons.close),
                  ),
                  const Expanded(
                    child: Text('DEMO', style: TextStyle(color: accent)),
                  ),
                  IconButton(
                    tooltip: 'Demo zuruecksetzen',
                    onPressed: _loadContent,
                    icon: const Icon(Icons.restart_alt),
                  ),
                ],
              ),
            if (!widget.isDemo && !_menuInPage)
              Align(
                alignment: Alignment.centerRight,
                child: IconButton(
                  tooltip: 'Wallbox-Menue',
                  onPressed: _showMenu,
                  icon: const Icon(Icons.more_horiz),
                ),
              ),
            if (_progress < 100 && !_failed)
              LinearProgressIndicator(value: _progress / 100, minHeight: 2),
            Expanded(
              child: _failed
                  ? Center(
                      child: Padding(
                        padding: const EdgeInsets.all(24),
                        child: Column(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            const Icon(Icons.wifi_off, size: 36, color: muted),
                            const SizedBox(height: 16),
                            Text(
                              widget.isDemo
                                  ? 'Demo konnte nicht geladen werden.'
                                  : 'Wallbox nicht erreichbar.',
                            ),
                            const SizedBox(height: 16),
                            FilledButton.icon(
                              onPressed: _loadContent,
                              icon: const Icon(Icons.refresh),
                              label: const Text('Erneut laden'),
                            ),
                          ],
                        ),
                      ),
                    )
                  : WebViewWidget(controller: _controller),
            ),
          ],
        ),
      ),
    );
  }
}
