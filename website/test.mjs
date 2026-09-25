import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, mkdir, writeFile, readFile, rm, readdir } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { build, renderMarkdown } from './build.mjs';

test('Remote UI parses source-prefixed fleet status per target', async () => {
  const html = await readFile(new URL('./public/control.html', import.meta.url), 'utf8');
  assert.match(html, /const source = dv\.getUint8\(0\)/);
  assert.match(html, /dv\.byteLength >= 25/);
  assert.match(html, /fleetActive = dv\.getUint8\(20\)/);
  assert.match(html, /members = dv\.getUint8\(21\)/);
  assert.match(html, /code = dv\.getUint16\(22, true\)/);
  assert.match(html, /statuses:\[\{\},\{\}\]/);
  assert.match(html, /Screen chip:.*fleetDescription/s);
});

test('Remote wardrive export stays aligned with firmware WiGLE 1.6 rows', async () => {
  const html = await readFile(new URL('./public/control.html', import.meta.url), 'utf8');
  assert.match(html, /WigleWifi-1\.6,appRelease=AxD,model=ESP32,release=1\.7\.5/);
  assert.match(html, /MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type/);
  assert.doesNotMatch(html, /WigleWifi_1\.4/);
  // Fleet Hunter support
  assert.match(html, /55:\s*"Fleet Hunter"/);
  assert.match(html, /data-op="55"/);
  assert.match(html, /hunt-radar/);
  assert.match(html, /parseHuntRow/);
  // Swarm Topology Graph support
  assert.match(html, /56:\s*"Topology Map"/);
  assert.match(html, /data-op="56"/);
  assert.match(html, /tab-topology/);
  assert.match(html, /parseTopologyRow/);
  assert.match(html, /topo-canvas/);
  // BLE Ecosystem Intel & Continuity Decoder support
  assert.match(html, /57:\s*"BLE Intel"/);
  assert.match(html, /data-op="57"/);
  assert.match(html, /tab-bleintel/);
  assert.match(html, /parseBleIntelRow/);
  assert.match(html, /bleintel-list/);
  // Dual-Band RF Spectrogram & Waterfall Analyzer support
  assert.match(html, /58:\s*"Spectrogram"/);
  assert.match(html, /data-op="58"/);
  assert.match(html, /tab-spectrogram/);
  assert.match(html, /parseSpectrogramRow/);
  assert.match(html, /spec-waterfall-canvas/);
  assert.match(html, /spec-board-subtitle/);
  assert.match(html, /spec-board-subheader/);
  assert.match(html, /spec-btn-band/);
  assert.match(html, /spec-btn-ch-prev/);
  assert.match(html, /spec-btn-ch-next/);
  assert.match(html, /specPaletteLut/);
  // Wi-Fi 6 / 802.11ax OFDMA & BSS Color Intel support
  assert.match(html, /59:\s*"Wi-Fi 6 Intel"/);
  assert.match(html, /data-op="59"/);
  assert.match(html, /tab-wifi6/);
  assert.match(html, /parseWifi6IntelRow/);
  assert.match(html, /wifi6-color-matrix/);
  assert.match(html, /wifi6-list/);
  // Targeted Deauth & Disassociation Forensic Analyzer support
  assert.match(html, /63:\s*"Deauth Forensics"/);
  assert.match(html, /data-op="63"/);
  assert.match(html, /tab-deauthforensics/);
  assert.match(html, /parseDeauthForensicRow/);
  assert.match(html, /deauth-event-tbody/);
  assert.match(html, /deauth-alert-banner/);
  // SD Card File Manager & Web Serial support
  assert.match(html, /64:\s*"File List"/);
  assert.match(html, /65:\s*"File Download"/);
  assert.match(html, /66:\s*"File Delete"/);
  assert.match(html, /tab-files/);
  assert.match(html, /files-table/);
  assert.match(html, /file-progress-card/);
  assert.match(html, /file-preview-card/);
  assert.match(html, /parseFileMessage/);
  assert.match(html, /connect-serial/);
  assert.match(html, /toggleSerial/);
  assert.match(html, /startFileDownload/);
  // iPhone / iOS Web Share & Ready Card support
  assert.match(html, /file-ready-card/);
  assert.match(html, /ready-share-btn/);
  assert.match(html, /shareReadyFile/);
  assert.match(html, /isIosDevice/);
  assert.match(html, /ready-copy-btn/);
});

