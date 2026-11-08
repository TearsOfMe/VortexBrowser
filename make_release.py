#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os
import sys
import shutil
import subprocess
import zipfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DIST_DIR = os.path.join(SCRIPT_DIR, "dist")

VER_STR = "0.99-RC1"
APP_NAME = "VortexBrowser"
RELEASE_DATE = "29.10.2026"

STAGING_DIR = os.path.join(DIST_DIR, "staging")
AMIGA_DRAWER = os.path.join(STAGING_DIR, "VortexBrowser")
SERVER_LINUX_DRAWER = os.path.join(STAGING_DIR, "Server-Linux")
SERVER_RPI4_DRAWER = os.path.join(STAGING_DIR, "Server-RaspberryPi4")

print(f"=== [1/6] Preparing Staging Directories for {APP_NAME} {VER_STR} ===")
if os.path.exists(DIST_DIR):
    shutil.rmtree(DIST_DIR)
os.makedirs(AMIGA_DRAWER, exist_ok=True)
os.makedirs(os.path.join(AMIGA_DRAWER, "icons"), exist_ok=True)
os.makedirs(SERVER_LINUX_DRAWER, exist_ok=True)
os.makedirs(SERVER_RPI4_DRAWER, exist_ok=True)

# -------------------------------------------------------------
# AmigaGuide Documentation
# -------------------------------------------------------------
GUIDE_CONTENT = """@database "VortexBrowser.guide"
@author "TearsOfMe"
@(c) "2026 VortexBrowser Project"
@$VER: VortexBrowser.guide 0.99 RC1 (29.10.2026)

@node "Main" "VortexBrowser 0.99 RC1 - Benutzerhandbuch"
@{b}========================================================================@{ub}
           @{b}VORTEXBROWSER 0.99 RC1 FUER AMIGAOS 4.1@{ub}
      Moderner Web-Browser mit Remote-Rendering & Streaming
@{b}========================================================================@{ub}

Willkommen zu @{b}VortexBrowser 0.99 RC1@{ub}!

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

@{b}AmigaOS 4 Client:@{ub}
- AmigaOS 4.1 Final Edition (Update 1 oder Update 2 empfohlen)
- PowerPC Prozessor (G4, G5, SAM440, SAM460, X1000, X5000 oder QEMU pegasos2/sam460ex)
- Grafikkarte mit 24/32-Bit Farbtiefe (Radeon HD oder Radeon RX fuer Hardware-Scaling empfohlen)
- Netzwerkkarte und aktiver Internet-/LAN-Zugang
- AHI Audio-Treiber fuer Soundausgabe

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
1. Kopieren Sie den Ordner @{b}Server-Linux/@{ub} oder @{b}Server-RaspberryPi4/@{ub} auf Ihren Server.
2. Oeffnen Sie ein Terminal im Server-Verzeichnis.
3. Starten Sie das automatisierte Installationsskript:
   @{b}./install_server.sh@{ub}  (bzw. @{b}./install_rpi4.sh@{ub})
   Das Skript richtet automatisch die virtuelle Umgebung ein, installiert alle
   Abhaengigkeiten und Chromium und kompiliert bei Bedarf die SIMD-Beschleunigung.
4. Notieren Sie sich die am Ende des Skripts angezeigte @{b}Server-IP-Adresse@{ub}
   (z. B. 192.168.178.50).
5. Starten Sie den Server mit:
   @{b}./run_server.sh@{ub}  (bzw. @{b}./run_rpi4.sh@{ub})

@{b}Schritt 2: VortexBrowser auf dem AmigaOS 4 starten@{ub}
1. Entpacken Sie das Archiv @{b}VortexBrowser-0.99-RC1-AmigaOS4.lha@{ub} an Ihren
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
- @{"Passwort-Manager & Auto-Login" link "Passwords"}
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
  eigene Favoriten anlegen.
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

@node "Passwords" "Passwort-Manager & Auto-Login"
@{b}Passwort-Manager & Auto-Login@{ub}

VortexBrowser enthaelt einen sicheren, integrierten Passwort-Manager:

- @{b}Lokale, starke Verschluesselung:@{ub}
  Alle Anmeldedaten werden verschluesselt mit @{b}Blowfish-CBC@{ub} und einem ueber
  @{b}SHA-256@{ub} abgeleiteten Master-Key in @{b}PROGDIR:passwords.dat@{ub} gespeichert.
  Es werden keinerlei Passwoerter im Klartext abgelegt!
- @{b}Passwort-Tresor oeffnen:@{ub}
  Druecken Sie @{b}Amiga+K@{ub} oder waehlen Sie im Menue @{b}Projekt -> Passwort-Manager...@{ub}.
- @{b}Auto-Login fuer die aktuelle Seite:@{ub}
  Druecken Sie @{b}Amiga+L@{ub}. VortexBrowser erkennt automatisch die Anmeldefelder der
  aktuellen Website und fuellt Benutzername und Passwort selbstaendig aus.

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
   - @{b}Server IP / Host & Port:@{ub} Manuelle Eingabe des Streaming-Servers (z. B. 192.168.178.50:7788)
   - @{b}Stream-Aufloesung:@{ub} Nativ (1:1), 720p HD, 540p qHD (siehe Performance!)
   - @{b}JPEG-Qualitaet:@{ub} Slider von 20% bis 100%
   - @{b}Video-Optimierung:@{ub} Dynamische Bildrate bei Videowiedergabe
   - @{b}Aktuellen Server beim Beenden automatisch speichern@{ub}

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
  @{b}Amiga+L@{ub}       Auto-Login fuer aktuelle Seite ausfuehren
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

@node "ServerSetup" "Server-Einrichtung (Linux PC / Raspberry Pi)"
@{b}Server-Einrichtung (Linux PC / Raspberry Pi)@{ub}

Der Server kann auf jedem gaengigen Linux-System betrieben werden (z. B. Raspberry Pi 4,
Raspberry Pi 5, Intel/AMD PC mit Debian, Ubuntu, Linux Mint, CachyOS, Arch, etc.).

@{b}Automatische Installation mit install_server.sh:@{ub}
1. Oeffnen Sie ein Terminal im Server-Verzeichnis.
2. Fuehren Sie das Skript aus:
   @{b}./install_server.sh@{ub} (oder @{b}./install_rpi4.sh@{ub})
3. Das Skript fuehrt vollautomatisch folgende Schritte durch:
   - Ueberprueft Python 3 (benoetigt Python 3.10 oder neuer).
   - Erstellt eine isolierte virtuelle Umgebung (@{b}.venv@{ub}).
   - Installiert die benoetigten Pakete (@{i}playwright@{ui}, @{i}lz4@{ui}, @{i}Pillow@{ui}).
   - Laedt die optimierte Chromium-Browser-Engine herunter.
   - Kompiliert die native SIMD-Grafikbeschleunigung (@{b}fast_pixconv.so@{ub}).
   - Ermittelt die IP-Adresse Ihres Rechners im Netzwerk.

@{b}Manuelles Starten:@{ub}
   @{b}./run_server.sh@{ub} (bzw. @{b}./run_rpi4.sh@{ub})

@{b}Ports im Netzwerk:@{ub}
- Port @{b}7788@{ub}: Haupt-Streaming-Port (MJPEG/LZ4 und Steuerung)
- Port @{b}9757@{ub}: Audio-Streaming

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

@endnode

@node "EnglishDoc" "English Documentation Summary"
@{b}English Documentation Summary@{ub}

@{b}VortexBrowser 0.99 RC1@{ub} is a remote web rendering browser client for AmigaOS 4.1.
It renders modern web pages (HTML5, CSS3, ES2024 JavaScript, WebGL, YouTube, etc.)
on a companion Linux PC or Raspberry Pi 4/5 server, streaming ultra-smooth visuals
and 16-bit 48kHz audio directly to AmigaOS with native Reaction GUI controls.

@{b}Quick Start:@{ub}
1. On your Linux PC or Raspberry Pi:
   Enter the server directory and run:
   @{b}./install_server.sh@{ub}
   Then start the server with:
   @{b}./run_server.sh@{ub}
   Note the IP address displayed.
2. On your Amiga:
   Double-click @{b}VortexBrowser@{ub}.
   Enter your server IP address (Port: 7788) and connect!

@{b}Key Features:@{ub}
- Full multi-tab browsing (Amiga+T to open, Amiga+W to close).
- Quick launch toolbar with icons and customizable bookmarks.
- Integrated Download Manager (Amiga+D).
- Encrypted Password Vault & Auto-Login (Amiga+K / Amiga+L).
- Complete Dark Mode toggle (Amiga+U) with DarkReader and dark Reaction GUI.
- Hardware-accelerated scaling via CompositeTags (select 720p or 540p stream for low CPU).
- 16-bit stereo 48kHz synchronized audio via AHI.

@endnode
"""

