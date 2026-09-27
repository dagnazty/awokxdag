import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';

// Exercise the actual browser download assembler without a site/firmware build.
const html = await readFile(new URL('../../website/public/control.html', import.meta.url), 'utf8');
const start = html.indexOf('function finishFileDownload(name, size) {');
const end = html.indexOf('\nfunction downloadBlob(', start);
assert.ok(start >= 0 && end > start);
const source = html.slice(start, end);
const csv = Buffer.from(
  'WigleWifi-1.6,appRelease=AxD,model=ESP32,release=1.7.7,device=AxD,display=none,board=AxD,brand=AxD\r\n' +
  'MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type\r\n' +
  '00:11:22:33:44:55,Test,[OPEN],2026-09-22 12:00:00,1,2412,-55,42.0,-83.0,200,5,,,WIFI\r\n'
);

function transfer(data = csv, preview = false) {
  const saved = [], previews = [], errors = [];
  const chunks = new Map();
  const total = Math.ceil(data.length / 96) || 1;
  // Deliberately receive chunks out of order, as a wireless relay can.
  for (let i = total; i >= 1; --i) {
    chunks.set(i, data.subarray((i - 1) * 96, i * 96).toString('base64'));
  }
  const context = vm.createContext({
    activeFileDownload: {name: 'wardrive-0001.csv', totalChunks: total, chunks, isPreview: preview},
    lastDownloadedFile: null,
    Blob, Uint8Array, atob,
    log() {}, formatBytes: String,
    alert: message => errors.push(message),
    getFileTypeInfo: () => ({mime: 'text/csv', isText: true}),
    isIosDevice: () => false,
    showFileReadyCard() {},
    downloadBlob: (blob, name) => saved.push({blob, name}),
    displayFilePreview: (name, blob) => previews.push({blob, name}),
  });
  context.resetDownloadProgress = () => { context.activeFileDownload = null; };
  vm.runInContext(source, context);
  return {context, chunks, saved, previews, errors,
    finish: (size = data.length) => context.finishFileDownload('wardrive-0001.csv', size)};
}

test('complete download preserves both CSV headers and all bytes in sequence', async () => {
  const t = transfer();
  t.finish();
  assert.equal(t.errors.length, 0);
  assert.equal(t.saved.length, 1);
  assert.equal(await t.saved[0].blob.text(), csv.toString());
  assert.equal(t.context.activeFileDownload, null);
});

test('missing header chunk never becomes a headerless download or preview', () => {
  for (const preview of [false, true]) {
    const t = transfer(csv, preview);
    t.chunks.delete(1);
    t.finish();
    assert.match(t.errors[0], /Missing chunk 1/);
    assert.equal(t.saved.length + t.previews.length, 0);
    assert.equal(t.context.lastDownloadedFile, null);
  }
});

test('missing middle or final chunks reject the download', () => {
  for (const i of [2, Math.ceil(csv.length / 96)]) {
    const t = transfer();
    t.chunks.delete(i);
    t.finish();
    assert.match(t.errors[0], /Missing chunk/);
    assert.equal(t.saved.length, 0);
  }
});

test('invalid base64 and short chunks reject the download', () => {
  for (const data of ['%%%invalid%%%', Buffer.from('short').toString('base64')]) {
    const t = transfer();
    t.chunks.set(1, data);
    t.finish();
    assert.equal(t.errors.length, 1);
    assert.equal(t.saved.length, 0);
  }
});

test('mismatched completion size or filename rejects the download', () => {
  const t = transfer();
  t.finish(csv.length + 96);
  assert.match(t.errors[0], /Chunk count/);
  assert.equal(t.saved.length, 0);
  const other = transfer();
  other.context.finishFileDownload('other.csv', csv.length);
  assert.match(other.errors[0], /did not match/);
  assert.equal(other.saved.length, 0);
});

test('complete preview and empty file remain supported', async () => {
  const t = transfer(csv, true);
  t.finish();
  assert.equal(t.errors.length, 0);
  assert.equal(await t.previews[0].blob.text(), csv.toString());
  assert.equal(t.saved.length, 0);
  const empty = transfer(Buffer.alloc(0));
  empty.finish();
  assert.equal(empty.errors.length, 0);
  assert.equal(empty.saved[0].blob.size, 0);
});

const reliableStart = html.indexOf('let lastReliableFile = null;');
const reliableEnd = html.indexOf('function parseFileMessage(', reliableStart);
assert.ok(reliableStart > 0 && reliableEnd > reliableStart);
const reliableSource = html.slice(reliableStart, reliableEnd);

