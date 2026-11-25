// Browser regression of the bundled firmware page against explicit HTTP fixtures.
const assert = require('node:assert/strict');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const { chromium } = require('playwright');
const root = path.resolve(__dirname,'..');
const header = fs.readFileSync(path.join(root,'firmware/HFMConnect/WebUiAssets.h'),'utf8');
function asset(name) {
  const at = header.indexOf(name+'[]');
  assert(at >= 0,name);
  const from = header.indexOf('{',at), to = header.indexOf('}',from);
  return Buffer.from(header.slice(from+1,to).match(/0x[0-9a-f]{2}/gi).map(s=>parseInt(s,16)));
}
const pageHtml = zlib.gunzipSync(asset('WEBUI_INDEX_GZ'));
const reference = zlib.gunzipSync(asset('WEBUI_REF_GZ'));
const defs = [
  {id:0,module:'HFM',variant:'c814',protocol:'KS-3',contact:8,mux:1,available:true,dtc:true,measures:[]},
  {id:1,module:'ABS',variant:'0aa2@30',protocol:'KS-1',contact:6,mux:3,available:true,dtc:true,measures:[]},
  {id:2,module:'ME2',variant:'GL_ISO2',protocol:'CDB',contact:4,mux:0,available:false,dtc:false,measures:[]},
  {id:3,module:'KLA',variant:'GL_ISO2',protocol:'GL_ISO2',contact:7,mux:0,available:true,dtc:true,clear:true,measures:[]}
];
async function main(){
  let state, history, jobs, screen, requests, requestSequence, failRead, failJob, serviceParams, plan;
  function reset(){
    state={id:-1,epoch:1,series:'',contact:0,confirmed:false,module:'HFM',ident:[],raw:[],faults:[]};
    history=[];jobs=0;screen=1;requests=[];requestSequence=[];failRead=false;failJob=0;serviceParams=[];plan={ok:false,checks:[]};
  }
  reset();
  const server=http.createServer(async(req,res)=>{
    const url=new URL(req.url,'http://127.0.0.1');const chunks=[];
    for await(const ch of req)chunks.push(ch);
    const body=new URLSearchParams(Buffer.concat(chunks).toString());
    function json(data){res.setHeader('content-type','application/json');res.end(JSON.stringify(data));}
    if(url.pathname==='/'){res.setHeader('content-type','text/html; charset=utf-8');return res.end(pageHtml);}
    if(url.pathname==='/ref.json'){res.setHeader('content-type','application/json');return res.end(reference);}
    if(url.pathname==='/api/scanner/catalog')return json(url.searchParams.has('series')?defs:['124']);
    if(url.pathname==='/api/scanner' && req.method==='GET')return json({selection:state,scan:history});
    if(url.pathname==='/api/scanner'){
      const action=body.get('action'),id=Number(body.get('id'));requests.push([action,id,body.get('contact')]);
      if(action==='select')state={...state,id,epoch:state.epoch+1,confirmed:false,series:'124',module:defs[id].module};
      if(action==='attach')Object.assign(state,{confirmed:true,contact:Number(body.get('contact')),identified:true,ident:['41','42']});
      if(action==='scan'){history=[{id,module:defs[id].module,identified:true,complete:true,contact:state.contact,codes:[]}];state={...state,confirmed:false,epoch:state.epoch+1};}
      if(action==='faults')Object.assign(state,{faults_valid:true,faults:[{code:37,status:0}]});
      return json({id:++jobs,ok:true});
    }
    if(url.pathname==='/api/services')return json({services:{ops:[{id:'dtc_clear',allowed:true,name:'Стирание памяти неисправностей'}],plan,pw_lock_s:0}});
    if(url.pathname==='/api/job' && req.method==='POST' && body.get('op')==='dtc_clear'){
      serviceParams.push(Object.fromEntries(body));
      plan={ok:true,expires_in_ms:60000,token:'1234ABCD',checks:[]};
      return json({id:++jobs,ok:true});
    }
    if(url.pathname==='/api/job' && req.method==='POST'){screen=Number(body.get('screen'));requestSequence.push(screen);++jobs;if(failRead)failJob=jobs;return json({id:jobs,ok:true});}
    if(url.pathname==='/api/job')return json(Number(url.searchParams.get('id'))===failJob ? {state:'failed',message:'Истекло время ответа'} : {state:'done',message:'Готово'});
    if(url.pathname==='/api/hht/screen')return json({screen,epoch:state.epoch,words:screen===2 ? [850,800,null,null,null,null,null,null] : [0,null,null,null,null,null,null,null]});
    if(url.pathname==='/api/status')return json({link:'paused',session:'disconnected',device:{},vehicle:null});
    if(url.pathname==='/api/files')return json({files:[],used:0,total:100000});
    if(url.pathname==='/api/report')return json({generator:'HFM Connect',selected_module:state,scan:history,status:{}});
    if(url.pathname==='/api/settings')return json({ui_theme:'auto'});
    if(url.pathname==='/api/report')return json({scan:history});
    return json({});
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  try {
    browser=await chromium.launch({headless:true,...(process.env.HFM_CHROME ? {executablePath:process.env.HFM_CHROME}: {})});
    for(const [width,theme] of [[360,'dark'],[390,'light'],[1100,'light']]){
      reset();const p=await browser.newPage({viewport:{width,height:780},colorScheme:theme});
      const errors=[];p.on('pageerror',e=>errors.push(e.message));
      await p.goto('http://127.0.0.1:'+server.address().port+'/#scanner');await p.waitForSelector('#scanModule option',{state:'attached'});
      await p.selectOption('#scanModule','0');await p.click('[data-scan="select"]');
      await p.waitForSelector('#scanConfirm');await p.click('[data-scan="attach"]');
      await p.waitForTimeout(100);assert.equal(requests.some(x=>x[0]==='attach'),false);
      await p.check('#scanConfirm');await p.click('[data-scan="attach"]');await p.waitForSelector('#scanScreen');
      await p.click('[data-scan="sample"]');await p.waitForFunction(()=>document.querySelector('#scanResults').textContent.includes('0 °C'));
      await p.selectOption('#scanScreen','2');await p.click('[data-scan="sample"]');
      await p.waitForFunction(()=>document.querySelector('#scanResults').textContent.includes('800 об/мин'));
      assert.deepEqual(requestSequence.slice(-2),[1,2]);
      failRead=true;
      await p.click('[data-scan="sample"]');
      await p.waitForFunction(()=>!document.querySelector('#scanResults').textContent.includes('800 об/мин'));
      failRead=false;
      // A user can select the next ECU while the previous status request is in flight.
      let releaseStatus;
      const held=new Promise(resolve=>{releaseStatus=resolve;});
      const pendingStatus=p.waitForRequest(req=>new URL(req.url()).pathname==='/api/scanner' && req.method()==='GET');
      const holdStatus=async route=>{
        if(route.request().method()==='GET')await held;
        await route.continue();
      };
      await p.route('**/api/scanner',holdStatus);
      try {
        await pendingStatus;
        await p.selectOption('#scanModule','1');
        await p.click('[data-scan="select"]');
      } finally {releaseStatus();}
      await p.check('#scanConfirm');
      await p.unroute('**/api/scanner',holdStatus);
      assert.equal(await p.inputValue('#scanPin'),'6');
      await p.click('[data-scan="attach"]');await p.waitForSelector('[data-scan="scan"]');
      await p.click('[data-scan="faults"]');await p.waitForFunction(()=>document.querySelector('#scanResults').textContent.includes('0025'));
      await p.click('[data-scan="scan"]');await p.waitForSelector('a[download]');
      assert.match(await p.locator('#scanSummary').innerText(),/Неисправностей нет/);
      assert.equal(await p.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),true);
      assert.equal(await p.evaluate(()=>S.timers.length),1,'scanner keeps one timer across polls');
      await p.selectOption('#scanModule','2');await p.waitForSelector('#scanRoute .note');
      assert.equal(await p.locator('[data-scan="attach"]').count(),0);
      await p.goto('http://127.0.0.1:'+server.address().port+'/#reports');
      await p.click('#mkRep');
      await p.waitForSelector('#repCard');
      assert.match(await p.locator('#repCard').innerText(),/Результаты опроса/);
      assert.match(await p.locator('#repCard').innerText(),/Антиблокировочная система/);
      await p.goto('http://127.0.0.1:'+server.address().port+'/#scanner');
      await p.waitForSelector('#scanModule option',{state:'attached'});
      await p.selectOption('#scanModule','3');await p.click('[data-scan="select"]');
      await p.check('#scanConfirm');await p.click('[data-scan="attach"]');
      await p.click('[data-scan="clear"]');
      await p.locator('label:has(#stopCf)').click();
      await p.locator('label:has(#batCf)').click();
      await p.click('#prepBtn');await p.waitForSelector('#svcPw');
      assert.equal(serviceParams.length,1);assert.equal(serviceParams[0].route,'3');assert.equal(serviceParams[0].epoch,String(state.epoch));
      assert.equal(serviceParams[0].confirm,'1');assert.equal(serviceParams[0].battery,'1');
      assert.deepEqual(errors,[]);
      await p.close();
      console.log('Browser passed:',width,theme);
    }
  } finally {if(browser)await browser.close();server.close();}
}
main().catch(e=>{console.error(e);process.exitCode=1;});
