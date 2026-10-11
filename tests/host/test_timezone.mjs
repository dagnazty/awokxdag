// Runs the checked-in C++ lookup/offset bodies as JS with type-only adaptation.
// This checks the flash data and decoder without compiling firmware.
import {readFileSync} from 'node:fs';
import {test} from 'node:test';
import assert from 'node:assert/strict';
const header = readFileSync(new URL('../../AWOKxDAG/gps_timezone.h', import.meta.url), 'utf8');
const data = readFileSync(new URL('../../AWOKxDAG/timezone_data.h', import.meta.url), 'utf8');
const kTree = [...data.split('kTree[] =')[1].split(';\nstatic const Zone')[0].matchAll(/"(?:\\.|[^"\\])*"/g)].map(m => JSON.parse(m[0])).join('');
const kZones = [...data.split('kZones[] = {')[1].split('};')[0].matchAll(/\{"([^"]+)", (\d+), (\d+)\}/g)].map(([,name,first,count]) => ({name,first:+first,count:+count}));
const kPeriods = [...data.split('kPeriods[] = {')[1].split('};')[0].matchAll(/\{(\d+)U, (-?\d+)\}/g)].map(([,since,offsetAndDst]) => ({since:+since,offsetAndDst:+offsetAndDst}));
const kZoneCount = kZones.length, kEpoch = 1577836800, kEnd = 4102444800;
const zoneByName = name => kZones.findIndex(z => z.name === name);
function body(name) {
  let start = header.indexOf('{', header.indexOf(` ${name}(`)), end = start + 1, depth = 1;
  while (depth) {if (header[end] === '{') depth++; if (header[end] === '}') depth--; end++;}
  return header.slice(start + 1, end - 1);
}
function adapt(s) {
  return s.replace(/int\(kTree\[([^\]]+)\]\)/g, 'kTree.charCodeAt($1)')
    .replace('(low + high) / 2', 'Math.trunc((low + high) / 2)')
    .replace(/sizeof\(kTree\) - 1/g, 'kTree.length')
    .replace(/\bconst (?:int|uint32_t|Zone&) /g,'const ')
    .replace(/\b(?:int|double) (?=[a-z])/g,'let ')
    .replace(/\b(?:int|size_t|uint32_t)\(/g,'Math.trunc(');
}
const zoneAt = new Function('lat','lon','kTree','kZoneCount','zoneByName', adapt(body('zoneAt')));
const at = (lat,lon) => zoneAt(lat,lon,kTree,kZoneCount,zoneByName);
const offsetBody = adapt(body('offsetAt')).replace('return true;', 'return {minutes, dst};');
const offset = new Function('zone','epoch','kZones','kPeriods','kZoneCount','kEpoch','kEnd', 'let minutes, dst;\n'+offsetBody);
const info = (name,date) => offset(zoneByName(name),Date.parse(date)/1000,kZones,kPeriods,kZoneCount,kEpoch,kEnd);

test('map bounds and table integrity', () => {
  for (const pair of [[NaN,0],[0,Infinity],[91,0],[0,181]]) assert.equal(at(...pair),-1);
  for (const pair of [[90,180],[-90,-180],[0,180],[0,-180]]) assert.ok(at(...pair)>=0);
  for (const z of kZones) {
    assert.ok(z.first + z.count <= kPeriods.length);
    assert.equal(kPeriods[z.first].since,0);
    for (let i=1;i<z.count;i++) assert.ok(kPeriods[z.first+i].since>kPeriods[z.first+i-1].since);
  }
  assert.equal(info('missing','2026-01-01'),false);
  assert.equal(info('America/Detroit','2100-01-01'),false);
});
test('GPS coordinates choose geographic zones worldwide', () => {
  for (const [lat,lon,name] of [
    [42.3314,-83.0458,'America/Detroit'],[41.8781,-87.6298,'America/Chicago'],
    [33.4484,-112.074,'America/Phoenix'],[21.3069,-157.8583,'Pacific/Honolulu'],
    [51.5074,-0.1278,'Europe/London'],[27.7172,85.324,'Asia/Kathmandu'],
    [-33.8688,151.2093,'Australia/Sydney'],[-31.55,159.08,'Australia/Lord_Howe'],
    [33.5731,-7.5898,'Africa/Casablanca'],[28.6139,77.209,'Asia/Kolkata']
  ]) assert.equal(kZones[at(lat,lon)].name,name);
});
test('Detroit spring and autumn transitions are correct to the second', () => {
  for (const [date,minutes,dst] of [
    ['2026-03-08T06:59:59Z',-300,false],['2026-03-08T07:00:00Z',-240,true],
    ['2026-11-01T05:59:59Z',-240,true],['2026-11-01T06:00:00Z',-300,false]
  ]) assert.deepEqual(info('America/Detroit',date),{minutes,dst});
});
test('no-DST, fractional offsets, southern hemisphere and irregular rules', () => {
  for (const [name,date,minutes,dst] of [
    ['America/Phoenix','2026-07-01',-420,false],['Pacific/Honolulu','2026-07-01',-600,false],
    ['Asia/Kathmandu','2026-07-01',345,false],['Asia/Kolkata','2026-07-01',330,false],
    ['Australia/Sydney','2026-01-01',660,true],['Australia/Sydney','2026-07-01',600,false],
    ['Australia/Lord_Howe','2026-01-01',660,true],['Australia/Lord_Howe','2026-07-01',630,false],
    ['Africa/Casablanca','2026-03-01',0,true],['Africa/Casablanca','2026-07-01',60,false],
    ['America/Detroit','2099-07-01',-240,true]
  ]) assert.deepEqual(info(name,date),{minutes,dst},`${name} ${date}`);
});
test('local dates roll over without changing the absolute clock', () => {
  for (const [name,date,expected] of [
    ['America/Detroit','2026-01-01T02:00:00Z','2025-12-31T21:00:00.000Z'],
    ['Asia/Kathmandu','2026-01-01T23:00:00Z','2026-01-02T04:45:00.000Z']
  ]) assert.equal(new Date(Date.parse(date)+info(name,date).minutes*60000).toISOString(),expected);
});
// Optional full parity check against the pinned upstream file downloaded for generation.
if (process.env.AWOK_TZ_REFERENCE) {
  const {createRequire} = await import('node:module');
  const reference = createRequire(import.meta.url)(process.env.AWOK_TZ_REFERENCE);
  test('100,000 deterministic points match upstream map decoder', () => {
    let seed = 12345;
    const random = () => ((seed = (Math.imul(seed,1664525)+1013904223)>>>0)/2**32);
    for (let i=0;i<100000;i++) {
      const lat=random()*180-90,lon=random()*360-180;
      assert.equal(kZones[at(lat,lon)].name,reference(lat,lon));
    }
  });
}