function reliableTransfer(data, preview = false) {
  const t = transfer(data, preview);
  t.chunks.clear();
  const acknowledgments = [];
  const connection = {
    connected: true,
    cmdCh: {writeValue: async bytes => {
      const view = new DataView(bytes.buffer);
      assert.equal(bytes[0], 69);
      acknowledgments.push({token: view.getUint32(1, true), seq: view.getUint32(5, true)});
    }},
  };
  Object.assign(t.context.activeFileDownload, {reliable: true, token: 1234, connection});
  Object.assign(t.context, {setTimeout: () => 1, clearTimeout() {}, updateDownloadProgress() {}});
  vm.runInContext(reliableSource, t.context);
  return {...t, connection, acknowledgments, receive: async (seq, kind = 0, content, token = 1234, from = connection) => {
    const payload = content ?? data.subarray((seq - 1) * 96, seq * 96).toString('base64');
    t.context.parseReliableFileChunk(`$FILECHUNK,${token},${seq},${data.length},${kind},${payload}`, from);
    await connection.fileAckWrites;
  }};
}

test('browser ACK protocol recovers dropped chunks 12 and 15 for download and preview', async () => {
  const data = Buffer.concat(Array.from({length: 12}, () => csv));
  for (const preview of [false, true]) {
    const t = reliableTransfer(data, preview);
    const total = Math.ceil(data.length / 96);
    for (let seq = 1; seq <= total; ++seq) {
      // Model one chunk in flight: a dropped packet produces no ACK, so the
      // sender must repeat that same sequence before advancing the SD reader.
      if (![12, 15].includes(seq)) await t.receive(seq);
      if (!t.acknowledgments.some(ack => ack.seq === seq)) await t.receive(seq);
      assert.ok(t.acknowledgments.some(ack => ack.seq === seq));
    }
    await t.receive(total + 1, 1, 'wardrive-0001.csv');
    assert.equal(t.errors.length, 0);
    const artifacts = preview ? t.previews : t.saved;
    assert.equal(artifacts.length, 1);
    assert.deepEqual(Buffer.from(await artifacts[0].blob.arrayBuffer()), data);
    // Lost completion ACK: repeat completion, but don't save/preview twice.
    await t.receive(total + 1, 1, 'wardrive-0001.csv');
    assert.equal(artifacts.length, 1);
    assert.equal(t.acknowledgments.at(-1).seq, total + 1);
  }
});

test('lost browser ACK retries the same chunk without duplicating its bytes', async () => {
  const t = reliableTransfer(csv);
  const write = t.connection.cmdCh.writeValue;
  t.connection.cmdCh.writeValue = async () => { throw new Error('GATT busy'); };
  await t.receive(1);
  assert.equal(t.acknowledgments.length, 0);
  assert.equal(t.chunks.size, 1);
  t.connection.cmdCh.writeValue = write;
  await t.receive(1);
  assert.equal(t.acknowledgments.length, 1);
  assert.equal(t.chunks.size, 1);
});

test('bad chunk, foreign bridge, stale token, and incomplete completion are not ACKed', async () => {
  const t = reliableTransfer(csv);
  await t.receive(1, 0, '%%%');
  await t.receive(1, 0, undefined, 1234, {connected: true});
  assert.equal(t.acknowledgments.length, 0);
  await t.receive(1);
  await t.receive(2, 0, undefined, 9999);
  await t.receive(Math.ceil(csv.length / 96) + 1, 1, 'wardrive-0001.csv');
  assert.equal(t.acknowledgments.length, 1);
  assert.equal(t.saved.length, 0);
});

test('new request sends its token in little endian and rejects stale first chunks', async () => {
  const t = reliableTransfer(csv);
  const writes = [];
  t.connection.cmdCh.writeValue = async bytes => writes.push(bytes);
  Object.assign(t.context, {TGT_SCREEN: 1, crypto: {getRandomValues: a => {a[0] = 0x12345678;}}});
  t.context.activeFileDownload.index = 7;
  t.context.requestReliableFile(t.context.activeFileDownload);
  await t.connection.fileAckWrites;
  assert.deepEqual(Array.from(writes[0]), [68, 7, 1, 0x78, 0x56, 0x34, 0x12]);
  await t.receive(1, 0, undefined, 1234); // previous transfer, before any current data
  assert.equal(t.chunks.size, 0);
  await t.receive(1, 0, undefined, 0x12345678);
  assert.equal(t.chunks.size, 1);
});

test('reliable transfer handles an empty file and acknowledges terminal errors', async () => {
  const t = reliableTransfer(Buffer.alloc(0));
  await t.receive(1);
  await t.receive(2, 1, 'wardrive-0001.csv');
  assert.equal(t.saved[0].blob.size, 0);
  const failed = reliableTransfer(csv);
  await failed.receive(2, 2, 'Cannot open file');
  assert.match(failed.errors[0], /Cannot open file/);
  assert.equal(failed.acknowledgments[0].seq, 2);
  await failed.receive(2, 2, 'Cannot open file');
  assert.equal(failed.errors.length, 1);
  assert.equal(failed.acknowledgments.length, 2);
});

