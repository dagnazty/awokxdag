// Source-adapted navigation with mocked display, Wi-Fi, storage and probes.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
export const read=name=>readFileSync(new URL(`../../AWOKxDAG/${name}`,import.meta.url),'utf8');
export const source=read('networktools.ino');
export function block(s,start){
  assert.ok(start>=0);let end=s.indexOf('{',start)+1,depth=1;
  while(depth){if(s[end]==='{')depth++;if(s[end]==='}')depth--;end++;}
  return s.slice(start,end);
}
export function fn(s,name){return block(s,s.search(new RegExp(`(?:void|bool|int|String) ${name}\\(`)));}
function adapt(s){return s
  .replace(/^(?:void|bool|int|String) (\w+)\(([^)]*)\)/gm,(_,n,args)=>`function ${n}(${args.replace(/const char\* |const String& |NetJob |bool |int /g,'')})`)
  .replace(/const (?:bool|int|char\*|String) /g,'const ').replace(/\b(?:int|String|unsigned) (\w+)\s*=/g,'let $1 =')
  .replace(/const NetSummary& /g,'const ').replace(/NetSummary& /g,'let ').replace(/NetResult& /g,'const ')
  .replace('summary = NetSummary{};', 'Object.assign(summary, makeSummary());')
  .replaceAll('NetSummary{}','makeSummary()').replace(/(net\w+Summary)\.ssid, sizeof\(summary.ssid\)/g,'$1.ssid, 33')
  .replace('snprintf(summary.ssid, sizeof(summary.ssid), "%s", WiFi.SSID().c_str());','summary.ssid = WiFi.SSID();')
  .replaceAll('NetJob::','NetJob.').replaceAll('NetStage::','NetStage.').replaceAll('View::','View.').replaceAll('AwokKeyboard::','AwokKeyboard.').replaceAll('NetworkParse::','NetworkParse.')
  .replaceAll('.c_str()','').replaceAll('.length()', '.length').replace(/\bstrlen\((\w+)\)/g,'$1.length')
  .replace('const detail(item.detail);','const detail = item.detail;')
  .replaceAll('SavedWifi::wipe(netPendingPassword, sizeof(netPendingPassword));','netPendingPassword = "";')
  .replace('memcpy(netPendingPassword, netPassword, netPassword.length);','netPendingPassword = netPassword;')
  .replace('(const uint8_t*)request','request').replace('IPAddress(239,255,255,250)','"239.255.255.250"')
  .replace(/\b(0x[0-9a-f]+)U\b/g,'$1')
  .replace(/uint32_t lastDraw = 0;/,'let lastDraw = 0;')
  .replace(/\(wifiCount \+ kNetMenuRows - 1\) \/ kNetMenuRows/g,'Math.floor((wifiCount + kNetMenuRows - 1) / kNetMenuRows)')
  .replace('((netShowHosts ? netHostCount : netResultCount) + kNetResultRows - 1) / kNetResultRows','Math.floor(((netShowHosts ? netHostCount : netResultCount) + kNetResultRows - 1) / kNetResultRows)');}
