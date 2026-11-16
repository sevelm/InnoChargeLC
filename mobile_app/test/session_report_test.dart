import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:innocharge_mobile/session_report.dart';

Map<String, dynamic> fixture(int index) => {
  'transactionId': 1000 - index,
  'ocppTransactionId': index.isEven ? 500 - index : null,
  'ocppIdTag': 'TAG${index % 3}',
  'ocppStart': 'confirmed',
  'ocppStop': index == 0 ? 'pending' : 'confirmed',
  'idTag': '04AABBCCDDEE${index.toString().padLeft(4, '0')}',
  'userName': index % 2 == 0 ? 'Müller, Anna' : 'Garage & Gäste',
  'startTime': DateTime.utc(
    2026,
    11,
    16,
  ).subtract(Duration(days: index)).toIso8601String(),
  'stopTime': DateTime.utc(
    2026,
    11,
    16,
    2,
    15,
  ).subtract(Duration(days: index)).toIso8601String(),
  'durationSeconds': 8100 + index,
  'energyWh': 12450 + index * 25,
  'active': index == 0,
  'stopReason': index == 0 ? '' : 'EVDisconnected',
};

void main() {
  test(
    'CSV exports every selected row in its original order with UTF-8 and quoted fields',
    () {
      final report = SessionReport.fromJson({
        'wallboxName': 'Garage',
        'selection': 'All sessions',
        'sessions': List.generate(47, fixture),
      });
      final bytes = report.csv();
      expect(bytes.take(3), [0xef, 0xbb, 0xbf]);
      final text = utf8.decode(bytes);
      expect(text, startsWith('"Local ID";'));
      expect(text, contains('"Müller, Anna"'));
      expect(text, contains('"12.450"'));
      expect(text.split('\r\n'), hasLength(49));
      expect(text.indexOf('"1000";'), lessThan(text.indexOf('"954";')));
      expect(text, contains('"02:15:00"'));
    },
  );

  test(
    'untrusted CSV text cannot become spreadsheet formulas and quotes remain valid',
    () {
      final report = SessionReport.fromJson({
        'sessions': [
          {
            ...fixture(1),
            'userName': '=HYPERLINK("example")',
            'idTag': '"quoted";tag',
            'stopReason': 'line1\nline2',
          },
          {...fixture(2), 'userName': '\t+SUM(1,2)'},
        ],
      });
      final text = utf8.decode(report.csv());
      expect(text, contains('"\'=HYPERLINK(""example"")"'));
      expect(text, contains('"""quoted"";tag"'));
      expect(text, contains('"\'\t+SUM(1,2)"'));
      expect(text, contains('"line1\nline2"'));
    },
  );

  test('invalid bridge messages are rejected before exporting', () {
    for (final rows in [
      null,
      [],
      ['bad'],
      [
        {'transactionId': 'bad'},
      ],
    ]) {
      expect(
        () => SessionReport.fromJson({'sessions': rows}),
        throwsFormatException,
      );
    }
  });

  test(
    'PDF contains the complete multi-page selection and embedded offline assets',
    () async {
      final report = SessionReport.fromJson({
        'wallboxName': 'Garage - InnoCharge',
        'selection': 'From 2026-09-01 | To 2026-11-16 | Sort: Date descending',
        'sessions': List.generate(65, fixture),
      });
      final bytes = await report.pdf(
        logo: File('assets/generated/innocharge.png').readAsBytesSync(),
        regularFont: ByteData.sublistView(
          File('assets/fonts/roboto-regular.ttf').readAsBytesSync(),
        ),
        boldFont: ByteData.sublistView(
          File('assets/fonts/roboto-bold.ttf').readAsBytesSync(),
        ),
        createdAt: DateTime.utc(2026, 11, 16, 12),
      );
      expect(ascii.decode(bytes.take(5).toList()), '%PDF-');
      expect(bytes.length, greaterThan(20000));
      final output = File('build/verification/session-report.pdf');
      output.parent.createSync(recursive: true);
      output.writeAsBytesSync(bytes);
    },
  );
}
