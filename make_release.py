#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import fnmatch
import os
import re
import shutil
import subprocess
import sys
import zipfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DIST_DIR = os.path.join(SCRIPT_DIR, "dist")

VER_STR = "1.0"
APP_NAME = "VortexBrowser"
RELEASE_DATE = "22.11.2026"

STAGING_DIR = os.path.join(DIST_DIR, "staging")
AMIGA_DRAWER = os.path.join(STAGING_DIR, "VortexBrowser")
SERVER_LINUX_DRAWER = os.path.join(STAGING_DIR, "Server-Linux")
SERVER_WINDOWS_DRAWER = os.path.join(STAGING_DIR, "Server-Windows")

print(f"=== [1/6] Preparing Staging Directories for {APP_NAME} {VER_STR} ===")
if os.path.exists(STAGING_DIR):
    shutil.rmtree(STAGING_DIR)
os.makedirs(DIST_DIR, exist_ok=True)
os.makedirs(AMIGA_DRAWER, exist_ok=True)
os.makedirs(os.path.join(AMIGA_DRAWER, "icons"), exist_ok=True)
os.makedirs(SERVER_LINUX_DRAWER, exist_ok=True)
os.makedirs(SERVER_WINDOWS_DRAWER, exist_ok=True)

# -------------------------------------------------------------
# AmigaGuide Documentation
# -------------------------------------------------------------
GUIDE_CONTENT = """@database "VortexBrowser.guide"
@author "TearsOfMe"
@(c) "2026 VortexBrowser Project"
@$VER: VortexBrowser.guide 1.0 (22.11.2026)

@node "Main" "VortexBrowser 1.0 - Benutzerhandbuch"
@{b}========================================================================@{ub}
           @{b}VORTEXBROWSER 1.0 FUER AMIGAOS 4.1@{ub}
      Moderner Web-Browser mit Remote-Rendering & Streaming
@{b}========================================================================@{ub}

Willkommen zu @{b}VortexBrowser 1.0@{ub}!

VortexBrowser ermoeglicht modernes Surfen im World Wide Web auf Ihrem AmigaOS 4.1
System mit voller Unterstuetzung fuer moderne Web-Standards (HTML5, CSS3,
JavaScript ES2024, WebGL, YouTube, Twitch, etc.) bei maximaler Geschwindigkeit
und minimaler CPU-Auslastung.

@{b}Inhaltsverzeichnis:@{ub}

  * @{"Uebersicht & Funktionsweise" link "Overview"}
  * @{"Systemanforderungen" link "Requirements"}
  * @{"Installation & Schnellstart" link "Installation"}
  * @{"Bedienung & Funktionen" link "Features"}
      - @{"Multi-Tab-Browsing" link "Tabs"}
      - @{"Schnellwahl & Lesezeichen" link "Bookmarks"}
      - @{"Download-Manager" link "Downloads"}
      - @{"Passwort-Manager" link "Passwords"}
      - @{"Dark Mode (Dunkles Theme)" link "DarkMode"}
      - @{"Audio-Streaming (16-Bit AHI)" link "Audio"}
      - @{"Kontextmenues & Mausbedienung" link "MouseContext"}
  * @{"Einstellungen (Preferences)" link "Preferences"}
  * @{"Workbench ToolTypes (Icon-Infos)" link "ToolTypes"}
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

@{b}VortexBrowser loest dieses Problem mit einer modernen Client-Server-Architektur:@{ub}

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

@{b}AmigaOS 4.1 Client:@{ub}
- @{b}Betriebssystem:@{ub} AmigaOS 4.1 Final Edition (Update 1 oder Update 2 empfohlen)
- @{b}Prozessor / Binary:@{ub}
  * @{b}VortexBrowser@{ub} (Universal PowerPC): AmigaOne X5000, A1222/A1222+, Sam440, Sam460,
    Pegasos II, AmigaOne XE/SE/uA1, Classic PPC oder QEMU (pegasos2/sam460ex)
  * @{b}VortexBrowser-AltiVec@{ub}: PowerPC mit AltiVec (AmigaOne X1000, G4, G5)
- @{b}Arbeitsspeicher:@{ub} Mindestens 256 MB RAM (512 MB+ RAM fuer HD-Streams empfohlen)
- @{b}Grafikkarte:@{ub} RTG (Picasso96) mit 24/32-Bit TrueColor; Radeon HD oder Radeon RX
  fuer Hardware-Composite-Scaling empfohlen
- @{b}Hardware-H.264 (optional):@{ub} va.library (Enhancer Software) fuer Radeon UVD/VCN
- @{b}Netzwerk:@{ub} Roadshow TCP/IP (bsdsocket.library v4+), Ports 7788 & 9757
- @{b}Audio:@{ub} Konfiguriertes AHI (ahi.device, Unit 0) fuer 16-Bit Stereo-Audio
- @{b}Bibliotheken:@{ub} Standard AmigaOS 4.1 ReAction-Klassen & locale.library
  (optional codesets.library v5/6). Alle Codecs (libavcodec, lz4, jpeg) sind
  statisch eingebunden - keine externen .so-Dateien erforderlich!

@{b}Rendering-Server (Backend):@{ub}
- Linux-PC, Mac oder Raspberry Pi 3 / 4 / 5 (64-Bit, ab 1 GB RAM auf Pi 3 mit ZRAM;
  2-8 GB RAM auf Pi 4/5 bzw. Desktop-Server optimal)
- Python 3.10+, Rustwright + Playwright, Ungoogled-Chromium (Standard) oder Firefox
- Lokale Netzwerkverbindung zum Amiga (LAN Ethernet empfohlen)

@endnode

@node "Installation" "Installation & Schnellstart"
@{b}Installation & Schnellstart@{ub}

@{b}Schritt 1: Server starten (Linux PC oder Raspberry Pi)@{ub}
1. Entpacken Sie das Server-Archiv @{b}VortexBrowser-1.0-Server-Linux.zip@{ub}
   (bzw. kopieren Sie den Ordner @{b}Server-Linux/@{ub}) auf Ihren Server.
2. Oeffnen Sie ein Terminal im Server-Verzeichnis.
3. Starten Sie das universelle Installationsskript:
   @{b}./install_server.sh@{ub}
   Das Skript erkennt automatisch Ihre Hardware (Intel QuickSync, AMD Radeon Vulkan,
   Raspberry Pi V3D), richtet die Python-Umgebung ein, installiert Treiber und
   kompiliert passgenau die native SIMD-Grafikbeschleunigung.
4. Notieren Sie sich die am Ende angezeigte @{b}Server-IP-Adresse@{ub}
   (z. B. 192.168.178.2 oder Ihre lokale Server-IP).
5. Starten Sie den Server mit:
   @{b}./run_server.sh@{ub}

@{b}Schritt 2: VortexBrowser auf dem AmigaOS 4 starten@{ub}
1. Entpacken Sie das Archiv @{b}VortexBrowser-1.0-AmigaOS4.lha@{ub} an Ihren
   gewuenschten Ort (z. B. @{b}Work:Internet/VortexBrowser@{ub}).
2. Starten Sie @{b}VortexBrowser@{ub} per Doppelklick auf das Programm-Icon.
3. Beim ersten Start fragt VortexBrowser Sie direkt nach der IP-Adresse Ihres Servers
   (oder druecken Sie Amiga+1 / Amiga+E).
4. Tragen Sie die IP-Adresse ein und klicken Sie auf @{b}Verbinden@{ub}.
5. VortexBrowser verbindet sich sofort mit dem Server und laedt Ihre Startseite!

@endnode

@node "Features" "Bedienung & Funktionen"
@{b}Bedienung & Funktionen@{ub}

VortexBrowser bietet eine intuitive, an modernen Amiga-Standards orientierte Reaction-Oberflaeche:

- @{"Multi-Tab-Browsing" link "Tabs"}
- @{"Schnellwahl & Lesezeichen" link "Bookmarks"}
- @{"Download-Manager" link "Downloads"}
- @{"Passwort-Manager" link "Passwords"}
- @{"Dark Mode (Dunkles Theme)" link "DarkMode"}
- @{"Audio-Streaming (16-Bit AHI)" link "Audio"}
- @{"Kontextmenues & Mausbedienung" link "MouseContext"}

@endnode

@node "Tabs" "Multi-Tab-Browsing"
@{b}Multi-Tab-Browsing@{ub}

VortexBrowser unterstuetzt echtes Tabbed-Browsing mit mehreren gleichzeitigen Webseiten:

- @{b}Neuer Tab:@{ub} Klicken Sie auf die @{b}[+]@{ub}-Schaltflaeche in der Tab-Leiste
  oder druecken Sie @{b}Amiga+T@{ub}.
- @{b}Tab wechseln:@{ub} Klicken Sie einfach auf den Reiter des gewuenschten Tabs.
- @{b}Tab schliessen:@{ub} Klicken Sie auf die @{b}[-]@{ub}-Schaltflaeche neben der Tab-Leiste
  oder druecken Sie @{b}Amiga+W@{ub}.
- @{b}Link in neuem Tab oeffnen:@{ub} Rechtsklick auf einen Link und im Kontextmenue
  @{b}"In neuem Tab oeffnen"@{ub} waehlen.

Jeder Tab wird serverseitig in einer eigenen Browser-Instanz verwaltet, sodass Ihre
Sitzungen sauber getrennt bleiben.

@endnode

@node "Bookmarks" "Schnellwahl & Lesezeichen"
@{b}Schnellwahl & Lesezeichen@{ub}

VortexBrowser besitzt eine praktische Schnellwahl-Leiste direkt ueber der Adressleiste:

- @{b}Standard-Schnellwahl-Buttons:@{ub}
  Enthaelt Schnellzugriffe mit massgeschneiderten Icons fuer beliebte Websites
  (Amiga-News, OS4Depot, Amigans, Aminet, YouTube, Twitch, Wikipedia, Google, GitHub).
  Die Buttons nutzen dynamisch die gesamte verfuegbare Breite des Fensters aus.

- @{b}Schnellwahl unbegrenzt erweitern & anpassen:@{ub}
  In den @{b}Einstellungen (Prefs)@{ub} auf dem Reiter @{b}Schnellwahl@{ub} koennen Sie
  die Beschriftungen, URLs und Symbole der Knoepfe nach Belieben anpassen oder
  @{b}unbegrenzt viele@{ub} eigene Favoriten anlegen.
- @{b}Knoepfe [+] und [-]:@{ub}
  Direkt in der Schnellwahl-Leiste koennen Sie mit [+] die aktuelle Seite zur Schnellwahl
  hinzufuegen und mit [-] den aktuellen Eintrag entfernen (wird der letzte geloescht,
  wird die Standard-Startseite wiederhergestellt).
- @{b}Lesezeichen hinzufuegen:@{ub}
  Druecken Sie @{b}Amiga+A@{ub} oder waehlen Sie im Menue @{b}Lesezeichen -> Lesezeichen hinzufuegen@{ub}.

@endnode

@node "Downloads" "Download-Manager"
@{b}Download-Manager@{ub}

Der Download-Manager von VortexBrowser bietet eine uebersichtliche Verwaltung aller Downloads:

- @{b}Automatischer Start:@{ub}
  Sobald Sie auf einer Webseite einen Download anklicken (oder im Kontextmenue
  @{b}"Link speichern unter..."@{ub} waehlen), uebernimmt der VortexBrowser Download-Manager
  den Download transparent im Hintergrund.
- @{b}Download-Fenster oeffnen:@{ub}
  Druecken Sie @{b}Amiga+D@{ub} oder waehlen Sie im Menue @{b}Projekt -> Download-Manager...@{ub}.
- @{b}Echtzeitanzeige:@{ub}
  Fuer jeden Download werden Dateiname, Status, Fortschrittsbalken in Prozent,
  Download-Geschwindigkeit und die geschaetzte Restzeit (ETA) angezeigt.
- @{b}Aktionen:@{ub}
  - @{b}Datei oeffnen:@{ub} Startet die Datei direkt mit dem passenden Standardprogramm.
  - @{b}Ordner oeffnen:@{ub} Oeffnet das Zielverzeichnis auf der Workbench.
  - @{b}Eintrag loeschen:@{ub} Entfernt den markierten Eintrag aus der Liste.
  - @{b}Aufraeumen:@{ub} Loescht alle beendeten Downloads aus der Liste.
- @{b}Speicherort:@{ub}
  Standardmaessig werden Dateien in @{b}RAM:@{ub} gespeichert (in den Einstellungen frei waehlbar).

@endnode

@node "Passwords" "Passwort-Manager"
@{b}Passwort-Manager@{ub}

VortexBrowser enthaelt einen sicheren, integrierten Passwort-Manager:

- @{b}Lokale, starke Verschluesselung:@{ub}
  Alle Anmeldedaten werden verschluesselt mit @{b}Blowfish-CBC@{ub} und einem ueber
  @{b}SHA-256@{ub} abgeleiteten Master-Key in @{b}PROGDIR:passwords.dat@{ub} gespeichert.
  Es werden keinerlei Passwoerter im Klartext abgelegt!
- @{b}Passwort-Tresor oeffnen:@{ub}
  Druecken Sie @{b}Amiga+K@{ub} oder waehlen Sie im Menue @{b}Projekt -> Passwort-Manager...@{ub}.
- @{b}Benutzerdaten kopieren:@{ub}
  Benutzername und Passwort koennen bequem per Mausklick in die Amiga-Zwischenablage
  kopiert werden.

@endnode

@node "DarkMode" "Dark Mode (Dunkles Theme)"
@{b}Dark Mode (Dunkles Theme)@{ub}

VortexBrowser bietet einen vollstaendigen Dunkelmodus fuer augenschonendes Surfen:

- @{b}Umschalten:@{ub}
  Druecken Sie @{b}Amiga+U@{ub} oder waehlen Sie im Menue @{b}Ansicht -> Dunkles Theme an/aus@{ub}.
- @{b}Serverseitige Webseiten-Anpassung:@{ub}
  Auf dem Server injiziert @{i}DarkReader@{ui} eine hochwertige dynamische CSS-Dunkel-Farbpalette,
  die alle Webseiten in ein sauberes, kontrastreiches dunkles Layout verwandelt.
- @{b}Amiga Reaction GUI Anpassung:@{ub}
  Die gesamte Amiga-Oberflaeche (Hintergruende, Rahmen, Adressleiste, Suchleiste, Tabs,
  Knoepfe und Fenster) wird automatisch an ein passendes dunkles Farbschema angepasst.

@endnode

@node "Audio" "Audio-Streaming (16-Bit AHI)"
@{b}Audio-Streaming (16-Bit AHI)@{ub}

VortexBrowser streamt Audio von YouTube-Videos, Twitch-Livestreams oder Audio-Websites
in Echtzeit synchron zum Amiga:

- @{b}Audio-Qualitaet:@{ub}
  16-Bit Stereo, 48.000 Hz Samplingrate ueber AmigaOS AHI.
- @{b}Geringe Latenz:@{ub}
  Der Stream wird ueber einen separaten, schlanken TCP-Audiokanal uebertragen, sodass
  keine Ruckler bei der Grafikdarstellung auftreten.

@endnode

@node "MouseContext" "Kontextmenues & Mausbedienung"
@{b}Kontextmenues & Mausbedienung@{ub}

- @{b}Mausrad:@{ub}
  AmigaOS 4 Wheel-Events werden direkt in stufenloses Web-Scrollen umgesetzt.
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
   - Standard-Suchmaschine (DuckDuckGo, Google, Bing, Startpage, Ecosia, etc.)

2. @{b}Schnellwahl:@{ub}
   - Konfiguration der Knoepfe in der Schnellwahl-Leiste
   - Zuweisung von Titeln, URLs und PNG-Icons

3. @{b}Bildschirm / Fenster:@{ub}
   - Auswahl zwischen Workbench-Fenster und eigenem Screen
   - Feste Fenstergroessen-Presets oder automatische Anpassung

4. @{b}Downloads:@{ub}
   - Standard-Download-Verzeichnis (z. B. RAM: oder Work:Downloads/)
   - Option: Vor jedem Download Zielverzeichnis abfragen

5. @{b}Streaming Server:@{ub}
   - @{b}Server IP / Host & Port:@{ub} Manuelle Eingabe des Streaming-Servers (z. B. 127.0.0.1:7788 oder Server-IP)
   - @{b}Stream-Aufloesung:@{ub} Nativ (1:1), 720p HD, 540p qHD (siehe Performance!)
   - @{b}JPEG-Qualitaet:@{ub} Slider von 20% bis 100%
   - @{b}Video-Optimierung:@{ub} Dynamische Bildrate bei Videowiedergabe
   - @{b}Aktuellen Server beim Beenden automatisch speichern@{ub}

@endnode

@node "ToolTypes" "Workbench ToolTypes (Icon-Infos)"
@{b}Workbench ToolTypes (Icon-Infos)@{ub}

VortexBrowser unterstuetzt die typische Amiga-Konfiguration ueber @{b}ToolTypes@{ub} in
den Programm-Icons (@{i}VortexBrowser.info@{ui}, @{i}VortexBrowser-AltiVec.info@{ui}).

@{b}So bearbeiten Sie die ToolTypes auf der Workbench:@{ub}
1. Markieren Sie das Icon mit einem einfachen Mausklick.
2. Waehlen Sie im Menue @{b}Icons -> Information...@{ub} (oder druecken Sie @{b}Rechte Amiga + I@{ub}).
3. In der Liste der ToolTypes koennen Sie Einstellungen direkt anpassen.

@{b}Wichtige AmigaOS-Klammerregel:@{ub}
ToolTypes, die in Klammern stehen wie @{i}(SERVER=192.168.178.2)@{ui}, gelten auf dem Amiga
als @{b}inaktiv / auskommentiert@{ub}.
Um einen Wert zu aktivieren, entfernen Sie die Klammern (z. B. @{b}SERVER=192.168.178.2@{ub})
und klicken Sie auf @{b}Speichern@{ub}!

@{b}Unterstuetzte ToolTypes:@{ub}

- @{b}SERVER=<IP oder Hostname>@{ub}
  Gibt die IP-Adresse oder den Namen des Streaming-Servers an (z. B. @{i}SERVER=192.168.178.2@{ui}).

- @{b}PORT=<Portnummer>@{ub}
  Gibt den Port des Streaming-Servers an (Standard: @{i}PORT=7788@{ui}).

- @{b}URL=<Adresse>@{ub}  (oder STARTURL)
  Die Webseite, die beim Start ueber das Icon geoeffnet werden soll (z. B. @{i}URL=https://www.amiga-news.de@{ui}).

- @{b}STREAMMODE=<Modus>@{ub}
  Bestimmt das Streaming-System:
  * @{i}AUTO@{ui}     : Intelligente Umschaltung (H.264 bei Videos via va.library, sonst sparsames 2D).
  * @{i}H264@{ui}     : Dauerhafter H.264 Video-Stream ueber Hardware.
  * @{i}LZ4@{ui}      : Reines 2D Dirty-Rect Streaming (fuer schwaechere Grafikkarten ohne VA-API).

- @{b}BITRATE=<Zahl in kbps>@{ub}
  Setzt die H.264 Video-Bitrate (1000 bis 10000 kbps, Standard: @{i}BITRATE=8000@{ui}).

- @{b}FPS=<Zahl>@{ub}
  Ziel-Bildwiederholrate (15, 25, 30, 50, 60 FPS, Standard: @{i}FPS=25@{ui}).

- @{b}DARKMODE=<0 oder 1>@{ub}
  Erzwingt das dunkle Erscheinungsbild (Dark Mode) beim Start (@{i}DARKMODE=1@{ui} oder @{i}DARKMODE=0@{ui}).

- @{b}DOWNLOADDIR=<Pfad>@{ub}
  Standard-Download-Verzeichnis (z. B. @{i}DOWNLOADDIR=Work:Downloads@{ui}).

@{b}Stack-Groesse:@{ub}
Das Icon enthaelt fest hinterlegt @{b}Stack: 200000@{ub} Byte (~200 KB). Dies stellt sicher,
dass Netzwerk, FFmpeg-Dekodierung und die Benutzeroberflaeche ohne Stack-Fehler stabil laufen.

@endnode

@node "Performance" "Tipps zur CPU-Entlastung & Performance"
@{b}Tipps zur CPU-Entlastung & Performance@{ub}

Wenn Sie einen Amiga mit schwaecherem PowerPC-Prozessor (wie z. B. SAM440, SAM460
oder Standard-G4) besitzen, beachten Sie folgenden wichtigen Tipp:

@{b}Hardware-Composite-Scaling nutzen!@{ub}
Ein Vollbild-Stream mit 1920x1080 Pixeln erfordert die Dekodierung von ueber
2 Millionen Pixeln pro Bild.

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

@{b}Projekt & Server:@{ub}
  @{b}Amiga+1@{ub}       Server konfigurieren (IP & Port eingeben)
  @{b}Amiga+2@{ub}       Mit Server verbinden
  @{b}Amiga+O@{ub}       URL-Eingabefeld fokussieren
  @{b}Amiga+T@{ub}       Neuen Tab oeffnen
  @{b}Amiga+W@{ub}       Aktuellen Tab schliessen
  @{b}Amiga+D@{ub}       Download-Manager oeffnen
  @{b}Amiga+K@{ub}       Passwort-Manager oeffnen
  @{b}Amiga+E@{ub}       Einstellungen (Preferences) oeffnen
  @{b}Amiga+Q@{ub}       VortexBrowser beenden

@{b}Navigation:@{ub}
  @{b}Amiga+B@{ub}       Zurueck (Back)
  @{b}Amiga+F@{ub}       Vorwaerts (Forward)
  @{b}Amiga+R@{ub}       Seite neu laden (Reload)
  @{b}Amiga+S@{ub}       Ladevorgang stoppen (Stop)
  @{b}Amiga+H@{ub}       Startseite laden (Home)

@{b}Ansicht:@{ub}
  @{b}Amiga+U@{ub}       Dunkles Theme (Dark Mode) an/aus
  @{b}F11@{ub}           Vollbild / Workbench-Fenster umschalten
  @{b}Amiga++@{ub}       Schrift vergroessern
  @{b}Amiga+-@{ub}       Schrift verkleinern
  @{b}Amiga+0@{ub}       Standardschriftgroesse zuruecksetzen

@{b}Bearbeiten:@{ub}
  @{b}Amiga+C@{ub}       Markierten Text in Zwischenablage kopieren
  @{b}Amiga+V@{ub}       Aus Zwischenablage einfuegen
  @{b}Amiga+X@{ub}       Ausschneiden

@endnode

@node "ServerSetup" "Server-Einrichtung (Universeller Linux-Server)"
@{b}Server-Einrichtung (Universeller Linux-Server)@{ub}

Der universelle VortexBrowser-Server laeuft auf jedem gaengigen Linux-System
(z. B. Raspberry Pi 4, Raspberry Pi 5, Intel Atom/Core PC, AMD Ryzen unter Debian,
Ubuntu, CachyOS, Arch Linux, Fedora, Linux Mint, etc.).

@{b}Intelligente Hardware-Erkennung (Universal Server):@{ub}
Anstelle separater Builds buendelt VortexBrowser alle Faehigkeiten in einem
einzigen, hochoptimierten Server, der bei Installation und Start die Hardware
automatisch erkennt und konfiguriert:
- @{b}Intel QuickSync & VA-API@{ub} (`iHD` / `i965`) fuer Intel Core, Celeron & Atom Mini-PCs
- @{b}AMD Radeon Vulkan & VA-API@{ub} fuer Ryzen CPUs & APUs (z. B. unter CachyOS / Arch)
- @{b}Raspberry Pi V3D EGL@{ub} fuer Raspberry Pi 4 und Raspberry Pi 5
- @{b}Native C-SIMD Pixel-Pipeline@{ub}: Erkennt automatisch AVX2, SSE4.1 oder ARM NEON
  und kompiliert die SIMD-Routinen passgenau fuer Ihre Prozessor-Architektur.

@{b}Automatische Installation mit install_server.sh:@{ub}
1. Entpacken Sie das Server-Archiv @{b}VortexBrowser-1.0-Server-Linux.zip@{ub}.
2. Oeffnen Sie ein Terminal im Verzeichnis @{b}Server-Linux/@{ub}.
3. Fuehren Sie das universelle Installationsskript aus:
   @{b}./install_server.sh@{ub}
4. Das Skript fuehrt vollautomatisch folgende Schritte durch:
   - Erkennt das Linux-Betriebssystem (Arch/CachyOS, Debian/Ubuntu, Fedora/RHEL).
   - Ueberprueft Python 3 (benoetigt Python 3.10 oder neuer).
   - Erstellt eine isolierte virtuelle Umgebung (@{b}.venv@{ub}).
   - Installiert erforderliche Grafiktreiber (Intel VA-API, AMD Vulkan, Mesa).
   - Installiert benoetigte Bibliotheken (@{i}playwright@{ui}, @{i}lz4@{ui}, @{i}Pillow@{ui}, @{i}turbojpeg@{ui}).
   - Sucht vorhandene Browser (Chromium, ungoogled-chromium, Brave) oder laedt Playwright-Chromium.
   - Kompiliert die native SIMD-Grafikbeschleunigung (@{b}fast_pixconv.so@{ub}) fuer Ihre CPU.
   - Ermittelt die IP-Adresse Ihres Rechners im Netzwerk.

@{b}Server starten:@{ub}
   @{b}./run_server.sh@{ub}
   (Waehlt automatisch die ideale Aufloesung: 1080p@25fps auf Desktop-PCs
   oder sparsame 720p@25fps auf Single-Board-Computern wie dem Raspberry Pi).

@{b}Ports im Netzwerk:@{ub}
- Port @{b}7788@{ub}: Haupt-Streaming-Port (MJPEG/LZ4, Steuerung, 2D Dirty-Rect)
- Port @{b}9757@{ub}: 16-Bit Stereo Audio-Streaming (AHI)

@endnode

@node "Troubleshooting" "Fehlerbehebung (FAQ)"
@{b}Fehlerbehebung (FAQ)@{ub}

@{b}Frage: VortexBrowser zeigt "Nicht verbunden" oder "Verbindung fehlgeschlagen".@{ub}
@{b}Antwort:@{ub}
1. Ueberpruefen Sie, ob der Server auf dem Linux-PC/Raspberry Pi laeuft (@{b}./run_server.sh@{ub}).
2. Druecken Sie @{b}Amiga+1@{ub} und ueberpruefen Sie die IP-Adresse des Servers.
3. Pruefen Sie, ob eine Firewall auf dem Server die Ports 7788 und 9757 blockiert.

@{b}Frage: Die CPU-Last auf meinem Amiga ist bei Videos sehr hoch.@{ub}
@{b}Antwort:@{ub}
Waehlen Sie im Menue @{b}Ansicht -> Stream: 720p HD@{ub} oder @{b}Stream: 540p qHD@{ub}.
Dadurch uebernimmt die Radeon-Grafikkarte das Skalieren ueber Composite in Hardware,
was die CPU-Auslastung auf 20-30% senkt!

@{b}Frage: Ich hoere keinen Ton bei Videos.@{ub}
@{b}Antwort:@{ub}
1. Stellen Sie sicher, dass Ihr AHI auf dem Amiga korrekt konfiguriert ist (z. B. Unit 0).
2. Auf dem Linux-Server muss PulseAudio oder PipeWire aktiv sein.

@{b}Frage: Der Server haengt beim Klicken/Navigieren (RUSTWRIGHT TIMEOUT DIAGNOSTIC).@{ub}
@{b}Antwort:@{ub}
Deaktivieren Sie Rustwright beim Server-Start (Fallback auf Standard-Playwright):
   @{b}VORTEX_DISABLE_RUSTWRIGHT=1 ./run_server.sh@{ub}
   oder direkt: @{b}python server.py --no-rustwright@{ub}

@endnode

@node "EnglishDoc" "English Documentation Summary"
@{b}English Documentation Summary@{ub}

@{b}VortexBrowser 1.0@{ub} is a remote web rendering browser client for AmigaOS 4.1.
It renders modern web pages (HTML5, CSS3, ES2024 JavaScript, WebGL, YouTube, etc.)
on a companion Linux PC, AMD Ryzen, Intel Core/Atom or Raspberry Pi 4/5 server, streaming ultra-smooth visuals
and 16-bit 48kHz audio directly to AmigaOS with native Reaction GUI controls.

@{b}AmigaOS 4.1 Client Requirements:@{ub}
- AmigaOS 4.1 Final Edition (Update 1 or Update 2 recommended).
- PowerPC CPU: @{b}VortexBrowser@{ub} works on all AmigaOS 4 PPC machines (X5000, A1222/+,
  Sam440/460, Pegasos II, XE/SE/uA1, Classic PPC, QEMU); @{b}VortexBrowser-AltiVec@{ub} for
  G4, G5 and AmigaOne X1000.
- 256 MB RAM minimum (512 MB+ RAM recommended); 24/32-bit RTG screen (Radeon HD/RX
  recommended; optional va.library for hardware H.264 decoding).
- Roadshow TCP/IP (ports 7788 & 9757), AHI (ahi.device Unit 0), standard ReAction
  classes (optional codesets.library). All codecs are statically linked.

@{b}Quick Start:@{ub}
1. On your Linux PC or Raspberry Pi:
   Extract @{b}VortexBrowser-1.0-Server-Linux.zip@{ub}, enter the @{b}Server-Linux/@{ub} directory and run:
   @{b}./install_server.sh@{ub}
   (Automatically detects your CPU/GPU hardware, drivers and SIMD instruction set).
   Then start the server with:
   @{b}./run_server.sh@{ub}
   Note the IP address displayed.
2. On your Amiga:
   Extract @{b}VortexBrowser-1.0-AmigaOS4.lha@{ub} and double-click @{b}VortexBrowser@{ub}.
   Enter your server IP address (Port: 7788) and connect!

@{b}Key Features:@{ub}
- Full multi-tab browsing (Amiga+T to open, Amiga+W to close).
- Full-width quick launch toolbar with customizable bookmarks (unlimited favorites in Prefs).
- Integrated Download Manager (Amiga+D).
- Encrypted Password Vault (Amiga+K).
- Complete Dark Mode toggle (Amiga+U) with DarkReader and dark Reaction GUI.
- Hardware-accelerated scaling via CompositeTags (select 720p or 540p stream for low CPU).
- 16-bit stereo 48kHz synchronized audio via AHI.

@endnode
"""

