#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os
import sys
import shutil
import subprocess
import zipfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DIST_DIR = os.path.join(SCRIPT_DIR, "dist")
STAGING_DIR = os.path.join(DIST_DIR, "staging")
AMIGA_DRAWER = os.path.join(STAGING_DIR, "VortexBrowse")
SERVER_DRAWER = os.path.join(STAGING_DIR, "Server")

print("=== [1/6] Preparing Staging Directories ===")
if os.path.exists(DIST_DIR):
    shutil.rmtree(DIST_DIR)
os.makedirs(AMIGA_DRAWER, exist_ok=True)
os.makedirs(SERVER_DRAWER, exist_ok=True)
os.makedirs(os.path.join(AMIGA_DRAWER, "icons"), exist_ok=True)

# -------------------------------------------------------------
# AmigaGuide Documentation
# -------------------------------------------------------------
GUIDE_CONTENT = """@database "VortexBrowse.guide"
@author "TearsOfMe"
@(c) "2026 VortexBrowse Project"
@$VER: VortexBrowse.guide 2.0 (29.10.2026)

@node "Main" "VortexBrowse 2.0 - Benutzerhandbuch"
@{b}========================================================================@{ub}
           @{b}VORTEXBROWSE 2.0 FUER AMIGAOS 4.1@{ub}
      Moderner Web-Browser mit Remote-Rendering & Streaming
@{b}========================================================================@{ub}

Willkommen zu @{b}VortexBrowse 2.0@{ub}!

VortexBrowse ermoeglicht modernes Surfen im World Wide Web auf Ihrem AmigaOS 4.1
System mit voller Unterstuetzung fuer moderne Web-Standards (HTML5, CSS3,
JavaScript ES2024, WebGL, YouTube, Twitch, etc.) bei maximaler Geschwindigkeit
und minimaler CPU-Auslastung.

@{b}Inhaltsverzeichnis:@{ub}

  * @{"Uebersicht & Funktionsweise" link "Overview"}
  * @{"Systemanforderungen" link "Requirements"}
  * @{"Installation & Schnellstart" link "Installation"}
  * @{"Newlib vs. clib4 Versionen" link "RuntimeVersions"}
  * @{"Bedienung & Funktionen" link "Features"}
      - @{"Multi-Tab-Browsing" link "Tabs"}
      - @{"Odyssey-Schnellwahl & Lesezeichen" link "Bookmarks"}
      - @{"Download-Manager" link "Downloads"}
      - @{"Passwort-Manager & Auto-Login" link "Passwords"}
      - @{"Dark Mode (Dunkles Theme)" link "DarkMode"}
      - @{"Audio-Streaming (16-Bit AHI)" link "Audio"}
      - @{"Kontextmenues & Mausbedienung" link "MouseContext"}
  * @{"Einstellungen (Preferences)" link "Preferences"}
  * @{"Tipps zur CPU-Entlastung & Performance" link "Performance"}
  * @{"Tastatur-Kuerzel (Shortcuts)" link "Shortcuts"}
  * @{"Server-Einrichtung (Linux PC / Raspberry Pi)" link "ServerSetup"}
  * @{"Fehlerbehebung (FAQ)" link "Troubleshooting"}
  * @{"English Documentation" link "EnglishDoc"}

@endnode

@node "Overview" "Uebersicht & Funktionsweise"
@{b}Uebersicht & Funktionsweise@{ub}

Klassische und aeltere Webbrowser auf dem Amiga haben oft Schwierigkeiten mit
hochkomplexen JavaScript-Websites, modernen Verschluesselungs-Standards (TLS 1.3),
Videowiedergabe und WebGL.

@{b}VortexBrowse loest dieses Problem mit einer modernen Client-Server-Architektur:@{ub}

1. @{b}Der Rendering-Server@{ub} (laeuft auf einem beliebigen Linux-PC, Raspberry Pi 4/5,
   oder Heimserver im lokalen Netzwerk):
   - Fuehrt eine vollwertige, moderne Chromium-Engine (Playwright/CDP) aus.
   - Rendert Webseiten pixelgenau, fuehrt moderne JavaScript-Skripte aus und dekodiert
     Videos und interaktive Webanwendungen hardwareunterstuetzt.
   - Wandelt den Bildschirminhalt in Echtzeit ueber optimierte C-SIMD-Routinen in
     einen hocheffizienten MJPEG- oder LZ4-Stream um.
   - Streamt Audio synchron in 16-Bit Stereo ueber einen separaten TCP-Audiokanal.

2. @{b}Der AmigaOS 4 Client@{ub} (native Reaction/BOOPSI PowerPC-Anwendung):
   - Empfaengt den Stream und stellt ihn verzuegerungsfrei auf der Workbench oder
     einem eigenen Screen dar.
   - Nutzt die 2D-Hardwarebeschleunigung moderner Grafikkarten (Radeon HD / Radeon RX)
     ueber @{i}CompositeTags()@{ui} zum freien Skalieren von Aufloesungen (z. B. 720p HD
     oder 540p qHD auf die volle Fenstergroesse).
   - Sendet Tastatur-, Maus-, Klick- und Rad-Ereignisse unmittelbar an den Server.
   - Spielt Audio ueber AHI direkt und synchron ab.

Das Ergebnis ist ein ultra-schneller, vollstaendiger moderner Web-Browser mit dem
authentischen Look & Feel von AmigaOS 4!

@endnode

@node "Requirements" "Systemanforderungen"
@{b}Systemanforderungen@{ub}

@{b}AmigaOS 4 Client:@{ub}
- AmigaOS 4.1 Final Edition (Update 1 oder Update 2 empfohlen)
- PowerPC Prozessor (G4, G5, SAM440, SAM460, X1000, X5000 oder QEMU pegasos2/sam460ex)
- Grafikkarte mit 24/32-Bit Farbtiefe (Radeon HD oder Radeon RX fuer Hardware-Scaling empfohlen)
- Netzwerkkarte und aktiver Internet-/LAN-Zugang
- AHI Audio-Treiber fuer Soundausgabe
- Optional: clib4 Runtime (fuer VortexBrowse.clib4)

@{b}Rendering-Server (Backend):@{ub}
- Beliebiger Linux-PC, Raspberry Pi 4 / 5 oder Home-Server
- Betriebssystem: Raspberry Pi OS, Debian, Ubuntu, Arch, CachyOS, Fedora, etc.
- Python 3.10 oder neuer
- Mindestens 2 GB RAM (Raspberry Pi 4 mit 4GB/8GB optimal)
- Lokale Netzwerkverbindung zum Amiga (LAN Gigabit Ethernet oder 5GHz WLAN)

@endnode

@node "Installation" "Installation & Schnellstart"
@{b}Installation & Schnellstart@{ub}

@{b}Schritt 1: Server starten (Linux PC oder Raspberry Pi)@{ub}
1. Kopieren Sie den Ordner @{b}Server/@{ub} auf Ihren Linux-PC oder Raspberry Pi.
2. Oeffnen Sie ein Terminal im Server-Verzeichnis.
3. Starten Sie das automatisierte Installationsskript:
   @{b}./install_server.sh@{ub}
   Das Skript richtet automatisch die virtuelle Umgebung ein, installiert alle
   Abhaengigkeiten und Chromium und kompiliert bei Bedarf die SIMD-Beschleunigung.
4. Notieren Sie sich die am Ende des Skripts angezeigte @{b}Server-IP-Adresse@{ub}
   (z. B. 192.168.178.60).
5. Starten Sie den Server mit:
   @{b}./run_server.sh@{ub}
   (Alternativ koennen Sie ihn auch als dauerhaften Hintergrund-Dienst einrichten!)

@{b}Schritt 2: VortexBrowse auf dem AmigaOS 4 starten@{ub}
1. Entpacken Sie das Archiv @{b}VortexBrowse-2.0-AmigaOS4.lha@{ub} an Ihren
   gewuenschten Ort (z. B. @{b}Work:Internet/VortexBrowse@{ub}).
2. Starten Sie @{b}VortexBrowse@{ub} per Doppelklick auf das Programm-Icon.
3. Waehlen Sie im Menue @{b}Projekt -> Einstellungen...@{ub} (Tastatur: Amiga+E).
4. Wechseln Sie auf den Reiter @{b}Streaming Server@{ub}.
5. Tragen Sie unter @{b}Server-Host@{ub} die IP-Adresse Ihres Linux-PCs oder Raspberry Pis
   ein und stellen Sie sicher, dass Port @{b}7788@{ub} eingestellt ist.
6. Klicken Sie auf @{b}Speichern@{ub}.
7. VortexBrowse verbindet sich sofort mit dem Server und laedt Ihre Startseite!

@endnode

@node "RuntimeVersions" "Newlib vs. clib4 Versionen"
@{b}Newlib vs. clib4 Versionen@{ub}

Das VortexBrowse 2.0 Paket enthaelt zwei separat kompilierte ausfuehrbare Dateien:

1. @{b}VortexBrowse@{ub} (Standard, newlib):
   - Verwendet die offizielle AmigaOS 4 Standard-C-Bibliothek (@{i}newlib.library@{ui}).
   - Laeuft direkt "out-of-the-box" auf jeder AmigaOS 4.1 FE Installation ohne
     zusaetzliche Runtimes.
   - Vollstaendig statisch gelinkt mit @{i}liblz4.a@{ui} und @{i}libjpeg.a@{ui}.

2. @{b}VortexBrowse.clib4@{ub} (Alternative, clib4):
   - Verwendet die moderne, POSIX-konforme @{i}clib4@{ui} C-Runtime.
   - Bietet fuer Benutzer, die modernere Unix-Tools und Runtimes auf AmigaOS 4 einsetzen,
     optimierte Speicherverwaltung und POSIX-Signalbehandlung.
   - Ebenfalls vollstaendig statisch gelinkt, benoetigt keine externen Shared Objects.

@{b}Empfehlung:@{ub} Wenn Sie unsicher sind, verwenden Sie einfach @{b}VortexBrowse@{ub} (Newlib).
Beide Versionen bieten exakt denselben vollen Funktionsumfang, dieselbe Grafikbeschleunigung
und dieselbe Benutzeroberflaeche!

@endnode

@node "Features" "Bedienung & Funktionen"
@{b}Bedienung & Funktionen@{ub}

VortexBrowse bietet eine intuitive, an modernen Amiga-Browsern (wie Odyssey Web Browser
und IBrowser) orientierte Reaction-Oberflaeche:

- @{"Multi-Tab-Browsing" link "Tabs"}
- @{"Odyssey-Schnellwahl & Lesezeichen" link "Bookmarks"}
- @{"Download-Manager" link "Downloads"}
- @{"Passwort-Manager & Auto-Login" link "Passwords"}
- @{"Dark Mode (Dunkles Theme)" link "DarkMode"}
- @{"Audio-Streaming (16-Bit AHI)" link "Audio"}
- @{"Kontextmenues & Mausbedienung" link "MouseContext"}

@endnode

@node "Tabs" "Multi-Tab-Browsing"
@{b}Multi-Tab-Browsing@{ub}

VortexBrowse unterstuetzt echtes Tabbed-Browsing mit mehreren gleichzeitigen Webseiten:

- @{b}Neuer Tab:@{ub} Klicken Sie auf die @{b}[+]@{ub}-Schaltflaeche in der Tab-Leiste
  oder druecken Sie @{b}Amiga+T@{ub}.
- @{b}Tab wechseln:@{ub} Klicken Sie einfach auf den Reiter des gewuenschten Tabs.
- @{b}Tab schliessen:@{ub} Klicken Sie auf die @{b}[x]@{ub}-Schaltflaeche des aktiven Tabs
  oder druecken Sie @{b}Amiga+W@{ub}.
- @{b}Link in neuem Tab oeffnen:@{ub} Rechtsklick auf einen Link und im Kontextmenue
  @{b}"In neuem Tab oeffnen"@{ub} waehlen.

Jeder Tab wird serverseitig in einer eigenen Browser-Instanz verwaltet, sodass Ihre
Sitzungen sauber getrennt bleiben.

@endnode

@node "Bookmarks" "Odyssey-Schnellwahl & Lesezeichen"
@{b}Odyssey-Schnellwahl & Lesezeichen@{ub}

VortexBrowse besitzt eine praktische Schnellwahl-Leiste direkt unter der Adressleiste:

- @{b}Standard-Schnellwahl-Buttons:@{ub}
  Enthaelt vorkonfigurierte Schnellzugriffe mit massgeschneiderten Icons fuer beliebte
  Websites:
  - @{b}Amiga-News.de@{ub}
  - @{b}OS4Depot.net@{ub}
  - @{b}Amigans.net@{ub}
  - @{b}Aminet.net@{ub}
  - @{b}YouTube@{ub}
  - @{b}Twitch@{ub}
  - @{b}Wikipedia@{ub}
  - @{b}Google@{ub}
  - @{b}GitHub@{ub}

- @{b}Schnellwahl anpassen:@{ub}
  In den @{b}Einstellungen (Prefs)@{ub} auf dem Reiter @{b}Schnellwahl@{ub} koennen Sie
  die Beschriftungen, URLs und Symbole der Knoepfe nach Belieben anpassen oder neue
  eigene Favoriten anlegen!
- @{b}Aktuelle Seite hinzufuegen:@{ub}
  Waehlen Sie im Menue @{b}Lesezeichen -> Zu Schnellwahl hinzufuegen@{ub} (Amiga+D).

@endnode

@node "Downloads" "Download-Manager"
@{b}Download-Manager@{ub}

Der Download-Manager von VortexBrowse funktioniert aehnlich wie in Odyssey Web Browser
oder IBrowser:

- @{b}Automatischer Start:@{ub}
  Sobald Sie auf einer Webseite einen Download anklicken (oder im Kontextmenue
  @{b}"Link speichern unter..."@{ub} waehlen), uebernimmt der VortexBrowse Download-Manager
  den Download transparent im Hintergrund.
- @{b}Download-Fenster oeffnen:@{ub}
  Druecken Sie @{b}Amiga+J@{ub} oder waehlen Sie im Menue @{b}Projekt -> Download-Manager...@{ub}.
- @{b}Echtzeitanzeige:@{ub}
  Fuer jeden Download werden Dateiname, Status, Fortschrittsbalken in Prozent,
  Download-Geschwindigkeit (z. B. KB/s oder MB/s) und die geschaetzte Restzeit (ETA)
  angezeigt.
- @{b}Aktionen:@{ub}
  - @{b}Datei oeffnen:@{ub} Startet die heruntergeladene Datei direkt mit dem passenden
    AmigaOS-Standardprogramm.
  - @{b}Ordner oeffnen:@{ub} Oeffnet das Zielverzeichnis auf der Workbench.
  - @{b}Eintrag loeschen:@{ub} Entfernt den markierten Eintrag aus der Liste.
  - @{b}Aufraeumen:@{ub} Loescht alle beendeten Downloads aus der Liste.
- @{b}Speicherort:@{ub}
  Standardmaessig werden Dateien im Pfad @{b}Downloads:@{ub} bzw. @{b}RAM:Downloads/@{ub}
  gespeichert. Der Pfad laesst sich in den Einstellungen frei konfigurieren.

@endnode

@node "Passwords" "Passwort-Manager & Auto-Login"
@{b}Passwort-Manager & Auto-Login@{ub}

VortexBrowse enthaelt einen sicheren, integrierten Passwort-Manager:

- @{b}Lokale, starke Verschluesselung:@{ub}
  Alle Anmeldedaten werden verschluesselt mit @{b}Blowfish-CBC@{ub} und einem ueber
  @{b}SHA-256@{ub} abgeleiteten Master-Key in @{b}PROGDIR:passwords.dat@{ub} gespeichert.
  Es werden keinerlei Passwoerter im Klartext abgelegt!
- @{b}Passwort-Tresor oeffnen:@{ub}
  Druecken Sie @{b}Amiga+K@{ub} oder waehlen Sie im Menue @{b}Projekt -> Passwort-Manager...@{ub}.
- @{b}Auto-Login fuer die aktuelle Seite:@{ub}
  Druecken Sie @{b}Amiga+L@{ub}. VortexBrowse erkennt automatisch die Anmeldefelder der
  aktuellen Website und fuellt Benutzername und Passwort selbstaendig aus.

@endnode

@node "DarkMode" "Dark Mode (Dunkles Theme)"
@{b}Dark Mode (Dunkles Theme)@{ub}

VortexBrowse 2.0 bietet einen vollstaendigen Dunkelmodus fuer augenschonendes Surfen:

- @{b}Umschalten:@{ub}
  Druecken Sie @{b}Amiga+M@{ub} oder waehlen Sie im Menue @{b}Ansicht -> Dunkles Theme an/aus@{ub}.
- @{b}Serverseitige Webseiten-Anpassung:@{ub}
  Auf dem Server injiziert @{i}DarkReader@{ui} eine hochwertige dynamische CSS-Dunkel-Farbpalette,
  die alle Webseiten in ein sauberes, kontrastreiches dunkles Layout verwandelt.
- @{b}Amiga Reaction GUI Anpassung:@{ub}
  Die gesamte Amiga-Oberflaeche (Hintergruende, Rahmen, Adressleiste, Suchleiste, Tabs,
  Knoepfe und Fenster) wird automatisch an ein passendes dunkles Farbschema angepasst.

@endnode

@node "Audio" "Audio-Streaming (16-Bit AHI)"
@{b}Audio-Streaming (16-Bit AHI)@{ub}

VortexBrowse streamt Audio von YouTube-Videos, Twitch-Livestreams oder Audio-Websites
in Echtzeit synchron zum Amiga:

- @{b}Audio-Qualitaet:@{ub}
  16-Bit Stereo, 48.000 Hz Samplingrate ueber AmigaOS AHI.
- @{b}Geringe Latenz:@{ub}
  Der Stream wird ueber einen separaten, schlanken TCP-Audiokanal uebertragen, sodass
  keine Ruckler bei der Grafikdarstellung auftreten.
- @{b}Ein-/Ausschalten:@{ub}
  In den @{b}Einstellungen (Prefs)@{ub} unter @{b}Streaming Server@{ub} laesst sich die
  Audio-Uebertragung mit einem Klick aktivieren oder deaktivieren.

@endnode

@node "MouseContext" "Kontextmenues & Mausbedienung"
@{b}Kontextmenues & Mausbedienung@{ub}

- @{b}Mausrad:@{ub}
  AmigaOS 4 Wheel-Events (RAWKEY_NM_WHEEL_UP und RAWKEY_NM_WHEEL_DOWN) werden direkt in
  stufenloses Web-Scrollen umgesetzt.
- @{b}Rechtsklick auf Links:@{ub}
  Oeffnet ein Kontextmenue mit den Optionen:
  - Link in neuem Tab oeffnen
  - Link-Adresse in Zwischenablage kopieren
  - Link speichern unter... (Download-Manager)
- @{b}Rechtsklick auf Bilder:@{ub}
  Oeffnet ein Kontextmenue zum Kopieren der Bildadresse oder Herunterladen des Bildes.
- @{b}Text markieren & Kopieren:@{ub}
  Ausgewaehlter Text kann mit @{b}Amiga+C@{ub} in das AmigaOS-Clipboard (Unit 0) kopiert
  und mit @{b}Amiga+V@{ub} in Eingabefelder eingefuegt werden.

@endnode

@node "Preferences" "Einstellungen (Preferences)"
@{b}Einstellungen (Preferences)@{ub}

Oeffnen Sie das Einstellungsfenster ueber @{b}Projekt -> Einstellungen...@{ub} (Amiga+E).

Das Einstellungsfenster ist uebersichtlich in fuenf Reiter unterteilt:

1. @{b}Allgemein:@{ub}
   - Startseite (Home URL)
   - Standard-Suchmaschine (Google, DuckDuckGo, Bing, Ecosia, Qwant, Yahoo)
   - Vollbild beim Start aktivieren

2. @{b}Schnellwahl:@{ub}
   - Konfiguration der Knoepfe in der Odyssey-Schnellwahl-Leiste
   - Zuweisung von Titeln, URLs und PNG-Icons

3. @{b}Bildschirm / Fenster:@{ub}
   - Auswahl zwischen Workbench-Fenster und eigenem Screen
   - Feste Fenstergroessen-Presets (1024x768, 1280x720, 1920x1080)

4. @{b}Downloads:@{ub}
   - Standard-Download-Verzeichnis (z. B. Downloads: oder RAM:)
   - Option: Vor jedem Download Zielverzeichnis abfragen
   - Schaltflaeche zum direkten Oeffnen des Download-Managers

5. @{b}Streaming Server:@{ub}
   - @{b}Server-Preset:@{ub} Voreinstellungen fuer Raspberry Pi oder Host-PC
   - @{b}Server-Host & Port:@{ub} IP-Adresse und Port (Standard: 7788)
   - @{b}Stream-Aufloesung:@{ub} Vollbild (1:1), 720p HD, 540p qHD (siehe Performance!)
   - @{b}JPEG-Qualitaet:@{ub} Slider von 1 bis 100 (Standard: 100 fuer kristallklares Bild)
   - @{b}Dynamische Bitrate:@{ub} Reduziert Qualitaet automatisch waehrend schneller Videos
   - @{b}Aktuellen Server beim Beenden speichern:@{ub} Merkt sich geaenderte Server-IPs

@endnode

@node "Performance" "Tipps zur CPU-Entlastung & Performance"
@{b}Tipps zur CPU-Entlastung & Performance@{ub}

Wenn Sie einen Amiga mit schwaecherem PowerPC-Prozessor (wie z. B. SAM440, SAM460
oder Standard-G4) besitzen, beachten Sie folgenden wichtigen Tipp:

@{b}Hardware-Composite-Scaling nutzen!@{ub}
Ein Vollbild-Stream mit z. B. 1920x1080 Pixeln erfordert die Dekodierung von ueber
2 Millionen Pixeln pro Bild. Bei 30 Bildern/s benoetigt das Dekodieren auf der CPU
viel Rechenleistung.

@{b}Loesung:@{ub}
Stellen Sie in den Einstellungen unter @{b}Streaming Server -> Stream-Aufloesung@{ub}
den Wert auf:
  @{b}Stream: 720p HD (1280x720)@{ub}  oder  @{b}Stream: 540p qHD (960x540)@{ub}

@{b}Der geniale Vorteil:@{ub}
Der Server sendet nur 1280x720 bzw. 960x540 Pixel. Die Radeon-Grafikkarte Ihres
AmigaOS 4 Systems skaliert das Bild ueber @{b}CompositeTags()@{ub} vollkommen
kostenlos und in Hardware auf die volle Fenstergroesse!
Dies senkt die CPU-Auslastung auf dem Amiga um @{b}50% bis 70%@{ub}, waehrend das Bild
dank bi-linearer Filterung gestochen scharf und fluessig bleibt!

@endnode

@node "Shortcuts" "Tastatur-Kuerzel (Shortcuts)"
@{b}Tastatur-Kuerzel (Shortcuts)@{ub}

@{b}Projekt:@{ub}
  @{b}Amiga+N@{ub}       Neue Seite / Startseite laden
  @{b}Amiga+O@{ub}       URL-Eingabefeld fokussieren
  @{b}Amiga+T@{ub}       Neuen Tab oeffnen
  @{b}Amiga+W@{ub}       Aktuellen Tab schliessen
  @{b}Amiga+J@{ub}       Download-Manager oeffnen
  @{b}Amiga+K@{ub}       Passwort-Manager oeffnen
  @{b}Amiga+L@{ub}       Auto-Login fuer aktuelle Seite ausfuehren
  @{b}Amiga+E@{ub}       Einstellungen (Preferences) oeffnen
  @{b}Amiga+Q@{ub}       VortexBrowse beenden

@{b}Navigation:@{ub}
  @{b}Amiga+B@{ub}       Zurueck (Back)
  @{b}Amiga+F@{ub}       Vorwaerts (Forward)
  @{b}Amiga+R@{ub}       Seite neu laden (Reload)
  @{b}Amiga+S@{ub}       Ladevorgang stoppen (Stop)
  @{b}Amiga+H@{ub}       Startseite laden (Home)

@{b}Ansicht:@{ub}
  @{b}Amiga+M@{ub}       Dunkles Theme (Dark Mode) an/aus
  @{b}F11@{ub}           Vollbild / Workbench-Fenster umschalten
  @{b}Amiga++@{ub}       Schrift vergroessern
  @{b}Amiga+-@{ub}       Schrift verkleinern
  @{b}Amiga+0@{ub}       Standardschriftgroesse zuruecksetzen

@{b}Bearbeiten:@{ub}
  @{b}Amiga+C@{ub}       Markierten Text in Zwischenablage kopieren
  @{b}Amiga+V@{ub}       Aus Zwischenablage einfuegen
  @{b}Amiga+X@{ub}       Ausschneiden

@endnode

@node "ServerSetup" "Server-Einrichtung (Linux PC / Raspberry Pi)"
@{b}Server-Einrichtung (Linux PC / Raspberry Pi)@{ub}

Der Server kann auf jedem gaengigen Linux-System betrieben werden (z. B. Raspberry Pi 4,
Raspberry Pi 5, Intel/AMD PC mit Debian, Ubuntu, Linux Mint, CachyOS, Arch, etc.).

@{b}Automatische Installation mit install_server.sh:@{ub}
1. Oeffnen Sie ein Terminal im Verzeichnis @{b}Server/@{ub}.
2. Fuehren Sie das Skript aus:
   @{b}./install_server.sh@{ub}
3. Das Skript fuehrt vollautomatisch folgende Schritte durch:
   - Ueberprueft Python 3 (benoetigt Python 3.10 oder neuer).
   - Erstellt eine isolierte virtuelle Umgebung (@{b}.venv@{ub}).
   - Installiert die benoetigten Pakete (@{i}playwright@{ui}, @{i}lz4@{ui}, @{i}Pillow@{ui}).
   - Laedt die optimierte Chromium-Browser-Engine herunter.
   - Kompiliert die native SIMD-Grafikbeschleunigung (@{b}fast_pixconv.so@{ub}) mit AVX2
     bzw. ARM-NEON fuer maximale Geschwindigkeit.
   - Ermittelt die IP-Adresse Ihres Rechners im Netzwerk.
   - Bietet an, den Server als systemd-Dienst einzurichten, damit er bei jedem Neustart
     automatisch im Hintergrund laeuft!

@{b}Manuelles Starten:@{ub}
   @{b}./run_server.sh@{ub}

@{b}Ports im Netzwerk:@{ub}
- Port @{b}7788@{ub}: Haupt-Streaming-Port (MJPEG/LZ4 und Steuerung)
- Port @{b}9757@{ub}: Sekundaerer Port / Audio-Streaming

@endnode

@node "Troubleshooting" "Fehlerbehebung (FAQ)"
@{b}Fehlerbehebung (FAQ)@{ub}

@{b}Frage: VortexBrowse zeigt "Verbindung fehlgeschlagen" oder bleibt weiss.@{ub}
@{b}Antwort:@{ub}
1. Ueberpruefen Sie, ob der Server auf dem Linux-PC/Raspberry Pi laeuft (@{b}./run_server.sh@{ub}).
2. Stellen Sie sicher, dass in den Einstellungen von VortexBrowse die richtige IP-Adresse
   des Servers eingetragen ist.
3. Pruefen Sie, ob eine Firewall auf dem Server die Ports 7788 und 9757 blockiert.

@{b}Frage: Die CPU-Last auf meinem Amiga ist bei Videos sehr hoch.@{ub}
@{b}Antwort:@{ub}
Waehlen Sie im Menue @{b}Ansicht -> Stream: 720p HD@{ub} oder @{b}Stream: 540p qHD@{ub}.
Dadurch uebernimmt die Radeon-Grafikkarte das Skalieren ueber Composite in Hardware,
was die CPU-Auslastung auf 20-30% senkt!

@{b}Frage: Ich hoere keinen Ton bei Videos.@{ub}
@{b}Antwort:@{ub}
1. Pruefen Sie, ob in den VortexBrowse-Einstellungen die Option @{b}Audio-Streaming@{ub}
   aktiviert ist.
2. Stellen Sie sicher, dass Ihr AHI auf dem Amiga korrekt konfiguriert ist (z. B. Unit 0).
3. Auf dem Linux-Server muss PulseAudio oder PipeWire aktiv sein.

@endnode

@node "EnglishDoc" "English Documentation Summary"
@{b}English Documentation Summary@{ub}

@{b}VortexBrowse 2.0@{ub} is a remote web rendering browser client for AmigaOS 4.1.
It renders modern web pages (HTML5, CSS3, ES2024 JavaScript, WebGL, YouTube, etc.)
on a companion Linux PC or Raspberry Pi 4/5 server, streaming ultra-smooth visuals
and 16-bit 48kHz audio directly to AmigaOS with native Reaction GUI controls.

@{b}Quick Start:@{ub}
1. On your Linux PC or Raspberry Pi:
   Enter the @{b}Server/@{ub} directory and run:
   @{b}./install_server.sh@{ub}
   Then start the server with:
   @{b}./run_server.sh@{ub}
   Note the IP address displayed.
2. On your Amiga:
   Double-click @{b}VortexBrowse@{ub} (or @{b}VortexBrowse.clib4@{ub}).
   Open @{b}Project -> Preferences...@{ub} (Amiga+E), select the @{b}Streaming Server@{ub} tab,
   enter your server IP address (Port: 7788), and click @{b}Save@{ub}.

@{b}Key Features:@{ub}
- Full multi-tab browsing (Amiga+T to open, Amiga+W to close).
- Odyssey-style quick launch toolbar with icons and customizable bookmarks.
- Odyssey/IBrowser-like integrated Download Manager (Amiga+J).
- Encrypted Password Vault & Auto-Login (Amiga+K / Amiga+L).
- Complete Dark Mode toggle (Amiga+M) with DarkReader and dark Reaction GUI.
- Hardware-accelerated scaling via CompositeTags (select 720p or 540p stream for low CPU).
- 16-bit stereo 48kHz synchronized audio via AHI.

@endnode
"""