const resultStart = html.indexOf('function parseResult(dv, c){');
const resultEnd = html.indexOf('// Parse live WiGLE', resultStart);
const fileParserStart = html.indexOf('function parseFileMessage(');
const fileParserEnd = html.indexOf('\nif ($("connect-serial"))', fileParserStart);
assert.ok(resultStart >= 0 && resultEnd > resultStart && fileParserEnd > fileParserStart);

test('full BLE notification parser preserves files around 100 KiB and above 1 MiB', async () => {
  for (const size of [100 * 1024 - 1, 100 * 1024 + 1, 1024 * 1024 + 17]) {
    const data = Buffer.alloc(size);
    for (let i = 0; i < size; ++i) data[i] = (i * 17 + 31) & 255;
    const t = reliableTransfer(data);
    vm.runInContext(html.slice(resultStart, resultEnd) + html.slice(fileParserStart, fileParserEnd), t.context);
    const total = Math.ceil(size / 96);
    const receiveNotification = async (seq, kind, payload) => {
      // The actual source-prefixed notification format, including seq digit
      // transitions at 999/1000 and 9999/10000, routed through both dispatchers.
      const line = `$FILECHUNK,1234,${seq},${size},${kind},${payload}`;
      const bytes = Buffer.concat([Buffer.from([9]), Buffer.from(line)]);
      t.context.parseResult(new DataView(bytes.buffer, bytes.byteOffset, bytes.length), t.connection);
      await t.connection.fileAckWrites;
    };
    for (let seq = 1; seq <= total; ++seq) {
      const payload = data.subarray((seq - 1) * 96, seq * 96).toString('base64');
      if (![12, 15, 1000, 1024, 4096, 10000].includes(seq)) {
        await receiveNotification(seq, 0, payload);
      }
      if (t.acknowledgments.at(-1)?.seq !== seq) await receiveNotification(seq, 0, payload);
      assert.equal(t.acknowledgments.at(-1).seq, seq);
      if (seq % 1000 === 0) await receiveNotification(seq, 0, payload); // lost ACK -> duplicate
    }
    await receiveNotification(total + 1, 1, 'wardrive-0001.csv');
    assert.deepEqual(t.errors, []);
    assert.equal(t.saved.length, 1);
    assert.deepEqual(Buffer.from(await t.saved[0].blob.arrayBuffer()), data);
  }
});

const crcStart = html.indexOf('function fileCrcUpdate(');
const crcSource = html.slice(crcStart, start);
const pauseStart = html.indexOf('function sendFileAbort(');
const pauseEnd = html.indexOf('function deleteFileRemote(', pauseStart);

function verifiedTransfer(data, preview = false) {
  const t = reliableTransfer(data, preview);
  const writes = [], elements = new Map();
  const timers = [];
  t.connection.id = 'same-bridge';
  Object.assign(t.context, {
    $: id => { if (!elements.has(id)) elements.set(id, {style:{}}); return elements.get(id); },
    TGT_SCREEN: 1, serialConnected: false, activeConn: () => t.connection,
    reconnectConn: async c => { c.connected = true; },
    sdFileList: [{index:7,name:'wardrive-0001.csv'}],
    crypto: {getRandomValues: a => { a[0] = (t.context.activeFileDownload?.token || 1234) + 1; }},
    setTimeout: callback => {timers.push(callback); return timers.length;},
  });
  const original = t.connection.cmdCh.writeValue;
  t.connection.cmdCh.writeValue = async bytes => {writes.push(bytes); if(bytes[0] === 69) await original(bytes);};
  Object.assign(t.context.activeFileDownload, {verified:true,deviceId:'same-bridge',index:7,state:'requesting',size:data.length});
  vm.runInContext(crcSource + html.slice(pauseStart,pauseEnd),t.context);
  const crc = (t.context.fileCrcUpdate(0xffffffff, data) ^ 0xffffffff) >>> 0;
  const manifest = crc.toString(16).padStart(8,'0') + ',wardrive-0001.csv';
  return {...t,writes,elements,timers,crc,manifest,
    begin: () => t.receive(0xffffffff,3,manifest,t.context.activeFileDownload.token),
    chunk: seq => t.receive(seq,0,undefined,t.context.activeFileDownload.token),
    done: () => t.receive(Math.ceil(data.length/96)+1,4,manifest,t.context.activeFileDownload.token)};
}