with open(os.path.join(AMIGA_DRAWER, f"{APP_NAME}.guide"), "w", encoding="iso-8859-1") as f:
    f.write(GUIDE_CONTENT)

GUIDE_EN_CONTENT = """@database "VortexBrowser_EN.guide"
@author "TearsOfMe"
@(c) "(C) 2026 TearsOfMe"
@$VER: VortexBrowser_EN.guide 1.0 (22.11.2026)

@node MAIN "VortexBrowser 1.0 - Official AmigaOS 4.1 User Manual"
@next INTRO

@{b}==============================================================================
                      VORTEXBROWSER 1.0 FOR AMIGAOS 4.1
                 High-Performance Remote Web Browser (PPC)
==============================================================================@{ub}

Welcome to @{b}VortexBrowser 1.0@{ub}!
VortexBrowser brings the modern web (HTML5, CSS3, ES2024 JavaScript, YouTube,
Twitch, WebGL) smoothly to AmigaOS 4.1 by offloading browser rendering to a
Linux, macOS, Docker, or Raspberry Pi companion server and streaming pixels
using planar YUV420p + 2D Dirty-Rect + LZ4 (or H.264 via va.library) plus
16-bit 44.1 kHz stereo audio via AHI.

  @{" 1. Quick Installation (Amiga + Server)   " link INTRO}
  @{" 2. System Requirements                   " link REQS}
  @{" 3. Binaries & ToolTypes                  " link INSTALL}
  @{" 4. Controls & Keyboard Shortcuts         " link KEYS}
  @{" 5. Features & Startpage                  " link FEATURES}
  @{" 6. Troubleshooting (Rustwright / GPU)    " link TROUBLE}

@endnode

@node INTRO "1. Quick Installation (AmigaOS 4 + Companion Server)"
@prev MAIN
@next REQS

@{b}1. AmigaOS 4 (Client)@{ub}
- Extract vortexbrowser.lha or VortexBrowser-1.0-AmigaOS4.lha using UnArc
  into any drawer (e.g. Work:Internet/VortexBrowser/).
- No external .so libraries needed (libavcodec, liblz4, libjpeg are compiled
  statically into the binary).
- Launch:
  * @{b}VortexBrowser@{ub}         -> All AmigaOS 4 systems (X5000, A1222+, Sam440/460,
                             Pegasos II, QEMU, Classic PPC)
  * @{b}VortexBrowser-AltiVec@{ub} -> AltiVec CPUs (X1000, G4, G5)
- Configure Server Address in Icon ToolTypes (SERVER=<IP-or-host>, PORT=7788)
  or directly in Settings (Amiga+E).

@{b}2. Companion Server (choose one of 3 options)@{ub}
- @{b}Option A - Raspberry Pi 3 / 4 / 5 (Plug & Play):@{ub}
  Flash the matching .img.xz (rpi3/4/5) to a >= 16 GB microSD card, connect
  Ethernet, power on (~60-90s first boot), and connect from the Amiga to
  vortexbrowser.local (Pi 4), vortexbrowser-rpi3.local, or vortexbrowser-rpi5.local.
- @{b}Option B - Linux or macOS:@{ub}
  Copy Server-Linux/ to your host. Run ./install_server.sh (Linux) or
  ./install_macos.sh (macOS) once, then ./run_server.sh / ./run_macos.sh.
- @{b}Option C - Docker:@{ub}
  Run the Docker container with: --shm-size=2g -p 7788:7788 -p 9757:9757.

@endnode

@node REQS "2. System Requirements"
@prev INTRO
@next INSTALL

@{b}AmigaOS 4.1 Client:@{ub}
- AmigaOS 4.1 Final Edition (Update 1 or 2 recommended)
- 256 MB RAM minimum (512 MB+ recommended for HD streams)
- Picasso96 RTG 24/32-bit screen (Radeon HD/RX recommended for CompositeTags)
- Optional: va.library (Enhancer Software) for hardware H.264 decoding
- Roadshow TCP/IP (bsdsocket.library v4+) & AHI (ahi.device Unit 0)

@{b}Companion Server:@{ub}
- Linux x86_64/ARM64, macOS (Apple Silicon / Intel), Docker, or Raspberry Pi 3/4/5
- Python 3.10+, Ungoogled-Chromium (default) or Firefox, PulseAudio/PipeWire

@endnode

@node INSTALL "3. Binaries & ToolTypes"
@prev REQS
@next KEYS

Supported ToolTypes in VortexBrowser.info (remove parentheses to activate):
  SERVER=<IP-or-Hostname>  Companion server address (e.g. SERVER=192.168.178.2)
  PORT=<Number>            Control/video TCP port (default: 7788)
  FPS=<Number>             Target frame rate (15, 25, 30, 50, 60; default: 25)
  QUALITY=<1-100>          JPEG/Stream quality (default: 100)
  CUSTOMSCREEN             Open on a dedicated custom RTG screen
  WIDTH=<px> / HEIGHT=<px> Custom screen or window dimensions
  DEPTH=<16|24>            RTG screen depth (default: 24)
  DARK                     Start with Dark Mode enabled
  URL=<URL>                Initial page (default: @HOME built-in startpage)

Configuration files are stored in ENVARC:VortexBrowser/vortexbrowser.prefs.

@endnode

@node KEYS "4. Controls & Keyboard Shortcuts"
@prev INSTALL
@next FEATURES

  Amiga+T        Open new tab
  Amiga+W        Close current tab
  Amiga+R        Reload page
  Amiga+H        Go to Startpage (@HOME)
  Amiga+B / +N   Back / Forward
  Amiga+E        Settings (Server, Quick Dial, Bookmarks, Display)
  Amiga+D        Download Manager
  Amiga+K        Password Manager (Blowfish-CBC + SHA-256)
  Amiga+U        Toggle Dark Mode (DarkReader + Dark ReAction GUI)
  Right Click    Native AmigaOS 4 context menu (save link/image via ASL)

@endnode

@node FEATURES "5. Features & Startpage"
@prev KEYS
@next TROUBLE

- On a fresh install, VortexBrowser opens the built-in Startpage (@HOME)
  featuring clickable search engines (DuckDuckGo, Google, Bing, Startpage,
  Ecosia, OS4Depot, Aminet, Wikipedia, YouTube) and AmigaOS 4 news & download
  portals sorted by language (EN, DE, FR, IT, PL, CS).
- 8 default Quick Dial buttons: OS4Welt, Amiga-News, OS4Depot, Amigans,
  Aminet, YouTube, Twitch, Wikipedia.
- Help -> About VortexBrowser displays the active server engine
  (Chromium Rustwright DirectCDP, Chromium Playwright, or Firefox Playwright).

@endnode

@node TROUBLE "6. Troubleshooting (Rustwright / GPU)"
@prev FEATURES

- If a host experiences disconnects or hangs with Rustwright:
    VORTEX_DISABLE_RUSTWRIGHT=1 ./run_server.sh
  or:
    python3 server.py --no-rustwright
- If headless Chromium fails due to GPU drivers:
    python3 server.py --disable-gpu
- For Docker deployments, always pass --shm-size=2g for heavy websites.

@endnode
"""

