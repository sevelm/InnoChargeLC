import 'package:flutter_test/flutter_test.dart';
import 'package:innocharge_mobile/main.dart';

void main() {
  testWidgets('shows the InnoCharge discovery screen', (tester) async {
    await tester.pumpWidget(const InnoChargeApp());

    expect(find.text('InnoCharge'), findsOneWidget);
    expect(find.text('Wallbox verbinden'), findsOneWidget);
    expect(find.text('Verbinden'), findsOneWidget);
  });
}
