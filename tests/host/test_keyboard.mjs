// Source-derived keypad model and multi-tap state machine; no C++ build.
import {readFileSync,writeFileSync} from 'node:fs';
import {test} from 'node:test';
import assert from 'node:assert/strict';
const source = readFileSync(new URL('../../AWOKxDAG/keyboard_layout.h',import.meta.url),'utf8');
const groups = name => [...source.match(new RegExp(`${name}\\[\\] = \\{([\\s\\S]*?)\\};`))[1].matchAll(/"(?:\\.|[^"\\])*"/g)].map(m=>JSON.parse(m[0]));
const letters=groups('kGroups'),symbols=groups('kSymbols');
const timeout=+source.match(/kTapTimeoutMs = (\d+)/)[1];
const kSlots=+source.match(/kSlots = (\d+)/)[1];
function body(name) {
  let start=source.indexOf('{',source.indexOf(` ${name}(`)),end=start+1,depth=1;
  while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}
  return source.slice(start+1,end-1);
}
function key(index,mode,mini=false) {
  if(index<0||index>=kSlots)return {valid:()=>false};
  const k={index,label:'',sublabel:'',choices:''};
  for(const field of ['x','y','w','h']) {
    const expression=source.match(new RegExp(`k\\.${field} = ([^;]+);`))[1].replace('index / 3','Math.trunc(index / 3)');
    k[field]=new Function('index','mini',`return ${expression}`)(index,mini);
  }
  if(index>=12)k.label=['Cancel','Del','Done'][index-12];
  else if(index===9||index===11){k.label=index===9?'*':'#';k.sublabel=index===9?'Mode':'Next';}
  else {
    k.label=index===10?'0':String(index+1);
    k.choices=mode===2?k.label:mode===3?symbols[index]:mode===1?letters[index].toUpperCase():letters[index];
    if(mode!==2){k.sublabel=mode===3?k.choices:k.choices.slice(0,-1);if(index===10)k.sublabel=mode===3?'sp !':'Space';}
  }
  k.valid=()=>true;
  k.contains=(x,y)=>x>=k.x&&x<k.x+k.w&&y>=k.y&&y<k.y+k.h;
  return k;
}
function adapt(s) {
  return s.replace(/const (?:Key|int) /g,'const ').replace(/\bint (?=[a-z])/g,'let ')
    .replace(/uint32_t\(([^)]+)\)/g,'(($1) >>> 0)').replace(/strlen\(k.choices\)/g,'k.choices.length');
}
function tapBody(name) {
  return adapt(body(name)).replace(/\b(index|choice|lastMs|commit|expire)\b/g,'this.$1');
}
const Tap=new Function('key','kTapTimeoutMs','Digits',`return class {
  index=-1;choice=0;lastMs=0;
  commit(){${tapBody('commit')}}
  expire(now){${tapBody('expire')}}
  press(pressed,mode,now,room){let replace;${tapBody('press').replace('return 0;','return {value:0,replace};').replace('return k.choices[this.choice];','return {value:k.choices[this.choice],replace};')}}
}`)(key,timeout,2);
const move=new Function('focus','direction','horizontal','mode','key','kSlots',adapt(body('move')));
const hit=new Function('x','y','mode','key','kSlots',adapt(body('hit')));

test('all 95 printable characters available; large targets and labels fit',()=>{
  const chars=new Set();
  for(let mode=0;mode<4;mode++)for(const mini of [false,true]) {
    const keys=Array.from({length:kSlots},(_,i)=>key(i,mode,mini));
    for(const k of keys){
      for(const c of k.choices)chars.add(c);
      assert.ok(k.x>=0&&k.y>=0&&k.x+k.w<=(mini?128:240)&&k.y+k.h<=(mini?128:320));
      if(!mini)assert.ok(k.w>=70&&k.h>=40);
      assert.ok(k.label.length*6*(mini||k.label.length>1?1:2)<k.w);
      assert.ok(k.sublabel.length*6<k.w);
      if(!mini)assert.equal(hit(k.x+k.w/2,k.y+k.h/2,mode,key,kSlots),k.index);
      for(const other of keys)if(other.index!==k.index)assert.ok(k.x+k.w<=other.x||other.x+other.w<=k.x||k.y+k.h<=other.y||other.y+other.h<=k.y);
    }
  }
  assert.equal([...chars].sort().join(''),Array.from({length:95},(_,i)=>String.fromCharCode(i+32)).join(''));
});
test('Mini joystick reaches every key in all modes',()=>{
  for(let mode=0;mode<4;mode++) {
    const seen=new Set([1]),queue=[1];
    while(queue.length) {
      const focus=queue.shift();
      for(const horizontal of [true,false])for(const direction of [-1,1]){
      const target=move(focus,direction,horizontal,mode,key,kSlots);
      if(!seen.has(target)){seen.add(target);queue.push(target);}
      }
    }
    assert.equal(seen.size,kSlots);
  }
});
test('repeat taps cycle, another key appends, timeout and Next commit',()=>{
  const tap=new Tap();
  assert.deepEqual(tap.press(1,0,100,true),{value:'a',replace:false});
  assert.deepEqual(tap.press(1,0,250,true),{value:'b',replace:true});
  assert.deepEqual(tap.press(1,0,400,true),{value:'c',replace:true});
  assert.deepEqual(tap.press(1,0,550,true),{value:'2',replace:true});
  assert.deepEqual(tap.press(1,0,700,true),{value:'a',replace:true});
  assert.deepEqual(tap.press(2,0,850,true),{value:'d',replace:false});
  tap.commit();
  assert.deepEqual(tap.press(2,0,1000,true),{value:'d',replace:false});
  assert.deepEqual(tap.press(2,0,2000,true),{value:'d',replace:false});
});
test('full fields can cycle the pending letter but cannot append; numeric mode appends immediately',()=>{
  const tap=new Tap();tap.press(1,0,100,true);
  assert.deepEqual(tap.press(1,0,200,false),{value:'b',replace:true});
  assert.equal(tap.press(2,0,300,false).value,0);
  tap.commit();
  assert.deepEqual(tap.press(1,2,400,true),{value:'2',replace:false});
  assert.deepEqual(tap.press(1,2,500,true),{value:'2',replace:false});
  tap.commit();
  assert.equal(tap.press(1,1,600,true).value,'A');
  tap.commit();
  assert.equal(tap.press(10,0,700,true).value,' ');
  assert.equal(tap.press(10,0,800,true).value,'0');
});
test('millis rollover still expires pending characters correctly',()=>{
  const tap=new Tap();tap.press(1,0,0xfffffff0,true);
  assert.equal(tap.expire(0x100),false);
  assert.equal(tap.expire(0x500),true);
});
if(process.env.AWOK_KEYPAD_PREVIEW)writeFileSync(process.env.AWOK_KEYPAD_PREVIEW,JSON.stringify([false,true].map(mini=>[0,1,2,3].map(mode=>Array.from({length:kSlots},(_,i)=>key(i,mode,mini))))));
