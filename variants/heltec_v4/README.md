# Heltec V4 WiFi Remote Repeater — Setup Guide

## Variant Files

Copy the following into `variants/heltec_v4/`:
- `HeltecV4Board.h`, `HeltecV4Board.cpp`
- `target.h`, `target.cpp`
- `pins_arduino.h`

Copy `heltec_v4.json` into `boards/`

## Config Files (SPIFFS)

The Heltec V4 has no SD card slot — config lives in SPIFFS.

Create a `data/remote/` folder in your project root:

```
data/
  remote/
    wifi.cfg
    mqtt.cfg
```

### data/remote/wifi.cfg
```
YourSSID
YourPassword
BackupSSID
BackupPassword
```

### data/remote/mqtt.cfg
```
6818ce5f77dd45bb90facf753ba81d81.s1.eu.hivemq.cloud
8883
meckremote
yourpassword
heltec-wifi-1
```

### Upload config to SPIFFS
```bash
pio run -e meck_wifi_repeater_heltec_v4 -t uploadfs
```

This uploads the `data/` folder contents to SPIFFS on the device.

### Flash firmware
```bash
pio run -e meck_wifi_repeater_heltec_v4 -t upload
```

## Notes

- The OLED display shows basic repeater status (same as stock repeater)
- WiFi MQTT and Mycelium dashboard work identically to T-Deck Pro builds
- OTA firmware updates work over WiFi via the Mycelium dashboard
- Config changes require re-uploading SPIFFS (`-t uploadfs`)
- The same `main.cpp`, `WiFiMQTT.h/cpp`, and `MyMesh.cpp` are shared
  with T-Deck Pro and T5S3 builds — no Heltec-specific source changes