with open(os.path.join(AMIGA_DRAWER, f"{APP_NAME}.guide"), "w", encoding="iso-8859-1") as f:
    f.write(GUIDE_CONTENT)

# -------------------------------------------------------------
# German Quickstart Readme: LIESMICH.txt
# -------------------------------------------------------------
LIESMICH_CONTENT = f"""==============================================================================
                    {APP_NAME.upper()} 0.99 RC1 FUER AMIGAOS 4.1
                 Schnellstartanleitung & Kurzanleitung
==============================================================================

{APP_NAME} ist ein moderner Web-Browser fuer AmigaOS 4.1, der ueber ein
leistungsfaehiges Remote-Rendering-Backend (auf Linux-PC oder Raspberry Pi 4/5)
die gesamte moderne Webwelt (HTML5, CSS3, JavaScript ES2024, YouTube, Twitch,
WebGL, etc.) fluessig und ressourcenschonend auf den Amiga bringt.

------------------------------------------------------------------------------
1. INHALT DIESES ARCHIVS
------------------------------------------------------------------------------
- {APP_NAME}             : Ausfuehrbares Programm fuer AmigaOS 4 (newlib.library)
- {APP_NAME}.info        : Echtes AmigaOS 4 Icon im VideoVortex-Look
- {APP_NAME}.guide       : Ausfuehrliches AmigaGuide-Handbuch mit allen Details
- LIESMICH.txt          : Diese Schnellstartanleitung (Deutsch)
- README.txt            : Schnellstartanleitung (Englisch)
- icons/                : Symbole fuer die Schnellwahl-Leiste

------------------------------------------------------------------------------
2. SCHNELLSTART IN 2 SCHRITTEN
------------------------------------------------------------------------------

SCHRITT 1: Server starten (auf Linux-PC oder Raspberry Pi 4/5)
1. Entpacken Sie das Server-Archiv (Linux oder Raspberry Pi) auf Ihrem Rechner.
2. Oeffnen Sie ein Terminal im Server-Ordner und starten Sie die Installation:
      ./install_server.sh
3. Starten Sie anschliessend den Server mit:
      ./run_server.sh
4. Merken Sie sich die angezeigte IP-Adresse (z. B. 192.168.178.50).

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
  - [+] Seite hinzufuegen, [-] Seite entfernen.

* Download-Manager:
  - Oeffnen mit: Amiga+D (oder Menue "Projekt -> Download-Manager...").
  - Live-Geschwindigkeit, Fortschritt in Prozent, Datei oeffnen, Ordner oeffnen.

* Dark Mode (Dunkles Theme):
  - Umschalten mit: Amiga+U.
  - Dunkle Webseiten via DarkReader und dunkle Amiga Reaction-Oberflaeche!

* Passwort-Manager & Auto-Login:
  - Passwort-Manager oeffnen: Amiga+K
  - Gespeichertes Login automatisch eintragen: Amiga+L
  - Sichere Blowfish-Verschluesselung in PROGDIR:passwords.dat!

* TIPP ZUR CPU-ENTLASTUNG (fuer SAM440, SAM460, G4):
  - Menue "Ansicht -> Stream: 720p HD" oder "Stream: 540p qHD" waehlen.
  - Die Radeon-Grafikkarte skaliert das Bild ueber Hardware-CompositeTags()!
  - Senkt die Amiga-CPU-Last um 50-70%!

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
                    {APP_NAME.upper()} 0.99 RC1 FOR AMIGAOS 4.1
                 Quick Start Guide & Feature Overview
==============================================================================

{APP_NAME} brings the modern web to AmigaOS 4.1 using a high-performance
headless Chromium rendering backend running on your local network (Linux PC
or Raspberry Pi 4/5). Enjoy full HTML5, CSS3, ES2024 JavaScript, WebGL,
YouTube, and Twitch with low Amiga CPU usage and native Reaction GUI.

------------------------------------------------------------------------------
1. PACKAGE CONTENTS
------------------------------------------------------------------------------
- {APP_NAME}             : Standard AmigaOS 4 executable (newlib.library)
- {APP_NAME}.info        : Native AmigaOS 4 Icon in cosmic vortex look
- {APP_NAME}.guide       : Comprehensive AmigaGuide manual
- LIESMICH.txt          : German quickstart guide
- README.txt            : English quickstart guide
- icons/                : Toolbar and bookmark icon collection

------------------------------------------------------------------------------
2. QUICK START IN 2 STEPS
------------------------------------------------------------------------------

STEP 1: Start Server (Linux PC or Raspberry Pi 4/5)
1. Extract the Server archive on your companion Linux machine or Raspberry Pi.
2. Open a terminal and run:
      ./install_server.sh
3. Start the server with:
      ./run_server.sh
4. Note your server's local LAN IP address (e.g. 192.168.178.50).

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
  Amiga+L   : Auto-Login for current site
  Amiga+U   : Toggle Dark Mode (Light/Dark Theme)
  Amiga+E   : Open Preferences
  Amiga+Q   : Quit {APP_NAME}

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

# Copy Icons to icons/
for icon in os.listdir("amiga-client/icons"):
    if icon.endswith(".png"):
        shutil.copy2(os.path.join("amiga-client/icons", icon), os.path.join(AMIGA_DRAWER, "icons", icon))

# Copy Info Files from Kyvos OS4 ISO / repository if available
KYVOS_DOCS_INFO = "/home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VideoVortex/Data/MPlayer/Docs.info"
KYVOS_README_INFO = "/home/tears/kyvos-2.2.0-linux64/amigaos4/Download/VideoVortex/Data/MPlayer/Docs/README.info"
KYVOS_GUIDE_INFO = "/home/tears/kyvos-2.2.0-linux64/ISO/AmigaOS 4.1 Final Edition Update 2/Content/Workbench/Documentation/Commodities/ContextMenus.guide.info"

if os.path.exists(KYVOS_DOCS_INFO):
    shutil.copy2(KYVOS_DOCS_INFO, os.path.join(AMIGA_DRAWER, "icons.info"))
if os.path.exists(KYVOS_README_INFO):
    shutil.copy2(KYVOS_README_INFO, os.path.join(AMIGA_DRAWER, "LIESMICH.txt.info"))
    shutil.copy2(KYVOS_README_INFO, os.path.join(AMIGA_DRAWER, "README.txt.info"))
if os.path.exists(KYVOS_GUIDE_INFO):
    shutil.copy2(KYVOS_GUIDE_INFO, os.path.join(AMIGA_DRAWER, f"{APP_NAME}.guide.info"))

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
Restart=always
RestartSec=3
Environment=PYTHONUNBUFFERED=1

[Install]
WantedBy=default.target
"""

