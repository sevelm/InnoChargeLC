# InnoCharge Mobile

Flutter-Huelle fuer Android und iOS. Die App sucht InnoCharge-Wallboxen und
oeffnet deren `/app.html` in einer WebView. Die Weboberflaeche liegt auf der
Wallbox, nicht in der APK.

## Entwicklung

```sh
flutter pub get
flutter analyze
flutter test
flutter build apk --debug
```

APK: `build/app/outputs/flutter-apk/app-debug.apk`.
Fuer iOS den Ordner `mobile_app` OHNE `android/key.properties` und
`android/upload-keystore.jks` auf einen Mac mit Flutter und Xcode uebertragen,
`flutter pub get` ausfuehren und `ios/Runner.xcworkspace` oeffnen.
Signing-Team in Xcode waehlen. iOS ist unter Windows nicht buildbar/getestet.

Ohne eigenen Mac: [Codemagic-Einrichtung](CODEMAGIC.md). Die vorbereiteten
Workflows in `../codemagic.yaml` werden nur manuell gestartet. Noch kein
iOS-Cloud-Build ausgefuehrt; keine automatische oeffentliche Veroeffentlichung.

## Google Play: interner Test

Paketname: `at.innocharge.innocharge_mobile`. Fuer Google Play ein signiertes
App Bundle erstellen, nicht die Debug-APK hochladen:

```sh
flutter build appbundle --release
```

Ausgabe: `build/app/outputs/bundle/release/app-release.aab`.
In Play Console unter InnoCharge > Testen und veroeffentlichen > Test > Interner
Test hochladen. Das erzeugt noch keine oeffentliche Veroeffentlichung.

Der Release-Build nutzt `android/key.properties` und den dort referenzierten
Upload-Keystore. Ohne diese Daten schlaegt der Release-Aufruf fehl; es gibt keinen
Rueckfall auf den Debug-Schluessel. Debug-Builds behalten ihre eigene Signierung.
`tool/create_upload_key.ps1` dient nur zur ERSTEN Schluesselerzeugung und verweigert
das Ueberschreiben vorhandener Signierungsdateien. Nicht fuer Updates neu erzeugen.

**Sicher sichern:** `android/upload-keystore.jks` UND `android/key.properties`
zusammen verschluesselt/offline aufbewahren. Die Properties-Datei enthaelt die
Passwoerter. Beide Dateien sind von Git ausgeschlossen, gehoeren nicht in den Chat
oder in geteilte Projekt-ZIPs und werden nicht in die App eingebettet. Ein Mac fuer
den iOS-Build benoetigt diese Android-Geheimnisse ebenfalls nicht.
Bei Play App Signing verwaltet Google den App-Signaturschluessel; lokal liegt der
separate Upload-Schluessel. Ein verlorener Upload-Schluessel kann ueber Play Console
zurueckgesetzt werden, trotzdem ist ein eigenes Backup erforderlich.

Die bisherige Debug-App auf dem Testhandy hat eine andere Signatur als die
Play-Version. Fuer den Wechsel kann eine Deinstallation noetig sein; dabei gehen
die lokal gespeicherten App-Einstellungen verloren. Wallbox-Daten bleiben davon
unberuehrt. Vor der oeffentlichen Freigabe sind unter anderem Datenschutz,
Store-Eintrag, App-Zugriff fuer die Pruefung und Release-Tests abzuschliessen.

## Offline-Demo (ab 1.0.4+5)

Auf der Suchseite ist "Demo ansehen" ohne Anmeldung und ohne Wallbox erreichbar.
Die Demo ist dauerhaft gekennzeichnet und verwendet die echte Weboberflaeche aus
`../data/app.html`, aber mit rein lokal simulierten Statusdaten und Ladebefehlen.
Zustand, Phasenmodus und 11-/22-kW-Grenze lassen sich auswaehlen. Positive Vorgaben
unter 1,4 kW werden wie bei der Firmware auf mindestens 1,4 kW abgebildet; es ist
keine physikalische Simulation von Fahrzeug, Messung oder Stromregelung.
Die gespeicherte echte Wallbox wird nicht veraendert. Laufende Suchdurchlaeufe
werden abgebrochen; bereits gestartete Netzwerkpruefungen koennen noch auslaufen.

`assets/demo/simulator.js` ersetzt WebSocket, ohne einen echten Socket anzulegen.
Eine Content-Security-Policy sperrt externe Ressourcen und alle Netzwerkzugriffe
der Demo-Seite. Die WebView laedt ausschliesslich das eingebettete HTML-Dokument.
Schliessen verlaesst die Demo; Zuruecksetzen stellt die Anfangswerte wieder her.

Nach Aenderungen an `data/app.html`, `data/innocharge.svg` oder dem Simulator:

```sh
dart run tool/prepare_demo_assets.dart
flutter test
```

Das generierte `assets/generated/demo.html` mitliefern, auch beim Transfer zum Mac.
Normale Builds benoetigen den uebergeordneten Firmware-Ordner nicht. Die
Regenerierungspruefung in `demo_asset_test.dart` benoetigt dagegen das ganze Repo.
Browserchecks: `python tool/test_demo.py --browser <Chrome-Pfad>` (Playwright/Pillow).

Play-Review-Zugriff (erst mit dem neuen Bundle einreichen):

Name: `InnoCharge demo access`. Benutzername/Passwort bleiben leer.

> No account or password is required. Open the app and tap "Demo ansehen" on the
> device discovery screen. The clearly marked demo works offline and simulates
> wallbox status and charging settings without controlling real hardware. Normal
> operation requires an InnoCharge wallbox on the same local network.

