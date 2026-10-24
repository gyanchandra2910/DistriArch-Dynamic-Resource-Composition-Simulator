// Development integration harness. The server and every simulated result come
// from the native executable; Node is used only to drive real HTTP requests.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
const root = resolve(import.meta.dirname, '..');
const exe = resolve(root, 'build/distriarch.exe');
const port = 8001;
const base = `http://127.0.0.1:${port}`;
const folder = resolve(root, 'tmp/native-http');
mkdirSync(folder, { recursive: true });
const server = spawn(exe, ['serve', '--root', root, '--port', String(port)], {
  cwd: root, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'],
});
let logs = '';
server.stdout.on('data', b => { logs += b; });
server.stderr.on('data', b => { logs += b; });
let passed = 0;
function ok(name, fn) { fn(); passed++; console.log(`[PASS] HTTP / ${name}`); }
async function get(path) { return fetch(base + path, { signal: AbortSignal.timeout(30000) }); }
async function post(path, json) {
  return fetch(base + path, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(json), signal: AbortSignal.timeout(30000) });
}
function cli(args) {
  const r = spawnSync(exe, args, { cwd: root, windowsHide: true, encoding: 'utf8', timeout: 30000 });
  assert.equal(r.status, 0, r.stderr || r.error?.message); return r;
}
try {
  const deadline = Date.now() + 15000;
  while (true) {
    try { if ((await get('/api/health')).ok) break; } catch {}
    if (Date.now() > deadline || server.exitCode !== null) throw Error(`Native server did not start: ${logs}`);
    await new Promise(r => setTimeout(r, 100));
  }
  const health = await (await get('/api/health')).json();
  ok('health identifies native C++20', () => { assert.equal(health.status, 'ok'); assert.equal(health.implementation, 'C++20'); assert.equal(health.version,'3.0.0');assert.equal(health.trace_schema,'event-boundaries-v1'); });
  ok('five scenarios', () => {});
  assert.equal((await (await get('/api/scenarios')).json()).length, 5);
  const config = await (await get('/api/config/phases?seed=47')).json();
  ok('unsigned seed and selected scenario', () => { assert.equal(config.seed, 47); assert.equal(config.name, 'phases'); });
  for (const [path, status] of [['/api/config/unknown', 404], ['/api/config/phases?seed=-1', 422], ['/api/config/phases?seed=4294967296', 422], ['/api/config/phases?seed=oops', 422], ['/api/not-a-route', 404]]) {
    const r = await get(path); ok(`${path} returns ${status}`, () => assert.equal(r.status, status));
  }
  const normalized = await (await post('/api/validate', config)).json();
  ok('validate preserves typed config', () => assert.deepEqual(normalized, config));
  for (const [name, bad] of [['unknown fields', { ...config, typo: true }], ['fractional seed', { ...config, seed: 1.5 }], ['bad input', { bad: true }]]) {
    const r = await post('/api/run', bad); ok(`reject ${name}`, () => assert.equal(r.status, 422));
  }
  const malformed = await fetch(base + '/api/run', { method: 'POST', headers: { 'content-type': 'application/json' }, body: '{invalid' });
  ok('malformed JSON returns 422', () => assert.equal(malformed.status, 422));
  const cfgPath = resolve(folder, 'config.json'), resultPath = resolve(folder, 'cli.json');
  writeFileSync(cfgPath, JSON.stringify(config));
  cli(['run', '--config', cfgPath, '--output', resultPath]);
  const expected = JSON.parse(readFileSync(resultPath, 'utf8'));
  const response = await post('/api/run', config);
  const result = await response.json();
  ok('actual HTTP result equals full native CLI trace', () => { assert.equal(response.status, 200); assert.deepEqual(result, expected); });
  ok('native trace complete and conserves work', () => { assert.equal(result.implementation, 'C++20');assert.equal(result.version,3);assert.equal(result.trace_schema,'event-boundaries-v1'); assert.equal(result.status, 'completed'); assert.equal(result.summary.completed_chunks, 20); assert.equal(result.summary.unfinished_jobs, 0); });
  const replay = await (await post('/api/run', config)).json();
  ok('actual HTTP deterministic replay', () => assert.deepEqual(replay, result));
  const comparisons = await (await post('/api/compare', config)).json();
  ok('three matched policies', () => { assert.equal(comparisons.length, 3); assert.equal(new Set(comparisons.map(x => x.workload_hash)).size, 1); assert.equal(new Set(comparisons.map(x => x.hardware_hash)).size, 1); });
  for (const policy of ['static', 'demand', 'cost']) {
    writeFileSync(cfgPath, JSON.stringify({ ...config, policy })); cli(['run', '--config', cfgPath, '--output', resultPath]);
    const r = JSON.parse(readFileSync(resultPath, 'utf8'));
    ok(`comparison ${policy} equals CLI`, () => assert.deepEqual(comparisons.find(x => x.policy === policy).summary, r.summary));
  }
  const saved = await (await get('/api/experiments')).json();
  ok('81 C++ experiments served', () => { assert.equal(saved.run_count, 81); assert.equal(saved.implementation, 'C++20'); });
  const page = await get('/'); const html = await page.text();
  ok('production React HTML served by C++', () => { assert.equal(page.status, 200); assert.match(html, /<div id="root">/); assert.equal(html,readFileSync(resolve(root,'frontend/dist/index.html'),'utf8')); });
  const asset = html.match(/src="([^"]+\.js)"/)[1];
  const bundle = await get(asset);
  ok('production JavaScript asset served', () => { assert.equal(bundle.status, 200); assert.match(bundle.headers.get('content-type'), /javascript/); });
  assert.equal(await bundle.text(),readFileSync(resolve(root,'frontend/dist',asset.replace(/^\//,'')),'utf8'));
  const invalidCLI = spawnSync(exe, ['run', '--typo', '1'], { cwd: root, windowsHide: true, encoding: 'utf8' });
  ok('native CLI rejects unknown option', () => { assert.equal(invalidCLI.status, 2); assert.match(invalidCLI.stderr, /Unknown option/); });
  const portable = spawnSync(exe, ['run', '--scenario', 'steady'], { cwd: root, windowsHide: true, encoding: 'utf8', env: { ...process.env, PATH: `${process.env.SystemRoot}\\System32` } });
  ok('standalone EXE with compiler/Node/Python absent from PATH', () => { assert.equal(portable.status, 0, portable.stderr); assert.equal(JSON.parse(portable.stdout).implementation, 'C++20'); });
  const report = { implementation: 'C++20', passed, failed: 0, cli_api_exact: true, port, health, portable_executable: true };
  writeFileSync(resolve(root, 'evidence/cpp-http-verification.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(`Native HTTP/CLI integration: ${passed} passed, 0 failed`);
} finally {
  server.kill();
}