# Populate Server-Linux Drawer
shutil.copy2("pi-server/server.py", os.path.join(SERVER_LINUX_DRAWER, "server.py"))
shutil.copy2("pi-server/fast_pixconv.c", os.path.join(SERVER_LINUX_DRAWER, "fast_pixconv.c"))
shutil.copy2("pi-server/fast_pixconv.so", os.path.join(SERVER_LINUX_DRAWER, "fast_pixconv.so"))
shutil.copy2("pi-server/darkreader.js", os.path.join(SERVER_LINUX_DRAWER, "darkreader.js"))
shutil.copy2("pi-server/requirements.txt", os.path.join(SERVER_LINUX_DRAWER, "requirements.txt"))
if os.path.exists("pi-server/extensions"):
    shutil.copytree("pi-server/extensions", os.path.join(SERVER_LINUX_DRAWER, "extensions"), dirs_exist_ok=True)
with open(os.path.join(SERVER_LINUX_DRAWER, "run_server.sh"), "w") as f:
    f.write(SERVER_RUN_SH)
os.chmod(os.path.join(SERVER_LINUX_DRAWER, "run_server.sh"), 0o755)
with open(os.path.join(SERVER_LINUX_DRAWER, "install_server.sh"), "w") as f:
    f.write(SERVER_INSTALL_SH)
