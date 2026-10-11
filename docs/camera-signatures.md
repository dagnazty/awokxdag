# Cameras scan: signature sources and limits

The Cameras tool listens to Wi-Fi frames and BLE advertisements. Every row is a
**suspected device**, even when it says HIGH. The tier describes the specificity
and combination of radio clues, not a measured probability or product proof.
Names and SSIDs can be spoofed; a registered OUI identifies an address block,
not the product using it. A row expires after 90 seconds without a new sighting,
so the live list does not fill with devices passed earlier. A quiet scan does
not establish that no camera exists.

| Clue | Use in firmware | Basis |
| --- | --- | --- |
| `B4:1E:52` | Flock Safety vendor OUI, LOW alone | [Flock's registered OUI](https://github.com/colonelpanichacks/ouispy-detector/blob/main/ouis.md) and [firmware-derived signatures](https://github.com/colonelpanichacks/flock-you/blob/main/datasets/firmware_derived_signatures.md) |
| `00:25:DF` | Axon Enterprise vendor OUI, LOW alone | [Axon OUI entry](https://github.com/colonelpanichacks/ouispy-detector/blob/main/ouis.md) |
| `Flock-` plus 4–8 hex characters | Flock-format Wi-Fi SSID, MED alone | [Flock field setup instructions](https://www.sandiegocounty.gov/content/dam/sdc/purchasing/docs/RFB_10493_Abstract.pdf) and [field detector research](https://github.com/zmattmanz/flock-detection) |
| `Flock`, `FS Ext Battery`, `Penguin`, `Pigvision`, `FlockCam`, and bounded `FS-`/`Penguin-` patterns | Flock-related BLE name, MED alone | [field BLE observations](https://github.com/NSM-Barii/flock-back/blob/main/docs/ble.md) and [field detector research](https://github.com/zmattmanz/flock-detection) |
| BLE manufacturer ID `0x09C8` | XUNTONG; possible Flock-related battery, LOW alone | [Bluetooth SIG assigned company ID](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/Assigned_Numbers.pdf) and [field observations](https://github.com/zmattmanz/flock-detection) |
| Exact BLE services `0x3100`, `0x3200`, `0x3300`, `0x3400`, `0x3500` | Raven-related clue: LOW for one, MED for two or more distinct services | [Raven field research](https://github.com/zmattmanz/flock-detection); generic services `0x1809`, `0x180A`, and `0x1819` are deliberately excluded |

The scanner promotes a Flock name paired with its OUI, XUNTONG manufacturer
data, or a Raven service to HIGH. A Flock-format SSID paired with the Flock OUI
or an Axon product name paired with the Axon OUI is also HIGH. A single vendor
OUI stays LOW. Existing consumer-camera heuristics remain and are labelled with
the same evidence tiers.

Only the two directly registered Flock/Axon OUIs were added. [SquachWatch's
signature audit](https://github.com/skizzophrenic/SquachWatch-CYD/blob/master/docs/DETECTIONS.md)
found that only one of its 29 Flock prefixes belongs to Flock Safety; most
others belong to shared radio suppliers. Bulk importing them would create
false positives. BLE OUI matching applies only
to public addresses; randomized BLE and locally administered Wi-Fi addresses
cannot establish the registered vendor. The BLE scanner retains active scan
mode to receive names in scan responses: it sends BLE scan requests but makes
no connection to detected devices. The device alternates Wi-Fi and BLE windows,
so it cannot hear every transmission. A 200 ms scan-response timeout lets BLE
advertisements without a reply reach the classifier during the six-second BLE
window; [NimBLE's default](https://github.com/h2zero/NimBLE-Arduino/blob/master/src/NimBLEScan.cpp)
is 10.24 seconds.
