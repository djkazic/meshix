# meshix

Firmware for the LilyGo T-Watch S3, speaking the MeshCore protocol over the air. Built around three goals: long battery life, fully standalone operation (canned messages + on-screen keyboard), and wire-level MeshCore interoperability.

## Hardware

- ESP32-S3 (16MB flash, 8MB OPI PSRAM)
- SX1262 LoRa radio (NSS=5, DIO1=9, RST=8, BUSY=7, SCK=3, MISO=4, MOSI=1)
- ST7789 240x240 LCD (CS=12, DC=38, MOSI=13, SCK=18, backlight=45)
- Capacitive touch on Wire1 (SDA=39, SCL=40, INT=16)
- AXP2101 PMU + sensors on Wire (SDA=10, SCL=11)

AXP2101 rail map: ALDO3 powers the radio and ALDO4 must stay on or the radio browns out; ALDO1/ALDO2 feed the display and sensors; BLDO2 drives the haptic.

## Build & flash

```
pio run
pio run -t upload --upload-port /dev/cu.usbmodemXXXX
```

## CLI (USB serial, 115200)

`adv` send self-advert · `bat` power · `time <epoch>` set RTC ·
`addchan <name> <psk_base64>` add a channel
