// Run with an authorized browser tab, or {playwright: a Playwright Page}.
// The application is exercised only through visible controls and downloaded JSON.
import assert from 'node:assert/strict';
import { readFile, writeFile } from 'node:fs/promises';
export async function configuration(tab) {
  await tab.playwright.getByRole('button',{name:'Configuration',exact:true}).click();
  const c=JSON.parse(await tab.playwright.getByLabel('Configuration JSON',{exact:true}).evaluate(e=>e.value));
  await tab.playwright.getByRole('button',{name:'Close configuration',exact:true}).click();
  return c;
}
export async function setConfiguration(tab,c) {
  await tab.playwright.getByRole('button',{name:'Configuration',exact:true}).click();
  await tab.playwright.getByLabel('Configuration JSON',{exact:true}).fill(JSON.stringify(c));
  await tab.playwright.getByRole('button',{name:'Apply configuration',exact:true}).click();
  await tab.playwright.getByRole('dialog',{name:'Simulation configuration',exact:true}).waitFor({state:'hidden'});
}
export async function exportResult(tab) {
  const promise=tab.playwright.waitForEvent('download');
  await tab.playwright.getByRole('button',{name:'Result & trace',exact:true}).click();
  const download=await promise;return JSON.parse(await readFile(await download.path(),'utf8'));
}
export async function startPaused(tab) {
  await tab.playwright.getByRole('button',{name:'Run simulation',exact:true}).click();
  await tab.playwright.getByRole('button',{name:'Pause',exact:true}).click();
  return exportResult(tab);
}
export async function seek(tab,index) {
  const slider=tab.playwright.getByLabel('Event position',{exact:true});
  let current=Number(await slider.evaluate(e=>e.value));
  const maximum=Number(await slider.getAttribute('max'));
  if (index<current&&index<current-index) {await slider.press('Home');current=0;}
  if (index>current&&maximum-index<index-current) {await slider.press('End');current=maximum;}
  for (;current!==index;current+=index>current?1:-1) await slider.press(index>current?'ArrowRight':'ArrowLeft');
  assert.equal(Number(await slider.evaluate(e=>e.value)),index);
}
export async function composed(tab) {
  return tab.playwright.evaluate(()=>Number([...document.querySelectorAll('.metric')].find(e=>e.textContent.includes('COMPOSED SYSTEMS')).querySelector('.metric-value').textContent.trim()));
}
export async function policyRegression(tab) {
  const c=await configuration(tab);c.resources.find(r=>r.id==='cpu-a').rate=777;
  c.links[0].latency=.00123;c.settings.cooldown=2.25;
  await setConfiguration(tab,c);
  const hashes=[];
  for (const policy of ['static','demand','cost']) {
    await tab.playwright.getByLabel('Policy',{exact:true}).selectOption(policy);
    const actual=await configuration(tab);
    assert.deepEqual(actual,{...c,policy},'policy-only changes must preserve every custom input');
    const r=await startPaused(tab);
    assert.deepEqual(r.config,actual,'run must submit the preserved custom input');
    hashes.push([r.workload_hash,r.hardware_hash]);
    await tab.playwright.getByRole('button',{name:'Reset',exact:true}).click();
  }
  assert.deepEqual(hashes[0],hashes[1]);assert.deepEqual(hashes[1],hashes[2]);
  return {policies:3,custom_rate:777,fingerprints:hashes[0],passed:true};
}
export async function commitRegression(tab) {
  const r=await startPaused(tab);
  const commit=r.events.find(e=>e.kind==='composition');
  assert.ok(commit);assert.equal(r.events[commit.index-1].time,commit.time,'fixture must exercise simultaneous events');
  await seek(tab,commit.index-1);assert.equal(await composed(tab),0,'composition cannot appear before its commit event');
  await seek(tab,commit.index);assert.equal(await composed(tab),1);
  await seek(tab,commit.index-1);assert.equal(await composed(tab),0,'backward seek must restore pre-commit state');
  await seek(tab,commit.index);assert.equal(await composed(tab),1);
  return {before:commit.index-1,commit:commit.index,time:commit.time,passed:true};
}
export async function boundaryRegression(tab) {
  const r=await exportResult(tab);const discovery=r.events.find(e=>e.kind==='discovery');
  await tab.playwright.getByRole('button',{name:`Inspect ${discovery.resource}`,exact:true}).click();
  await seek(tab,discovery.index-1);
  assert.equal(await tab.playwright.locator('.inspector-name .state-label').innerText(),'Unregistered');
  await seek(tab,discovery.index);
  assert.equal(await tab.playwright.locator('.inspector-name .state-label').innerText(),'Discovered');
  const release=r.events.find(e=>e.kind==='release');
  await seek(tab,release.index-1);assert.equal(await composed(tab),1);
  await seek(tab,release.index);assert.equal(await composed(tab),0);
  await seek(tab,release.index-1);assert.equal(await composed(tab),1);
  await seek(tab,release.index);assert.equal(await composed(tab),0);
  const paused=Number(await tab.playwright.getByLabel('Event position',{exact:true}).evaluate(e=>e.value));
  for(const speed of ['1','4','16','64']) {
    await tab.playwright.getByLabel('Playback speed',{exact:true}).selectOption(speed);
    assert.equal(Number(await tab.playwright.getByLabel('Event position',{exact:true}).evaluate(e=>e.value)),paused,'speed cannot move a paused cursor');
  }
  assert.deepEqual(await exportResult(tab),r,'seek and speed cannot change exported evidence');
  return {discovery:discovery.index,release:release.index,speeds:4,export_unchanged:true,passed:true};
}
async function awaitProxy(log,predicate) {
  const end=Date.now()+10000;
  while(Date.now()<end) {
    let rows=[];try {rows=(await readFile(log,'utf8')).trim().split('\n').filter(Boolean).map(x=>JSON.parse(x));}catch{}
    if(rows.some(predicate)) return;
    await new Promise(r=>setTimeout(r,30));
  }
  throw Error('Expected delayed test response did not arrive');
}
export async function staleResponseRegression(tab,{settings,log}) {
  await writeFile(log,'');await writeFile(settings,JSON.stringify({rules:[{path:'/api/validate',rate:888,delay_ms:1200}]}));
  const old=await configuration(tab);old.resources.find(r=>r.id==='cpu-a').rate=888;
  await tab.playwright.getByRole('button',{name:'Configuration',exact:true}).click();
  await tab.playwright.getByLabel('Configuration JSON',{exact:true}).fill(JSON.stringify(old));
  await tab.playwright.getByRole('button',{name:'Apply configuration',exact:true}).click();
  await awaitProxy(log,r=>r.rate===888&&r.phase==='received');
  await tab.playwright.getByRole('button',{name:'Close configuration',exact:true}).click();
  await tab.playwright.getByLabel('Policy',{exact:true}).selectOption('demand');
  const latest=await configuration(tab);latest.resources.find(r=>r.id==='cpu-a').rate=999;
  await setConfiguration(tab,latest);
  await awaitProxy(log,r=>r.rate===888&&r.phase==='attempted');
  assert.equal((await configuration(tab)).resources.find(r=>r.id==='cpu-a').rate,999,'late validation cannot replace latest edited config');
  await writeFile(log,'');await writeFile(settings,JSON.stringify({rules:[{path:'/api/config/concurrent',delay_ms:1200}]}));
  await tab.playwright.getByLabel('Scenario',{exact:true}).selectOption('concurrent');
  await awaitProxy(log,r=>r.path.startsWith('/api/config/concurrent')&&r.phase==='received');
  await tab.playwright.getByLabel('Scenario',{exact:true}).selectOption('phases');
  await awaitProxy(log,r=>r.path.startsWith('/api/config/concurrent')&&r.phase==='attempted');
  const fresh=await configuration(tab);assert.equal(fresh.name,'phases');assert.equal(fresh.resources.find(r=>r.id==='cpu-a').rate,70,'scenario changes intentionally reload defaults');assert.equal(fresh.policy,'demand');
  await writeFile(log,'');await writeFile(settings,JSON.stringify({rules:[{path:'/api/run',delay_ms:1200}]}));
  await tab.playwright.getByRole('button',{name:'Run simulation',exact:true}).click();
  await awaitProxy(log,r=>r.path==='/api/run'&&r.phase==='received');
  await tab.playwright.getByLabel('Policy',{exact:true}).selectOption('static');
  await awaitProxy(log,r=>r.path==='/api/run'&&r.phase==='attempted');
  assert.equal(await tab.playwright.getByRole('button',{name:'Result & trace',exact:true}).isEnabled(),false,'stale run cannot restore a cleared result');
  await writeFile(log,'');await writeFile(settings,JSON.stringify({rules:[{path:'/api/compare',delay_ms:1200}]}));
  await tab.playwright.getByRole('button',{name:'Compare policies',exact:true}).click();
  await awaitProxy(log,r=>r.path==='/api/compare'&&r.phase==='received');
  await tab.playwright.getByLabel('Policy',{exact:true}).selectOption('cost');
  await awaitProxy(log,r=>r.path==='/api/compare'&&r.phase==='attempted');
  assert.equal(await tab.playwright.getByText('Let the engine settle the comparison',{exact:true}).isVisible(),true,'stale comparison cannot restore a cleared comparison');
  await writeFile(settings,JSON.stringify({rules:[]}));
  return {late_validation:true,late_default_config:true,late_run:true,late_comparison:true,passed:true};
}
export async function controlRegression(tab) {
  const r=await startPaused(tab);
  const slider=tab.playwright.getByLabel('Event position',{exact:true});const before=Number(await slider.evaluate(e=>e.value));
  await tab.playwright.getByRole('button',{name:'Step event',exact:true}).click();
  assert.equal(Number(await slider.evaluate(e=>e.value)),before+1,'Step advances exactly one event');
  await tab.playwright.getByLabel('Playback speed',{exact:true}).selectOption('64');
  await tab.playwright.getByRole('button',{name:'Resume',exact:true}).click();
  await tab.playwright.getByRole('button',{name:'Run simulation',exact:true}).waitFor({state:'visible'});
  assert.equal(Number(await slider.evaluate(e=>e.value)),r.events.length-1);
  assert.equal(await composed(tab),0);assert.deepEqual(await exportResult(tab),r);
  const promise=tab.playwright.waitForEvent('download');await tab.playwright.getByRole('button',{name:'Config',exact:true}).click();const download=await promise;
  assert.deepEqual(JSON.parse(await readFile(await download.path(),'utf8')),r.config);
  await tab.playwright.getByRole('button',{name:'Compare policies',exact:true}).click();
  const deadline=Date.now()+10000;
  while(!await tab.playwright.getByRole('button',{name:'Compare policies',exact:true}).isEnabled()) {if(Date.now()>deadline)throw Error('Comparison did not complete');await new Promise(resolve=>setTimeout(resolve,30));}
  assert.equal(await tab.playwright.getByText('Let the engine settle the comparison',{exact:true}).isVisible(),false);
  await tab.playwright.getByRole('button',{name:'Reset',exact:true}).click();
  assert.equal(await tab.playwright.getByRole('button',{name:'Result & trace',exact:true}).isEnabled(),false);
  assert.equal(await tab.playwright.getByRole('button',{name:'Step event',exact:true}).isEnabled(),false);
  assert.equal(await composed(tab),0);
  return {run_pause_step_resume_reset:true,comparison:true,config_export:true,result_export_unchanged:true,event_count:r.events.length,makespan:r.elapsed,passed:true};
}