with open(os.path.join(AMIGA_DRAWER, f"{APP_NAME}_EN.guide"), "w", encoding="iso-8859-1") as f:
    f.write(GUIDE_EN_CONTENT)
en_guide_info = os.path.join(AMIGA_DRAWER, f"{APP_NAME}.guide.info")
if os.path.exists(en_guide_info):
    shutil.copy2(en_guide_info, os.path.join(AMIGA_DRAWER, f"{APP_NAME}_EN.guide.info"))

# -------------------------------------------------------------
# German Quickstart Readme: LIESMICH.txt
# -------------------------------------------------------------
LIESMICH_CONTENT = f"""==============================================================================
                    {APP_NAME.upper()} 1.0 FUER AMIGAOS 4.1
                 Schnellstartanleitung & Kurzanleitung
==============================================================================

{APP_NAME} ist ein moderner Web-Browser fuer AmigaOS 4.1, der ueber ein
leistungsfaehiges Remote-Rendering-Backend (auf Linux-PC oder Raspberry Pi 4/5)
die gesamte moderne Webwelt (HTML5, CSS3, JavaScript ES2024, YouTube, Twitch,
WebGL, etc.) fluessig und ressourcenschonend auf den Amiga bringt.

------------------------------------------------------------------------------
1. INHALT DIESES ARCHIVS
------------------------------------------------------------------------------
- {APP_NAME}             : Universelles Programm fuer AmigaOS 4 (automatische CPU-Erkennung)
- {APP_NAME}-AltiVec     : Optimierte Version fuer CPUs mit AltiVec/VMX (G4, X1000)
- {APP_NAME}.guide       : Ausfuehrliches AmigaGuide-Handbuch mit allen Details
- LIESMICH.txt          : Diese Schnellstartanleitung (Deutsch)
- README.txt            : Schnellstartanleitung (Englisch)
- icons/                : Symbole fuer die Schnellwahl-Leiste
- Alternative-Icons/    : Alternative Icons im Cyber-Globe- und Orb-Design

------------------------------------------------------------------------------
2. SCHNELLSTART IN 2 SCHRITTEN
------------------------------------------------------------------------------

SCHRITT 1: Server starten (auf Linux-PC oder Raspberry Pi 4/5)
1. Entpacken Sie das Server-Archiv (VortexBrowser-1.0-Server-Linux.zip) auf Ihrem Rechner.
2. Oeffnen Sie ein Terminal im Server-Ordner ("Server-Linux/") und starten Sie die Installation:
      ./install_server.sh
   (Erkennt automatisch Intel-, AMD- und Raspberry Pi-Hardware sowie Grafiktreiber und SIMD)
3. Starten Sie anschliessend den Server mit:
      ./run_server.sh
4. Merken Sie sich die angezeigte IP-Adresse (z. B. 192.168.178.2 oder lokale LAN-IP).

SCHRITT 2: {APP_NAME} auf dem Amiga starten
1. Starten Sie "{APP_NAME}" per Doppelklick.
2. Beim ersten Start fragt {APP_NAME} Sie direkt nach der Server-IP-Adresse
   (oder druecken Sie Amiga+1 / Amiga+E).
3. Tragen Sie die IP-Adresse ein und klicken Sie auf "Verbinden".
4. Fertig! {APP_NAME} verbindet sich sofort und surft los!

------------------------------------------------------------------------------
3. WICHTIGE FEATURES & BEDIENUNG
------------------------------------------------------------------------------
* Tabs:
  - Neuer Tab: [+] in der Tableiste oder Amiga+T
  - Tab schliessen: [-] neben der Tableiste oder Amiga+W

* Schnellwahl:
  - Knoepfe fuer beliebte Amiga- und Medienseiten direkt erreichbar.
  - Dynamische Anpassung an die gesamte Fensterbreite.
  - Unbegrenzt viele Favoriten in den Einstellungen (Prefs) konfigurierbar.
  - [+] Seite hinzufuegen, [-] Seite entfernen.

* Download-Manager:
  - Oeffnen mit: Amiga+D (oder Menue "Projekt -> Download-Manager...").
  - Live-Geschwindigkeit, Fortschritt in Prozent, Datei oeffnen, Ordner oeffnen.

* Dark Mode (Dunkles Theme):
  - Umschalten mit: Amiga+U.
  - Dunkle Webseiten via DarkReader und dunkle Amiga Reaction-Oberflaeche!

* Passwort-Manager:
  - Passwort-Manager oeffnen: Amiga+K
  - Passwoerter verschluesselt speichern und per Klick ins Clipboard kopieren
  - Sichere Blowfish-Verschluesselung in PROGDIR:passwords.dat!

* TIPP ZUR CPU-ENTLASTUNG (fuer SAM440, SAM460, G4):
  - Menue "Ansicht -> Stream: 720p HD" oder "Stream: 540p qHD" waehlen.
  - Die Radeon-Grafikkarte skaliert das Bild ueber Hardware-CompositeTags()!
  - Senkt die Amiga-CPU-Last um 50-70%!

------------------------------------------------------------------------------
4. WORKBENCH TOOLTYPES (ICON-EINSTELLUNGEN)
------------------------------------------------------------------------------
Sie koennen VortexBrowser auch direkt ueber ToolTypes im Icon konfigurieren!
Klicken Sie dazu das Icon an und waehlen Sie Workbench-Menue "Icons -> Information..."
(oder Tastenkombination Rechte Amiga + I):

- SERVER=192.168.178.2 : IP-Adresse oder Hostname des Streaming-Servers
- PORT=7788            : Server-Port (Standard: 7788)
- URL=https://...      : Startseite beim Starten per Doppelklick
- STREAMMODE=AUTO      : Streaming-Modus (AUTO, H264 oder LZ4)
- BITRATE=8000         : H.264 Video-Bitrate in kbps (1000 bis 10000)
- FPS=25               : Bildwiederholrate (15, 25, 30, 50, 60 FPS)
- DARKMODE=0           : Dark-Mode beim Start erzwingen (1=Ein, 0=Aus)
- DOWNLOADDIR=RAM:     : Standard-Download-Verzeichnis (z. B. Work:Downloads)

HINWEIS: ToolTypes in Klammern wie (SERVER=...) sind inaktiv (Muster).
Entfernen Sie die Klammern, um den Wert zu aktivieren, und klicken Sie auf Speichern!

Ausfuehrliche Informationen finden Sie in "{APP_NAME}.guide"!
Viel Spass beim modernen Surfen auf AmigaOS 4!
==============================================================================
"""

