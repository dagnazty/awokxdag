// Exercise real settings/save/load/menu control flow with NVS and UI mocks.
// No firmware or C++ build is performed.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const source=readFileSync(new URL('../../AWOKxDAG/settings.ino',import.meta.url),'utf8');
function fn(name) {
  const start=source.search(new RegExp('(?:void|bool|int) '+name+'\\('));
  assert.notEqual(start,-1,name);
  let end=source.indexOf('{',start)+1,depth=1;
  while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}
  return source.slice(start,end);
}
function harness() {
  const actions=[];let persisted=null;
  const c=vm.createContext({String,settingsGroup:0,settingsEdit:-1,settingsChoice:-1,settingsResetConfirm:false,
    settingsNotice:'',lastSettingsWriteOk:true,gpsRawEcho:false,gpsDiagnosticsFromSettings:false,
    deviceSettings:{version:1,gpsBaud:115200,backlightTimeoutMs:0,brightnessPercent:100,flags:0},
    kDeviceSettingsVersion:1,kSettingSkipSplash:2,kSettingConfirmAttacks:1,kSettingNmeaEcho:4,
    kBacklightTimeoutOptions:[0,15000,30000,60000,120000,300000],kBacklightTimeoutOptionCount:6,
    kBrightnessOptions:[20,40,60,80,100],kBrightnessOptionCount:5,
    kGpsBaudOptions:[115200,9600,38400,57600,4800],kGpsBaudOptionCount:5,
    attackConfirmLabel:['x'],canOpen:true,writeLength:20,recordFirmwareAudit:(...args)=>actions.push(['audit',...args]),
    Serial:{printf(){},println(){}},noteActivity:()=>actions.push(['activity']),
    setBacklightLit:on=>actions.push(['backlight',on]),
    applyGpsBaud:(baud,persist)=>{c.deviceSettings.gpsBaud=baud;actions.push(['baud',baud,persist]);},
    deviceSettingsDefaults:out=>Object.assign(out,{version:1,gpsBaud:115200,backlightTimeoutMs:0,brightnessPercent:100,flags:0}),
    drawSettings:()=>actions.push(['drawSettings']),drawStatus:()=>actions.push(['status']),drawHome:()=>actions.push(['home']),
    drawGpsDiagnostics:()=>actions.push(['gpsDiagnostics']),startScreenTest:()=>actions.push(['screenTest']),
    Preferences:class {
      begin(){return c.canOpen;}
      putBytes(key,record,size){actions.push(['write',key,size]);if(c.writeLength===size)persisted={...record};return c.writeLength;}
      isKey(){return persisted!==null;}
      getBytesLength(){return 20;}
      getBytes(key,out){Object.assign(out,persisted);return 20;}
      end(){}
    }});
  const names=['settingsHit','settingFlag','setSettingFlag','gpsBaudIsKnown','settingsChoiceCount',
    'settingsCurrentChoice','beginSettingsEdit','applySettingsChoice','saveDeviceSettings','loadDeviceSettings',
    'resetDeviceSettings','openSettings','handleSettingsTouch'];
  let adapted=names.map(fn).join('\n')
    // Drop the classic-Mini-only branches, keeping the default (#else) path.
    .replace(/#ifdef AWOK_CLASSIC_MINI_WIRING[\s\S]*?#else\n/g,'')
    .replace(/#ifdef AWOK_CLASSIC_MINI_WIRING[\s\S]*?#endif\n/g,'')
    .replace(/#endif\n/g,'')
    .replace(/^(?:void|bool|int) (\w+)\(([^)]*)\)/gm,(_,name,args)=>`function ${name}(${args.replace(/unsigned long |uint8_t |bool |int /g,'')})`)
    .replace(/const (?:bool|int) /g,'const ').replace(/\b(?:int|bool) (\w+)\s*=/g,'let $1 =')
    .replace(/static_cast<uint8_t>\(([^)]*)\)/g,'($1 & 255)')
    .replaceAll('Preferences preferences;','const preferences = new Preferences();')
    .replaceAll('DeviceSettingsRecord loaded = {};','const loaded = {};')
    .replaceAll('static_cast<unsigned long>', 'Number')
    .replaceAll('AwokPins::kGpsBaud','115200').replaceAll('unsigned(', 'Number(')
    .replaceAll('&deviceSettings','deviceSettings').replaceAll('&loaded','loaded')
    .replace(/sizeof\((?:deviceSettings|loaded)\)/g,'20')
    .replace('i / 2 : i','Math.floor(i / 2) : i');
  vm.runInContext(adapted,c);
  return {c,actions,tap:(x,y)=>c.handleSettingsTouch(x,y),saved:()=>persisted,writes:()=>actions.filter(a=>a[0]==='write')};
}
test('all groups open from root; margins do nothing; Back returns one level',()=>{
  for(let group=1;group<=4;group++){
    const t=harness();t.tap(120,66+(group-1)*44);assert.equal(t.c.settingsGroup,group);
    t.tap(60,298);assert.equal(t.c.settingsGroup,0);t.tap(60,298);assert.equal(t.actions.at(-1)[0],'home');
  }
  const t=harness();t.tap(2,60);t.tap(120,88);assert.equal(t.c.settingsGroup,0);assert.equal(t.writes().length,0);
});
test('selecting or cancelling brightness does not apply or persist; Save applies exactly once',()=>{
  const t=harness();t.c.settingsGroup=1;t.tap(120,126);assert.equal(t.c.settingsEdit,1);
  t.tap(62,124);assert.equal(t.c.settingsChoice,0);assert.equal(t.c.deviceSettings.brightnessPercent,100);
  t.tap(60,298);assert.equal(t.c.settingsEdit,-1);assert.equal(t.writes().length,0);
  t.tap(120,126);t.tap(62,178);t.tap(180,298);
  assert.equal(t.c.deviceSettings.brightnessPercent,60);assert.equal(t.writes().length,1);
  assert.equal(t.saved().brightnessPercent,60);assert.equal(t.c.settingsGroup,1);
  t.c.deviceSettings.brightnessPercent=100;t.c.loadDeviceSettings();assert.equal(t.c.deviceSettings.brightnessPercent,60);
});
test('each explicit choice saves the selected value, including splash inversion and NMEA sync',()=>{
  const cases=[[0,5,'backlightTimeoutMs',300000],[1,0,'brightnessPercent',20],[2,1,'gpsBaud',9600],
    [3,1,'flags',2],[4,0,'flags',1],[5,1,'flags',4]];
  for(const [item,choice,field,value]of cases){
    const t=harness();t.c.beginSettingsEdit(item);t.c.settingsChoice=choice;t.c.applySettingsChoice();
    assert.equal(t.c.deviceSettings[field],value);assert.equal(t.saved()[field],value);assert.equal(t.writes().length,1);
    if(item===2)assert.deepEqual(t.actions.find(a=>a[0]==='baud'),['baud',9600,false]);
    if(item===5)assert.equal(t.c.gpsRawEcho,true);
  }
});
test('non-preset valid brightness is preserved until a choice is made; gaps cannot select a value',()=>{
  const t=harness();t.c.deviceSettings.brightnessPercent=75;t.c.beginSettingsEdit(1);
  assert.equal(t.c.settingsChoice,-1);t.tap(180,298);assert.equal(t.writes().length,0);
  t.tap(120,124);t.tap(62,150);assert.equal(t.c.settingsChoice,-1);
  t.tap(60,298);assert.equal(t.c.deviceSettings.brightnessPercent,75);
});
test('failed writes keep applied RAM values, expose Retry save, and can persist on retry',()=>{
  for(const fail of ['open','short']){
    const t=harness();t.c.canOpen=fail!=='open';t.c.writeLength=fail==='short'?2:20;
    t.c.beginSettingsEdit(1);t.c.settingsChoice=1;t.c.applySettingsChoice();
    assert.equal(t.c.lastSettingsWriteOk,false);assert.equal(t.c.deviceSettings.brightnessPercent,40);
    assert.match(t.c.settingsNotice,/RAM only/);assert.equal(t.saved(),null);
    t.c.canOpen=true;t.c.writeLength=20;t.tap(180,298);assert.equal(t.c.lastSettingsWriteOk,true);
    assert.equal(t.saved().brightnessPercent,40);assert.match(t.c.settingsNotice,/Saved/);
  }
});
test('defaults require a different confirmation target; Cancel performs no write',()=>{
  const t=harness();t.c.deviceSettings.brightnessPercent=40;t.tap(120,242);
  assert.equal(t.c.settingsResetConfirm,true);assert.equal(t.writes().length,0);
  t.tap(120,242);assert.equal(t.writes().length,0); // repeated tap at old button
  t.tap(120,298);assert.equal(t.c.settingsResetConfirm,false);assert.equal(t.c.deviceSettings.brightnessPercent,40);
  t.tap(120,242);t.tap(120,198);assert.equal(t.c.deviceSettings.brightnessPercent,100);
  assert.equal(t.c.settingsResetConfirm,false);assert.equal(t.writes().length,1);
  assert.equal(t.c.gpsRawEcho,false);
});
test('diagnostics launch without changing preferences; opening Settings resets only navigation',()=>{
  const t=harness();t.c.settingsGroup=4;t.tap(120,74);assert.equal(t.c.gpsDiagnosticsFromSettings,true);
  assert.equal(t.actions.at(-1)[0],'gpsDiagnostics');t.tap(120,178);assert.equal(t.actions.at(-1)[0],'screenTest');
  assert.equal(t.c.settingsGroup,4);assert.equal(t.writes().length,0);
  t.c.openSettings();assert.equal(t.c.settingsGroup,0);assert.equal(t.c.settingsEdit,-1);
  assert.equal(t.writes().length,0);
});

test('every sleep-choice button center works for Mini actions and Touch without writing early',()=>{
  for(let choice=0;choice<6;choice++){
    const t=harness();t.c.beginSettingsEdit(0);
    t.tap(8+(choice%2)*116+54,102+Math.floor(choice/2)*54+22);
    assert.equal(t.c.settingsChoice,choice);assert.equal(t.writes().length,0);
    t.tap(180,298);assert.equal(t.saved().backlightTimeoutMs,t.c.kBacklightTimeoutOptions[choice]);
  }
});
test('saving the current GPS baud does not restart the receiver',()=>{
  const t=harness();t.c.beginSettingsEdit(2);t.c.applySettingsChoice();
  assert.equal(t.actions.filter(a=>a[0]==='baud').length,0);assert.equal(t.writes().length,1);
});

test('live GPS summary refresh never interrupts editing; screen-test return preserves Diagnostics',()=>{
  const t=harness();t.c.now=2000;t.c.millis=()=>t.c.now;t.c.currentView='settings';t.c.settingsGroup=2;
  let update=fn('updateSettingsPage').replace('void updateSettingsPage()', 'function updateSettingsPage()')
    .replace('static uint32_t lastDrawMs = 0;', '').replace('View::kSettings', '"settings"');
  vm.runInContext('let lastDrawMs=0;\n'+update,t.c);
  t.c.updateSettingsPage();assert.equal(t.actions.at(-1)[0],'drawSettings');
  t.actions.length=0;t.c.beginSettingsEdit(2);t.actions.length=0;t.c.now=4000;
  t.c.updateSettingsPage();assert.equal(t.actions.length,0);assert.equal(t.c.settingsEdit,2);
  const screen=readFileSync(new URL('../../AWOKxDAG/screentest.ino',import.meta.url),'utf8');
  const stop=screen.match(/void stopScreenTest\(\) \{[^}]*\}/)[0].replace('void stopScreenTest()', 'function stopScreenTest()');
  t.c.finishScreenTest=()=>t.actions.push(['finishScreenTest']);t.c.settingsGroup=4;t.c.settingsEdit=-1;
  vm.runInContext(stop,t.c);t.c.stopScreenTest();
  assert.deepEqual(t.actions.map(a=>a[0]),['finishScreenTest','drawSettings']);assert.equal(t.c.settingsGroup,4);
});
