// Run the real menu/SD selection control flow with JS mocks, without compiling.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const source=readFileSync(new URL('../../AWOKxDAG/files.ino',import.meta.url),'utf8');
function fn(name) {
  const start=source.search(new RegExp('(?:String|void|bool|int) '+name+'\\('));
  assert.notEqual(start,-1,name);
  const open=source.indexOf('{',start);let depth=1,end=open+1;
  while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}
  return source.slice(start,end);
}
class Text {
  constructor(s=''){this.value=String(s);}
  [Symbol.toPrimitive](){return this.value;}
  lastIndexOf(s){return this.value.lastIndexOf(s);}
  substring(a,b){return new Text(this.value.substring(a,b));}
  toLowerCase(){this.value=this.value.toLowerCase();}
  startsWith(s){return this.value.startsWith(s);}
  endsWith(s){return this.value.endsWith(s);}
  compareTo(s){return this.value<String(s)?-1:this.value>String(s)?1:0;}
}
const functions=['fileBaseName','fileFullPath','fileKind','fileIsNewer','refreshFileView',
  'filePageCount','fileButtonHit','fileSelectionProtected','scanSdFiles','handleFilesTouch'];
let adapted=functions.map(fn).join('\n')
  .replace(/^(?:String|void|bool|int) (\w+)\(([^)]*)\)/gm,(_,name,args)=>
    `function ${name}(${args.replace(/const String& |time_t |int /g,'')})`)
  .replace('String name = fileBaseName(path);', 'String name = String(fileBaseName(path));')
  .replace(/const (?:String|time_t|int|bool|FileRow) /g,'const ')
  .replace(/\b(?:String|File|int) (\w+)\s*=/g,'let $1 =')
  .replaceAll('time_t(0)','0').replace(/(\w+)\.c_str\(\)/g,'$1')
  .replace('const int ', 'const ')
  .replaceAll('(fileViewCount + kFilesPerPage - 1) / kFilesPerPage','Math.floor((fileViewCount + kFilesPerPage - 1) / kFilesPerPage)')
  .replace('(y - 46) / 44','Math.floor((y - 46) / 44)')
  .replace('(y - kFileCardTop) / kFileCardPitch','Math.floor((y - kFileCardTop) / kFileCardPitch)');
