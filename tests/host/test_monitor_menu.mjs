// Run the firmware's Monitor menu control flow with UI/radio mocks. No firmware
// build. Monitor now uses the shared card renderer (drawMenuCard): title + a
// detail line, 4 per page, running state shown by a green outline.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const read=name=>readFileSync(new URL(`../../AWOKxDAG/${name}`,import.meta.url),'utf8');
const main=read('AWOKxDAG.ino'), input=read('input.ino');
function block(source,start){
  assert.ok(start>=0);let end=source.indexOf('{',start)+1,depth=1;
  while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}
  return source.slice(start,end);
}
function fn(source,name){return block(source,source.search(new RegExp(`(?:void|bool|int) ${name}\\(`)));}
function adapt(source){return source
  .replace(/^(?:void|bool|int) (\w+)\(([^)]*)\)/gm,(_,name,args)=>`function ${name}(${args.replace(/const String& |bool |int /g,'')})`)
  .replace(/const (?:bool|int) /g,'const ').replace(/\bint (\w+)\s*=/g,'let $1 =')
  .replaceAll('View::','View.').replace('position / kMonitorPerPage','Math.floor(position / kMonitorPerPage)')
  .replace('(monitorVisibleCount() + kMonitorPerPage - 1) / kMonitorPerPage','Math.floor((monitorVisibleCount() + kMonitorPerPage - 1) / kMonitorPerPage)');}