test('CRC32 standard vectors match and verified download rejects same-length corruption', async () => {
  const t = verifiedTransfer(csv);
  assert.equal((t.context.fileCrcUpdate(0xffffffff,Buffer.from('123456789')) ^ 0xffffffff) >>> 0,0xcbf43926);
  await t.begin();
  for(let seq=1;seq<=Math.ceil(csv.length/96);seq++) await t.chunk(seq);
  const corrupt=Buffer.from(csv.subarray(0,96));corrupt[5]^=1;
  t.chunks.set(1,corrupt.toString('base64'));
  await t.done();
  assert.equal(t.saved.length,0);
  assert.equal(t.context.activeFileDownload.state,'paused');
  assert.equal(t.context.activeFileDownload.needsRestart,true);
  assert.match(t.elements.get('file-progress-state').textContent,/CRC32/);
});
test('verified resume retains >100 KiB prefix and requests only the missing suffix', async () => {
  const data=Buffer.alloc(130*1024+17);for(let i=0;i<data.length;i++)data[i]=i%251;
  for(const preview of [false,true]) {
    const t=verifiedTransfer(data,preview);await t.begin();
    for(let seq=1;seq<=1100;seq++)await t.chunk(seq);
    t.connection.connected=false;t.context.pauseFileDownload('Disconnected',false);
    assert.equal(t.chunks.size,1100);
    await t.context.resumeFileDownload();await t.connection.fileAckWrites;
    const request=t.writes.findLast(bytes=>bytes[0]===70),view=new DataView(request.buffer);
    assert.equal(request.length,19);assert.equal(view.getUint32(7,true),1101);
    assert.equal(view.getUint32(11,true),data.length);assert.equal(view.getUint32(15,true),t.crc);
    await t.begin();
    for(let seq=1101;seq<=Math.ceil(data.length/96);seq++)await t.chunk(seq);
    await t.done();
    const artifacts=preview?t.previews:t.saved;
    assert.deepEqual(Buffer.from(await artifacts[0].blob.arrayBuffer()),data);
    assert.equal(t.context.activeFileDownload,null);
  }
});
test('resume chooses the first gap, tolerates duplicate manifest and re-ACKs completion', async () => {
  const t=verifiedTransfer(csv);await t.begin();await t.chunk(1);await t.chunk(3);
  t.context.pauseFileDownload('Pause',false);await t.context.resumeFileDownload();await t.connection.fileAckWrites;
  assert.equal(new DataView(t.writes.findLast(x=>x[0]===70).buffer).getUint32(7,true),2);
  await t.begin();await t.begin();
  for(let seq=2;seq<=Math.ceil(csv.length/96);seq++)await t.chunk(seq);
  const token=t.context.activeFileDownload.token;await t.done();
  await t.receive(Math.ceil(csv.length/96)+1,4,t.manifest,token);
  assert.equal(t.saved.length,1);
});
test('changed snapshot forces restart; wrong filename retains original chunks', async () => {
  const t=verifiedTransfer(csv);await t.begin();await t.chunk(1);
  await t.receive(0xffffffff,3,'00000001,wardrive-0001.csv');
  assert.equal(t.context.activeFileDownload.needsRestart,true);
  await t.context.resumeFileDownload(true);await t.connection.fileAckWrites;
  assert.equal(t.chunks.size,0);
  assert.equal(new DataView(t.writes.findLast(x=>x[0]===70).buffer).getUint32(7,true),0);
  const other=verifiedTransfer(csv);await other.begin();await other.chunk(1);
  await other.receive(0xffffffff,3,other.manifest.replace('0001','0002'));
  assert.equal(other.context.activeFileDownload.state,'paused');assert.equal(other.chunks.size,1);
});
test('pause ignores late data, wrong bridge cannot resume, timeout retains partial file', async () => {
  const t=verifiedTransfer(csv);await t.begin();await t.chunk(1);
  t.timers.at(-1)();assert.equal(t.chunks.size,1);assert.equal(t.context.activeFileDownload.state,'paused');
  await t.chunk(2);assert.equal(t.chunks.size,1);
  t.context.activeConn=()=>({id:'other',connected:true});await t.context.resumeFileDownload();
  assert.ok(!t.writes.some(x=>x[0]===70));
});
test('all-data resume requests completion only and empty snapshots verify', async () => {
  for(const data of [csv,Buffer.alloc(0)]) {
    const t=verifiedTransfer(data);await t.begin();
    const total=Math.ceil(data.length/96)||1;
    for(let seq=1;seq<=total;seq++)await t.chunk(seq);
    t.context.pauseFileDownload('Pause before done',false);await t.context.resumeFileDownload();await t.connection.fileAckWrites;
    assert.equal(new DataView(t.writes.findLast(x=>x[0]===70).buffer).getUint32(7,true),total+1);
    await t.begin();
    await t.receive(total+1,4,t.manifest,t.context.activeFileDownload.token);
    assert.equal(t.saved[0].blob.size,data.length);
  }
});