os.chmod(os.path.join(SERVER_LINUX_DRAWER, "install_server.sh"), 0o755)
with open(os.path.join(SERVER_LINUX_DRAWER, "vortexbrowser.service"), "w") as f:
    f.write(SYSTEMD_SERVICE)

# Populate Server-RaspberryPi4 Drawer
shutil.copy2("rpi4-server/server.py", os.path.join(SERVER_RPI4_DRAWER, "server.py"))
shutil.copy2("rpi4-server/fast_pixconv.c", os.path.join(SERVER_RPI4_DRAWER, "fast_pixconv.c"))
if os.path.exists("rpi4-server/fast_pixconv_aarch64.so"):
    shutil.copy2("rpi4-server/fast_pixconv_aarch64.so", os.path.join(SERVER_RPI4_DRAWER, "fast_pixconv.so"))
    shutil.copy2("rpi4-server/fast_pixconv_aarch64.so", os.path.join(SERVER_RPI4_DRAWER, "fast_pixconv_aarch64.so"))
shutil.copy2("rpi4-server/darkreader.js", os.path.join(SERVER_RPI4_DRAWER, "darkreader.js"))
shutil.copy2("rpi4-server/requirements.txt", os.path.join(SERVER_RPI4_DRAWER, "requirements.txt"))
if os.path.exists("rpi4-server/extensions"):
    shutil.copytree("rpi4-server/extensions", os.path.join(SERVER_RPI4_DRAWER, "extensions"), dirs_exist_ok=True)