export function harness(){
  const calls=[],cards=[],buttons=[],texts=[];let header=[];
  const makeSummary=()=>({status:'',job:'None',ip:10,mask:24,ssid:'Lab',checked:0,timeouts:0,errors:0,limited:false,subnetLimited:false,hostsLimited:false});
  const c=vm.createContext({String,max:Math.max,min:Math.min,NetJob:Object.fromEntries(['None','Join','Hosts','Ports','Cameras','Printers','Sip','Upnp'].map(s=>[s,s])),
    NetStage:Object.fromEntries(['Idle','ArpSend','Probe','SsdpWait'].map(s=>[s,s])),
    View:Object.fromEntries(['kNetworkMenu','kNetworkSetup','kNetworkResults','kNetworkHost','kNetworkDetail','kNetworkAps','kNetworkEdit','kWardriveUpload'].map(s=>[s,s])),
    AwokKeyboard:{Lower:0},netKeyTap:{commit:()=>calls.push(['keyCommit'])},netKeyMode:0,
    currentView:'kNetworkMenu',netJob:'None',netLastJob:'None',netStage:'Idle',netOpen:true,netMenuSection:0,netMenuPage:0,netPage:0,netApPage:0,
    netHostsPage:0,netServicesPage:0,netResultsHost:-1,netResultsActions:false,netSetupReturnResults:false,netSetupReturnUpload:false,
    netHostCount:0,netResultCount:0,netHosts:[],netResults:[],netSelectedHost:-1,netSelectedResult:0,netShowHosts:true,
    netHostSummary:makeSummary(),netResultSummary:makeSummary(),makeSummary,
    netSsid:'Lab',netPassword:'',netPendingPassword:'',netSaveOnJoin:true,netSavedSelected:-1,netEdit:'',netEditPassword:false,netStatus:'',netLimited:false,netSubnetLimited:false,netHostsLimited:false,
    netCompleted:0,netTimeouts:0,netErrors:0,netRefused:0,netSessionIp:0,netSessionMask:0,
    netFirst:1,netLast:30,netCursor:0,netHostCursor:0,netHostEnd:0,netPortCursor:0,netDeadline:0,netMappingIndex:0,netUpnpStep:0,
    kNetMenuRows:4,kNetResultRows:3,kNetSipLocalPort:49160,kBackground:0,kMuted:1,kAccent:2,kWarn:3,
    wifiCount:0,wifiEntries:[],lastWifiScanOk:true,WL_CONNECTED:3,connected:true,workspace:true,validRange:true,
    ssid:'Lab',ip:10,mask:24,now:1000,expired:false,saveOk:true,stationOk:true,
    millis:()=>c.now,netExpired:()=>c.expired,
    WiFi:{status:()=>c.connected?3:0,localIP:()=>({toString:()=>`192.168.1.${c.ip}`,value:c.ip}),
      subnetMask:()=>({value:c.mask}),SSID:()=>c.ssid,persistent(){},setAutoReconnect(){},disconnect(){},
      begin:(...a)=>calls.push(['join',...a])},
    netIpNumber:ip=>ip.value,netIpText:ip=>`192.168.1.${ip}`,macToString:mac=>mac||'00:11:22:33:44:55',
    strcmp:(a,b)=>a===b?0:1,NetworkParse:{range:()=>c.validRange},
    netEnsureWorkspace:()=>c.workspace,netCloseSocket:()=>calls.push(['socketClose']),
    netUdp:{stop(){},begin:()=>true,beginPacket:()=>true,write:(s,n)=>n,endPacket:()=>true},
    netSnapshot(){},netJobName:job=>job,recordFirmwareAudit:(...a)=>calls.push(['audit',...a]),
    netRememberJoinedWifi:()=>{calls.push(['rememberWifi']);return true;},
    drawNetworkSaved:()=>{c.currentView='kNetworkSaved';calls.push(['savedWifi']);},
    ensureWifiStation:()=>c.stationOk,esp_wifi_set_storage(){},WIFI_STORAGE_RAM:0,
    shutdownWifi:()=>{c.connected=false;calls.push(['shutdownWifi']);},
    closeNetworkTools:()=>{c.netOpen=false;calls.push(['closeNetworkTools']);},drawReconMenu:()=>calls.push(['recon']),
    netFreeWorkspace:()=>{c.netHosts=c.netResults=null;calls.push(['free']);},
    scanWifi:()=>calls.push(['scanWifi']),
    netWipe:()=>{}, // adapted below so reference arguments clear the correct string
    netSaveCsv:()=>{calls.push(['save',c.netShowHosts]);return c.saveOk;},
    netFinish:message=>{calls.push(['finish',message]);c.netJob='None';c.netStatus=message;c.drawNetworkResults();},
    drawNetworkEditor:()=>{c.currentView='kNetworkEdit';calls.push(['editor']);},
    openWardriveUpload:()=>{c.netSetupReturnUpload=true;c.currentView='kWardriveUpload';calls.push(['upload']);},
    drawWardriveUpload:()=>{c.currentView='kWardriveUpload';calls.push(['drawUpload']);},
    display:{fillScreen(){cards.length=buttons.length=texts.length=0;}},
    drawHeader:(...a)=>{header=a;},netCard:(...a)=>cards.push(a),netText:(...a)=>texts.push(a),drawSmallButton:(...a)=>buttons.push(a)});
  for(const n of ['kNetServiceLabels','kNetServiceDescriptions'])c[n]=Array.from(source.match(new RegExp(`${n}\\[\\] = \\{([^}]+)\\}`))[1].matchAll(/"([^"]+)"/g),m=>m[1]);
  c.kNetServiceJobs=source.match(/kNetServiceJobs\[\] = \{([^}]+)\}/)[1].split(',').map(s=>s.trim().split('::')[1]);
  const names=['netConnectionLabel','netHostSelectionValid','netPager','netRememberResultsPage','netOpenResults','netOpenSetup','netReturnFromSetup',
    'drawNetworkMenu','drawNetworkSetup','drawNetworkAps','netResultPages','drawNetworkResults','drawNetworkHost','drawNetworkDetail',
    'netCardHit','handleNetworkSetupTouch','handleNetworkApsTouch','handleNetworkMenuTouch','handleNetworkResultsTouch','handleNetworkHostTouch',
    'netStartJob','netDisconnect','netJoin','netReleaseWorkspaceForUpload'];
  let script=adapt(names.map(n=>fn(source,n)).join('\n')+'\n'+fn(read('gps.ino'),'gpsMenuHit'));
  script=script.replace(/netWipe\((net\w+)\)/g,'$1 = ""');
  vm.runInContext(script,c);
  // Execute the real connection-update branch with no probe simulation.
  const update=fn(source,'updateNetworkTools');
  const join=block(update,update.indexOf('if (netJob == NetJob::Join)'));
  vm.runInContext(adapt('function updateJoin(){'+join+'}'),c);
  const lost=block(update,update.indexOf('if (WiFi.status() != WL_CONNECTED ||'));
  vm.runInContext(adapt('function updateLost(){'+lost+'}'),c);
  const nav=fn(source,'updateNetworkNavigation').replace('static uint32_t lastDraw = 0;','').replace('static String lastConnection;','');
  vm.runInContext('let lastDraw = 0, lastConnection = "";\n'+adapt(nav),c);
  return {c,calls,cards,buttons,texts,header:()=>header,
    snapshot:()=>({header,cards:[...cards],buttons:[...buttons],texts:[...texts]}),
    hosts:(count=8)=>{c.netHostCount=count;c.netHosts=Array.from({length:count},(_,i)=>({ip:20+i,mac:'00:11:22:33:44:55'}));},
    results:(count=5)=>{c.netResultCount=count;c.netResults=Array.from({length:count},(_,i)=>({ip:20+i,port:443,kind:'TCP',detail:'Port open'}));},
    menuTap:(x,y)=>c.handleNetworkMenuTouch(x,y),resultTap:(x,y)=>c.handleNetworkResultsTouch(x,y)};
}
