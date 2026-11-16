import 'dart:convert';
import 'dart:typed_data';

import 'package:pdf/pdf.dart';
import 'package:pdf/widgets.dart' as pw;

/// A read-only copy of the complete selection, in the order shown by the app.
class SessionReport {
  SessionReport._(this.wallboxName, this.selection, this.sessions);

  factory SessionReport.fromJson(Map<String, dynamic> json) {
    final rows = json['sessions'];
    if (rows is! List || rows.isEmpty || rows.length > 10000) {
      throw const FormatException('Invalid session selection.');
    }
    final sessions = <Map<String, dynamic>>[];
    for (final row in rows) {
      if (row is! Map<String, dynamic> || row['transactionId'] is! int) {
        throw const FormatException('Invalid session record.');
      }
      sessions.add(Map<String, dynamic>.unmodifiable(row));
    }
    return SessionReport._(
      _text(json['wallboxName'], fallback: 'InnoCharge'),
      _text(json['selection'], fallback: 'All sessions'),
      List.unmodifiable(sessions),
    );
  }

  final String wallboxName;
  final String selection;
  final List<Map<String, dynamic>> sessions;

  static const headers = [
    'Local ID',
    'OCPP ID',
    'OCPP tag',
    'OCPP start',
    'OCPP stop',
    'Status',
    'ID Tag',
    'User',
    'Start',
    'Stop',
    'Duration',
    'Energy kWh',
    'Stop reason',
  ];

  static String _text(dynamic value, {String fallback = ''}) {
    if (value == null) return fallback;
    return value.toString().replaceAll(
      RegExp(r'[\x00-\x08\x0b\x0c\x0e-\x1f]'),
      '',
    );
  }

  static String timestamp(dynamic value) {
    final parsed = DateTime.tryParse(_text(value))?.toLocal();
    if (parsed == null) return '';
    String two(int part) => part.toString().padLeft(2, '0');
    return '${two(parsed.day)}.${two(parsed.month)}.${parsed.year} '
        '${two(parsed.hour)}:${two(parsed.minute)}:${two(parsed.second)}';
  }

  static String duration(dynamic value) {
    final seconds = value is num && value.isFinite
        ? value.toInt().clamp(0, 1 << 40)
        : 0;
    return [
      seconds ~/ 3600,
      seconds % 3600 ~/ 60,
      seconds % 60,
    ].map((part) => part.toString().padLeft(2, '0')).join(':');
  }

  static String delivery(dynamic value) =>
      const {
        'none': '-',
        'pending': 'Pending',
        'sent': 'Unconfirmed',
        'confirmed': 'Confirmed',
        'uncertain': 'Review',
        'failed': 'Failed',
      }[value] ??
      '-';

  List<String> _row(Map<String, dynamic> session) {
    final wh = session['energyWh'];
    return [
      _text(session['transactionId']),
      _text(session['ocppTransactionId']),
      _text(session['ocppIdTag']),
      delivery(session['ocppStart']),
      delivery(session['ocppStop']),
      session['active'] == true ? 'Active' : 'Closed',
      _text(session['idTag']),
      _text(session['userName']),
      timestamp(session['startTime']),
      session['active'] == true ? '' : timestamp(session['stopTime']),
      duration(session['durationSeconds']),
      wh is num && wh.isFinite
          ? (wh.clamp(0, double.infinity) / 1000).toStringAsFixed(3)
          : '',
      _text(session['stopReason']),
    ];
  }

  static String _csvCell(String value) {
    // User names/tags are text, even if they look like spreadsheet formulas.
    if (RegExp(r'^\s*[=+@\-]').hasMatch(value) ||
        value.startsWith('\t') ||
        value.startsWith('\r')) {
      value = "'$value";
    }
    return '"${value.replaceAll('"', '""')}"';
  }

  Uint8List csv() => Uint8List.fromList(
    utf8.encode(
      '\uFEFF${[headers, ...sessions.map(_row)].map((row) => row.map(_csvCell).join(';')).join('\r\n')}\r\n',
    ),
  );

