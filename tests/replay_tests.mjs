import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { gunzipSync } from 'node:zlib';
import { currentCompositions, intervalActive, onlineAt, projectResult, RequestEpoch, withPolicy } from '../frontend/src/replay.ts';
const r=JSON.parse(gunzipSync(readFileSync(new URL('../results/traces/main-phases-11-cost.json.gz',import.meta.url))));
const c=r.events.find(e=>e.kind==='composition');
test('before/after commit at the same timestamp, forward and backward',()=>{
  assert.equal(r.events[c.index-1].time,c.time);
  for (const [cursor,count] of [[c.index-1,0],[c.index,1],[c.index-1,0],[c.index,1]]) assert.equal(Object.keys(currentCompositions(projectResult(r,cursor))).length,count);
});
test('discovery does not become visible at the preceding service completion',()=>{
  for (const e of r.events.filter(e=>e.kind==='discovery')) {
    assert.equal(r.events[e.index-1].time,e.time);
    assert.equal(projectResult(r,e.index-1).discovery[e.resource],undefined);
    assert.equal(projectResult(r,e.index).discovery[e.resource],e.time);
  }
});
test('release is atomic at its event, including all reservation rows',()=>{
  const e=r.events.find(e=>e.kind==='release');
  const before=projectResult(r,e.index-1),after=projectResult(r,e.index);
  assert.ok(currentCompositions(before)[e.app]);assert.equal(currentCompositions(after)[e.app],undefined);
  const used=p=>Object.fromEntries(p.ownership.map(x=>[x.resource,x.used]));
  assert.ok(Object.values(used(before)).some(n=>n>0));assert.ok(Object.values(used(after)).every(n=>n===0));
});
test('every event boundary agrees with independently replayed event publications',()=>{
  const compositions={},discovered={},owners={};
  for (const e of r.events) {
    if (e.kind==='composition') compositions[e.app]=e.composition;
    if (e.kind==='release') delete compositions[e.app];
    if (e.kind==='discovery') discovered[e.resource]=e.time;
    for (const x of r.ownership.filter(x=>x.event_index===e.index)) owners[x.resource]=x.used;
    const p=projectResult(r,e.index);
    assert.deepEqual(currentCompositions(p),compositions);
    assert.deepEqual(p.discovery,discovered);
    assert.deepEqual(Object.fromEntries(p.ownership.map(x=>[x.resource,x.used])),owners);
    for (const t of p.transfers) assert.equal(t.end!==null,t.end_event!==null&&t.end_event<=e.index);
    for (const i of p.intervals) assert.equal(i.finished,i.end_event!==null&&i.end_event<=e.index);
  }
});
test('a completing interval remains active until its own same-time completion event',()=>{
  const end=r.events.find(e=>(e.kind==='service'||e.kind==='hop')&&e.index>0&&r.events[e.index-1].time===e.time);
  assert.ok(end);const before=projectResult(r,end.index-1);const i=before.intervals.find(x=>x.end_event===end.index);
  assert.ok(i);assert.equal(intervalActive(i,end.time),true);
  assert.equal(intervalActive(projectResult(r,end.index).intervals.find(x=>x.end_event===end.index),end.time),false);
});
test('a new CPU request at commit time stays hidden until it is submitted',()=>{
  const next=r.intervals.find(i=>i.kind==='compute'&&i.requested_event>c.index&&i.requested===c.time);
  assert.ok(next);assert.ok(!projectResult(r,c.index).intervals.some(i=>i.requested_event===next.requested_event));assert.ok(projectResult(r,next.requested_event).intervals.some(i=>i.requested_event===next.requested_event));
});
test('future phases, requests, transitions and physical online events stay hidden',()=>{
  const p=projectResult(r,-1);assert.equal(p.events.length,0);assert.equal(p.ownership.length,0);assert.equal(p.intervals.length,0);assert.equal(p.transfers.length,0);assert.equal(p.transitions.length,0);assert.equal(p.jobs[0].phases.length,0);assert.equal(onlineAt(p,'cpu-a'),false);
  for (const cursor of [0,c.index-1,c.index,r.events.length-1]) {const p=projectResult(r,cursor);for (const j of p.jobs) for (const phase of j.phases) assert.ok(phase.event_index<=cursor);}
});
test('pause/speed do not alter projections or original final evidence',()=>{
  const original=JSON.stringify(r),paused=projectResult(r,c.index);for (const speed of [1,4,16,64]) {assert.deepEqual(projectResult(r,c.index),paused);assert.ok(projectResult(r,Math.min(r.events.length-1,c.index+speed)));}
  assert.equal(JSON.stringify(r),original);const final=projectResult(r,r.events.length-1);assert.deepEqual(final.summary,r.summary);assert.equal(final.transfers.reduce((n,f)=>n+(f.end===null?0:f.bytes),0),r.summary.bytes_transferred);
});
test('policy-only changes preserve every custom field and do not mutate the input',()=>{
  const config=structuredClone(r.config);config.resources[0].rate=777;config.links[0].latency=.00123;config.settings.cooldown=2.25;
  for (const policy of ['static','demand','cost']) assert.deepEqual(withPolicy(config,policy),{...config,policy});assert.equal(config.policy,r.config.policy);assert.equal(withPolicy(null,'static'),null);
});
test('superseded request tickets stay invalid even when abort is ignored',()=>{
  const epoch=new RequestEpoch();const old=epoch.begin(),latest=epoch.begin();assert.equal(epoch.current(old),false);assert.equal(epoch.current(latest),true);epoch.begin();assert.equal(epoch.current(latest),false);
});
