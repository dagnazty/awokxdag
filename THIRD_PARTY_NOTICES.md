# Third-party references and notices

## NM-CYD-C5 hardware references — RockBase IoT and CYM

The `AWOK_NM_CYD_C5` profile uses factual board-interface information from the
official [RockBase IoT NM-CYD-C5 repository](https://github.com/RockBase-iot/NM-CYD-C5):
ESP32-C5 memory configuration and the ST7789, XPT2046, SD, backlight, and
LP-UART pin map. [CYM](https://github.com/JimGat/CYM) by **JimGat** was also
consulted as an independent working-hardware reference for the shared SPI bus,
panel color/inversion configuration, and native portrait touch orientation.

AxD's display wrapper, board profile, build integration, UI, and feature code
are its own implementation; source bodies from either project were not copied.

## Piglet — Hamspiced

Fleet Wardrive's explicit coordinator/node ESP-NOW topology was informed by
[Piglet](https://github.com/Hamspiced/piglet) by **Hamspiced**. Piglet's
Core/Node behavior provided the reference for keeping a user-selected Core
authoritative while nodes discover, join, heartbeat, and reconnect.

AxD's fleet wire protocol, roster and channel assignment, row aggregation,
display UI, and web integration are implemented for AxD; Piglet source code was
not copied into this repository. Piglet is distributed under the
[Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International
license](https://github.com/Hamspiced/piglet/blob/main/LICENSE). Thanks to
Hamspiced for publishing Piglet and its ESP-NOW design openly.

## ESP32 Marauder — justcallmekoko

AxD's direct wardrive upload workflow references ESP32 Marauder's documented
network/file/destination flow and SD credential-file convention. AxD consolidates
the credentials into its own `wardrive_upload.txt` format; its screen, multipart
streaming client, TLS validation, response handling, and audit integration are
newly implemented for AxD, and no uploader source body was copied.

[ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder) is maintained
by **justcallmekoko (Justin Hazard)** and distributed under GPL-3.0. Thanks for
publishing the hardware mappings, radio lifecycle work, and direct-upload user
workflow that informed this firmware.

## Evil-M5Project — 7h30th3r0n3

The Network Tools and Flipper-like BLE service hints reference the feature designs
in [Evil-Cardputer-v1-5-5.ino](https://github.com/7h30th3r0n3/Evil-M5Project/blob/main/Evil-Cardputer-v1-5-5.ino),
reviewed on 2026-09-11. Relevant sections include `scanHosts`, `scanPorts`, the
CCTV toolkit, `detectPrinter`, `sipScan`, `listUPnPMappings`, and Wall of Flippers.
Thanks to **7h30th3r0n3** for making that work available.

AWOKxDAG's network engine, parsers, and UI integration are newly written for this
firmware. Upstream scanner function bodies were not copied. Advertised BLE service
UUIDs serve as protocol identification hints. Evil-M5's `scanPorts` section credits
[pr3y/Bruce](https://github.com/pr3y/bruce); that section was not transplanted.
Do not assume the sketch's top-level notice supersedes notices on third-party
code or dependencies.

The reviewed Evil-Cardputer sketch includes this MIT notice:

```text
Copyright (c) 2026 7h30th3r0n3

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