  Future<Uint8List> pdf({
    required Uint8List logo,
    required ByteData regularFont,
    required ByteData boldFont,
    DateTime? createdAt,
  }) async {
    final regular = pw.Font.ttf(regularFont);
    final bold = pw.Font.ttf(boldFont);
    final report = pw.Document(
      title: 'Charge Sessions - $wallboxName',
      author: 'InnoCharge',
      theme: pw.ThemeData.withFont(base: regular, bold: bold),
    );
    final brand = pw.MemoryImage(logo);
    final created = timestamp((createdAt ?? DateTime.now()).toIso8601String());
    final rows = sessions.map(_row).toList();
    const ink = PdfColor.fromInt(0xff263238);
    report.addPage(
      pw.MultiPage(
        pageFormat: PdfPageFormat.a4.landscape,
        margin: const pw.EdgeInsets.all(24),
        maxPages: 500,
        header: (context) => pw.Column(
          children: [
            pw.Container(
              color: ink,
              padding: const pw.EdgeInsets.all(12),
              child: pw.Row(
                mainAxisAlignment: pw.MainAxisAlignment.spaceBetween,
                children: [
                  pw.Image(
                    brand,
                    width: 140,
                    height: 34,
                    fit: pw.BoxFit.contain,
                  ),
                  pw.Column(
                    crossAxisAlignment: pw.CrossAxisAlignment.end,
                    children: [
                      pw.Text(
                        'Charge Sessions',
                        style: pw.TextStyle(
                          color: PdfColors.white,
                          fontSize: 17,
                          fontWeight: pw.FontWeight.bold,
                        ),
                      ),
                      pw.SizedBox(height: 4),
                      pw.Text(
                        wallboxName,
                        style: const pw.TextStyle(
                          color: PdfColors.white,
                          fontSize: 10,
                        ),
                      ),
                    ],
                  ),
                ],
              ),
            ),
            pw.Padding(
              padding: const pw.EdgeInsets.symmetric(vertical: 9),
              child: pw.Row(
                crossAxisAlignment: pw.CrossAxisAlignment.start,
                children: [
                  pw.Expanded(
                    child: pw.Text(
                      selection,
                      style: const pw.TextStyle(fontSize: 9),
                    ),
                  ),
                  pw.SizedBox(width: 16),
                  pw.Text(
                    'Sessions: ${sessions.length}  |  Created: $created',
                    style: const pw.TextStyle(fontSize: 9),
                  ),
                ],
              ),
            ),
          ],
        ),
        footer: (context) => pw.Padding(
          padding: const pw.EdgeInsets.only(top: 8),
          child: pw.Row(
            mainAxisAlignment: pw.MainAxisAlignment.spaceBetween,
            children: [
              pw.Text(
                'InnoCharge - $wallboxName',
                style: const pw.TextStyle(
                  fontSize: 8,
                  color: PdfColors.grey700,
                ),
              ),
              pw.Text(
                'Page ${context.pageNumber} / ${context.pagesCount}',
                style: const pw.TextStyle(
                  fontSize: 8,
                  color: PdfColors.grey700,
                ),
              ),
            ],
          ),
        ),
        build: (context) => [
          pw.TableHelper.fromTextArray(
            headers: headers,
            data: rows,
            headerStyle: pw.TextStyle(
              color: PdfColors.white,
              fontWeight: pw.FontWeight.bold,
              fontSize: 7,
            ),
            headerDecoration: const pw.BoxDecoration(color: ink),
            cellStyle: const pw.TextStyle(fontSize: 7),
            cellPadding: const pw.EdgeInsets.all(4),
            oddRowDecoration: const pw.BoxDecoration(color: PdfColors.grey100),
            border: pw.TableBorder.all(color: PdfColors.grey300, width: 0.4),
            columnWidths: {
              for (var i = 0; i < headers.length; i++)
                i: pw.FlexColumnWidth(
                  [
                    0.8,
                    0.8,
                    1.2,
                    1.0,
                    1.0,
                    0.8,
                    1.2,
                    1.4,
                    1.7,
                    1.7,
                    1.0,
                    0.9,
                    1.5,
                  ][i],
                ),
            },
          ),
        ],
      ),
    );
    return report.save();
  }
}
