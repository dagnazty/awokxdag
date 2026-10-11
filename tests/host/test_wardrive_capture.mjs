// Run the firmware's actual dedup and SD flush control flow with tiny tables.
// This does not compile or flash firmware.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const source=readFileSync(new URL('../../AWOKxDAG/gps.ino',import.meta.url),'utf8');
const main=readFileSync(new URL('../../AWOKxDAG/AWOKxDAG.ino',import.meta.url),'utf8');
function fn(name){
  const start=source.search(new RegExp(`(?:void|bool) ${name}\\(`));
  assert.ok(start>=0,name);let end=source.indexOf('{',start)+1,depth=1;
  while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}
  return source.slice(start,end);
}
function dedup(capacity=4){
  const c=vm.createContext({wardriveBloom:new Uint8Array(8),wardriveMacs:Array.from({length:capacity},()=>new Uint8Array(6)),
    wardriveMacCount:0,wardriveMacCursor:0,kWardriveBloomBits:64,kMaxWardriveMacs:capacity,
    // All MACs collide deliberately. Only exact confirmation may mark one seen.
    macHash1:()=>5,macHash2:()=>9,
    memcmp:(a,b,n)=>a.slice(0,n).every((x,i)=>x===b[i])?0:1,
    memcpy:(a,b,n)=>{for(let i=0;i<n;i++)a[i]=b[i];}});
  let code=['wardriveMacSeen','wardriveAddMac','wardriveResetDedup'].map(fn).join('\n')
    .replace(/^(?:bool|void) (\w+)\(const uint8_t\* mac\)/gm,'function $1(mac)')
    .replace('void wardriveResetDedup()','function wardriveResetDedup()')
    .replace(/const uint32_t /g,'const ').replace(/for \(int /g,'for (let ')
    .replace('memset(wardriveBloom, 0, sizeof(wardriveBloom));','wardriveBloom.fill(0);');
  vm.runInContext(code,c);return c;
}
function mac(n){return Uint8Array.from([2,17,34,(n>>>16)&255,(n>>>8)&255,n&255]);}
test('Bloom collisions never discard a new Wi-Fi or BLE address',()=>{
  const c=dedup();const first=mac(1),second=mac(2);
  assert.equal(c.wardriveMacSeen(first),false);
  c.wardriveAddMac(first);
  assert.equal(c.wardriveMacSeen(first),true);
  assert.equal(c.wardriveMacSeen(second),false); // identical Bloom bits
  c.wardriveAddMac(second);
  assert.equal(c.wardriveMacSeen(second),true);
  assert.equal(c.wardriveMacCount,2);
});
test('bounded exact cache retains recent sightings and resets for a new session',()=>{
  const c=dedup();for(let i=0;i<4;i++)c.wardriveAddMac(mac(i));
  assert.equal(c.wardriveMacCount,4);
  for(let i=0;i<4;i++)assert.equal(c.wardriveMacSeen(mac(i)),true);
  c.wardriveAddMac(mac(4));assert.equal(c.wardriveMacCount,4);assert.equal(c.wardriveMacCursor,1);
  assert.equal(c.wardriveMacSeen(mac(0)),false); // old entry can be logged again
  for(let i=1;i<=4;i++)assert.equal(c.wardriveMacSeen(mac(i)),true);
  for(let i=5;i<2000;i++){
    assert.equal(c.wardriveMacSeen(mac(i)),false);
    c.wardriveAddMac(mac(i));
  }
  assert.equal(c.wardriveMacCount,4);assert.equal(c.wardriveMacSeen(mac(1999)),true);
  c.wardriveResetDedup();assert.equal(c.wardriveMacCount,0);assert.equal(c.wardriveMacCursor,0);
  assert.equal(c.wardriveMacSeen(mac(1999)),false);assert.equal(c.wardriveBloom.every(x=>x===0),true);
});
test('SD flush runs only after new rows and reports a write error',()=>{
  let calls=0,failed=false;
  const c=vm.createContext({wardriveCsvReady:true,wardriveStats:{rows:0,flushedRows:0,lastFlushMs:0,didFlush:false,writeError:false},
    g_wardriveFile:{flush:()=>{calls++;},getWriteError:()=>failed},millis:()=>2400});
  vm.runInContext(fn('flushWardriveCsv').replace('void flushWardriveCsv()','function flushWardriveCsv()'),c);
  c.flushWardriveCsv();assert.equal(calls,0);assert.equal(c.wardriveStats.didFlush,false);
  c.wardriveStats.rows=1;c.flushWardriveCsv();assert.equal(calls,1);assert.equal(c.wardriveStats.flushedRows,1);
  assert.equal(c.wardriveStats.lastFlushMs,2400);c.flushWardriveCsv();assert.equal(calls,1);
  c.wardriveStats.rows=2;failed=true;c.flushWardriveCsv();assert.equal(calls,2);
  assert.equal(c.wardriveStats.writeError,true);assert.equal(c.wardriveCsvReady,false);
  c.flushWardriveCsv();assert.equal(calls,2);
});
test('the existing static MAC table is used without changing BLE or radio scheduling',()=>{
  assert.match(main,/wardriveMacs\[kMaxWardriveMacs\]\[6\]/);
  assert.match(source,/memcmp\(wardriveMacs\[i\], mac, 6\)/);
  assert.match(source,/memcpy\(wardriveMacs\[wardriveMacCursor\], mac, 6\)/);
  assert.match(source,/wardriveSched\.bleCallbacks = &wardriveBleCallbacks/);
  assert.match(source,/radioSchedulerTick\(wardriveSched\)/);
});
test('capture pressure counters are reset, updated, and shown on the board',()=>{
  assert.match(main,/std::atomic<int> bleHitHead\{0\};/);
  assert.match(main,/std::atomic<int> bleHitTail\{0\};/);
  assert.match(main,/std::atomic<uint32_t> wardriveBleQueueDrops\{0\}/);
  const callback=source.slice(source.indexOf('class WardriveBleCallbacks'),source.indexOf('WardriveBleCallbacks wardriveBleCallbacks'));
  assert.match(callback,/if \(next == bleHitTail\) \{\s*wardriveBleQueueDrops\.fetch_add\(1, std::memory_order_relaxed\);\s*return;/);
  const start=fn('startWardrive');
  assert.match(start,/wardriveWifiScanRestarts = 0;/);
  assert.match(start,/wardriveBleQueueDrops\.store\(0, std::memory_order_relaxed\);/);
  const update=fn('updateWardrive');
  assert.match(update,/else \{\s*\/\/ not started \/ failed: kick off a scan\s*\+\+wardriveWifiScanRestarts;/);
  assert.match(source,/Wi-Fi retries %lu \| BLE drops %lu/);
});
