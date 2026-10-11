// Run the Recon menu control flow with UI mocks (no firmware build). Verifies
// the Network-Tools-style card layout: group picker and tool lists render as
// title+description cards, 4 per page, with a Back / Prev / Next pager, and that
// navigation, paging and launch routing are preserved.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const read=name=>readFileSync(new URL(`../../AWOKxDAG/${name}`,import.meta.url),'utf8');
const main=read('AWOKxDAG.ino'), input=read('input.ino');
function block(s,start){
  assert.ok(start>=0);let end=s.indexOf('{',start)+1,depth=1;
  while(depth){if(s[end]==='{')depth++;if(s[end]==='}')depth--;end++;}
  return s.slice(start,end);
}
function fn(s,name){return block(s,s.search(new RegExp(`(?:void|bool|int|String) ${name}\\(`)));}
function adapt(s){return s
  .replace(/^(?:void|bool|int|String) (\w+)\(([^)]*)\)/gm,(_,n,a)=>`function ${n}(${a.replace(/const String& |bool |int /g,'')})`)
  .replace(/const (?:bool|int|String) /g,'const ')
  .replace(/\b(?:int|String) (\w+)\s*=/g,'let $1 =')
  .replace(/for \(const auto& (\w+) : (\w+)\)/g,'for (const $1 of $2)')
  .replaceAll('View::','View.')
  .replace('(reconVisibleItemCount() + kMenuPerPage - 1) / kMenuPerPage',
           'Math.floor((reconVisibleItemCount() + kMenuPerPage - 1) / kMenuPerPage)');}

const cats=[...main.match(/kReconCategories\[\] = \{([^}]*)\}/)[1].matchAll(/"([^"]+)"/g)].map(m=>m[1]);
const catDetails=[...main.match(/kReconCategoryDetails\[\] = \{([\s\S]*?)\}/)[1].matchAll(/"([^"]+)"/g)].map(m=>m[1]);
const items=[...main.match(/kReconItems\[\] = \{([\s\S]*?)\};/)[1].matchAll(/\{"([^"]+)", "([^"]+)", (\d)\}/g)]
  .map(m=>({label:m[1],detail:m[2],category:Number(m[3])}));
const num=name=>Number(main.match(new RegExp(`constexpr int ${name} =\\s*(\\d+)`))[1]);

function harness(){
  const cards=[],buttons=[],calls=[];let header=[];
  const c=vm.createContext({String,max:Math.max,min:Math.min,strcmp:(a,b)=>a===b?0:1,
    reconCategory:-1,reconPage:0,savedCount:7,currentView:'kRecon',
    View:{kRecon:'kRecon'},kReconCategories:cats,kReconCategoryDetails:catDetails,kReconItems:items,
    kReconCategoryCount:cats.length,kReconItemCount:items.length,
    kMenuPerPage:num('kMenuPerPage'),kMenuFirstY:num('kMenuFirstY'),
    kMenuRowPitch:num('kMenuRowPitch'),kMenuRowHeight:num('kMenuRowHeight'),
    kFooterTop:278,kScreenWidth:240,kBackground:0,kMuted:1,kAccent:2,
    display:{fillScreen(){cards.length=buttons.length=0;header=[];}},
    drawHeader:(...a)=>{header=a;},drawReconCard:(...a)=>cards.push(a),
    drawSmallButton:(...a)=>buttons.push(a),
    openNetworkTools:()=>calls.push(['network']),drawHome:()=>calls.push(['home']),
    launchReconItem:i=>calls.push(['launch',i])});
  vm.runInContext(adapt(['reconVisibleItemCount','reconPageCount','reconItemIndex',
    'reconItemLabel','reconItemDetail','drawReconMenu'].map(n=>fn(main,n)).join('\n')),c);
  vm.runInContext(adapt('function reconTap(x, y) {\n'+block(input,input.indexOf('if (currentView == View::kRecon)'))+'\n}'),c);
  return {c,cards,buttons,calls,header:()=>header,
    tap:(x,y)=>{c.currentView='kRecon';c.reconTap(x,y);},
    footer:()=>buttons.map(b=>b[4]),titles:()=>cards.map(cd=>cd[1])};
}
const rowY=r=>num('kMenuFirstY')+r*num('kMenuRowPitch')+2;