with open(os.path.join(AMIGA_DRAWER, "LIESMICH.txt"), "w", encoding="iso-8859-1") as f:
    f.write(LIESMICH_CONTENT)

# -------------------------------------------------------------
# English Quickstart Readme: README.txt
# -------------------------------------------------------------
README_CONTENT = f"""==============================================================================
                    {APP_NAME.upper()} 1.0 FOR AMIGAOS 4.1
                 Quick Start Guide & Feature Overview
==============================================================================

{APP_NAME} brings the modern web to AmigaOS 4.1 using a high-performance
headless Chromium rendering backend running on your local network (Linux PC
or Raspberry Pi 4/5). Enjoy full HTML5, CSS3, ES2024 JavaScript, WebGL,
YouTube, and Twitch with low Amiga CPU usage and native Reaction GUI.

------------------------------------------------------------------------------
1. PACKAGE CONTENTS
------------------------------------------------------------------------------
- {APP_NAME}             : Universal executable with dynamic CPU detection (newlib)
- {APP_NAME}-AltiVec     : Dedicated SIMD build for PowerPC G4 and AmigaOne X1000
- {APP_NAME}.guide       : Comprehensive AmigaGuide manual
- LIESMICH.txt          : German quickstart guide
- README.txt            : English quickstart guide
- icons/                : Toolbar and bookmark icon collection
- Alternative-Icons/    : Alternative Cyber-Globe and Orb icons

------------------------------------------------------------------------------
2. QUICK START IN 2 STEPS
------------------------------------------------------------------------------

STEP 1: Start Server (Linux PC, AMD, Intel or Raspberry Pi 4/5)
1. Extract "VortexBrowser-1.0-Server-Linux.zip" on your companion Linux machine.
2. Open a terminal in "Server-Linux/" and run:
      ./install_server.sh
   (Automatically detects your CPU/GPU hardware, drivers and SIMD)
3. Start the server with:
      ./run_server.sh
4. Note your server's local IP address (e.g. 192.168.178.2 or your LAN IP).

STEP 2: Start {APP_NAME} on AmigaOS 4
1. Launch "{APP_NAME}".
2. On first run, {APP_NAME} prompts you directly for the server IP address
   (or press Amiga+1 / Amiga+E).
3. Enter your server's IP address and click "Connect".
4. Done! {APP_NAME} immediately connects and loads your start page!

------------------------------------------------------------------------------
3. KEYBOARD SHORTCUTS
------------------------------------------------------------------------------
  Amiga+1   : Configure server address (IP:Port)
  Amiga+2   : Connect to server
  Amiga+T   : Open new tab
  Amiga+W   : Close active tab
  Amiga+D   : Open Download Manager
  Amiga+K   : Open Password Vault
  Amiga+U   : Toggle Dark Mode (Light/Dark Theme)
  Amiga+E   : Open Preferences
  Amiga+Q   : Quit {APP_NAME}

------------------------------------------------------------------------------
4. WORKBENCH TOOLTYPES (ICON SETTINGS)
------------------------------------------------------------------------------
You can also configure {APP_NAME} directly via ToolTypes inside its .info icon!
Select the icon on Workbench and choose "Icons -> Information..." (or press RA+I):

- SERVER=192.168.178.2 : Server IP address or hostname
- PORT=7788            : Server TCP port (default: 7788)
- URL=https://...      : Startup URL when launched from Workbench
- STREAMMODE=AUTO      : Streaming system (AUTO, H264, or LZ4)
- BITRATE=8000         : H.264 video stream bitrate in kbps (1000 to 10000)
- FPS=25               : Frame rate target (15, 25, 30, 50, 60 FPS)
- DARKMODE=0           : Enforce dark mode on startup (1=On, 0=Off)
- DOWNLOADDIR=RAM:     : Default download folder (e.g. Work:Downloads)

NOTE: ToolTypes enclosed in parentheses like (SERVER=...) are commented out.
Remove the parentheses to activate the setting and click "Save"!

For comprehensive documentation, open "{APP_NAME}.guide" in MultiView!
Enjoy modern web browsing on AmigaOS 4!
==============================================================================
"""