test('Markdown supports tables, safe HTML, stable unique anchors, and document links', () => {
  const {html, headings} = renderMarkdown('## Setup\n\n## Setup\n\n[Notes](CHANGELOG.md#100---2026-01-01)\n\n[Link mode](docs/link-mode.md)\n\n| Pin | Use |\n| --- | --- |\n| 3 | Touch |\n\n<script>alert(1)</script>\n\n[bad](javascript:alert%281%29)\n\n- [x] Done', 'README.md', new Set(['README.md','CHANGELOG.md','docs/link-mode.md']));
  assert.deepEqual(headings.map(h => h.id), ['setup','setup-1']);
  assert.match(html, /href="changelog.html#100---2026-01-01"/);
  assert.match(html, /href="docs\/link-mode.html"/);
  assert.match(html, /<table>/);
  assert.match(html, /class="table-scroll"/);
  assert.doesNotMatch(html, /<script|javascript:/);
  assert.match(html, /disabled/);
});

test('Changing only source Markdown updates pages, release summary and navigation', async () => {
  const root = await mkdtemp(path.join(os.tmpdir(),'awok-site-test-'));
  try {
    const out = path.join(root,'output');
    await mkdir(path.join(root,'docs'));
    await writeFile(path.join(root,'README.md'),'# AWOKxDAG\n\n**Firmware guide** for ESP32-C5.\n\n- **Version:** 1.0.0\n\n## Features\n\nOriginal content.\n\n[Changes](CHANGELOG.md)\n\n[Details](docs/link-mode.md)');
    await writeFile(path.join(root,'CHANGELOG.md'),'# Changelog\n\n## [Unreleased]\n\nFuture feature.\n\n## [1.0.0] - 2026-01-01\n\nInitial release.');
    await writeFile(path.join(root,'docs/link-mode.md'),'# Link mode\n\n## Pairing\n\n[Guide](../README.md#features)');
    await build(root,out);
    let html = await readFile(path.join(out,'index.html'),'utf8');
    assert.match(html,/release-version">v1.0.0/);
    assert.match(html,/README v1.0.0/);
    assert.match(await readFile(path.join(out,'changelog.html'),'utf8'),/CHANGELOG v1.0.0/);
    assert.match(await readFile(path.join(out,'docs/link-mode.html'),'utf8'),/DOCS v1.0.0/);
    assert.doesNotMatch(html,/release-version">vUnreleased/);
    assert.match(html,/href="https:\/\/espterminator.com\/"/);
    assert.match(await readFile(path.join(out,'docs/link-mode.html'),'utf8'),/href="..\/index.html#features"/);
    await writeFile(path.join(root,'README.md'),'# AWOKxDAG\n\nUpdated description.\n\n- **Version:** 2.0.0\n\n## New section\n\nNew source content.');
    await writeFile(path.join(root,'CHANGELOG.md'),'# Changelog\n\n## [Unreleased]\n\nFuture feature.\n\n## [2.0.0] - 2026-02-01\n\nNew release.');
    await build(root,out);
    html = await readFile(path.join(out,'index.html'),'utf8');
    assert.match(html,/release-version">v2.0.0/);
    assert.match(html,/New source content/);
    assert.match(html,/href="#new-section"/);
    assert.doesNotMatch(html,/Original content/);
    // Check every generated local HTML link and fragment, including subpath pages.
    async function check(folder) {
      for (const entry of await readdir(folder,{withFileTypes:true})) {
        const file = path.join(folder,entry.name);
        if (entry.isDirectory()) await check(file);
        else if (file.endsWith('.html')) {
          const page = await readFile(file,'utf8');
          for (const [,href] of page.matchAll(/href="([^"]+)"/g)) {
            if (/^(https?:|mailto:)/.test(href)) continue;
            const [target,fragment] = href.split('#');
            const linked = target ? path.resolve(path.dirname(file),target) : file;
            const content = await readFile(linked,'utf8');
            if (fragment) assert.ok(content.includes(`id="${fragment}"`),`Missing ${href} in ${file}`);
          }
        }
      }
    }
    // Keep the fixture doc reference current after changing the guide heading.
    await writeFile(path.join(root,'docs/link-mode.md'),'# Link mode\n\n## Pairing\n\n[Guide](../README.md#new-section)');
    await build(root,out);
    await check(out);
  } finally { await rm(root,{recursive:true,force:true}); }
});
