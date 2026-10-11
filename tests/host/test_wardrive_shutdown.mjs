import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

// Exercise the actual shutdown control flow without compiling firmware.
// Radio/SD functions are mocks: these tests cannot reproduce controller faults.
const main = readFileSync(new URL('../../AWOKxDAG/AWOKxDAG.ino', import.meta.url), 'utf8');
const gps = readFileSync(new URL('../../AWOKxDAG/gps.ino', import.meta.url), 'utf8');

function functionBody(source, name) {
  const signature = source.indexOf(`void ${name}(`);
  assert.notEqual(signature, -1, name);
  const start = source.indexOf('{', signature);
  let depth = 1;
  for (let end = start + 1; end < source.length; ++end) {
    if (source[end] === '{') ++depth;
    if (source[end] === '}' && --depth === 0) return source.slice(start + 1, end);
  }
  assert.fail(`Unterminated function: ${name}`);
}

function shutdownHarness({ headless = false, phase = 'wifi', initialized = true,
                           bleAvailable = true, deinitResult = true,
                           deinitThrows = false } = {}) {
  const events = [];
  const scan = {
    stop() { events.push('scan.stop'); return true; },
    clearResults() { events.push('scan.clear'); },
  };
  const context = vm.createContext({
    wardriveActive: true,
    wardriveSched: {
      active: true, phase, bleAvailable,
      exitWifi() { events.push('wifi.exit'); },
    },
    wardriveNetworks: 10,
    wardriveBleCount: 3,
    RadioPhase: { kWifi: 'wifi', kBle: 'ble' },
    NimBLEDevice: {
      isInitialized() { return initialized; },
      getScan() { events.push('scan.get'); return scan; },
      deinit(clearAll) {
        assert.equal(clearAll, true);
        events.push('ble.deinit');
        if (deinitThrows) throw new Error('simulated controller failure');
        if (deinitResult) initialized = false;
        return deinitResult;
      },
    },
    Serial: {
      println(message) { events.push(`log:${message}`); },
      printf() { events.push('stopped'); },
    },
    delay() {},
    logMemory(stage) { events.push(`memory:${stage}`); },
    closeWardriveCsv() { events.push('csv.close'); },
    ensureWifiStation(releaseBle) {
      assert.equal(releaseBle, false);
      events.push('wifi.ensure');
    },
    kLinkChannel: 1,
    WIFI_SECOND_CHAN_NONE: 0,
    esp_wifi_set_channel() { events.push('wifi.channel'); },
    linkBroadcastStatus() { events.push('link.status'); },
  });
  for (const [source, name, args] of [
    [main, 'releaseBleMemory', ''],
    [main, 'radioSchedulerEnd', 's'],
    [gps, 'stopWardrive', ''],
  ]) {
    let body = functionBody(source, name);
    body = body.replace(/#ifdef AWOK_HEADLESS\n([\s\S]*?)#else\n([\s\S]*?)#endif/g,
      (_, bridge, screen) => headless ? bridge : screen);
    body = body.replace(/NimBLEScan\* scan =/g, 'const scan =')
      .replace(/static_cast<unsigned long>\((\w+)\)/g, '$1')
      .replace(/NimBLEDevice::/g, 'NimBLEDevice.')
      .replace(/scan->/g, 'scan.')
      .replace(/RadioPhase::/g, 'RadioPhase.');
    vm.runInContext(`function ${name}(${args}) {${body}}`, context);
  }
  return { context, events };
}

for (const phase of ['wifi', 'ble']) {
  test(`wardrive exit during ${phase}: close CSV, stop BLE once, restore STA`, () => {
    const { context, events } = shutdownHarness({ phase });
    context.stopWardrive();
    assert.equal(context.wardriveActive, false);
    assert.equal(context.wardriveSched.active, false);
    assert.ok(events.indexOf('csv.close') < events.indexOf('ble.deinit'));
    for (const event of ['csv.close', 'scan.stop', 'scan.clear', 'ble.deinit', 'wifi.ensure']) {
      assert.equal(events.filter(value => value === event).length, 1, event);
    }
    assert.equal(events.includes('wifi.exit'), phase === 'wifi');
    assert.ok(events.indexOf('ble.deinit') < events.indexOf('wifi.ensure'));
    const count = events.length;
    context.stopWardrive();
    context.radioSchedulerEnd(context.wardriveSched);
    assert.equal(events.length, count, 'duplicate stops must be inert');
  });
}

test('headless wardrive exit preserves the bridge BLE controller', () => {
  const { context, events } = shutdownHarness({ headless: true });
  context.stopWardrive();
  assert.ok(events.includes('scan.stop'));
  assert.ok(events.includes('csv.close'));
  assert.ok(!events.includes('ble.deinit'));
  assert.equal(context.NimBLEDevice.isInitialized(), true);
});

test('Wi-Fi-only fallback exits without creating a BLE scan object', () => {
  const { context, events } = shutdownHarness({ bleAvailable: false });
  context.stopWardrive();
  assert.ok(events.includes('csv.close'));
  assert.ok(!events.includes('scan.get'));
  assert.ok(!events.includes('ble.deinit'));
});

test('already deinitialized BLE is not accessed during exit', () => {
  const { context, events } = shutdownHarness({ initialized: false });
  context.stopWardrive();
  assert.ok(!events.includes('scan.get'));
  assert.ok(!events.includes('ble.deinit'));
  assert.ok(events.includes('wifi.ensure'));
});

test('CSV is closed even if controller teardown subsequently fails', () => {
  const { context, events } = shutdownHarness({ deinitThrows: true });
  assert.throws(() => context.stopWardrive(), /simulated controller failure/);
  assert.ok(events.indexOf('csv.close') < events.indexOf('ble.deinit'));
});

test('deinit failure is reported instead of logged as successful', () => {
  const { context, events } = shutdownHarness({ deinitResult: false });
  context.stopWardrive();
  assert.ok(events.includes('memory:BLE shutdown failed'));
  assert.ok(!events.includes('memory:after BLE shutdown'));
});