with open(os.path.join(AMIGA_DRAWER, "VortexBrowse.guide"), "w", encoding="iso-8859-1") as f:
    f.write(GUIDE_CONTENT)

# -------------------------------------------------------------
# German Quickstart Readme: LIESMICH.txt
# -------------------------------------------------------------
LIESMICH_CONTENT = """==============================================================================
                    VORTEXBROWSE 2.0 FUER AMIGAOS 4.1
                 Schnellstartanleitung & Kurzanleitung
==============================================================================

VortexBrowse ist ein moderner Web-Browser fuer AmigaOS 4.1, der ueber ein
leistungsfaehiges Remote-Rendering-Backend (auf Linux-PC oder Raspberry Pi 4/5)
die gesamte moderne Webwelt (HTML5, CSS3, JavaScript ES2024, YouTube, Twitch,
WebGL, etc.) fluessig und ressourcenschonend auf den Amiga bringt.

------------------------------------------------------------------------------
1. INHALT DIESES ARCHIVS
------------------------------------------------------------------------------
- VortexBrowse          : Standard-Version fuer AmigaOS 4 (newlib.library)
- VortexBrowse.clib4    : Alternative Version fuer clib4-Umgebungen
- VortexBrowse.guide    : Ausfuehrliches AmigaGuide-Handbuch mit allen Details
- LIESMICH.txt          : Diese Schnellstartanleitung (Deutsch)
- README.txt            : Schnellstartanleitung (Englisch)
- icons/                : Symbole fuer die Odyssey-Schnellwahl-Leiste

------------------------------------------------------------------------------
2. SCHNELLSTART IN 2 SCHRITTEN
------------------------------------------------------------------------------

SCHRITT 1: Server starten (auf Linux-PC oder Raspberry Pi 4/5)
1. Kopieren Sie den Ordner "Server/" aus dem Gesamtpaket auf Ihren Linux-Rechner.
2. Oeffnen Sie ein Terminal im Server-Ordner und starten Sie die Installation:
      ./install_server.sh
   (Das Skript richtet automatisch Python, Playwright, Chromium und SIMD ein.)
3. Starten Sie anschliessend den Server mit:
      ./run_server.sh
4. Merken Sie sich die IP-Adresse des Rechners (z. B. 192.168.178.60).

SCHRITT 2: VortexBrowse auf dem Amiga starten
1. Starten Sie "VortexBrowse" per Doppelklick.
2. Druecken Sie Amiga+E (oder Menue "Projekt -> Einstellungen...").
3. Wechseln Sie auf den Reiter "Streaming Server".
4. Tragen Sie unter "Server-Host" die IP-Adresse Ihres Servers ein (Port: 7788).
5. Klicken Sie auf "Speichern" - VortexBrowse verbindet sich und surft los!

------------------------------------------------------------------------------
3. WICHTIGE FEATURES & BEDIENUNG
------------------------------------------------------------------------------
* Tabs:
  - Neuer Tab: [+] in der Tableiste oder Tastatur: Amiga+T
  - Tab schliessen: [x] im Tab oder Tastatur: Amiga+W

* Download-Manager:
  - Funktioniert wie bei Odyssey oder IBrowser.
  - Oeffnen mit: Amiga+J (oder Menue "Projekt -> Download-Manager...").
  - Live-Geschwindigkeit, Fortschritt in Prozent, Datei oeffnen, Ordner oeffnen.

* Dark Mode (Dunkles Theme):
  - Umschalten mit: Amiga+M.
  - Dunkle Webseiten via DarkReader und dunkle Amiga Reaction-Oberflaeche!

* Passwort-Manager & Auto-Login:
  - Passwort-Manager oeffnen: Amiga+K
  - Gespeichertes Login automatisch eintragen: Amiga+L
  - Sichere Blowfish-Verschluesselung in PROGDIR:passwords.dat!

* TIPP ZUR CPU-ENTLASTUNG (fuer SAM440, SAM460, G4):
  - Menue "Ansicht -> Stream: 720p HD" oder "Stream: 540p qHD" waehlen.
  - Die Radeon-Grafikkarte skaliert das Bild ueber Hardware-CompositeTags()!
  - Senkt die Amiga-CPU-Last um 50-70%!

Ausfuehrliche Informationen finden Sie in "VortexBrowse.guide"!
Viel Spass beim modernen Surfen auf AmigaOS 4!
==============================================================================
"""

