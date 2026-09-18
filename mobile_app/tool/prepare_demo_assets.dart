import 'dart:io';

import 'package:html/dom.dart';
import 'package:html/parser.dart';

// Package the real UI with an isolated simulator, without changing firmware files.
String buildDemoDocument(Directory app) {
  final data = Directory('${app.parent.path}/data');
  final document = parse(File('${data.path}/app.html').readAsStringSync());
  final logo = parseFragment(
    File('${data.path}/innocharge.svg').readAsStringSync(),
  ).querySelector('svg')!;
  logo.attributes['role'] = 'img';
  logo.attributes['aria-label'] = 'InnoCharge';
  document.querySelector('.brand')!.nodes
    ..clear()
    ..add(logo);
  document.querySelectorAll('link').forEach((element) => element.remove());
  document.head!.nodes.insert(
    0,
    Element.tag('meta')
      ..attributes['http-equiv'] = 'Content-Security-Policy'
      ..attributes['content'] =
          "default-src 'none'; script-src 'unsafe-inline'; "
          "style-src 'unsafe-inline'; img-src data:; connect-src 'none'; "
          "base-uri 'none'; form-action 'none'",
  );
  document.head!.append(
    Element.tag('script')
      ..text = File('${app.path}/assets/demo/simulator.js').readAsStringSync(),
  );
  document.querySelector('title')!.text = 'InnoCharge Demo';
  return document.outerHtml;
}

void main(List<String> arguments) {
  final app = File.fromUri(Platform.script).parent.parent;
  final output = File('${app.path}/assets/generated/demo.html');
  final generated = buildDemoDocument(app);
  if (arguments.contains('--check')) {
    if (!output.existsSync() || output.readAsStringSync() != generated) {
      stderr.writeln(
        'Demo asset is stale. Run dart run tool/prepare_demo_assets.dart',
      );
      exitCode = 1;
    }
    return;
  }
  output.parent.createSync(recursive: true);
  output.writeAsStringSync(generated);
  stdout.writeln(
    'Generated offline demo from data/app.html and innocharge.svg.',
  );
}
