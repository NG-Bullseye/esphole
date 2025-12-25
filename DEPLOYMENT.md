# ESP-Hole Deployment Dokumentation

## Voraussetzungen

- Python 3.x installiert
- PlatformIO Core
- ESP8266 NodeMCU Board
- USB-Kabel

## Installation

### PlatformIO installieren

```bash
pip install platformio
```

## Konfiguration

### 1. WiFi-Zugangsdaten

In `src/main.cpp` die WLAN-Zugangsdaten anpassen:

```cpp
#define wifi_ssid "Dein-WLAN-Name"
#define wifi_password "Dein-WLAN-Passwort"
```

### 2. Blocklisten generieren

```bash
python utils/gen_block_lists.py
```

Dies lädt die Blockliste herunter und generiert 37 Dateien im `data/` Verzeichnis.

## Deployment auf ESP8266

### 1. Firmware kompilieren und hochladen

```bash
python -m platformio run --target upload
```

Dies kompiliert den Code und lädt ihn auf den ESP8266 hoch (ca. 30 Sekunden).

### 2. Dateisystem (Blocklisten) hochladen

```bash
python -m platformio run --target uploadfs
```

Dies lädt die Blocklisten-Dateien aus `data/` auf das LittleFS-Dateisystem des ESP8266 (ca. 10 Sekunden).

## Serial Monitor auslesen

```bash
python -m platformio device monitor
```

Ausgabe beenden: `Ctrl+C`

### Was wird angezeigt?

```
WiFi connected | IP address: 192.168.178.87
LittleFS mounted successfully
Files in LittleFS:
  hosts_10 (2525 bytes)
  ...
DNS Server ready

Domain: doubleclick.net Blocked | Find took 17 ms
Domain: google.com | IP:142.250.185.46
```

- **Blocked**: Domain wurde geblockt (LED blinkt)
- **IP:x.x.x.x**: Domain wurde durchgelassen

## Fritz!Box Konfiguration

### Problem: Fritz!Box-Suffix `.fritz.box`

Die Fritz!Box hängt `.fritz.box` an DNS-Anfragen an. Der Code entfernt dies automatisch:

```cpp
dom.replace(".fritz.box", "");
dom.replace(".local", "");
```

### DNS-Server konfigurieren

**Option 1: Router-weit (empfohlen)**

1. Fritz!Box öffnen: `http://fritz.box`
2. **Heimnetz → Netzwerk → Netzwerkeinstellungen**
3. **IPv4-Konfiguration → DHCP-Server**
4. **Lokaler DNS-Server**: `192.168.178.87` (IP des ESP8266)
5. **Fritz!Box neu starten**
6. **Clients neu starten** oder DHCP erneuern: `ipconfig /release && ipconfig /renew`

**Option 2: Einzelnes Gerät**

Windows:
1. Systemsteuerung → Netzwerkverbindungen
2. WLAN → Eigenschaften → IPv4 → Eigenschaften
3. DNS-Server: `192.168.178.87`

## Features

### LED-Anzeige

- **Dauerhaft an**: ESP-Hole läuft normal
- **Kurz aus (100ms)**: Domain wurde geblockt

### Parent-Domain-Blocking

Blockiert automatisch alle Subdomains:
- `doubleclick.net` geblockt → `adclick.g.doubleclick.net` wird auch geblockt
- Ausgabe: `Domain: adclick.g.doubleclick.net (parent: doubleclick.net) Blocked`

### Statistiken

- **3.486 Domains** in der Blockliste
- **37 Dateien** nach Domain-Länge organisiert (`hosts_4` bis `hosts_40`)

## Testen

### DNS-Blockierung testen

```bash
# Direkt gegen ESP8266 testen
nslookup doubleclick.net 192.168.178.87
# Sollte zurückgeben: 0.0.0.0 (geblockt)

# System-DNS testen (nach Konfiguration)
nslookup doubleclick.net
# Server sollte sein: 192.168.178.87
```

### Serial Monitor zeigt keine DNS-Anfragen?

**Mögliche Ursachen:**
1. **Fritz!Box nicht konfiguriert**: Siehe "Fritz!Box Konfiguration"
2. **DNS-Cache**: `ipconfig /flushdns` ausführen
3. **Browser nutzt DoH**: DNS over HTTPS in Chrome/Firefox deaktivieren
4. **Keine DHCP-Erneuerung**: PC/Router neu starten

## Troubleshooting

### "Error: file open failed"

LittleFS nicht korrekt hochgeladen:
```bash
python -m platformio run --target uploadfs
```

### LED blinkt nicht

1. Prüfen ob DNS-Anfragen am ESP8266 ankommen (Serial Monitor)
2. Fritz!Box-Konfiguration prüfen
3. Browser-Cache leeren

### Keine Blockierung

1. Testen: `nslookup ads.google.com 192.168.178.87`
2. Sollte `0.0.0.0` zurückgeben
3. Wenn nicht: Filesystem neu hochladen

## Blocklisten aktualisieren

```bash
# Neue Blocklisten generieren
python utils/gen_block_lists.py

# Auf ESP8266 hochladen
python -m platformio run --target uploadfs
```

## Technische Details

- **Board**: ESP8266 NodeMCU v2
- **Dateisystem**: LittleFS
- **DNS-Port**: 53
- **Flash**: 310 KB (30% belegt)
- **RAM**: 29 KB (35% belegt)
- **LED**: GPIO2 (LED_BUILTIN)

## Limitierungen

❌ **YouTube-Werbung**: Kann nicht blockiert werden (gleiche Domain wie Videos)
❌ **HTTPS-Inhalte**: DNS blockt nur Domain-Namen, nicht Inhalte
❌ **Bereits gecachte IPs**: Browser-Cache muss geleert werden
✅ **Display-Werbung**: Wird effektiv blockiert
✅ **Tracking**: Über 3.400 Tracking-Domains geblockt