with open(os.path.join(AMIGA_DRAWER, "LIESMICH.txt"), "w", encoding="iso-8859-1") as f:
    f.write(LIESMICH_CONTENT)

# -------------------------------------------------------------
# English Quickstart Readme: README.txt
# -------------------------------------------------------------
README_CONTENT = """==============================================================================
                    VORTEXBROWSE 2.0 FOR AMIGAOS 4.1
                 Quick Start Guide & Feature Overview
==============================================================================

VortexBrowse brings the modern web to AmigaOS 4.1 using a high-performance
headless Chromium rendering backend running on your local network (Linux PC
or Raspberry Pi 4/5). Enjoy full HTML5, CSS3, ES2024 JavaScript, WebGL,
YouTube, and Twitch with low Amiga CPU usage and native Reaction GUI.

------------------------------------------------------------------------------
1. PACKAGE CONTENTS
------------------------------------------------------------------------------
- VortexBrowse          : Standard AmigaOS 4 executable (newlib.library)
- VortexBrowse.clib4    : Alternative executable for clib4 environments
- VortexBrowse.guide    : Comprehensive AmigaGuide manual
- LIESMICH.txt          : German quickstart guide
- README.txt            : English quickstart guide
- icons/                : Toolbar and bookmark icon collection

------------------------------------------------------------------------------
2. QUICK START IN 2 STEPS
------------------------------------------------------------------------------

STEP 1: Start Server (Linux PC or Raspberry Pi 4/5)
1. Copy the "Server/" folder from the complete package to your Linux machine.
2. Open a terminal in the Server folder and execute:
      ./install_server.sh
   (Automatically installs Python virtualenv, Playwright, Chromium, and SIMD.)
3. Start the server with:
      ./run_server.sh
4. Note your server's local LAN IP address (e.g. 192.168.178.60).

STEP 2: Start VortexBrowse on AmigaOS 4
1. Double-click "VortexBrowse" on Workbench.
2. Press Amiga+E (or menu "Project -> Preferences...").
3. Click on the "Streaming Server" tab.
4. Enter your server's IP address under "Server Host" (Port: 7788).
5. Click "Save" - VortexBrowse connects instantly!

------------------------------------------------------------------------------
3. KEY FEATURES & SHORTCUTS
------------------------------------------------------------------------------
* Multi-Tab Browsing:
  - New Tab: [+] button or shortcut Amiga+T
  - Close Tab: [x] button or shortcut Amiga+W

* Integrated Download Manager:
  - Odyssey / IBrowser style download window.
  - Open with Amiga+J (or menu "Project -> Download Manager...").
  - Live transfer speeds, ETA, Open File, Open Drawer, Clean finished.

* Dark Mode:
  - Toggle with Amiga+M.
  - Server-side DarkReader CSS engine plus dark Reaction GUI styling!

* Password Manager & Auto-Login:
  - Open Password Vault: Amiga+K
  - Auto-fill login on current page: Amiga+L
  - AES/Blowfish encrypted master vault in PROGDIR:passwords.dat!

* LOW CPU TIP FOR SAM440, SAM460, G4:
  - In menu "View", pick "Stream: 720p HD" or "Stream: 540p qHD".
  - Your Radeon graphics card scales the stream in hardware via CompositeTags()!
  - Reduces Amiga CPU usage by 50-70%!

For comprehensive documentation, open "VortexBrowse.guide" in MultiView!
Enjoy modern web browsing on AmigaOS 4!
==============================================================================
"""