if os.path.exists("rpi4-server/install_rpi4.sh"):
    shutil.copy2("rpi4-server/install_rpi4.sh", os.path.join(SERVER_RPI4_DRAWER, "install_server.sh"))
else:
    with open(os.path.join(SERVER_RPI4_DRAWER, "install_server.sh"), "w") as f:
        f.write(SERVER_INSTALL_SH)
os.chmod(os.path.join(SERVER_RPI4_DRAWER, "install_server.sh"), 0o755)

if os.path.exists("rpi4-server/run_rpi4.sh"):
    shutil.copy2("rpi4-server/run_rpi4.sh", os.path.join(SERVER_RPI4_DRAWER, "run_server.sh"))
else:
    with open(os.path.join(SERVER_RPI4_DRAWER, "run_server.sh"), "w") as f:
        f.write(SERVER_RUN_SH)
os.chmod(os.path.join(SERVER_RPI4_DRAWER, "run_server.sh"), 0o755)

if os.path.exists("rpi4-server/README_RPI4.md"):
    shutil.copy2("rpi4-server/README_RPI4.md", os.path.join(SERVER_RPI4_DRAWER, "README_RPI4.txt"))
with open(os.path.join(SERVER_RPI4_DRAWER, "vortexbrowser.service"), "w") as f:
    f.write(SYSTEMD_SERVICE)

