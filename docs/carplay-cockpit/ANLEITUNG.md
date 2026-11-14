# CarPlay-Karte im Virtual Cockpit – Testversion

Mit diesem Paket zeigt das Virtual Cockpit die CarPlay-Karte (Google Maps oder Apple Karten) an
der Stelle der Audi-Karte. Entwickelt und getestet in einem A6 C8; Tester melden es auch in
einem Q8 und einem e-tron als funktionierend. **Alles auf eigene Gefahr.**

## Voraussetzungen

- **MMI-Softwareversion prüfen:** im MMI unter *Einstellungen → Systeminformationen → Version*
  (oder ähnlich). Dort muss `MH2p_…_P2711` stehen. Bei anderen Versionen bitte erst
  melden. CarPlay läuft dort zwar weiter, aber im Cockpit kommt nichts an.
- **CarPlay kabellos** funktioniert schon. Mit Kabel geht es nicht.
- **SD-Karte** (oder USB-Stick, falls dein Auto ModKit-Updates vom Stick annimmt), FAT32
  formatiert.
- **Liegt bei:** LawPauls Mod *NavCompassIgnore* (Ordner `Mods/NavCompassIgnore`,
  https://github.com/LawPaul/MH2p_NavCompassIgnore). Ohne ihn schaltet das Cockpit auf den
  Kompass, sobald in CarPlay eine Navigation startet, dann ist keine Karte zu sehen. Wer ihn
  schon hat, bekommt ihn einfach noch einmal installiert.

## Installieren

1. Den **Inhalt** der Zip-Datei direkt auf die leere Karte entpacken. Danach liegen auf der
   obersten Ebene die Ordner `Data`, `Logs`, `Meta`, `Mods`.
2. Prüfen, dass `Mods/ClusterIntegration/enable_carplay_altscreen.txt` da ist. Die leere
   Datei schaltet die CarPlay-Karte ein und liegt in diesem Paket schon bei.
3. Zündung an (am besten Motor laufen lassen), Karte einstecken. Das MMI startet neu und
   installiert. Am Ende meldet es, dass es jetzt normal neu startet, und der Bildschirm wird
   schwarz. Dann die Karte herausziehen. Das MMI startet danach normal.
4. **Die Karte danach nicht löschen oder überschreiben.** Auf ihr liegen die Sicherungen
   (`Mods/ClusterIntegration/Update/Backup`), die zum Deinstallieren gebraucht werden.

## Auf eine neue Version aktualisieren

Nicht vorher deinstallieren. Den Inhalt der neuen Zip-Datei auf **dieselbe** Karte entpacken und
vorhandene Dateien ersetzen lassen; den Ordner `Mods/ClusterIntegration/Update/Backup` dabei nicht
löschen (die Zip-Datei enthält keinen, er bleibt also stehen). Dann die Karte wie beim
Installieren einstecken. Der Installer erkennt, was schon installiert ist, und ersetzt nur, was
sich geändert hat.

## Benutzen

- iPhone verbinden, in CarPlay Google Maps oder Apple Karten öffnen.
- Im Cockpit die Kartenansicht wählen, also dort, wo sonst die Audi-Karte ist. Nach ein paar
  Sekunden erscheint die CarPlay-Karte.
- Lenkrad-Rädchen zoomt die Karte (Google Maps und Apple Karten; Waze reagiert nicht darauf).
  Beide Cockpit-Ansichten (große Karte / große Tachos) gehen.
- Der Zoom wird gemerkt: Nach einem Neuverbinden oder MMI-Neustart zoomt die Karte ein paar
  Sekunden nach dem Erscheinen wieder dorthin, wo du sie gelassen hast.
- Die Cockpit-Karte wechselt zusammen mit CarPlay auf dem MMI zwischen Tag und Nacht.
- Das iPhone wird um sein Tempolimit-Schild auf der Cockpit-Karte gebeten. Im Testauto ist
  bisher keins erschienen; ob die Karten-App eins zeichnet, hängt womöglich von Region und
  Route ab. Bitte melden, was ihr seht.
- Die CarPlay-Karte bleibt im Cockpit, auch wenn das MMI etwas anderes zeigt, zum Beispiel
  Radio, Rückfahrkamera oder die Audi-Navigation.
- **Umschalten im Cockpit:** Im Cockpit-Menü *Navigation* steht unter „Karte“ die Zeile
  **Smartphone Map** mit Häkchen (in früheren Versionen „Carplay Map“). Sie erscheint nur,
  solange CarPlay oder Android Auto seine Karte im Cockpit zeigt. Häkchen an: Karte vom
  Handy. Häkchen aus: Audi-Karte. Die Einstellung bleibt nach einem Neustart erhalten.
- Die Zoomanzeige der Audi-Karte unten (Lupe, „400 m“), das Verkehrslogo links unten („TMC“)
  und das Logo der Satellitenkarte („Google“) sind ausgeblendet, solange die CarPlay-Karte
  läuft. Höhe, Straße und Kompass bleiben stehen.
- **Android Auto (per Kabel):** Google Maps erscheint ebenfalls im Cockpit, über die volle
  Breite, mit der Abbiegekarte in beiden Ansichten frei neben den Rundinstrumenten. Das
  Rädchen zoomt dort nicht; Google Maps zoomt selbst.

## Rückmeldung

Am meisten helfen:

- Fotos vom Cockpit.
- Die Datei `Logs/ClusterIntegration.log` von der Karte. Bitte vor dem Verschicken
  durchsehen, sie enthält Daten über dein MMI.
- Kurz beschreiben: Auto, MMI-Version, iPhone/iOS-Version und was passiert oder nicht
  passiert.

Falls das Cockpit nach dem Start länger als ein paar Sekunden die Audi-Karte zeigt, obwohl
CarPlay läuft: bitte Bescheid geben, wie lange es gedauert hat.

## Wieder entfernen

- **Nur die CarPlay-Karte abschalten:** auf der Karte `enable_carplay_altscreen.txt` löschen
  und die Karte wie bei der Installation noch einmal einstecken.
- **Alles entfernen:** im Ordner `Mods/ClusterIntegration` eine leere Datei `uninstall.txt`
  anlegen und die Karte noch einmal einstecken. Danach ist das MMI wieder im
  Auslieferungszustand, was diesen Mod angeht. NavCompassIgnore bleibt installiert; wer ihn
  auch loswerden will, legt zusätzlich in `Mods/NavCompassIgnore` eine `uninstall.txt` an
  (nicht, wenn er schon vorher drauf war).

Kostenlos, nicht für den Verkauf (CC BY-NC-SA 4.0 bzw. GPL-3.0). NavCompassIgnore und das
MH2p SD ModKit sind von LawPaul (https://github.com/LawPaul), CC BY-NC-SA 4.0, unverändert
beigelegt.

- Download (neueste Version): https://github.com/CrEaK/mh2p-cluster/releases
- Fehler und Fragen: https://github.com/CrEaK/mh2p-cluster/issues
- Stand des Projekts: https://github.com/jonny190/mh2p-cluster/pull/23