const tools=['DeauthMonitor','RogueWatch','BleDetect','KarmaWatch','BeaconWatch','AuthFlood','AdvancedWatch','DeauthForensics'];
const flags=['deauthMonitorActive','rogueWatchActive','bleDetectActive','karmaWatchActive','beaconWatchActive','authFloodActive','advancedWatchActive','deauthForensicsActive'];
const views=['kDeauthMonitor','kRogueWatch','kBleSpamWatch','kKarmaWatch','kBeaconWatch','kAuthFlood','kAdvancedWatch','kDeauthForensics'];
const numMain=name=>Number(main.match(new RegExp(`(?:const int|constexpr int) ${name} = (\\d+)`))[1]);
const FIRST=numMain('kMenuFirstY'),PITCH=numMain('kMenuRowPitch'),CARDH=numMain('kMenuCardHeight');
const PERPAGE=numMain('kMonitorPerPage'),KGOOD=5;
const cy=i=>FIRST+i*PITCH+Math.floor(CARDH/2);   // vertical center of card row i
function harness(){
  const calls=[],cards=[],buttons=[];
  const c=vm.createContext({String,max:Math.max,min:Math.min,monitorCategory:-1,monitorPage:0,
    kMonitorItemCount:8,kMonitorPerPage:PERPAGE,kMenuFirstY:FIRST,kMenuRowPitch:PITCH,kMenuCardHeight:CARDH,
    kScreenWidth:240,kBackground:0,kMuted:1,kAccent:2,kGood:KGOOD,
    View:{kMonitor:'monitor'},drawHeader:(...a)=>calls.push(['header',...a]),
    drawMenuCard:(...a)=>cards.push(a),drawSmallButton:(...a)=>buttons.push(a),
    drawHome:()=>calls.push(['home']),display:{fillScreen(){cards.length=buttons.length=0;}},
    deauthForensicsPageCount:()=>c.forensicsPages,deauthForensicsPage:0,forensicsPages:1,
    clearDeauthForensics:()=>calls.push(['clear']),exportDeauthForensicsToSd:()=>{calls.push(['export']);return true;},
    lastDeauthForensicsCsvOk:false,deauthFrameCount:3,disassocFrameCount:4,deauthEventsSinceDraw:5,haveDeauthHit:true,
    bleDetectTotal:10,bleDetectSpam:4,bleDetectPeakRate:3,bleDetectRate:1,bleDetectAlert:true,bleDetectLastVendor:2});
  for(const name of ['kMonitorItems','kMonitorDescriptions','kMonitorCategories'])
    c[name]=Array.from(main.match(new RegExp(`${name}\\[\\] = \\{([^}]+)\\}`))[1].matchAll(/"([^"]+)"/g),m=>m[1]);
  c.kMonitorGroups=main.match(/kMonitorGroups\[\] = \{([^}]+)\}/)[1].split(',').map(Number);
  for(let i=0;i<tools.length;i++){
    c[flags[i]]=false;c.View[views[i]]=views[i];
    c['start'+tools[i]]=()=>{calls.push(['start',i]);c[flags[i]]=true;};
    c['stop'+tools[i]]=()=>{calls.push(['stop',i]);c[flags[i]]=false;};
    c['draw'+tools[i]]=()=>calls.push(['view',i]);
    c['reset'+tools[i]]=()=>calls.push(['reset',i]);
  }
  const names=['monitorItemRunning','monitorVisibleCount','monitorItemIndex','monitorPageCount','setMonitorOrigin',
    'launchMonitorItem','returnToMonitor','drawMonitorHeader','openMonitorMenu','drawMonitorMenu','handleMonitorTouch'];
  vm.runInContext(adapt(names.map(n=>fn(main,n)).join('\n')+'\n'+fn(read('gps.ino'),'gpsMenuHit')),c);
  const routes=views.map(view=>block(input,input.indexOf(`if (currentView == View::${view})`))).join('\n');
  vm.runInContext(adapt('function toolTap(x) {\n'+routes+'\n}'),c);
  return {c,calls,cards,buttons,tap:(x,y)=>c.handleMonitorTouch(x,y)};
}
test('groups preserve all eight detectors with descriptions and correct pagination',()=>{
  const t=harness();t.c.openMonitorMenu();assert.equal(t.cards.length,3);
  const expected=[[0,1,3,4,5],[2],[6,7]];
  for(let group=0;group<3;group++){
    t.c.monitorCategory=group;t.c.monitorPage=0;t.c.drawMonitorMenu();
    assert.equal(t.c.monitorVisibleCount(),expected[group].length);
    assert.equal(t.c.monitorPageCount(),group===0?2:1);   // WI-FI: 5 tools over 4/page
    assert.deepEqual(expected[group].map((_,i)=>t.c.monitorItemIndex(i)),expected[group]);
    assert.equal(t.c.monitorItemIndex(-1),-1);assert.equal(t.c.monitorItemIndex(expected[group].length),-1);
  }
  assert.equal(new Set(t.c.kMonitorItems).size,8);
  assert.ok(t.c.kMonitorDescriptions.every(s=>s.length>0&&s.length*6<=208));
  assert.ok(t.c.kMonitorItems.every(s=>s.length*12<=208));
});
test('card centers used by Touch and Mini launch every tool and stop returns to its page',()=>{
  const expected=[[0,1,3,4],[5],[2],[6,7]];
  for(let pageIndex=0;pageIndex<expected.length;pageIndex++)for(let row=0;row<expected[pageIndex].length;row++){
    const t=harness(),index=expected[pageIndex][row],group=pageIndex<2?0:pageIndex-1,page=pageIndex===1?1:0;
    t.c.openMonitorMenu();t.tap(120,cy(group));if(page)t.tap(200,298);
    t.tap(120,cy(row));assert.deepEqual(t.calls.at(-1),['start',index]);
    t.c.currentView=views[index];t.c.toolTap(30);
    assert.deepEqual(t.calls.find(a=>a[0]==='stop'),['stop',index]);
    assert.equal(t.c.monitorCategory,group);assert.equal(t.c.monitorPage,page);assert.equal(t.c.currentView,'monitor');
  }
});
test('reopening a running tool never restarts it or clears its results',()=>{
  for(let index=0;index<8;index++){
    const t=harness();t.c[flags[index]]=true;t.c.launchMonitorItem(index);
    assert.deepEqual(t.calls,[['view',index]]);
  }
});
test('remote-launched tools derive their group/page; stopped tools do not tear down twice',()=>{
  for(let index=0;index<8;index++){
    const t=harness();t.c[flags[index]]=true;t.c.returnToMonitor(index);t.c.returnToMonitor(index);
    assert.equal(t.calls.filter(a=>a[0]==='stop').length,1);
    assert.equal(t.c.monitorCategory,index===2?1:index>=6?2:0);
    assert.equal(t.c.monitorPage,index===5?1:0);   // WI-FI's 5th tool (index 5) spills to page 2
  }
  const t=harness();t.c.launchMonitorItem(-1);t.c.returnToMonitor(8);assert.equal(t.calls.length,0);
});
test('margins, gaps, blank rows and disabled paging do nothing',()=>{
  const t=harness();t.c.openMonitorMenu();t.calls.length=0;
  for(const [x,y]of [[6,cy(0)],[236,cy(0)],[120,FIRST+CARDH+3],[120,cy(3)],[120,270],[240,298]])t.tap(x,y);
  assert.equal(t.calls.length,0);assert.equal(t.c.monitorCategory,-1);
  t.tap(120,cy(0));t.calls.length=0;                              // enter WI-FI (2 pages)
  t.tap(120,298);assert.equal(t.c.monitorPage,0);assert.equal(t.calls.length,0);  // Prev disabled on page 1
  t.tap(200,298);assert.equal(t.c.monitorPage,1);                 // Next
  t.calls.length=0;t.tap(200,298);assert.equal(t.c.monitorPage,1);// Next disabled on last page
  t.tap(120,cy(1));assert.equal(t.calls.length,0);                // page 2 has one tool at row 0; row 1 blank
  t.tap(120,298);assert.equal(t.c.monitorPage,0);                 // Prev returns
});
test('Groups returns to picker, Home exits, reopening and stale pages reset safely',()=>{
  const t=harness();t.c.monitorCategory=0;t.c.monitorPage=1;t.tap(40,298);
  assert.equal(t.c.monitorCategory,-1);assert.equal(t.c.monitorPage,0);t.tap(120,298);assert.equal(t.calls.at(-1)[0],'home');
  t.c.monitorCategory=2;t.tap(180,298);assert.equal(t.calls.at(-1)[0],'home');
  t.c.monitorCategory=0;t.c.monitorPage=99;t.c.drawMonitorMenu();assert.equal(t.c.monitorPage,1);
  t.c.monitorCategory=99;t.c.drawMonitorMenu();assert.equal(t.c.monitorCategory,-1);assert.equal(t.c.monitorPage,0);
  t.c.monitorCategory=1;t.c.openMonitorMenu();assert.equal(t.c.monitorCategory,-1);
});
test('running status shows as a green card + running line; stopped cards show the description',()=>{
  const t=harness();t.c.bleDetectActive=true;t.c.openMonitorMenu();
  assert.match(t.cards[1][2],/1 running/);assert.equal(t.cards[1][3],KGOOD);   // BLUETOOTH group card
  t.tap(120,cy(1));                                            // enter BLUETOOTH
  assert.match(t.cards[0][2],/Running.*view/);assert.equal(t.cards[0][3],KGOOD);
  t.c.bleDetectActive=false;t.c.drawMonitorMenu();
  assert.match(t.cards[0][2],/advert/);assert.notEqual(t.cards[0][3],KGOOD);   // stopped -> description
  t.c.drawMonitorHeader('BLE SPAM WATCH',true,'ALERT: spam flood nearby');
  assert.deepEqual(t.calls.at(-1),['header','BLE SPAM WATCH','Running | ALERT: spam flood nearby']);
  t.c.drawMonitorHeader('BLE SPAM WATCH',false,'listening');assert.match(t.calls.at(-1)[2],/^Stopped/);
});
test('existing Reset, forensic Clear/Export/paging, and Rogue Home actions are preserved',()=>{
  for(const index of [3,4,5,6]){
    const t=harness();t.c.currentView=views[index];t.c.toolTap(180);assert.deepEqual(t.calls,[['reset',index],['view',index]]);
  }
  const t=harness();t.c.currentView=views[0];t.c.toolTap(180);assert.equal(t.c.deauthFrameCount,0);assert.equal(t.c.haveDeauthHit,false);
  t.c.currentView=views[2];t.c.toolTap(180);assert.equal(t.c.bleDetectTotal,0);assert.equal(t.c.bleDetectAlert,false);
  t.c.currentView=views[7];t.c.toolTap(120);assert.equal(t.calls.at(-2)[0],'clear');
  t.c.toolTap(200);assert.equal(t.calls.at(-2)[0],'export');assert.equal(t.c.lastDeauthForensicsCsvOk,true);
  t.c.forensicsPages=3;t.c.toolTap(150);assert.equal(t.c.deauthForensicsPage,1);t.c.toolTap(90);assert.equal(t.c.deauthForensicsPage,0);
  t.c.toolTap(30);assert.equal(t.c.monitorCategory,2);
  t.c.currentView=views[1];t.c.rogueWatchActive=true;t.c.toolTap(180);
  assert.equal(t.c.rogueWatchActive,false);assert.equal(t.calls.at(-1)[0],'home');
});
test('menu body dispatch precedes footer gate and every detector uses matching state and Stop label',()=>{
  assert.ok(input.indexOf('handleMonitorTouch(x, y)')<input.indexOf('if (y < kFooterTop) return;'));
  assert.match(main,/case kHomeMonitor: openMonitorMenu\(\);/);  // Home tile still opens Monitor
  const files=['deauth','roguewatch','bledetect','karmawatch','beaconwatch','authflood','advancedwatch','deauthforensics'];
  for(let i=0;i<8;i++){
    const source=read(files[i]+'.ino');
    assert.match(source,new RegExp(`drawMonitorHeader\\([^;]*${flags[i]}`));
    assert.match(source,new RegExp(`${flags[i]} \\? "Stop" : "Back"`));
  }
});