# -------------------------------------------------------------
# Overall German and English Readmes for Complete Archive
# -------------------------------------------------------------
OVERALL_DE_TXT = f"""==============================================================================
            {APP_NAME.upper()} 0.99 RC1 - GESAMTPAKET FUER AMIGAOS 4 & SERVER
==============================================================================

Dieses Komplettpaket enthaelt alles fuer den Betrieb von {APP_NAME} 0.99 RC1
auf AmigaOS 4.1 sowie dem Rendering-Server auf Linux oder Raspberry Pi 4/5.

VERZEICHNISSTRUKTUR:
--------------------
- {APP_NAME}/          : Der vollstaendige AmigaOS 4 Client-Ordner
  * {APP_NAME}         : Ausfuehrbares Programm (Newlib-Version)
  * {APP_NAME}.info    : Echtes AmigaOS 4 Icon im VideoVortex-Look
  * {APP_NAME}.guide   : Ausfuehrliches AmigaGuide-Handbuch
  * LIESMICH.txt       : Deutsche Kurzanleitung
  * README.txt         : Englische Kurzanleitung
  * icons/             : Symbolleisten-Icons
- {APP_NAME}.info      : Workbench-Ordner-Icon fuer den Client-Ordner
- Server-Linux/        : Der Rendering-Server fuer Linux PC (x86_64)
- Server-RaspberryPi4/ : Der Rendering-Server optimiert fuer Raspberry Pi 4/5 (ARM64)

SCHNELLSTART:
-------------
1. Server einrichten (auf Linux PC oder Raspberry Pi):
   - Kopieren Sie "Server-Linux/" oder "Server-RaspberryPi4/" auf Ihren Rechner.
   - Starten Sie "./install_server.sh" und anschliessend "./run_server.sh".
   - Notieren Sie sich die IP-Adresse.

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
# Create 2: Linux Server Zip Archive
# -------------------------------------------------------------
print(f"=== [5/6] Creating Linux & Raspberry Pi Server Archives ===")
LINUX_ZIP = os.path.join(DIST_DIR, f"{APP_NAME}-{VER_STR}-Linux.zip")
with zipfile.ZipFile(LINUX_ZIP, "w", zipfile.ZIP_DEFLATED) as zf:
    for root, dirs, files in os.walk(SERVER_LINUX_DRAWER):
        for file in files:
            full_path = os.path.join(root, file)
            rel_path = os.path.join("Server-Linux", os.path.relpath(full_path, SERVER_LINUX_DRAWER))
            zf.write(full_path, rel_path)
print(f"[+] Created Linux Server Zip: {LINUX_ZIP} ({os.path.getsize(LINUX_ZIP)} bytes)")

# -------------------------------------------------------------
# Create 3: Raspberry Pi 4 Server Zip Archive
# -------------------------------------------------------------
RPI4_ZIP = os.path.join(DIST_DIR, f"{APP_NAME}-{VER_STR}-RaspberryPi4.zip")
with zipfile.ZipFile(RPI4_ZIP, "w", zipfile.ZIP_DEFLATED) as zf:
    for root, dirs, files in os.walk(SERVER_RPI4_DRAWER):
        for file in files:
            full_path = os.path.join(root, file)
            rel_path = os.path.join("Server-RaspberryPi4", os.path.relpath(full_path, SERVER_RPI4_DRAWER))
            zf.write(full_path, rel_path)
print(f"[+] Created Raspberry Pi 4 Server Zip: {RPI4_ZIP} ({os.path.getsize(RPI4_ZIP)} bytes)")

# -------------------------------------------------------------
# Create 4: Complete Zip Archive
# -------------------------------------------------------------
COMPLETE_ZIP = os.path.join(DIST_DIR, f"{APP_NAME}-{VER_STR}-Complete.zip")
with zipfile.ZipFile(COMPLETE_ZIP, "w", zipfile.ZIP_DEFLATED) as zf:
    for root, dirs, files in os.walk(STAGING_DIR):
        for file in files:
            full_path = os.path.join(root, file)
            rel_path = os.path.relpath(full_path, STAGING_DIR)
            zf.write(full_path, rel_path)
print(f"[+] Created Complete Zip: {COMPLETE_ZIP} ({os.path.getsize(COMPLETE_ZIP)} bytes)")

print(f"=== [6/6] Verifying Archives ===")
res = subprocess.run(["lha", "v", LHA_ARCHIVE], capture_output=True, text=True)
print(res.stdout)

print("\nRelease packaging successfully completed for AmigaOS 4, Linux, and Raspberry Pi 4!")
