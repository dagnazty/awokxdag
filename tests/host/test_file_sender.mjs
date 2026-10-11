// Exercise the firmware snapshot/resume control flow as JS with File/radio mocks.
// No firmware or host C++ compilation. Arduino-specific calls are adapted below.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const source=readFileSync(new URL('../../AWOKxDAG/files.ino',import.meta.url),'utf8');
const crcSource=readFileSync(new URL('../../AWOKxDAG/file_crc.h',import.meta.url),'utf8');
const body=source.slice(source.indexOf('void linkStreamFileVerified('),source.indexOf('\nvoid linkDeleteFile('));
let adapted=body.replace(/void linkStreamFileVerified\([^)]*\)/,'function send(index,token,startSeq,snapshotBytes,expectedCrc)')
  .replace('AxdFileChunkMsg chunk;','const chunk = {kind:0,totalBytes:0,data:""};')
  .replace('const char* error = nullptr;','let error = null;').replace('File file;','let file = null;').replace('String base;','let base = "";')
  .replace(/\bconst (?:String|size_t) /g,'const ').replace(/\buint32_t /g,'let ')
  .replace('uint8_t raw[kFileChunkBytes];','const raw = new Uint8Array(kFileChunkBytes);')
  .replace('chunk.totalBytes / kFileChunkBytes','Math.floor(chunk.totalBytes / kFileChunkBytes)')
  .replace(/sizeof\(raw\)/g,'raw.length').replace(/\b(?:0x[\da-f]+|\d+)U\b/gi,m=>m.slice(0,-1))
  .replace(/([\w]+)\.c_str\(\)/g,'$1')
  .replace('strcpy(chunk.data, "Checking SD snapshot");','chunk.data = "Checking SD snapshot";')
  .replace(/snprintf\(chunk.data, sizeof\(chunk.data\), "%08lx,%s", static_cast<unsigned long>\(checksum\), base\);/g,'chunk.data = (checksum >>> 0).toString(16).padStart(8,"0") + "," + base;')
  .replace('snprintf(chunk.data, sizeof(chunk.data), "%s", error);','chunk.data = error;')
  .replace('encodeBase64Chunk(raw, got, chunk.data, sizeof(chunk.data));','chunk.data = Buffer.from(raw.subarray(0,got)).toString("base64");')
  .replaceAll('checksum = state ^ 0xffffffff;', 'checksum = (state ^ 0xffffffff) >>> 0;')
  .replaceAll('(prefixState ^ 0xffffffff)', '((prefixState ^ 0xffffffff) >>> 0)');
let crc=crcSource.slice(crcSource.indexOf('inline uint32_t fileCrcUpdate('))
  .replace(/inline uint32_t fileCrcUpdate\([^)]*\)/,'function fileCrcUpdate(state,bytes,length)')
  .replace('size_t i','let i').replace('int bit','let bit').replace(/0x([a-f\d]+)U/g,'0x$1').replace('0U','0').replace('state >> 1','state >>> 1').replace('return state;','return state >>> 0;');
function harness(data,options={}) {
  let position=0,clock=0;
  const packets=[],seeks=[];
  const file={size:()=>data.length,read:(buffer,length)=>{const n=Math.min(length,data.length-position);buffer.set(data.subarray(position,position+n));position+=n;return options.readFail?0:n;},seek:n=>{seeks.push(n);position=n;return true;},close(){}};
  const context=vm.createContext({Buffer,Uint8Array,kFileChunkBytes:96,FILE_READ:0,SD:{open:()=>file},
    fileReliableToken:{store(){}},fileReliableWaitingSeq:{store(){}},ensureSdCard:()=>true,
    fileRowCount:1,fileRows:[{name:'wardrive-0001.csv'}],fileFullPath:n=>n,fileBaseName:n=>n,
    scanSdFiles(){},flushWardriveCsv(){},g_wardriveCsvPath:'',min:Math.min,
    millis:()=>++clock,delay(){},g_fileStreamAborted:false,
    fileSendReliable:packet=>{packets.push({...packet});return options.failKind!==packet.kind;},
  });
  vm.runInContext(crc+adapted,context);
  const checksum=(context.fileCrcUpdate(0xffffffff,data,data.length)^0xffffffff)>>>0;
  return {context,packets,seeks,checksum,run:(seq=0,size=0,expected=0)=>context.send(0,1234,seq,size,expected)};
}
const sample=Buffer.from('123456789'.repeat(15000));
test('firmware CRC32 and new transfer manifest match every emitted byte',()=>{
  const t=harness(sample);assert.equal(t.context.fileCrcUpdate(0xffffffff,Buffer.from('123456789'),9)^-1,0xcbf43926|0);
  t.run();
  assert.equal(t.packets.find(p=>p.kind===3).data,`${t.checksum.toString(16).padStart(8,'0')},wardrive-0001.csv`);
  assert.deepEqual(Buffer.concat(t.packets.filter(p=>p.kind===0).map(p=>Buffer.from(p.data,'base64'))),sample);
  assert.equal(t.packets.at(-1).kind,4);
  assert.equal(t.packets.at(-1).seq,Math.ceil(sample.length/96)+1);
});
test('firmware resumes from exact byte offset and verifies the complete snapshot',()=>{
  for(const seq of [1,12,15,1025,Math.ceil(sample.length/96)+1]){
    const t=harness(sample);t.run(seq,sample.length,t.checksum);
    const offset=Math.min((seq-1)*96,sample.length);
    assert.equal(t.seeks.at(-1),offset);
    assert.deepEqual(Buffer.concat(t.packets.filter(p=>p.kind===0).map(p=>Buffer.from(p.data,'base64'))),sample.subarray(offset));
    assert.equal(t.packets.at(-1).kind,4);
  }
});
test('appended CSV resumes the original prefix; mutation and truncation refuse resume',()=>{
  const original=harness(sample);
  const growing=harness(Buffer.concat([sample,Buffer.from('new row\n')]));growing.run(15,sample.length,original.checksum);
  assert.equal(growing.packets.at(-1).kind,4);assert.equal(growing.packets.at(-1).totalBytes,sample.length);
  const changed=Buffer.from(sample);changed[0]^=1;
  for(const bytes of [changed,sample.subarray(0,100)]){
    const t=harness(bytes);t.run(15,sample.length,original.checksum);
    assert.equal(t.packets.at(-1).kind,2);assert.match(t.packets.at(-1).data,/FILE_CHANGED/);
    assert.equal(t.packets.filter(p=>p.kind===0).length,0);
  }
});
test('empty files, missing ACKs and SD read errors have explicit terminal results',()=>{
  const t=harness(Buffer.alloc(0));t.run();assert.equal(t.packets.at(-1).data,'00000000,wardrive-0001.csv');
  const resume=harness(Buffer.alloc(0));resume.run(2,0,0);assert.equal(resume.packets.at(-1).kind,4);
  for(const options of [{failKind:0},{readFail:true}]){
    const failed=harness(sample,options);failed.run();assert.equal(failed.packets.at(-1).kind,2);
  }
});