with open(os.path.join(AMIGA_DRAWER, "README.txt"), "w", encoding="utf-8") as f:
    f.write(README_CONTENT)

# -------------------------------------------------------------
# Copy Client Binaries and Icons
# -------------------------------------------------------------
print("=== [2/6] Copying AmigaOS 4 Client Binaries and Icons ===")
shutil.copy2("amiga-client/VortexBrowse.stripped", os.path.join(AMIGA_DRAWER, "VortexBrowse"))
shutil.copy2("amiga-client/VortexBrowse.info", os.path.join(AMIGA_DRAWER, "VortexBrowse.info"))
shutil.copy2("amiga-client/VortexBrowse.clib4.stripped", os.path.join(AMIGA_DRAWER, "VortexBrowse.clib4"))
shutil.copy2("amiga-client/VortexBrowse.info", os.path.join(AMIGA_DRAWER, "VortexBrowse.clib4.info"))

# Copy Icons to icons/
for icon in os.listdir("amiga-client/icons"):
    if icon.endswith(".png"):
        shutil.copy2(os.path.join("amiga-client/icons", icon), os.path.join(AMIGA_DRAWER, "icons", icon))

# Copy Info Files from Kyvos OS4 ISO / repository
KYVOS_DOCS_INFO = "/home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VideoVortex/Data/MPlayer/Docs.info"
KYVOS_README_INFO = "/home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VideoVortex/Data/MPlayer/Docs/README.info"
KYVOS_GUIDE_INFO = "/home/tears/kyvos-2.2.0-linux64/ISO/AmigaOS 4.1 Final Edition Update 2/Content/Workbench/Documentation/Commodities/ContextMenus.guide.info"

