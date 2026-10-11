import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const html=readFileSync(new URL('../../website/public/control.html',import.meta.url),'utf8');
const source=html.slice(html.indexOf('let serialWardriveSession = null;'),html.indexOf('// ---- per-bridge Wi-Fi list & Wardrive parsing'));
function harness() {
  const elements=new Map(),connection={name:'Test bridge',connected:true};let now=100000;
  const context=vm.createContext({$:id=>{if(!elements.has(id))elements.set(id,{});return elements.get(id);},
    activeConn:()=>connection,target:1,serialConnected:false,setInterval(){},formatBytes:n=>n+' B',Date:{now:()=>now}});
  vm.runInContext(source,context);
  return {context,connection,elements,tick:n=>{now+=n;context.renderWardriveDashboard();},read:id=>elements.get('wd-session-'+id)?.textContent,
    receive:(changes={},from=connection)=>{const row=['$WDSTAT',1,1,123,1,600,4125,96,800,120,75,10,0.9,2,900,200000,2,'2026-09-23 20:15:00','GMT-04:00 DST','wardrive-0001.csv',0,1,45.3,1];for(const [i,v]of Object.entries(changes))row[i]=v;context.parseWardriveStatus(row.join(','),from);}};
}
test('dashboard shows device counts, local time and actual SD state independently of browser rows',()=>{
  const t=harness();t.receive();assert.equal(t.read('wifi'),'800');assert.equal(t.read('ble'),'120');
  assert.equal(t.read('elapsed'),'00:10:00');assert.equal(t.read('distance'),'4.13 km');
  assert.equal(t.read('state'),'Recording');assert.equal(t.read('storage'),'Flushed');
  assert.match(t.read('clock'),/20:15:00 local/);assert.match(t.read('zone'),/DST/);
});
test('unknown, stale, disconnected, stopped and failed storage never claim healthy recording',()=>{
  const t=harness();t.context.renderWardriveDashboard();assert.equal(t.read('state'),'Waiting for telemetry');
  t.receive({13:0});assert.equal(t.read('state'),'NO SD RECORDING');
  t.receive({13:3});assert.equal(t.read('state'),'SD WRITE FAILED');
  t.receive({21:0});assert.equal(t.read('state'),'Waiting for GPS');
  t.receive({20:2,21:0,13:1,14:0});assert.equal(t.read('state'),'Waiting for coordinator GPS');
  t.receive({20:2,21:1,13:2,14:4});assert.equal(t.read('state'),'Recording');
  t.receive({20:3,21:0,13:4});assert.equal(t.read('state'),'Relaying to coordinator');
  assert.equal(t.read('gps'),'Coordinator provides location');
  t.receive();t.tick(21000);assert.equal(t.read('state'),'Telemetry stale');assert.equal(t.read('gps'),'Unknown (stale)');
  t.receive({4:0});assert.equal(t.read('state'),'Stopped');
  t.connection.connected=false;t.tick(1);assert.equal(t.read('state'),'Telemetry stale');
});
test('bridge/screen sessions remain separate, workers do not pretend to save SD, malformed rows ignored',()=>{
  const t=harness();t.receive();t.receive({2:0,8:20});assert.equal(t.read('wifi'),'800');
  t.context.target=0;t.context.renderWardriveDashboard();assert.equal(t.read('wifi'),'20');
  t.receive({2:0,13:4,20:3,23:6});assert.equal(t.read('storage'),'Relay to coordinator');
  assert.match(t.read('file'),/coordinator/);
  t.receive({2:0,8:-100});assert.equal(t.read('wifi'),'800');
  t.context.parseWardriveStatus('$WDSTAT,1,1',t.connection);assert.equal(t.read('wifi'),'800');
});
test('serial dashboard uses its own telemetry and does not combine another bridge',()=>{
  const t=harness();t.receive();t.context.serialConnected=true;t.receive({8:123},null);assert.equal(t.read('wifi'),'123');
});