with open(os.path.join(AMIGA_DRAWER, "README.txt"), "w", encoding="utf-8") as f:
    f.write(README_CONTENT)

# -------------------------------------------------------------
# Copy Client Binaries and Icons
# -------------------------------------------------------------
print("=== [2/6] Copying AmigaOS 4 Client Binaries and Icons ===")
shutil.copy2("amiga-client/VortexBrowser.stripped", os.path.join(AMIGA_DRAWER, APP_NAME))
shutil.copy2("amiga-client/VortexBrowser.info", os.path.join(AMIGA_DRAWER, f"{APP_NAME}.info"))

if os.path.exists("amiga-client/VortexBrowser-AltiVec.stripped"):
    shutil.copy2("amiga-client/VortexBrowser-AltiVec.stripped", os.path.join(AMIGA_DRAWER, "VortexBrowser-AltiVec"))
    if os.path.exists("amiga-client/VortexBrowser-AltiVec.info"):
        shutil.copy2("amiga-client/VortexBrowser-AltiVec.info", os.path.join(AMIGA_DRAWER, "VortexBrowser-AltiVec.info"))
    else:
        shutil.copy2("amiga-client/VortexBrowser.info", os.path.join(AMIGA_DRAWER, "VortexBrowser-AltiVec.info"))