test('group picker renders four cards per page with descriptions, Network Tools spilling to page 2',()=>{
  const t=harness();t.c.drawReconMenu();
  assert.deepEqual(t.titles(),['Wi-Fi','Bluetooth','RF & Packets','Field Tools']);
  assert.deepEqual(t.cards[0],[0,'Wi-Fi',catDetails[0]]);
  assert.equal(t.c.reconPageCount(),2);
  assert.deepEqual(t.footer(),['Home','Next']);        // page 1: Back=Home, Next only
  t.c.reconPage=1;t.c.drawReconMenu();
  assert.deepEqual(t.titles(),['Network Tools']);
  assert.deepEqual(t.footer(),['Home','Prev']);        // last page: Back=Home, Prev only
});
test('every group and tool card carries its source description',()=>{
  for(let g=0;g<4;g++){
    const t=harness();t.c.reconCategory=g;t.c.reconPage=0;t.c.drawReconMenu();
    const expected=items.filter(it=>it.category===g);
    for(const cd of t.cards){
      const it=expected.find(e=>cd[1].startsWith(e.label==='Saved'?'Saved':e.label));
      assert.ok(it,`card ${cd[1]} maps to a tool`);assert.equal(cd[2],it.detail);
    }
  }
});
test('picker taps enter a group or open Network Tools; group resets the page',()=>{
  const t=harness();t.c.reconPage=0;t.c.drawReconMenu();
  t.tap(120,rowY(2));assert.equal(t.c.reconCategory,2);assert.equal(t.c.reconPage,0);
  const n=harness();n.c.reconPage=1;n.c.drawReconMenu();n.tap(120,rowY(0));
  assert.deepEqual(n.calls.at(-1),['network']);         // Network Tools on page 2
});
test('Wi-Fi and Field Tools paginate (6 tools -> 2 pages) and launch the right index',()=>{
  for(const g of [0,3]){
    const t=harness();t.c.reconCategory=g;t.c.reconPage=0;t.c.drawReconMenu();
    assert.equal(t.c.reconVisibleItemCount(),6);assert.equal(t.c.reconPageCount(),2);
    t.tap(120,rowY(1));                                 // second card
    const idx=items.findIndex((it,i)=>it.category===g && i>=0 &&
      items.filter((e,j)=>e.category===g&&j<=i).length===2);
    assert.deepEqual(t.calls.at(-1),['launch',idx]);
    t.c.reconPage=0;t.c.drawReconMenu();t.tap(200,298); // Next
    assert.equal(t.c.reconPage,1);
    assert.equal(t.cards.length,2);                     // page 2 has the 2 remaining tools
  }
});
test('Saved card shows its live count as the title',()=>{
  const t=harness();t.c.reconCategory=0;t.c.reconPage=0;t.c.drawReconMenu();
  assert.ok(t.titles().includes('Saved (7)'));
});
test('footer Back leaves: picker to Home, tool list to the group picker; paging is bounded',()=>{
  const t=harness();t.c.reconCategory=-1;t.c.reconPage=0;t.c.drawReconMenu();
  t.tap(40,298);assert.deepEqual(t.calls.at(-1),['home']);          // picker Back -> Home
  const g=harness();g.c.reconCategory=2;g.c.reconPage=0;g.c.drawReconMenu();
  g.tap(40,298);assert.equal(g.c.reconCategory,-1);                 // list Back -> Groups
  const p=harness();p.c.reconCategory=0;p.c.reconPage=0;p.c.drawReconMenu();
  p.tap(120,298);assert.equal(p.c.reconPage,0);                     // no Prev on page 1
});
test('taps in the gaps between cards and below the last card do nothing',()=>{
  const t=harness();t.c.reconCategory=1;t.c.reconPage=0;t.c.drawReconMenu(); // BLE: 3 tools
  const before=t.calls.length;
  for(const y of [num('kMenuFirstY')-2,                    // above first card
                  num('kMenuFirstY')+num('kMenuRowHeight')+2, // gap after card 0
                  rowY(3)])                                  // 4th slot (only 3 tools)
    t.tap(120,y);
  assert.equal(t.calls.length,before);assert.equal(t.c.currentView,'kRecon');
});