if os.path.exists(KYVOS_DOCS_INFO):
    shutil.copy2(KYVOS_DOCS_INFO, os.path.join(AMIGA_DRAWER, "icons.info"))
if os.path.exists(KYVOS_README_INFO):
    shutil.copy2(KYVOS_README_INFO, os.path.join(AMIGA_DRAWER, "LIESMICH.txt.info"))
    shutil.copy2(KYVOS_README_INFO, os.path.join(AMIGA_DRAWER, "README.txt.info"))
if os.path.exists(KYVOS_GUIDE_INFO):
    shutil.copy2(KYVOS_GUIDE_INFO, os.path.join(AMIGA_DRAWER, "VortexBrowse.guide.info"))

# Create Drawer Icon for VortexBrowse itself (do_Type = 2)
with open("amiga-client/VortexBrowse.info", "rb") as f:
    drawer_data = bytearray(f.read())
drawer_data[48] = 2 # WBDRAWER
with open(os.path.join(STAGING_DIR, "VortexBrowse.info"), "wb") as f:
    f.write(drawer_data)

# -------------------------------------------------------------
# Server Files & Scripts
# -------------------------------------------------------------
print("=== [3/6] Packaging Rendering Server Files ===")
shutil.copy2("pi-server/server.py", os.path.join(SERVER_DRAWER, "server.py"))
shutil.copy2("pi-server/fast_pixconv.c", os.path.join(SERVER_DRAWER, "fast_pixconv.c"))
shutil.copy2("pi-server/fast_pixconv.so", os.path.join(SERVER_DRAWER, "fast_pixconv.so"))
shutil.copy2("pi-server/fast_pixconv_aarch64.so", os.path.join(SERVER_DRAWER, "fast_pixconv_aarch64.so"))
shutil.copy2("pi-server/darkreader.js", os.path.join(SERVER_DRAWER, "darkreader.js"))
shutil.copy2("pi-server/requirements.txt", os.path.join(SERVER_DRAWER, "requirements.txt"))

