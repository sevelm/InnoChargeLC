# iOS mit Codemagic

Das bestehende Repository `sevelm/InnoChargeLC` kann verwendet werden. Die Datei
`codemagic.yaml` liegt im Repository-Hauptordner; beide Workflows arbeiten in
`mobile_app`. Die Firmware wird weder gebaut noch hochgeladen. Codemagic kann
allerdings das gesamte freigegebene Repository lesen, nicht nur diesen Ordner.

## 1. Repository vorbereiten

- In GitHub sicherstellen, dass das Repository privat ist.
- Die vorgesehenen Aenderungen vor Commit und Push einzeln pruefen. Kein
  pauschales `git add .`: im Arbeitsverzeichnis gibt es auch Firmware-Aenderungen.
- Flutter-Quellen, Tests, `pubspec.yaml`, `pubspec.lock`, iOS-Projekt,
  `assets/demo/`, `assets/generated/` und `tool/prepare_demo_assets.dart` mitliefern.
- `data/app.html` und `data/innocharge.svg` muessen zur generierten Demo passen;
  der Test prueft das auch im Cloud-Build.
- Android-Keystore, `key.properties`, Apple-API-Schluessel, Zertifikate und
  Passwoerter niemals committen. Ignore-Regeln schuetzen keine bereits
  versionierten Dateien. Geheimnisse auch nicht in Screenshots oder Logs teilen.

## 2. Erster Build ohne Apple-Schluessel

1. Bei Codemagic anmelden und den passenden Tarif pruefen.
2. Unter "Add application" GitHub verbinden. Nur `InnoChargeLC` freigeben.
3. Das Repository als Flutter-Projekt hinzufuegen und die YAML-Konfiguration
   aus dem Hauptordner verwenden.
4. Den Branch mit den hochgeladenen Aenderungen waehlen.
5. Manuell "InnoCharge iOS - Build check (unsigned)" starten.

Der Workflow prueft Abhaengigkeiten, Dart-Code und Tests und baut anschliessend
fuer iOS. Er benoetigt keine Apple-Schluessel, erzeugt aber keine auf einem
iPhone installierbare App. Ein erfolgreicher Build ersetzt keinen Geraetetest.
Die erste Ausfuehrung ist der erste echte macOS-/Xcode-Test dieses Projekts.

Beide Workflows verwenden Mac M2, Flutter 3.44.5 und das aktuelle stabile
Codemagic-Xcode-Image (`latest`). Es gibt keine automatischen Push-/PR-Trigger.
Ein Build wird nach maximal 30 Minuten beendet; auch fehlgeschlagene Versuche
verbrauchen Build-Zeit. Keine kostenpflichtigen Optionen ungeprueft aktivieren.

## 3. Apple und Signierung einrichten

1. Im Apple Developer Portal eine explizite App-ID fuer InnoCharge registrieren:
   `at.innocharge.innochargeMobile` (bereits im Xcode-Projekt eingetragen).
   Nicht die Android-ID mit Unterstrich verwenden und InnoPlay nicht ersetzen.
2. In App Store Connect eine neue iOS-App namens InnoCharge mit dieser Bundle-ID
   anlegen. Als interne SKU kann `innocharge-ios` verwendet werden.
3. Unter Benutzer und Zugriff > Integrationen > App Store Connect API einen
   dedizierten Team-API-Schluessel erstellen. Codemagic empfiehlt fuer Signierung
   und Upload die Rolle "App Manager". Die Berechtigungen vor Freigabe pruefen.
4. Die einmal herunterladbare `.p8`-Datei sicher sichern. In der Codemagic-
   Developer-Portal-Integration hinterlegen, zusammen mit Key ID und Issuer ID.
   Die Integration exakt `InnoCharge Codemagic` nennen, wie in der YAML-Datei.
5. Unter Code signing identities ein Apple-Distribution-Zertifikat erstellen
   oder ein geeignetes vorhandenes Zertifikat samt privatem Schluessel laden.
   Neue Zertifikate samt Passwort sicher sichern. Bestehende InnoPlay-Zertifikate
   nicht widerrufen, um ein Zertifikatslimit zu umgehen.
6. Ein App-Store-Provisioning-Profil fuer die InnoCharge-Bundle-ID und dieses
   Zertifikat erstellen und in Codemagic laden/fetchen. Zertifikat und Profil
   muessen dort zusammenpassen. `ios_signing` nutzt diese hinterlegten Dateien;
   es ersetzt die erstmalige Einrichtung nicht.

## 4. Upload und TestFlight

"InnoCharge iOS - Upload for TestFlight" manuell starten. Der Workflow erstellt
und signiert die IPA und laedt sie zu App Store Connect hoch. Er startet weder
eine externe Beta-Pruefung noch eine oeffentliche App-Store-Pruefung automatisch.
Nach Apples Verarbeitung in TestFlight die Export-/Verschluesselungsfragen
beantworten, eine interne Testgruppe anlegen und den eigenen berechtigten
App-Store-Connect-Nutzer hinzufuegen. Den Build der Gruppe zuweisen und auf dem
iPhone ueber TestFlight installieren.

Der Workflow prueft nach `flutter build ipa`, ob genau eine nicht leere IPA
vorliegt: Flutter kann trotz fehlgeschlagenem IPA-Export Exit-Code 0 liefern.
Die Artefakt-Pfade beginnen mit `$CM_BUILD_DIR`, damit die Sammlung unabhaengig
vom Arbeitsverzeichnis auf den Flutter-Buildordner im Repository zugreift.
Bei "No artifacts were found" zuerst das Protokoll "Build signed IPA" pruefen,
insbesondere Export- und Signierungsfehler; ein gruener Build allein belegt
keinen erfolgreichen Upload zu Apple.

Die Versionsnummer kommt aus `pubspec.yaml`. Nur die iOS-Buildnummer wird durch
den Codemagic-Projektzaehler ersetzt. Beim Neu-Anlegen der Codemagic-Anwendung
oder bei parallelen anderen Build-Diensten muss eine bereits benutzte Nummer
vermieden werden. Android-Version und Google-Play-Einreichung bleiben unberuehrt.

Auf einem echten iPhone testen: lokale Netzwerkberechtigung, automatische Suche,
manuelle Verbindung, HTTP/WebSocket zur Wallbox, Ruecknavigation, Safe Areas,
Demo und Datenschutz-Link. Bei Discovery-Problemen insbesondere die iOS-
Multicast-Berechtigungen untersuchen; funktionierendes Android beweist dies nicht.

## Offizielle Anleitungen

- https://docs.codemagic.io/getting-started/adding-apps/
- https://docs.codemagic.io/yaml-code-signing/signing-ios/
- https://docs.codemagic.io/yaml-publishing/app-store-connect/
- https://codemagic.io/pricing/
