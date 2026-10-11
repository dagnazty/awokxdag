// Execute menu routing from the firmware with mocked radios/UI; no compilation.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const gps=readFileSync(new URL('../../AWOKxDAG/gps.ino',import.meta.url),'utf8');
const link=readFileSync(new URL('../../AWOKxDAG/link.ino',import.meta.url),'utf8');
function fn(source,name) {
  const start=source.search(new RegExp('(?:const char\\*|void|bool) '+name+'\\('));
  assert.notEqual(start,-1,name);
  let end=source.indexOf('{',start)+1,depth=1;
  while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}
  return source.slice(start,end);
}
function harness(mini=false) {
  const calls=[];
  const c=vm.createContext({gpsMenuPage:0,gpsDiagnosticsFromSettings:false,gpsMenuNotice:'',gpsStarted:true,gpsDataSeen:true,gpsLastDataMs:100,
    now:100,fix:false,gpsHasFix:()=>c.fix,millis:()=>c.now,
    wardriveActive:false,linkWardriveActive:false,fleetActive:false,fleetListening:false,fleetWardriveOn:false,
    fleetCoordinator:false,fleetMenuOpen:false,linkState:0,kLinkOff:0,kLinkDiscovering:1,kLinkAwaitConfirm:2,kLinkReady:3,
    linkPartnerLastSeenMs:100,kLinkPeerTimeoutMs:5000,kSettingNmeaEcho:64});
  const names=['drawHome','drawSettings','startWardrive','drawWardrive','openLinkWardrive','linkEnsureEspNow','drawLinkWardrive',
    'cycleGpsBaud','toggleSettingFlag','fleetLeave','fleetStartWardrive','fleetStopWardrive','fleetArm',
    'startLinkWardrive','stopLinkWardrive','linkCancelPairing','linkConfirm','linkUnpair','linkStartDiscovery'];
  for(const name of names)c[name]=(...args)=>calls.push([name,...args]);
  for(const [name,page]of [['drawGps',0],['drawGpsDiagnostics',1],['drawDriveMenu',2]])
    c[name]=()=>{c.gpsMenuPage=page;calls.push([name]);};
  let script=['gpsMenuHit','gpsReceptionLabel','driveModeState','selectDriveMode','openDriveMenu','redrawGpsPage','handleGpsTouch']
    .map(name=>fn(gps,name)).join('\n')+'\n'+fn(link,'handleLinkWardriveTouch');
  script=script.replace(/#ifdef AWOK_MINI_DISPLAY\n([\s\S]*?)#else\n([\s\S]*?)#endif/g,(_,a,b)=>mini?a:b)
    .replace(/^(?:const char\*|void|bool) (\w+)\(([^)]*)\)/gm,(_,name,args)=>`function ${name}(${args.replace(/int /g,'')})`)
    .replace('gpsMenuNotice.length()','gpsMenuNotice.length');
  vm.runInContext(script,c);
  return {c,calls,actions:()=>calls.map(a=>a[0]),tap:(x,y)=>c.handleGpsTouch(x,y),linkTap:(x,y)=>c.handleLinkWardriveTouch(x,y)};
}
test('GPS status distinguishes no data, stale data, acquiring and valid fix',()=>{
  const t=harness();t.c.gpsDataSeen=false;assert.equal(t.c.gpsReceptionLabel(),'NO GPS DATA');
  t.c.gpsDataSeen=true;assert.equal(t.c.gpsReceptionLabel(),'SEARCHING');
  t.c.fix=true;assert.equal(t.c.gpsReceptionLabel(),'FIX ACQUIRED');
  t.c.now=5100;assert.equal(t.c.gpsReceptionLabel(),'DATA STALE');
});
test('Touch and Mini overview actions reach Diagnostics and Drive; diagnostics refresh stays put',()=>{
  for(const mini of [false,true]){
    const t=harness(mini);t.tap(mini?200:120,mini?298:249);assert.equal(t.c.gpsMenuPage,1);
    t.tap(120,200);assert.deepEqual(t.actions().slice(-2),['cycleGpsBaud','drawGpsDiagnostics']);
    t.tap(120,248);assert.equal(t.calls.at(-2)[0],'toggleSettingFlag');
    t.c.redrawGpsPage();assert.equal(t.c.gpsMenuPage,1);
    t.tap(60,298);assert.equal(t.c.gpsMenuPage,0);
    t.tap(mini?120:180,298);assert.equal(t.c.gpsMenuPage,2);
    const count=t.calls.length;t.tap(2,70);t.tap(120,110);assert.equal(t.calls.length,count);
    t.c.redrawGpsPage();assert.equal(t.c.gpsMenuPage,2);
  }
});
test('Solo uses existing Wi-Fi/BLE start; Split opens pairing setup; Fleet opens role choice',()=>{
  const solo=harness();solo.c.gpsMenuPage=2;solo.tap(120,70);assert.deepEqual(solo.actions(),['startWardrive']);
  const split=harness();split.c.selectDriveMode(1);assert.deepEqual(split.actions(),['openLinkWardrive']);
  const fleet=harness();fleet.c.selectDriveMode(2);assert.equal(fleet.c.fleetMenuOpen,true);
  assert.deepEqual(fleet.actions(),['linkEnsureEspNow','drawLinkWardrive']);
});
test('active sessions are reopened without restart and mode changes are blocked',()=>{
  for(const [state,mode,resume]of [['wardriveActive',0,'drawWardrive'],['linkWardriveActive',1,'openLinkWardrive'],['fleetActive',2,'drawLinkWardrive'],['fleetListening',2,'drawLinkWardrive']]){
    const t=harness();t.c[state]=true;t.c.selectDriveMode(mode);assert.deepEqual(t.actions(),[resume]);
    t.calls.length=0;t.c.selectDriveMode((mode+1)%3);assert.deepEqual(t.actions(),['drawDriveMenu']);
    assert.match(t.c.gpsMenuNotice,/Stop|Leave/);
  }
  const t=harness();t.c.linkState=t.c.kLinkAwaitConfirm;t.c.selectDriveMode(0);assert.match(t.c.gpsMenuNotice,/Cancel/);
  t.c.linkState=t.c.kLinkReady;t.c.selectDriveMode(2);assert.match(t.c.gpsMenuNotice,/Unpair/);
});
test('Split Pair is reachable, confirmation is explicit, and unpaired Wi-Fi remains available',()=>{
  const t=harness();t.linkTap(120,120);assert.deepEqual(t.actions(),['linkStartDiscovery']);
  t.calls.length=0;t.linkTap(120,205);assert.deepEqual(t.actions(),['startLinkWardrive']);
  t.c.linkState=t.c.kLinkDiscovering;t.linkTap(120,298);assert.deepEqual(t.actions().slice(-2),['linkCancelPairing','drawLinkWardrive']);
  t.c.linkState=t.c.kLinkAwaitConfirm;t.linkTap(180,298);assert.equal(t.actions().at(-1),'linkConfirm');
  t.c.linkState=t.c.kLinkReady;t.linkTap(120,238);assert.equal(t.actions().at(-1),'startLinkWardrive');
  t.linkTap(180,298);assert.deepEqual(t.actions().slice(-2),['linkUnpair','drawLinkWardrive']);
});
test('Fleet role menu and active Home/Leave/Stop preserve existing behavior',()=>{
  const t=harness();t.c.fleetMenuOpen=true;t.linkTap(120,120);assert.deepEqual(t.actions(),['fleetStartWardrive','drawLinkWardrive']);
  t.linkTap(120,205);assert.deepEqual(t.actions().slice(-2),['fleetArm','drawLinkWardrive']);
  t.c.fleetActive=t.c.fleetCoordinator=t.c.fleetWardriveOn=true;
  t.calls.length=0;t.linkTap(40,299);assert.deepEqual(t.actions(),['drawHome']);
  t.linkTap(200,299);assert.deepEqual(t.actions().slice(-2),['fleetStopWardrive','drawLinkWardrive']);
  t.linkTap(120,299);assert.deepEqual(t.actions().slice(-2),['fleetLeave','drawDriveMenu']);
});
test('mode status distinguishes stopped, paired, joining and lost partner; Split stops once',()=>{
  const t=harness();assert.equal(t.c.driveModeState(),'No drive running');
  t.c.linkState=t.c.kLinkReady;assert.equal(t.c.driveModeState(),'Split: paired / stopped');
  t.c.linkWardriveActive=true;t.c.now=6000;assert.equal(t.c.driveModeState(),'Split: partner lost');
  t.linkTap(60,299);assert.deepEqual(t.actions(),['stopLinkWardrive','drawLinkWardrive']);
  t.c.fleetActive=true;assert.equal(t.c.driveModeState(),'Fleet: stopped / still joined');
  t.c.fleetActive=false;t.c.fleetListening=true;assert.equal(t.c.driveModeState(),'Fleet: waiting to join');
});

test('GPS and Link body controls dispatch before the generic footer-only gate',()=>{
  const input=readFileSync(new URL('../../AWOKxDAG/input.ino',import.meta.url),'utf8');
  const main=readFileSync(new URL('../../AWOKxDAG/AWOKxDAG.ino',import.meta.url),'utf8');
  for(const route of ['handleGpsTouch(x, y)','handleLinkWardriveTouch(x, y)']) {
    assert.ok(input.indexOf(route)>0);assert.ok(input.indexOf(route)<input.indexOf('if (y < kFooterTop) return;'));
  }
  assert.match(main,/lastGpsScreenDrawMs = millis\(\);\s+redrawGpsPage\(\);/);
});

test('GPS Diagnostics returns to its Settings origin without resetting the group',()=>{
  const t=harness();t.c.gpsMenuPage=1;t.c.gpsDiagnosticsFromSettings=true;
  t.tap(60,298);assert.equal(t.c.gpsDiagnosticsFromSettings,false);
  assert.deepEqual(t.actions(),['drawSettings']);
});