function harness(entries=[]) {
  let cursor=0;const removed=[],audits=[];
  const context=vm.createContext({Math,min:Math.min,max:Math.max,String:s=>new Text(s),
    kMaxFileRows:64,kFilesPerPage:4,kFileCardTop:88,kFileCardPitch:44,kFileCardHeight:40,
    fileRows:Array.from({length:64},()=>({name:new Text(''),size:0,modified:0})),
    fileRowCount:0,fileDirectoryCount:0,fileSelected:-1,fileConfirmDelete:false,fileTotalBytes:0,
    filePage:0,fileFilter:0,fileViewMode:0,fileViewRows:[],fileViewCount:0,fileListRevision:0,fileDrawRevision:0,
    fileNotice:'',kSdDirectory:'/awokxdag',g_wardriveFile:false,g_wardriveCsvPath:'',
    delay(){},ensureSdCard:()=>true,SD:{open:()=>{cursor=0;return {isDirectory:()=>true,close(){},openNextFile:()=>{
      if(cursor===entries.length)return null;const item=entries[cursor++];
      return {name:()=>item.name,size:()=>item.size??100,getLastWrite:()=>item.modified??0,isDirectory:()=>!!item.directory,close(){}};
    }};},remove:path=>{removed.push(String(path));return context.deleteOk;}},
    deleteOk:true,recordFirmwareAudit:(...args)=>audits.push(args),
    drawFilesManager:()=>{context.refreshFileView();context.fileDrawRevision=context.fileListRevision;},
    drawHome:()=>{context.destination='home';},drawStatus:()=>{context.destination='status';},
  });
  vm.runInContext(adapted,context);
  context.scanSdFiles();context.drawFilesManager();
  return {c:context,removed,audits,tap:(x,y)=>context.handleFilesTouch(x,y),
    names:()=>context.fileRows.slice(0,context.fileRowCount).map(r=>String(r.name))};
}
test('newest 64 are retained across the entire directory, sorted by date then filename',()=>{
  const entries=Array.from({length:90},(_,i)=>({name:`capture-${String(i).padStart(3,'0')}.pcap`,modified:i+1}));
  const t=harness([...entries,{name:'folder',directory:true}]);
  assert.equal(t.c.fileDirectoryCount,90);assert.equal(t.c.fileRowCount,64);
  assert.equal(t.names()[0],'capture-089.pcap');assert.equal(t.names().at(-1),'capture-026.pcap');
  assert.equal(t.c.fileTotalBytes,6400);
  const fallback=harness([{name:'wardrive-0001.csv'},{name:'wardrive-0015.csv'},{name:'wardrive-0012.csv'}]);
  assert.deepEqual(fallback.names(),['wardrive-0015.csv','wardrive-0012.csv','wardrive-0001.csv']);
});
test('filters map to real transfer indices and keep all unmatched files in Logs / other',()=>{
  const t=harness([{name:'notes.txt',modified:4},{name:'WARD RIVE.txt',modified:3},
    {name:'wardrive-0015.CSV',modified:2},{name:'test.PCAPNG',modified:1}]);
  t.tap(120,60);assert.equal(t.c.fileViewMode,1);t.tap(120,110);
  assert.equal(t.c.fileFilter,1);assert.equal(t.c.fileViewCount,1);
  t.tap(120,100);assert.equal(t.c.fileSelected,2);assert.equal(t.c.fileViewMode,2);
  t.c.fileFilter=2;t.c.refreshFileView();assert.equal(t.c.fileViewRows[0],3);
  t.c.fileFilter=3;t.c.refreshFileView();assert.equal(t.c.fileViewCount,2);
  assert.deepEqual(t.names(),['notes.txt','WARD RIVE.txt','wardrive-0015.CSV','test.PCAPNG']);
});
test('card gaps, margins and unavailable pagination do nothing; Back preserves page/filter',()=>{
  const t=harness(Array.from({length:5},(_,i)=>({name:`wardrive-000${i}.csv`})));
  t.tap(2,100);t.tap(120,129);t.tap(90,296);assert.equal(t.c.fileViewMode,0);assert.equal(t.c.filePage,0);
  t.tap(200,296);assert.equal(t.c.filePage,1);t.tap(200,296);assert.equal(t.c.filePage,1);
  t.tap(120,150);assert.equal(t.c.fileViewMode,0); // blank second card
  t.tap(120,100);assert.equal(t.c.fileViewMode,2);t.tap(50,296);assert.equal(t.c.filePage,1);
  t.c.fileFilter=2;t.c.refreshFileView();assert.equal(t.c.filePage,0);assert.equal(t.c.fileViewCount,0);
  t.tap(40,296);assert.equal(t.c.destination,'home'); // list Back returns to Home root, not Status
});
test('delete requires a separate confirmation target; cancel and failure retain the file',()=>{
  const t=harness([{name:'wardrive-0001.csv'}]);t.tap(120,100);t.tap(180,296);
  assert.equal(t.c.fileConfirmDelete,true);assert.equal(t.removed.length,0);
  t.tap(180,296);assert.equal(t.c.fileConfirmDelete,false);assert.equal(t.removed.length,0);
  t.tap(180,296);t.c.deleteOk=false;t.tap(120,240);
  assert.deepEqual(t.removed,['/awokxdag/wardrive-0001.csv']);assert.match(t.c.fileNotice,/Delete failed/);
  assert.equal(t.c.fileViewMode,2);assert.equal(t.c.fileConfirmDelete,false);
  t.tap(180,296);t.c.deleteOk=true;t.tap(120,240);assert.equal(t.c.fileViewMode,0);
  assert.match(t.c.fileNotice,/File deleted/);
});
test('active wardrive and stale directory revisions cannot delete another file',()=>{
  const t=harness([{name:'wardrive-0001.csv'}]);t.tap(120,100);
  t.c.g_wardriveFile=true;t.c.g_wardriveCsvPath='/awokxdag/wardrive-0001.csv';
  t.tap(180,296);assert.equal(t.c.fileConfirmDelete,false);assert.equal(t.removed.length,0);
  t.c.g_wardriveFile=false;t.tap(180,296);t.c.scanSdFiles();t.tap(120,240);
  assert.equal(t.removed.length,0);assert.equal(t.c.fileViewMode,0);assert.match(t.c.fileNotice,/select the file again/);
});

test('Mini file cards expose the same working selection coordinates as Touch',()=>{
  const t=harness([{name:'wardrive-0015.csv'}]);
  // MiniLayout sends the center of each registered button to handleFilesTouch.
  t.tap(8+224/2,88+40/2);assert.equal(t.c.fileViewMode,2);
  t.tap(4+112/2,280+36/2);assert.equal(t.c.fileViewMode,0);
  t.tap(8+224/2,44+38/2);assert.equal(t.c.fileViewMode,1);
  t.tap(8+224/2,46+2*44+40/2);assert.equal(t.c.fileFilter,2);
});
test('SD refresh failure empties stale entries and reports the error',()=>{
  const t=harness([{name:'wardrive-0015.csv'}]);
  t.tap(120,60);t.c.ensureSdCard=()=>false;t.tap(120,240);
  assert.equal(t.c.fileRowCount,0);assert.equal(t.c.fileViewCount,0);
  assert.match(t.c.fileNotice,/SD list failed/);t.tap(120,100);assert.equal(t.c.fileViewMode,0);
});