# Copy Icons to icons/
for icon in os.listdir("amiga-client/icons"):
    if icon.endswith(".png"):
        shutil.copy2(os.path.join("amiga-client/icons", icon), os.path.join(AMIGA_DRAWER, "icons", icon))

# Copy Info Files from Kyvos OS4 ISO / repository if available
KYVOS_DIR = os.environ.get("KYVOS_DIR", os.path.expanduser("~/kyvos-2.2.0-linux64"))
KYVOS_DOCS_INFO = os.path.join(KYVOS_DIR, "amigaos4/Download/VideoVortex/Data/MPlayer/Docs.info")
KYVOS_README_INFO = os.path.join(KYVOS_DIR, "amigaos4/Download/VideoVortex/Data/MPlayer/Docs/README.info")
KYVOS_GUIDE_INFO = os.path.join(KYVOS_DIR, "ISO/AmigaOS 4.1 Final Edition Update 2/Content/Workbench/Documentation/Commodities/ContextMenus.guide.info")

if os.path.exists(KYVOS_DOCS_INFO):
    shutil.copy2(KYVOS_DOCS_INFO, os.path.join(AMIGA_DRAWER, "icons.info"))
if os.path.exists(KYVOS_README_INFO):
    shutil.copy2(KYVOS_README_INFO, os.path.join(AMIGA_DRAWER, "LIESMICH.txt.info"))
    shutil.copy2(KYVOS_README_INFO, os.path.join(AMIGA_DRAWER, "README.txt.info"))
if os.path.exists(KYVOS_GUIDE_INFO):
    shutil.copy2(KYVOS_GUIDE_INFO, os.path.join(AMIGA_DRAWER, f"{APP_NAME}.guide.info"))

# Copy Alternative Circular Icons (3D Titanium Glossy Orb) into Alternative-Icons/ drawer
ALT_ICONS_SRC = "amiga-client/alt-icons"
if os.path.isdir(ALT_ICONS_SRC):
    ALT_ICONS_DST = os.path.join(AMIGA_DRAWER, "Alternative-Icons")
    os.makedirs(ALT_ICONS_DST, exist_ok=True)
    for fname in ("VortexBrowser.info", "VortexBrowser-AltiVec.info", "VortexBrowser_drawer.info"):
        src_info = os.path.join(ALT_ICONS_SRC, fname)
        if os.path.exists(src_info):
            shutil.copy2(src_info, os.path.join(ALT_ICONS_DST, fname))
    alt_drawer_src = os.path.join(ALT_ICONS_SRC, "VortexBrowser_drawer.info")
    if os.path.exists(alt_drawer_src):
        shutil.copy2(alt_drawer_src, os.path.join(AMIGA_DRAWER, "Alternative-Icons.info"))
    elif os.path.exists(KYVOS_DOCS_INFO):
        shutil.copy2(KYVOS_DOCS_INFO, os.path.join(AMIGA_DRAWER, "Alternative-Icons.info"))
    alt_readme_txt = (
        "Alternative kreisrunde Icons (Variante: 3D Titanium Glossy Orb, 64x64 ARGB)\n"
        "===========================================================================\n\n"
        "Zum Wechseln einfach die gewuenschten .info-Dateien aus diesem Ordner in das\n"
        "Hauptverzeichnis 'VortexBrowser/' kopieren (oder im Workbench-Fenster\n"
        "'Piktogramm -> Informationen...' per Drag & Drop austauschen).\n\n"
        "- VortexBrowser.info          : Standard ReAction Edition\n"
        "- VortexBrowser-AltiVec.info  : PowerPC AltiVec Edition (mit AltiVec-Badge)\n"
        "- VortexBrowser_drawer.info   : Ordner-Icon (als VortexBrowser.info eine Ebene hoeher)\n"
    )
    with open(os.path.join(ALT_ICONS_DST, "LIESMICH.txt"), "w", encoding="iso-8859-1") as f:
        f.write(alt_readme_txt)
    if os.path.exists(KYVOS_README_INFO):
        shutil.copy2(KYVOS_README_INFO, os.path.join(ALT_ICONS_DST, "LIESMICH.txt.info"))

# Copy Catalogs (Locale translations) into catalogs/ drawer
CATALOGS_SRC = "amiga-client/catalogs"
if os.path.isdir(CATALOGS_SRC):
    CATALOGS_DST = os.path.join(AMIGA_DRAWER, "catalogs")
    shutil.copytree(CATALOGS_SRC, CATALOGS_DST, dirs_exist_ok=True)
    if os.path.exists(KYVOS_DOCS_INFO):
        shutil.copy2(KYVOS_DOCS_INFO, os.path.join(AMIGA_DRAWER, "catalogs.info"))

# Drawer Icon for VortexBrowser drawer
if os.path.exists("amiga-client/VortexBrowser_drawer.info"):
    shutil.copy2("amiga-client/VortexBrowser_drawer.info", os.path.join(STAGING_DIR, f"{APP_NAME}.info"))
else:
    with open("amiga-client/VortexBrowser.info", "rb") as f:
        drawer_data = bytearray(f.read())
    drawer_data[48] = 2  # WBDRAWER
    with open(os.path.join(STAGING_DIR, f"{APP_NAME}.info"), "wb") as f:
        f.write(drawer_data)

# -------------------------------------------------------------
# Packaging Server Files
# -------------------------------------------------------------
print("=== [3/6] Packaging Rendering Server Files (Linux & Raspberry Pi 4) ===")

SERVER_RUN_SH = """#!/usr/bin/env bash
# ==============================================================================
# VortexBrowser Server Startup Script
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
echo " Starting VortexBrowser Server (Port 7788)..."
echo " Engine: ${ENGINE} | Codec: Auto (Turbo-MJPEG / LZ4) | Quality: 100"
echo "===================================================================="

if [ $# -eq 0 ]; then
    exec .venv/bin/python server.py --host 0.0.0.0 --port 7788 --width 1024 --height 768 --fps 30 --format yuv420p --jpeg-quality 100 --codec auto --engine "$ENGINE"
else
    exec .venv/bin/python server.py "$@"
fi
"""

