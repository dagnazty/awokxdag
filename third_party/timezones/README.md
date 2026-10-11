# Offline GPS timezone data

`AWOKxDAG/timezone_data.h` contains a geographic lookup tree and IANA offset/DST
transitions. It uses about **120 KB of mapped flash**, with no heap allocation,
SD database, new Arduino library, or location request to a web service.

- Geographic map and decoder: [PhotoStructure tz-lookup 11.7.0](https://github.com/photostructure/tz-lookup),
  CC0; see `tz-lookup-LICENSE.txt`. Based on
  [timezone-boundary-builder](https://github.com/evansiroky/timezone-boundary-builder)
  and [OpenStreetMap contributors](https://www.openstreetmap.org/copyright)
  (boundary data under ODbL). The C++ decoder is adapted from its quadtree algorithm.
- Rules: [IANA tzdb 2026d](https://www.iana.org/time-zones), packaged by
  [tzdata 2026.4](https://pypi.org/project/tzdata/2026.4/). IANA data is public
  domain; packaging license notices are in `tzdata-LICENSE.txt` / `tzdata-APACHE.txt`.

The compact map trades boundary precision for space: **the selected timezone can
be wrong near borders**, especially in sparsely populated areas and offshore.
This is a geographic estimate, not an exact point-in-polygon service. The GPS
screen shows the selected IANA zone name so the selection is inspectable.

Tables cover **2020-01-01 through 2099-12-31**, including half/quarter-hour
UTC offsets, southern hemisphere DST, and date-specific changes such as
Morocco's Ramadan transitions. Dates outside this range are not converted.
Future legislative changes require a data update; GPS does not transmit civil
clock rules. These are the rules known to this bundled IANA release.

The latest fresh GPS coordinates choose the zone at startup and every 30 seconds.
The last zone name persists in the `awok-tz` NVS namespace and remains in use
through outages and reboots until a fresh position replaces it. Local timestamps
are used for displays, logs and new WiGLE CSV rows. Existing files are unchanged.
Before both clock and zone are known, logs explicitly report uptime. Calendar
conversions never shift the absolute epoch used by TLS or NTP. libc's local
zone offset is updated at DST changes so new FAT timestamps are local as well.

## Regeneration (does not compile firmware)

`scripts/generate_timezones.py` pins the two upstream archive URLs and SHA-256
hashes. Download those archives to temporary files, then run:

```sh
python3 scripts/generate_timezones.py --lookup /tmp/awok-tz-lookup.tgz \
  --tzdata /tmp/awok-tzdata.whl
node --test tests/host/test_timezone.mjs
```

Requires Python 3.9+ and Node.js for the checks. No pip/npm installation is needed.
To update, review the upstream releases, change the pinned URLs/hashes and IANA
version comment in the generator, regenerate, and review map/transition changes.
The generator deduplicates schedules to keep tables small. It probes each UTC
day and bisects changes to the exact second; this relies on tzdb having at most
one transition per day in the supported range.

The tests interpret the checked-in C++ decoder bodies with type-only adaptation
and check known cities, invalid coordinates, DST transition seconds, local date
rollover, no-DST regions, and fractional offsets. For decoder parity, extract the
pinned archive's `package/tz.js` and set `AWOK_TZ_REFERENCE=/path/to/tz.js` when
running the timezone tests (100,000 deterministic coordinates). These checks do
not replace a firmware compile or hardware validation.