# Copy extensions (h264ify, ublock)
if os.path.exists("pi-server/extensions"):
    shutil.copytree("pi-server/extensions", os.path.join(SERVER_DRAWER, "extensions"), dirs_exist_ok=True)

# -------------------------------------------------------------
# Server Run Script: run_server.sh
# -------------------------------------------------------------
SERVER_RUN_SH = """#!/usr/bin/env bash
# ==============================================================================
# VortexBrowse Server Startup Script
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ ! -d ".venv" ]; then
    echo "[VortexServer] Python virtual environment not found!"
    echo "[VortexServer] Please run ./install_server.sh first."
    exit 1
fi

# Auto-compile SIMD converter if missing
if [ ! -f "fast_pixconv.so" ] && [ -f "fast_pixconv.c" ]; then
    echo "[VortexServer] Compiling native SIMD pixel converter..."
    gcc -O3 -march=native -shared -fPIC fast_pixconv.c -o fast_pixconv.so 2>/dev/null || true
fi

ENGINE="${VORTEX_ENGINE:-chromium}"

echo "===================================================================="
echo " Starting VortexBrowse Server (Ports 7788 & 9757)..."
echo " Engine: ${ENGINE} | Codec: Auto (Turbo-MJPEG / LZ4) | Quality: 100"
echo "===================================================================="

if [ $# -eq 0 ]; then
    exec .venv/bin/python server.py --host 0.0.0.0 --port 7788 --width 1024 --height 768 --fps 30 --format yuv420p --jpeg-quality 100 --codec auto --engine "$ENGINE"
else
    exec .venv/bin/python server.py "$@"
fi
"""

with open(os.path.join(SERVER_DRAWER, "run_server.sh"), "w") as f:
    f.write(SERVER_RUN_SH)
os.chmod(os.path.join(SERVER_DRAWER, "run_server.sh"), 0o755)