Eine Store-Freigabe ist damit nicht garantiert. Datenschutzerklaerung und deren
In-App-Zugang sowie die weiteren Store-Angaben sind separat fertigzustellen.

## Geraeteerkennung (Netzwerk)

`lib/device_discovery.dart` sucht per `_innocharge._tcp.local` und scannt bis
zu zwei lokale IPv4-/24-Netze mit maximal 16 gleichzeitigen HTTP-Pruefungen.
Ein Treffer muss beide Pruefungen bestehen:

- `/app.html`: HTTP 200, HTML und alle sechs App-Feld-IDs sowie der App-Client-Marker.
- WebSocket auf Port 81: vollstaendige, typisierte InnoCharge-Statusantwort
  auf `subscribeUpdates` fuer Seite `app`.

Die Pruefung setzt keine Ladeparameter und meldet sich danach wieder ab.
Auch manuelle Adressen und gespeicherte Geraete werden geprueft. Dies ist
Produkterkennung, kein kryptographischer Identitaetsnachweis. Die bestehende
Firmware wird unterstuetzt; kein neuer Discovery-Endpunkt ist notwendig.

## Darstellung

Logo-Blau `#588FC7`, Dunkelgrau `#263238`, Flaechen `#37474F`, Grau `#B0BEC5`.
Weiss/blauer Schriftzug mit transparentem Hintergrund, weisses App-Icon auf Dunkelgrau.
Android-Vollbild verwendet WindowInsets ueber `at.innocharge/display`; die
Systemleisten lassen sich voruebergehend per Randgeste einblenden.
Das Menue in der neuen `app.html` bietet Neuladen und Wallbox-Wechsel.
Bei aelteren Seiten bleibt ein nativer Menueknopf oberhalb der WebView sichtbar.
Die Webansicht respektiert Kameraaussparungen und bleibt bei kleinen Displays scrollbar.

## Datenschutz-Link

Auf der Geraetesuche oeffnet der dezente Textlink "Datenschutz" unter der Wallbox-Liste die Seite
`https://www.innocharge.at/kontakt/` im externen Browser. Der Link benoetigt keine
Wallbox-Verbindung; die Webseite benoetigt Internetzugang. Sie wird erst beim
Antippen geoeffnet. Der Datenschutztext wird auf der Website gepflegt.

## Logos und Icons

Einzige Logo-Originale: `../data/innologo.svg` (Bildzeichen) und
`../data/innocharge.svg` (Schriftzug). Die Webseiten referenzieren das SVG-Artwork
direkt; `currentColor` macht die sonst weissen Teile auf Login/Druck dunkelgrau.
Blau bleibt unveraendert. Der Druckbericht importiert dasselbe Artwork vor dem
Drucken inline, damit Popup-Beschraenkungen fuer externe SVGs kein Logo ausblenden.
`tool/prepare_brand_assets.py` rendert daraus transparente PNGs und ein quadratisches
1024px-Launcher-Icon nach `assets/generated/`, ohne die Proportionen zu veraendern.
Auch `../data/favicon.ico` wird daraus erzeugt (PNG-Inhalt unter dem bestehenden URL).
Keine generierten Bilder von Hand pflegen. Der Icon-Rand betraegt 224px bei
1024px; adaptive Android-Icons haben 26 Prozent Inset fuer die Launcher-Maske.
Voraussetzung nur fuer
erneute Logo-Exporte: Python mit `playwright` und Chromium (`playwright install chromium`)
oder `--browser <Chrome-Pfad>`. Unter Windows gibt es auch einen PowerShell-Aufruf
ueber `tool/prepare_brand_assets.ps1 -Browser <Chrome-Pfad>`.
Die fertigen Assets sind enthalten; normale Flutter-Builds benoetigen diese Tools nicht.

```sh
dart run flutter_launcher_icons
```

Nach erneuter Icon-Generierung die Xcode-Projektdatei pruefen: Version 0.14.4 des
Generators kann `ASSETCATALOG_COMPILER_GENERATE_SWIFT_ASSET_SYMBOL_EXTENSIONS`
faelschlich auf `AppIcon` setzen. Dieser Boolean muss `YES` bleiben.

HTML-Aenderungen separat als SPIFFS/UI-Image auf die Wallbox uebertragen:

```sh
pio run -t buildfs
```

Dieser Befehl wird im Firmware-Projekt (eine Ebene oberhalb) ausgefuehrt.
Er baut das UI-Image, fuehrt aber keinen Upload aus.

Die neue Ringanzeige benoetigt zusaetzlich die passende MAIN-Firmware: Der
App-WebSocket liefert `maxChargePower` (11/22 nach DIP-Schalter) und `ledStatus`
mit label, color, waveColor, animation und periodMs direkt aus der LED-Steuerung.
Ohne Ladegrenze bleibt die Leistungseingabe gesperrt. Der Ring zeigt den effektiven
Leistungssollwert der Wallbox, nicht die gemessene Ladeleistung. Die bestehende
Mindestleistung von 1,4 kW fuer positive Vorgaben bleibt unveraendert.

Die App verwendet eine eigene, ruhigere Darstellungspalette fuer die vom Controller
gemeldeten LED-Farben: Blau `#588fc7`, Gruen `#75b995`, Orange `#d5ad70`,
Rot `#d77e83`, Violett `#ad91c9`. Animationen und Zustandszuordnung bleiben erhalten.
Die physischen LEDs und die Firmware-Farbwerte werden dadurch nicht geaendert.

Web-/Branding-Regressionstests: `python tool/test_web_app.py --browser <Chrome-Pfad>`
(Python-Pakete `playwright` und `pillow`). Diese testen mit lokalen, simulierten
Wallbox-Daten; es werden keine Ladebefehle an eine echte Wallbox gesendet.
