// Loopback-only test transport for reproducible response-order races.
// Never used by the application or either Windows launcher.
import http from 'node:http';
import { appendFileSync, existsSync, mkdirSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
const root=resolve(import.meta.dirname,'..');mkdirSync(resolve(root,'tmp'),{recursive:true});
const settings=resolve(root,'tmp/browser-proxy-settings.json');
const log=resolve(root,'tmp/browser-proxy-log.jsonl');
const port=Number(process.env.DISTRIARCH_PROXY_PORT||8002);
const target=process.env.DISTRIARCH_PROXY_TARGET||'http://127.0.0.1:8000';
let sequence=0;
http.createServer(async(req,res)=>{
  const id=sequence++;const chunks=[];let length=0;
  for await (const chunk of req) {length+=chunk.length;if(length>4*1024*1024){res.writeHead(413);res.end();return;}chunks.push(chunk);}
  const body=Buffer.concat(chunks);let rate=null;try {rate=JSON.parse(body).resources?.find(r=>r.id==='cpu-a')?.rate??null;}catch{}
  const rules=existsSync(settings)?JSON.parse(readFileSync(settings,'utf8')).rules||[]:[];
  const delay=rules.find(r=>req.url.startsWith(r.path)&&(r.rate===undefined||r.rate===rate))?.delay_ms||0;
  const record={id,path:req.url,method:req.method,rate,delay};
  appendFileSync(log,JSON.stringify({...record,phase:'received'})+'\n');
  try {
    const response=await fetch(target+req.url,{method:req.method,headers:{'content-type':req.headers['content-type']||'application/json'},body:['GET','HEAD'].includes(req.method)?undefined:body});
    const data=Buffer.from(await response.arrayBuffer());
    if(delay) await new Promise(r=>setTimeout(r,delay));
    appendFileSync(log,JSON.stringify({...record,phase:'attempted',status:response.status})+'\n');
    res.writeHead(response.status,{'content-type':response.headers.get('content-type')||'application/octet-stream','cache-control':'no-store'});res.end(data);
  }catch(e){res.writeHead(502);res.end(String(e));}
}).listen(port,'127.0.0.1',()=>console.log(`Browser race-test proxy http://127.0.0.1:${port} -> ${target}`));