# -------------------------------------------------------------
# Server Installer Script: install_server.sh
# -------------------------------------------------------------
SERVER_INSTALL_SH = """#!/usr/bin/env bash
# ==============================================================================
# VortexBrowse Server 2.0 - Automated 1-Click Installer
# For Linux PC (Debian, Ubuntu, CachyOS, Arch, Fedora) & Raspberry Pi 4 / 5
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "===================================================================="
echo "        VortexBrowse 2.0 Server - Automatisches Setup"
echo "===================================================================="

# Check Python 3
if ! command -v python3 &>/dev/null; then
    echo "[!] FEHLER: python3 wurde nicht gefunden. Bitte installieren Sie Python 3.10+."
    exit 1
fi

PY_VER=$(python3 -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")
echo "[+] Python Version: $PY_VER erkannt."

# Check and build fast_pixconv.so
ARCH=$(uname -m)
echo "[+] CPU-Architektur: $ARCH"

if command -v gcc &>/dev/null; then
    echo "[+] Kompiliere native SIMD-Grafikbeschleunigung (fast_pixconv)..."
    gcc -O3 -march=native -shared -fPIC fast_pixconv.c -o fast_pixconv.so 2>/dev/null || true
fi

# Setup Python Virtualenv
if [ ! -d ".venv" ]; then
    echo "[+] Erstelle Python Virtual Environment (.venv)..."
    python3 -m venv .venv
fi

echo "[+] Installiere Python-Abhaengigkeiten (playwright, lz4, Pillow)..."
.venv/bin/pip install --upgrade pip
.venv/bin/pip install -r requirements.txt

echo "[+] Installiere Playwright Chromium Browser-Engine..."
.venv/bin/playwright install chromium

# Try installing system browser dependencies if on Debian/Ubuntu/Raspberry Pi OS
if [ -f /etc/debian_version ] && command -v sudo &>/dev/null; then
    echo "[+] Debian/Ubuntu/Raspberry Pi OS erkannt. Installiere Browser-Bibliotheken..."
    sudo .venv/bin/playwright install-deps chromium 2>/dev/null || true
fi

# Detect Local IP Addresses
LOCAL_IPS=$(hostname -I 2>/dev/null || ip route get 1.1.1.1 2>/dev/null | awk '{print $7}')

echo ""
echo "===================================================================="
echo "  VORTEXBROWSE SERVER ERFOLGREICH INSTALLIERT!"
echo "===================================================================="
echo ""
echo "Lokale IP-Adresse(n) fuer VortexBrowse auf Ihrem AmigaOS 4:"
for ip in $LOCAL_IPS; do
    echo "  -> Host: $ip   (Port: 7788)"
done
echo ""
echo "Starten des Servers:"
echo "  Manuell:      ./run_server.sh"
echo "  Im Hintergrund:"
echo "    nohup ./run_server.sh > server.log 2>&1 &"
echo ""
echo "Viel Spass mit VortexBrowse auf Ihrem Amiga!"
echo "===================================================================="
"""

with open(os.path.join(SERVER_DRAWER, "install_server.sh"), "w") as f:
    f.write(SERVER_INSTALL_SH)
os.chmod(os.path.join(SERVER_DRAWER, "install_server.sh"), 0o755)

# -------------------------------------------------------------
# Systemd Service Template: vortexbrowse.service
# -------------------------------------------------------------
SYSTEMD_SERVICE = """[Unit]
Description=VortexBrowse Remote Web Rendering Server for AmigaOS 4
After=network.target sound.target pipewire.service pulseaudio.service

[Service]
Type=simple
WorkingDirectory=%h/VortexBrowse/Server
ExecStart=%h/VortexBrowse/Server/run_server.sh
Restart=always
RestartSec=3
Environment=PYTHONUNBUFFERED=1

[Install]
WantedBy=default.target
"""

with open(os.path.join(SERVER_DRAWER, "vortexbrowse.service"), "w") as f:
    f.write(SYSTEMD_SERVICE)

# -------------------------------------------------------------
# Server German Readme: SERVER_ANLEITUNG.txt
# -------------------------------------------------------------
SERVER_DE_TXT = """==============================================================================
               VORTEXBROWSE 2.0 - SERVER-INSTALLATIONSANLEITUNG
==============================================================================

Dieses Verzeichnis enthaelt den Rendering-Server fuer VortexBrowse 2.0.
Der Server laeuft auf jedem Linux-PC (Ubuntu, Debian, CachyOS, Arch, etc.)
sowie auf dem Raspberry Pi 4 oder 5.

1. AUTOMATISCHE INSTALLATION (1-Klick)
--------------------------------------
Oeffnen Sie ein Terminal in diesem Verzeichnis und fuehren Sie aus:
   chmod +x install_server.sh run_server.sh
   ./install_server.sh

Das Skript kuemmert sich um alle noetigen Schritte:
- Erstellt eine isolierte Python-Umgebung (.venv)
- Installiert Playwright, LZ4 und Pillow
- Laedt die optimierte Chromium-Engine herunter
- Kompiliert die native C-SIMD-Grafikbeschleunigung fuer Ihre CPU
- Zeigt Ihnen die IP-Adresse an, die Sie im Amiga eintragen muessen!

2. SERVER STARTEN
-----------------
Manuell im Terminal:
   ./run_server.sh

Als Hintergrundprozess:
   nohup ./run_server.sh > server.log 2>&1 &

Als automatischer systemd-Hintergrunddienst (empfohlen):
   mkdir -p ~/.config/systemd/user/
   cp vortexbrowse.service ~/.config/systemd/user/
   systemctl --user daemon-reload
   systemctl --user enable --now vortexbrowse.service

3. VERBINDUNG VOM AMIGA HERSTELLEN
----------------------------------
Starten Sie VortexBrowse auf Ihrem AmigaOS 4, oeffnen Sie die Einstellungen
(Amiga+E) -> Reiter "Streaming Server", tragen Sie die IP-Adresse Ihres
Servers ein (Port: 7788) und klicken Sie auf Speichern.
==============================================================================
"""

with open(os.path.join(SERVER_DRAWER, "SERVER_ANLEITUNG.txt"), "w", encoding="utf-8") as f:
    f.write(SERVER_DE_TXT)

# -------------------------------------------------------------
# Server English Readme: SERVER_README.txt
# -------------------------------------------------------------
SERVER_EN_TXT = """==============================================================================
               VORTEXBROWSE 2.0 - SERVER SETUP INSTRUCTIONS
==============================================================================

This directory contains the remote rendering server for VortexBrowse 2.0.
It runs on any Linux PC (Ubuntu, Debian, CachyOS, Arch, etc.) as well as on
Raspberry Pi 4 or 5.

1. AUTOMATIC INSTALLATION (1-Click)
-----------------------------------
Open a terminal in this directory and execute:
   chmod +x install_server.sh run_server.sh
   ./install_server.sh

The script handles everything automatically:
- Creates a Python virtual environment (.venv)
- Installs Playwright, LZ4, and Pillow
- Downloads the headless Chromium browser engine
- Compiles native C-SIMD pixel acceleration for your CPU
- Displays your server's local LAN IP address!

2. STARTING THE SERVER
----------------------
Manually in terminal:
   ./run_server.sh

In the background:
   nohup ./run_server.sh > server.log 2>&1 &

As an automatic systemd background service (recommended):
   mkdir -p ~/.config/systemd/user/
   cp vortexbrowse.service ~/.config/systemd/user/
   systemctl --user daemon-reload
   systemctl --user enable --now vortexbrowse.service

3. CONNECTING FROM AMIGAOS 4
----------------------------
Launch VortexBrowse on AmigaOS 4, open Preferences (Amiga+E) -> "Streaming Server"
tab, enter your server's IP address (Port: 7788), and click Save.
==============================================================================
"""