SERVER_INSTALL_SH = """#!/usr/bin/env bash
# ==============================================================================
# VortexBrowser Server - Automated 1-Click Installer
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "===================================================================="
echo "      VortexBrowser Server - Automatische Installation             "
echo "===================================================================="

if ! command -v python3 &>/dev/null; then
    echo "[!] FEHLER: python3 wurde nicht gefunden. Bitte installieren Sie Python 3.10+."
    exit 1
fi

if [ ! -d ".venv" ]; then
    echo "[+] Erstelle Python Virtual Environment (.venv)..."
    python3 -m venv .venv
fi

echo "[+] Installiere Python-Abhaengigkeiten (playwright, lz4, Pillow)..."
.venv/bin/pip install --upgrade pip
.venv/bin/pip install -r requirements.txt

echo "[+] Installiere Playwright Chromium Browser-Engine..."
.venv/bin/playwright install chromium

if [ -f /etc/debian_version ] || [ -f /etc/os-release ]; then
    echo "[+] Installiere eventuell fehlende Browser-Systembibliotheken..."
    sudo .venv/bin/playwright install-deps chromium 2>/dev/null || .venv/bin/playwright install-deps chromium 2>/dev/null || true
fi

if command -v gcc &>/dev/null && [ -f "fast_pixconv.c" ]; then
    echo "[+] Kompiliere native C-SIMD Pixelbeschleunigung (fast_pixconv.so)..."
    gcc -O3 -march=native -shared -fPIC fast_pixconv.c -o fast_pixconv.so 2>/dev/null || true
fi

IP_ADDR=$(hostname -I 2>/dev/null | awk '{print $1}' || echo "Ihre-Server-IP")

echo ""
echo "===================================================================="
echo "  VORTEXBROWSER SERVER ERFOLGREICH INSTALLIERT!"
echo "===================================================================="
echo "  Server-IP fuer AmigaOS 4 Client : ${IP_ADDR}"
echo "  Server-Port                     : 7788"
echo ""
echo "  Starten Sie den Server jetzt mit:"
echo "    ./run_server.sh"
echo "===================================================================="
"""

SYSTEMD_SERVICE = """[Unit]
Description=VortexBrowser Remote Web Rendering Server for AmigaOS 4
After=network.target sound.target pipewire.service pulseaudio.service

[Service]
Type=simple
WorkingDirectory=%h/VortexBrowser/Server
ExecStart=%h/VortexBrowser/Server/run_server.sh
KillMode=mixed
TimeoutStopSec=15
Restart=always
RestartSec=3
Environment=PYTHONUNBUFFERED=1

[Install]
WantedBy=default.target
"""

# -------------------------------------------------------------
# Populate Universal Server-Linux Drawer from single server/ codebase
# -------------------------------------------------------------
shutil.copy2("server/server.py", os.path.join(SERVER_LINUX_DRAWER, "server.py"))
shutil.copy2("server/fast_pixconv.c", os.path.join(SERVER_LINUX_DRAWER, "fast_pixconv.c"))
if os.path.exists("server/fast_pixconv.so"):
    shutil.copy2("server/fast_pixconv.so", os.path.join(SERVER_LINUX_DRAWER, "fast_pixconv.so"))
SENSITIVE_PATH_PATTERNS = (
    ".venv",
    "venv",
    "__pycache__",
    ".git",
    ".pytest_cache",
    "chromium_profile",
    "firefox_profile",
    "*chromium_profile*",
    "*firefox_profile*",
    "local storage",
    "session storage",
    "indexeddb",
    "cookies",
    "cookies-journal",
    "login data",
    "web data",
    "*twitch_token*",
    "*twitch_cookie*",
    "*storage_state*",
    "*_cookies.json",
    "cookies.json",
    "*ftp_auth*",
    "*ssh_auth*",
    "deploy_remote*",
    ".env",
    ".env.*",
    "*.env",
    "*.sqlite",
    "*.sqlite3",
    "*.sqlite-wal",
    "*.sqlite-shm",
    "*.ldb",
    "*.pem",
    "*.key",
    "*.p12",
    "*.pfx",
    "*.kdbx",
    ".netrc",
    "id_rsa*",
    "id_ed25519*",
    "*.pid",
    "server.pid",
    "browser_bin.txt",
    "*.pyc",
    "*.pyo",
    ".ds_store",
)

