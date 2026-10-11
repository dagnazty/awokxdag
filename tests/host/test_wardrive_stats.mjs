// Execute the firmware's sampling control flow with GPS/clock mocks; no build.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const source = readFileSync(new URL('../../AWOKxDAG/gps.ino', import.meta.url), 'utf8');
const sample = source.slice(source.indexOf('void updateWardriveSessionStats()'), source.indexOf('\nString wardriveRecordingLabel()'))
  .replace('void updateWardriveSessionStats()', 'function update()')
  .replace('int wardriveStorageState()', 'function storage()')
  .replace(/const (?:uint32_t|bool|double) /g, 'const ')
  .replace('TinyGPSPlus::distanceBetween', 'distanceBetween');
function harness() {
  let now = 0, fix = true, hdop = 1, speed = 36, position = 0;
  const stats = {elapsedMs:0,sampledMs:0,fixMs:0,distanceM:0,lastPosition:false,
    mode:0,bytes:0,didFlush:false,writeError:false,count(){}};
  const context = vm.createContext({wardriveActive:true,linkWardriveActive:false,wardriveStartMs:0,
    wardriveNetworks:0,wardriveBleCount:0,wardriveCsvReady:true,wardriveStats:stats,
    millis:()=>now,gpsHasFix:()=>fix,distanceBetween:(a,b,c,d)=>Math.abs(c-a),
    gps:{hdop:{isValid:()=>true,age:()=>0,hdop:()=>hdop},
      speed:{isValid:()=>true,age:()=>0,kmph:()=>speed,mps:()=>speed/3.6},
      location:{lat:()=>position,lng:()=>0}}});
  vm.runInContext(sample,context);
  return {context,stats,tick:(ms,metres,options={})=>{
    now+=ms;position=metres;fix=options.fix??true;hdop=options.hdop??1;speed=options.speed??36;context.update();
  }};
}
test('distance counts plausible movement and ignores stationary jitter and GPS jumps',()=>{
  const t=harness();t.tick(1000,0);t.tick(1000,10);assert.equal(t.stats.distanceM,10);
  t.tick(1000,10.5);t.tick(1000,11,{speed:0});assert.equal(t.stats.distanceM,10);
  t.tick(1000,5000);assert.equal(t.stats.distanceM,10);
  assert.equal(t.stats.fixMs,5000);
});
test('fix loss, poor HDOP and long sampling gaps break the distance anchor',()=>{
  for(const options of [{fix:false},{hdop:8}]) {
    const t=harness();t.tick(1000,0);t.tick(1000,10,options);t.tick(1000,20);
    assert.equal(t.stats.distanceM,0);t.tick(1000,30);assert.equal(t.stats.distanceM,10);
  }
  const t=harness();t.tick(1000,0);t.tick(10000,30);t.tick(1000,40);
  assert.equal(t.stats.distanceM,0);assert.equal(t.stats.fixMs,2000);
  t.context.wardriveActive=false;t.tick(1000,50);assert.equal(t.stats.elapsedMs,12000);
});
test('storage preserves errors and worker relay identity after stopping',()=>{
  const t=harness();assert.equal(t.context.storage(),1);
  t.context.wardriveCsvReady=false;assert.equal(t.context.storage(),0);
  t.context.wardriveActive=false;assert.equal(t.context.storage(),0);
  t.stats.bytes=1024;t.stats.didFlush=true;assert.equal(t.context.storage(),2);
  t.stats.writeError=true;assert.equal(t.context.storage(),3);
  t.stats.mode=3;assert.equal(t.context.storage(),4);
});