with open(os.path.join(SERVER_DRAWER, "SERVER_README.txt"), "w", encoding="utf-8") as f:
    f.write(SERVER_EN_TXT)

# -------------------------------------------------------------
# Overall German and English Readmes for Zip Archive
# -------------------------------------------------------------
OVERALL_DE_TXT = """==============================================================================
            VORTEXBROWSE 2.0 - GESAMTPAPET FUER AMIGAOS 4 & SERVER
==============================================================================

Dieses Komplettpaket enthaelt alles, was Sie fuer den Betrieb von
VortexBrowse 2.0 auf AmigaOS 4.1 sowie dem Rendering-Server auf Linux oder
Raspberry Pi 4/5 benoetigen.

VERZEICHNISSTRUKTUR:
--------------------
- VortexBrowse/          : Der vollstaendige AmigaOS 4 Client-Ordner
  * VortexBrowse         : Ausfuehrbares Programm (Newlib-Version)
  * VortexBrowse.clib4   : Ausfuehrbares Programm (clib4-Version)
  * VortexBrowse.guide   : Ausfuehrliches AmigaGuide-Handbuch
  * LIESMICH.txt         : Deutsche Kurzanleitung
  * README.txt           : Englische Kurzanleitung
  * icons/               : Symbolleisten-Icons
- VortexBrowse.info      : Workbench-Ordner-Icon fuer den Client-Ordner
- Server/                : Der Rendering-Server fuer Linux PC / Raspberry Pi
  * install_server.sh    : Automatisches 1-Klick Installationsskript
  * run_server.sh        : Server-Startskript
  * server.py            : Haupt-Server-Programm
  * requirements.txt     : Python-Paketliste
  * fast_pixconv.so      : Vorkompilierte SIMD-Bibliothek fuer x86_64
  * fast_pixconv_aarch64.so : Vorkompilierte SIMD-Bibliothek fuer ARM64 / Pi
  * darkreader.js        : Dark-Mode-Engine
  * extensions/          : uBlock Origin & h264ify Erweiterungen
  * vortexbrowse.service : Vorlage fuer systemd-Hintergrunddienst

SCHNELLSTART:
-------------
1. Server einrichten (auf Linux PC oder Raspberry Pi):
   - Kopieren Sie den Ordner "Server/" auf Ihren Linux-PC oder Raspberry Pi.
   - Starten Sie "./install_server.sh" und anschliessend "./run_server.sh".
   - Notieren Sie sich die IP-Adresse.

2. Amiga-Client installieren:
   - Kopieren Sie den Ordner "VortexBrowse/" auf Ihren Amiga (z. B. Work:Internet/).
   - Starten Sie "VortexBrowse".
   - Druecken Sie Amiga+E -> Reiter "Streaming Server" -> IP-Adresse eintragen -> Speichern.

Ausfuehrliche Informationen finden Sie in "VortexBrowse/VortexBrowse.guide"!
==============================================================================
"""

with open(os.path.join(STAGING_DIR, "ANLEITUNG_DEUTSCH.txt"), "w", encoding="utf-8") as f:
    f.write(OVERALL_DE_TXT)

OVERALL_EN_TXT = """==============================================================================
         VORTEXBROWSE 2.0 - COMPLETE PACKAGE FOR AMIGAOS 4 & SERVER
==============================================================================

This complete distribution package contains everything needed to run
VortexBrowse 2.0 on AmigaOS 4.1 alongside the rendering server on a Linux PC
or Raspberry Pi 4/5.

PACKAGE STRUCTURE:
------------------
- VortexBrowse/          : The complete AmigaOS 4 client drawer
  * VortexBrowse         : Standard executable (Newlib version)
  * VortexBrowse.clib4   : Alternative executable (clib4 version)
  * VortexBrowse.guide   : Comprehensive AmigaGuide manual
  * LIESMICH.txt         : German quickstart guide
  * README.txt           : English quickstart guide
  * icons/               : Toolbar and bookmark icons
- VortexBrowse.info      : Workbench drawer icon for the client folder
- Server/                : Remote rendering server for Linux / Raspberry Pi
  * install_server.sh    : Automated 1-click installation script
  * run_server.sh        : Server startup script
  * server.py            : Core server program
  * requirements.txt     : Python dependencies list
  * fast_pixconv.so      : Precompiled SIMD acceleration for x86_64
  * fast_pixconv_aarch64.so : Precompiled SIMD acceleration for ARM64 / Pi
  * darkreader.js        : Dark mode CSS engine
  * extensions/          : uBlock Origin & h264ify extensions
  * vortexbrowse.service : Systemd background service unit template

QUICK START:
------------
1. Setup Server (on Linux PC or Raspberry Pi):
   - Copy the "Server/" folder to your Linux PC or Raspberry Pi.
   - Run "./install_server.sh" and then "./run_server.sh".
   - Note the reported IP address.

2. Install Amiga Client:
   - Copy the "VortexBrowse/" folder to your Amiga (e.g. Work:Internet/).
   - Launch "VortexBrowse".
   - Press Amiga+E -> "Streaming Server" tab -> Enter server IP -> Save.

For complete documentation, open "VortexBrowse/VortexBrowse.guide" in MultiView!
==============================================================================
"""

with open(os.path.join(STAGING_DIR, "INSTRUCTIONS_ENGLISH.txt"), "w", encoding="utf-8") as f:
    f.write(OVERALL_EN_TXT)

# -------------------------------------------------------------
# Create LHA Archive (Pure AmigaOS 4 Client)
# -------------------------------------------------------------
print("=== [4/6] Creating AmigaOS 4 LHA Archive (VortexBrowse-2.0-AmigaOS4.lha) ===")
LHA_ARCHIVE = os.path.join(DIST_DIR, "VortexBrowse-2.0-AmigaOS4.lha")

# In staging dir, pack VortexBrowse drawer and VortexBrowse.info drawer icon
cmd = ["lha", "a", LHA_ARCHIVE, "VortexBrowse", "VortexBrowse.info"]
res = subprocess.run(cmd, cwd=STAGING_DIR, capture_output=True, text=True)
if res.returncode != 0:
    print("LHA Error:", res.stderr)
    sys.exit(1)
print("[+] Created LHA Archive:", LHA_ARCHIVE, f"({os.path.getsize(LHA_ARCHIVE)} bytes)")

# -------------------------------------------------------------
# Create Complete Zip Archive (Client + Server + Docs)
# -------------------------------------------------------------
print("=== [5/6] Creating Complete Zip Archive (VortexBrowse-2.0-Complete.zip) ===")
ZIP_ARCHIVE = os.path.join(DIST_DIR, "VortexBrowse-2.0-Complete.zip")

with zipfile.ZipFile(ZIP_ARCHIVE, "w", zipfile.ZIP_DEFLATED) as zf:
    for root, dirs, files in os.walk(STAGING_DIR):
        for file in files:
            full_path = os.path.join(root, file)
            rel_path = os.path.relpath(full_path, STAGING_DIR)
            zf.write(full_path, rel_path)

print("[+] Created Complete Zip Archive:", ZIP_ARCHIVE, f"({os.path.getsize(ZIP_ARCHIVE)} bytes)")

print("=== [6/6] Verifying Archive Contents ===")
res = subprocess.run(["lha", "v", LHA_ARCHIVE], capture_output=True, text=True)
print(res.stdout)

print("\nPackaging successfully completed!")