SECRET_CONTENT_PATTERNS = (
    ("Private Key Material", re.compile(r"-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----")),
    ("GitHub Token", re.compile(r"\bgh[pousr]_[A-Za-z0-9]{20,}\b")),
    ("AWS Access Key", re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("Google API Key", re.compile(r"\bAIza[0-9A-Za-z\-_]{30,}\b")),
    ("Hardcoded SSH/FTP Password", re.compile(r"""\b(?:pw|ssh_password|ftp_password)\s*=\s*['"][^'"$\{\}\s]{4,}['"]""")),
)


def is_sensitive_relpath(rel_path: str) -> bool:
    norm = rel_path.replace("\\", "/").strip("/")
    for seg in norm.split("/"):
        low = seg.lower()
        if not low:
            continue
        for pat in SENSITIVE_PATH_PATTERNS:
            if fnmatch.fnmatch(low, pat):
                return True
    return False


def assert_tree_clean(root_dir: str, label: str) -> None:
    violations = []
    for root, dirs, files in os.walk(root_dir):
        for name in dirs + files:
            full_path = os.path.join(root, name)
            rel_path = os.path.relpath(full_path, root_dir)
            if is_sensitive_relpath(rel_path):
                violations.append(f"sensitive path in {label}: {rel_path}")
        for name in files:
            if not name.lower().endswith((".sh", ".py", ".yml", ".yaml", ".txt", ".md", ".service", ".guide", ".json", ".c", ".h")):
                continue
            full_path = os.path.join(root, name)
            rel_path = os.path.relpath(full_path, root_dir)
            try:
                with open(full_path, "r", encoding="utf-8", errors="ignore") as f:
                    content = f.read()
                for desc, rx in SECRET_CONTENT_PATTERNS:
                    if rx.search(content):
                        violations.append(f"{desc} detected in {label}/{rel_path}")
            except Exception:
                pass
    if violations:
        for v in violations:
            print(f"[SECURITY ERROR] {v}", file=sys.stderr)
        sys.exit(1)


shutil.copy2("server/darkreader.js", os.path.join(SERVER_LINUX_DRAWER, "darkreader.js"))
shutil.copy2("server/requirements.txt", os.path.join(SERVER_LINUX_DRAWER, "requirements.txt"))
shutil.copy2("server/sync_twitch_login.py", os.path.join(SERVER_LINUX_DRAWER, "sync_twitch_login.py"))
if os.path.exists("server/sync_twitch_login.sh"):
    shutil.copy2("server/sync_twitch_login.sh", os.path.join(SERVER_LINUX_DRAWER, "sync_twitch_login.sh"))
    os.chmod(os.path.join(SERVER_LINUX_DRAWER, "sync_twitch_login.sh"), 0o755)
if os.path.exists("server/extensions"):
    shutil.copytree(
        "server/extensions",
        os.path.join(SERVER_LINUX_DRAWER, "extensions"),
        dirs_exist_ok=True,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo", "*.log", ".DS_Store", "*.sqlite*", "*.ldb"),
    )
shutil.copy2("server/install_server.sh", os.path.join(SERVER_LINUX_DRAWER, "install_server.sh"))
os.chmod(os.path.join(SERVER_LINUX_DRAWER, "install_server.sh"), 0o755)
shutil.copy2("server/run_server.sh", os.path.join(SERVER_LINUX_DRAWER, "run_server.sh"))
os.chmod(os.path.join(SERVER_LINUX_DRAWER, "run_server.sh"), 0o755)
if os.path.exists("server/README.md"):
    shutil.copy2("server/README.md", os.path.join(SERVER_LINUX_DRAWER, "README.txt"))

# -------------------------------------------------------------
# Populate Native Server-Windows Drawer from single server/ codebase
# -------------------------------------------------------------
shutil.copy2("server/server.py", os.path.join(SERVER_WINDOWS_DRAWER, "server.py"))
shutil.copy2("server/fast_pixconv.c", os.path.join(SERVER_WINDOWS_DRAWER, "fast_pixconv.c"))
for _dll in ("fast_pixconv.dll", "fast_pixconv_avx2.dll"):
    if os.path.exists(os.path.join("server", _dll)):
        shutil.copy2(os.path.join("server", _dll), os.path.join(SERVER_WINDOWS_DRAWER, _dll))
shutil.copy2("server/darkreader.js", os.path.join(SERVER_WINDOWS_DRAWER, "darkreader.js"))
shutil.copy2("server/requirements.txt", os.path.join(SERVER_WINDOWS_DRAWER, "requirements.txt"))
shutil.copy2("server/sync_twitch_login.py", os.path.join(SERVER_WINDOWS_DRAWER, "sync_twitch_login.py"))
if os.path.exists("server/extensions"):
    shutil.copytree(
        "server/extensions",
        os.path.join(SERVER_WINDOWS_DRAWER, "extensions"),
        dirs_exist_ok=True,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo", "*.log", ".DS_Store", "*.sqlite*", "*.ldb"),
    )
if os.path.exists("server/install_windows.ps1"):
    shutil.copy2("server/install_windows.ps1", os.path.join(SERVER_WINDOWS_DRAWER, "install_windows.ps1"))
if os.path.exists("server/run_windows.bat"):
    shutil.copy2("server/run_windows.bat", os.path.join(SERVER_WINDOWS_DRAWER, "run_windows.bat"))
if os.path.exists("server/run_windows_tray.pyw"):
    shutil.copy2("server/run_windows_tray.pyw", os.path.join(SERVER_WINDOWS_DRAWER, "run_windows_tray.pyw"))
if os.path.exists("server/run_windows_hidden.vbs"):
    shutil.copy2("server/run_windows_hidden.vbs", os.path.join(SERVER_WINDOWS_DRAWER, "run_windows_hidden.vbs"))
if os.path.exists("server/stop_windows.bat"):
    shutil.copy2("server/stop_windows.bat", os.path.join(SERVER_WINDOWS_DRAWER, "stop_windows.bat"))
if os.path.exists("server/README_WINDOWS.md"):
    shutil.copy2("server/README_WINDOWS.md", os.path.join(SERVER_WINDOWS_DRAWER, "README_WINDOWS.md"))

assert_tree_clean(AMIGA_DRAWER, "Amiga drawer")
assert_tree_clean(SERVER_LINUX_DRAWER, "Server-Linux drawer")
assert_tree_clean(SERVER_WINDOWS_DRAWER, "Server-Windows drawer")

# -------------------------------------------------------------
# Overall German and English Readmes for Complete Archive
# -------------------------------------------------------------
OVERALL_DE_TXT = f"""==============================================================================
            {APP_NAME.upper()} 1.0 - GESAMTPAKET FUER AMIGAOS 4 & SERVER
==============================================================================

Dieses Komplettpaket enthaelt alles fuer den Betrieb von {APP_NAME} 1.0
auf AmigaOS 4.1 sowie dem Rendering-Server auf Linux oder Raspberry Pi 4/5.

VERZEICHNISSTRUKTUR:
--------------------
- {APP_NAME}/          : Der vollstaendige AmigaOS 4 Client-Ordner
  * {APP_NAME}         : Ausfuehrbares Programm (ReAction Standard)
  * {APP_NAME}-AltiVec : Ausfuehrbares Programm (PowerPC G4 AltiVec SIMD)
  * {APP_NAME}.info    : Echtes AmigaOS 4 Dual-State Glow Icon
  * {APP_NAME}.guide   : Ausfuehrliches AmigaGuide-Handbuch
  * LIESMICH.txt       : Deutsche Kurzanleitung
  * README.txt         : Englische Kurzanleitung
  * icons/             : Symbolleisten-Icons
- {APP_NAME}.info      : Workbench-Ordner-Icon fuer den Client-Ordner
- Server-Linux/        : Universeller Rendering-Server mit Hardware-Autodetect
                         (fuer alle Linux-PCs, Intel Core/Atom, AMD Ryzen, Raspberry Pi 4/5)

SCHNELLSTART:
-------------
1. Server einrichten (auf Linux PC, Intel Mini-PC/Stick oder Raspberry Pi 4/5):
   - Kopieren Sie den Ordner "Server-Linux/" auf Ihren Rechner.
   - Starten Sie "./install_server.sh" und anschliessend "./run_server.sh".
   - Notieren Sie sich die angezeigte IP-Adresse.

2. Amiga-Client installieren:
   - Kopieren Sie den Ordner "{APP_NAME}/" auf Ihren Amiga (z. B. Work:Internet/).
   - Starten Sie "{APP_NAME}".
   - Geben Sie die Server-IP ein und klicken Sie auf "Verbinden".

Ausfuehrliche Informationen finden Sie in "{APP_NAME}/{APP_NAME}.guide"!
==============================================================================
"""

with open(os.path.join(STAGING_DIR, "ANLEITUNG_DEUTSCH.txt"), "w", encoding="utf-8") as f:
    f.write(OVERALL_DE_TXT)

# -------------------------------------------------------------
# Create 1: AmigaOS 4 LHA Archive
# -------------------------------------------------------------
print(f"=== [4/6] Creating AmigaOS 4 LHA Archive ({APP_NAME}-{VER_STR}-AmigaOS4.lha) ===")
LHA_ARCHIVE = os.path.join(DIST_DIR, f"{APP_NAME}-{VER_STR}-AmigaOS4.lha")
cmd = ["lha", "a", LHA_ARCHIVE, APP_NAME, f"{APP_NAME}.info"]
res = subprocess.run(cmd, cwd=STAGING_DIR, capture_output=True, text=True)
if res.returncode != 0:
    print("LHA Error:", res.stderr)
    sys.exit(1)
print(f"[+] Created AmigaOS 4 LHA: {LHA_ARCHIVE} ({os.path.getsize(LHA_ARCHIVE)} bytes)")

# -------------------------------------------------------------
# Create 2: Universal Linux Server & Native Windows Server Zip Archives
# -------------------------------------------------------------
print(f"=== [5/6] Creating Linux Server, Windows Server & Complete Archives ===")
LINUX_ZIP = os.path.join(DIST_DIR, f"{APP_NAME}-{VER_STR}-Server-Linux.zip")
with zipfile.ZipFile(LINUX_ZIP, "w", zipfile.ZIP_DEFLATED) as zf:
    for root, dirs, files in os.walk(SERVER_LINUX_DRAWER):
        dirs[:] = [d for d in dirs if not is_sensitive_relpath(os.path.relpath(os.path.join(root, d), SERVER_LINUX_DRAWER))]
        for file in files:
            full_path = os.path.join(root, file)
            rel_sub = os.path.relpath(full_path, SERVER_LINUX_DRAWER)
            if is_sensitive_relpath(rel_sub):
                continue
            rel_path = os.path.join("Server-Linux", rel_sub)
            zf.write(full_path, rel_path)
print(f"[+] Created Universal Linux Server Zip: {LINUX_ZIP} ({os.path.getsize(LINUX_ZIP)} bytes)")

WINDOWS_ZIP = os.path.join(DIST_DIR, f"{APP_NAME}-{VER_STR}-Server-Windows.zip")
with zipfile.ZipFile(WINDOWS_ZIP, "w", zipfile.ZIP_DEFLATED) as zf:
    for root, dirs, files in os.walk(SERVER_WINDOWS_DRAWER):
        dirs[:] = [d for d in dirs if not is_sensitive_relpath(os.path.relpath(os.path.join(root, d), SERVER_WINDOWS_DRAWER))]
        for file in files:
            full_path = os.path.join(root, file)
            rel_sub = os.path.relpath(full_path, SERVER_WINDOWS_DRAWER)
            if is_sensitive_relpath(rel_sub):
                continue
            rel_path = os.path.join("Server-Windows", rel_sub)
            zf.write(full_path, rel_path)
print(f"[+] Created Native Windows Server Zip: {WINDOWS_ZIP} ({os.path.getsize(WINDOWS_ZIP)} bytes)")

# -------------------------------------------------------------
# Create 3: Complete Zip Archive
# -------------------------------------------------------------
COMPLETE_ZIP = os.path.join(DIST_DIR, f"{APP_NAME}-{VER_STR}-Complete.zip")
with zipfile.ZipFile(COMPLETE_ZIP, "w", zipfile.ZIP_DEFLATED) as zf:
    for root, dirs, files in os.walk(STAGING_DIR):
        dirs[:] = [d for d in dirs if not is_sensitive_relpath(os.path.relpath(os.path.join(root, d), STAGING_DIR))]
        for file in files:
            full_path = os.path.join(root, file)
            rel_path = os.path.relpath(full_path, STAGING_DIR)
            if is_sensitive_relpath(rel_path):
                continue
            zf.write(full_path, rel_path)
print(f"[+] Created Complete Zip: {COMPLETE_ZIP} ({os.path.getsize(COMPLETE_ZIP)} bytes)")

print(f"=== [6/6] Verifying Archives & Security Audit ===")
res = subprocess.run(["lha", "v", LHA_ARCHIVE], capture_output=True, text=True)
print(res.stdout)

audit_errors = []
for zpath in (LINUX_ZIP, WINDOWS_ZIP, COMPLETE_ZIP):
    with zipfile.ZipFile(zpath, "r") as zf:
        for entry in zf.namelist():
            if is_sensitive_relpath(entry):
                audit_errors.append(f"Sensitive entry inside {os.path.basename(zpath)}: {entry}")

lha_q = subprocess.run(["lha", "lq", LHA_ARCHIVE], capture_output=True, text=True)
for raw_line in lha_q.stdout.splitlines():
    entry_name = raw_line.strip().split()[-1] if raw_line.strip() else ""
    if entry_name and not entry_name.startswith("-") and is_sensitive_relpath(entry_name):
        audit_errors.append(f"Sensitive entry inside {os.path.basename(LHA_ARCHIVE)}: {entry_name}")

if audit_errors:
    for err in audit_errors:
        print(f"[SECURITY AUDIT FAILED] {err}", file=sys.stderr)
    for bad in (LHA_ARCHIVE, LINUX_ZIP, WINDOWS_ZIP, COMPLETE_ZIP):
        if os.path.exists(bad):
            os.unlink(bad)
    sys.exit(1)

print("[+] Security audit passed: zero credentials, tokens, cookies, or browser profiles in archives.")
print("\nRelease packaging successfully completed for AmigaOS 4, Linux Server, and Windows Server!")
