import test from 'node:test';
import assert from 'node:assert/strict';
import {harness,source,read} from './network_menu_harness.mjs';

test('overview exposes connection, hosts, services and previous results/upload without starting scans',()=>{
  const t=harness();t.c.drawNetworkMenu();assert.deepEqual(t.cards.map(c=>c[1]),['Connection','Hosts','Services','Results & Upload']);
  assert.match(t.header()[1],/192.168.1.10/);t.c.connected=false;t.c.drawNetworkMenu();assert.match(t.header()[1],/disconnected/);
  t.menuTap(120,80);assert.equal(t.c.currentView,'kNetworkSetup');
  t.c.drawNetworkMenu();t.menuTap(120,130);assert.equal(t.c.currentView,'kNetworkResults');assert.equal(t.c.netJob,'None');
  t.c.drawNetworkMenu();t.menuTap(120,230);assert.equal(t.c.netMenuSection,2);
  t.menuTap(120,180);assert.equal(t.c.currentView,'kWardriveUpload');assert.equal(t.c.netSetupReturnUpload,true);
  t.c.netReturnFromSetup();assert.equal(t.calls.at(-1)[0],'drawUpload');
});
test('all five service actions retain their jobs and all-host scope, including UPnP without discovery',()=>{
  for(let i=0;i<5;i++){
    const t=harness();if(i<4)t.hosts();t.c.netMenuSection=1;t.c.drawNetworkMenu();
    if(i===4)t.menuTap(200,298);t.menuTap(120,80+(i%4)*50);
    assert.equal(t.c.netJob,['Ports','Cameras','Printers','Sip','Upnp'][i]);assert.equal(t.c.netResultsHost,-1);
    assert.equal(t.c.netHostCursor,0);if(i<4)assert.equal(t.c.netHostEnd,8);
  }
});
test('missing connection, invalid subnet, no hosts, changed LAN and low memory show prerequisites without starting probes',()=>{
  for(const scenario of ['disconnected','subnet','empty','changed','memory']){
    const t=harness();t.hosts();
    if(scenario==='disconnected')t.c.connected=false;
    if(scenario==='subnet')t.c.validRange=false;
    if(scenario==='empty')t.c.netHostCount=0;
    if(scenario==='changed')t.c.ssid='Different';
    if(scenario==='memory')t.c.workspace=false;
    t.c.netStartJob('Ports',-1);assert.equal(t.c.netJob,'None');
    assert.match(t.c.netStatus,/Connect before|Unsupported subnet|Discover hosts|Low memory/);
    if(scenario==='empty'||scenario==='changed'){assert.equal(t.c.netShowHosts,true);assert.equal(t.c.currentView,'kNetworkResults');}
    if(scenario==='disconnected'||scenario==='subnet')assert.equal(t.c.currentView,'kNetworkSetup');
  }
});
test('selected-host service scan and detail return preserve the host page and service scope',()=>{
  const t=harness();t.hosts();t.c.netPage=1;t.c.drawNetworkResults();t.resultTap(120,130);
  assert.equal(t.c.netSelectedHost,4);assert.equal(t.c.currentView,'kNetworkHost');
  t.c.handleNetworkHostTouch(120,80);assert.equal(t.c.netJob,'Ports');assert.equal(t.c.netHostCursor,4);assert.equal(t.c.netHostEnd,5);
  assert.equal(t.c.netResultsHost,4);t.results();t.c.netFinish('Complete');
  t.resultTap(200,298);assert.equal(t.c.netPage,1);t.resultTap(120,80);assert.equal(t.c.netSelectedResult,3);
  t.c.drawNetworkResults();assert.equal(t.c.netPage,1);t.resultTap(40,298);assert.equal(t.c.netSelectedHost,4);assert.equal(t.c.currentView,'kNetworkHost');
  t.c.handleNetworkHostTouch(60,298);assert.equal(t.c.netShowHosts,true);assert.equal(t.c.netPage,1);assert.equal(t.c.netServicesPage,1);
});
test('Actions switches saved datasets with independent pages and reports export failure/success',()=>{
  const t=harness();t.hosts();t.results();t.c.netPage=2;t.c.drawNetworkResults();t.resultTap(120,260);
  assert.equal(t.c.netResultsActions,true);t.c.saveOk=false;t.resultTap(120,130);assert.match(t.c.netStatus,/SD save failed/);
  t.c.saveOk=true;t.resultTap(120,130);assert.equal(t.c.netStatus,'Snapshot saved');
  t.resultTap(120,80);assert.equal(t.c.netShowHosts,false);assert.equal(t.c.netPage,0);assert.equal(t.c.netResultsActions,false);
  t.resultTap(200,298);t.resultTap(120,260);t.resultTap(120,80);assert.equal(t.c.netShowHosts,true);assert.equal(t.c.netPage,2);
});
test('running scans allow pagination but require Stop before navigation, selection or a new job',()=>{
  const t=harness();t.hosts();t.c.netStartJob('Ports',-1);t.results();t.c.drawNetworkResults();
  t.resultTap(120,80);t.resultTap(40,298);assert.equal(t.c.currentView,'kNetworkResults');assert.equal(t.c.netSelectedResult,0);
  t.c.netStartJob('Hosts',-1);assert.equal(t.c.netJob,'Ports');t.resultTap(200,298);assert.equal(t.c.netPage,1);
  t.resultTap(120,260);assert.equal(t.c.netJob,'None');assert.deepEqual(t.calls.find(c=>c[0]==='finish'),['finish','Cancelled (partial)']);
  t.resultTap(120,260);assert.equal(t.c.netResultsActions,true);assert.equal(t.calls.filter(c=>c[0]==='finish').length,1);
});
test('Touch and Mini card centers match; margins, gaps, blank rows and disabled paging do nothing',()=>{
  const t=harness();t.c.drawNetworkMenu();
  for(const [x,y]of [[7,80],[232,80],[120,102],[120,152],[120,202],[120,259],[120,279],[240,298]])t.menuTap(x,y);
  assert.equal(t.c.netMenuSection,0);assert.equal(t.c.currentView,'kNetworkMenu');
  t.menuTap(120,180);assert.equal(t.c.netMenuSection,1);t.menuTap(120,298);assert.equal(t.c.netMenuPage,0);
  t.menuTap(200,298);t.menuTap(200,298);t.menuTap(120,130);assert.equal(t.c.netMenuPage,1);assert.equal(t.c.netJob,'None');
  t.c.netShowHosts=false;t.c.drawNetworkResults();t.resultTap(120,130);assert.equal(t.c.currentView,'kNetworkResults');
  t.resultTap(120,155);assert.equal(t.c.currentView,'kNetworkMenu');assert.equal(t.c.netMenuSection,1);
});
test('AP picker keeps T9 entry and bounded pages; rescan failures return to connection setup',()=>{
  const t=harness();t.c.wifiEntries=Array.from({length:5},(_,i)=>({ssid:i===4?'':'AP'+i}));t.c.wifiCount=5;
  t.c.netPassword='secretpass';t.c.drawNetworkAps();t.c.handleNetworkApsTouch(150,298);assert.equal(t.c.netApPage,1);
  t.c.handleNetworkApsTouch(120,130);assert.equal(t.c.currentView,'kNetworkAps');
  t.c.handleNetworkApsTouch(120,80);assert.equal(t.c.netSsid,'');assert.equal(t.c.netPassword,'');assert.match(t.c.netStatus,/hidden/);
  t.c.handleNetworkSetupTouch(120,80);assert.equal(t.c.netEditPassword,false);assert.equal(t.c.currentView,'kNetworkEdit');
  t.c.drawNetworkSetup();t.c.handleNetworkSetupTouch(120,130);assert.equal(t.c.netEditPassword,true);
  t.c.lastWifiScanOk=false;t.c.handleNetworkApsTouch(200,298);assert.match(t.c.netStatus,/AP scan failed/);assert.equal(t.c.currentView,'kNetworkSetup');
});
test('connection progress has one explicit Cancel; success/timeout and invalid credentials are visible',()=>{
  const t=harness();t.c.netPassword='short';t.c.netJoin();assert.equal(t.c.netJob,'None');assert.match(t.c.netStatus,/WPA key/);
  t.c.connected=false;t.c.netPassword='secretpass';t.c.netJoin();assert.equal(t.c.netJob,'Join');assert.equal(t.c.netPassword,'');
  assert.deepEqual(t.buttons.map(b=>b[4]),['Cancel connection']);
  t.c.handleNetworkSetupTouch(60,298);t.c.handleNetworkSetupTouch(120,80);assert.equal(t.c.netJob,'Join');
  t.c.connected=true;t.c.updateJoin();assert.equal(t.c.netJob,'None');assert.match(t.c.netStatus,/Connected:/);
  t.c.connected=false;t.c.netPassword='secretpass';t.c.netJoin();t.c.expired=true;t.c.updateJoin();
  assert.equal(t.c.netJob,'None');assert.match(t.c.netStatus,/timed out/);
  t.c.expired=false;t.c.netPassword='secretpass';t.c.netJoin();t.c.handleNetworkSetupTouch(120,240);
  assert.equal(t.c.netJob,'None');assert.equal(t.c.netStatus,'Disconnected');
});
test('saved Wi-Fi controls opt in, open the list, and save only after a matching successful join',()=>{
  const t=harness();t.c.drawNetworkSetup();
  t.c.handleNetworkSetupTouch(50,265);assert.equal(t.c.netSaveOnJoin,false);
  t.c.handleNetworkSetupTouch(170,265);assert.equal(t.c.currentView,'kNetworkSaved');
  t.c.drawNetworkSetup();t.c.netPassword='secretpass';t.c.connected=false;t.c.netJoin();
  t.c.connected=true;t.c.ssid='Other';t.c.updateJoin();assert.equal(t.c.netJob,'Join');
  t.c.ssid='Lab';t.c.updateJoin();assert.equal(t.c.netJob,'None');
  assert.equal(t.calls.filter(c=>c[0]==='rememberWifi').length,0);
  t.c.netSaveOnJoin=true;t.c.connected=false;t.c.netPassword='secretpass';t.c.netJoin();
  t.c.connected=true;t.c.updateJoin();
  assert.equal(t.calls.filter(c=>c[0]==='rememberWifi').length,1);
});
test('stale host/result indices and upload memory release cannot dereference cleared tables',()=>{
  const t=harness();t.hosts();t.results();t.c.netSelectedHost=50;t.c.drawNetworkHost();assert.equal(t.c.currentView,'kNetworkResults');
  t.c.netSelectedResult=50;t.c.drawNetworkDetail();assert.equal(t.c.currentView,'kNetworkResults');
  t.c.netStartJob('Ports',50);assert.equal(t.c.netJob,'None');assert.match(t.c.netStatus,/Host list changed/);
  t.c.netResultsHost=4;t.c.netPage=2;t.c.netReleaseWorkspaceForUpload();
  assert.equal(t.c.netResultsHost,-1);assert.equal(t.c.netHostCount,0);assert.equal(t.c.netResultCount,0);assert.equal(t.c.netPage,0);
  t.c.drawNetworkResults();assert.equal(t.cards.length,0);t.c.drawNetworkHost();assert.equal(t.c.currentView,'kNetworkResults');
});
test('idle connection refresh updates navigation without interrupting password entry or uploads',()=>{
  const t=harness();t.c.drawNetworkMenu();t.c.connected=false;t.c.updateNetworkNavigation();assert.match(t.header()[1],/disconnected/);
  t.cards.length=0;t.c.now+=2000;t.c.updateNetworkNavigation();assert.equal(t.cards.length,0); // no redraw that resets Mini scrolling
  for(const view of ['kNetworkEdit','kWardriveUpload']){t.c.currentView=view;t.c.now+=2000;t.c.updateNetworkNavigation();assert.equal(t.c.currentView,view);}
  t.c.netJob='Ports';t.c.currentView='kNetworkHost';t.c.now+=2000;t.c.updateNetworkNavigation();assert.equal(t.c.currentView,'kNetworkHost');
});
test('completed, empty, limited and connection-lost states retain truthful status and explicit next actions',()=>{
  const t=harness();t.c.netShowHosts=false;t.c.netStatus='Complete; silent hosts may be absent';
  t.c.netResultSummary.checked=42;t.c.netResultSummary.timeouts=9;t.c.netResultSummary.limited=true;t.c.drawNetworkResults();
  assert.ok(t.texts.some(a=>a[1].includes('No retained')));assert.ok(t.texts.some(a=>a[1].includes('42 checked; 9 timeout')));
  assert.ok(t.texts.some(a=>a[1].includes('export is partial')));assert.ok(t.buttons.some(a=>a[4]==='Choose services'));
  t.c.netStatus='Connection changed/lost (partial)';t.c.connected=false;t.c.drawNetworkResults();
  assert.match(t.header()[1],/disconnected/);assert.ok(t.texts.some(a=>a[1].includes('Connection changed/lost')));
  t.c.netSetupReturnResults=true;t.c.netReturnFromSetup();assert.equal(t.c.currentView,'kNetworkResults');
  assert.match(read('upload.ino'),/netSetupReturnUpload = false; drawNetworkMenu\(\)/);
  assert.match(source,/networkToolsOpen\(\) \{ return netOpen; \}/);
});

test('connection loss clears host selection after finishing partial results; Back unwinds groups then exits',()=>{
  const t=harness();t.hosts();t.results();t.c.netShowHosts=false;t.c.netResultsHost=4;t.c.netSelectedHost=4;
  t.c.netJob='Ports';t.c.connected=false;t.c.updateLost();
  assert.equal(t.c.netJob,'None');assert.equal(t.c.netHostCount,0);assert.equal(t.c.netResultsHost,-1);
  assert.equal(t.c.netSelectedHost,-1);assert.equal(t.c.netResultCount,5);
  assert.deepEqual(t.calls.find(c=>c[0]==='finish'),['finish','Connection changed/lost (partial)']);
  t.resultTap(40,298);assert.equal(t.c.currentView,'kNetworkMenu');
  t.c.netMenuSection=1;t.c.netMenuPage=1;t.menuTap(40,298);assert.equal(t.c.netMenuSection,0);assert.equal(t.c.netMenuPage,0);
  t.menuTap(40,298);assert.equal(t.c.netOpen,false);assert.equal(t.calls.at(-1)[0],'recon');
});
