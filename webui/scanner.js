'use strict';
const MODULE_NAMES = {
  HFM:'Управление двигателем HFM', ABS:'Антиблокировочная система', ASR:'Устойчивость и тяга',
  BAS:'Усилитель экстренного торможения', ZAE:'Подушки безопасности', AB2:'Подушки безопасности',
  EGS:'Электронное управление коробкой', EAG:'Автоматическая коробка передач',
  KLA:'Климат-контроль', HAU:'Отопление салона', TAU:'Температура в салоне',
  EFP:'Электронный дроссель и круиз-контроль', TPM:'Электронный дроссель', LLR:'Регулирование холостого хода',
  PSE:'Пневмосистема и центральный замок', PSE1:'Пневмосистема комфорта', KFB:'Блок комфорта',
  IF1:'Дистанционный центральный замок', IF2:'Инфракрасный центральный замок', FFZ:'Радиоуправление замками',
  EDW:'Охранная сигнализация', EDW2:'Охранная сигнализация', EZS1:'Электронный замок зажигания',
  EPH:'Помощь при парковке', KI:'Комбинация приборов', ADS:'Адаптивная подвеска', ASD:'Блокировка дифференциала',
  PML:'Параметрическое рулевое управление', VG:'Раздаточная коробка', CVD:'Складная крыша и защитная дуга',
  DM:'Диагностический модуль', DM2:'Диагностический модуль', EDS:'Дизельный двигатель', ERE:'Регулирование дизеля',
  EVE:'Система впрыска дизеля', GM:'Базовый модуль', LH1:'Управление двигателем LH', EZ1:'Система зажигания',
  PMS:'Управление двигателем PMS', SH1:'Автономный отопитель', SH2:'Автономный отопитель',
  ESV1:'Регулировка сиденья', SAM1:'Обработка сигналов кузова', DBE1:'Потолочная панель',
  APS:'Навигация', CNS:'Связь и навигация', AID:'Телематический модуль', FGS:'Управление коробкой',
  AKS:'Автоматическое сцепление', GEM:'Электроника кузова', AAM:'Блок систем кузова',
  LWR:'Корректор фар', UBF1:'Нижняя панель управления', UBF2:'Нижняя панель управления',
  ME2:'Управление двигателем ME', M1L:'Управление двигателем, левый ряд', M1R:'Управление двигателем, правый ряд'
};
const scannerHistory = new Map();
const moduleName = code => MODULE_NAMES[code] || 'Электронный блок ' + code;
const diagCode = n => Number(n).toString(16).padStart(4,'0').toUpperCase();
function describeHht(field,raw) {
  if(raw==null)return 'Нет достоверного значения';
  const [, , unit, decimals, signed, choices]=field;
  if(choices.length) return choices.filter(([mask])=>(raw & mask)!==0).map(x=>x[1]).join(' · ') || 'Нет активных признаков';
  const value=(signed ? raw : raw&65535)/10**(decimals || 0);
  return value.toLocaleString('ru-RU',{maximumFractionDigits:decimals || 0,minimumFractionDigits:decimals || 0})+(unit ? ' '+unit : '');
}
function scannerChart(canvas,values) {
  if(!canvas || values.length<2)return;
  const view=canvas.getContext('2d'), width=canvas.width=360,height=canvas.height=58;
  view.clearRect(0,0,width,height);
  const lo=Math.min(...values),hi=Math.max(...values);
  view.strokeStyle=getComputedStyle(canvas).getPropertyValue('--accent');view.lineWidth=2;view.beginPath();
  values.forEach((v,i)=>{
    const x=3+i*(width-6)/(values.length-1),y=hi===lo ? height/2 : height-5-(v-lo)*(height-10)/(hi-lo);
    if(i)view.lineTo(x,y);else view.moveTo(x,y);
  });view.stroke();
}
async function scannerCommand(action,id,extra={}) {
  const sent=await post('scanner',Object.assign({action,id},extra));
  if(!sent.ok)throw Error('Очередь диагностики занята');
  const deadline=Date.now()+(action==='scan' ? 10*60000 : 90000);
  while(Date.now()<deadline) {
    await sleep(300);
    const reply=await api('job?id='+sent.id);
    if(reply.state==='failed')throw Error(reply.message || 'Операция не выполнена');
    if(reply.state==='done')return reply.message;
  }
  throw Error('Время ожидания истекло. Проверьте состояние операции в журнале');
}
function scannerReport(report,moduleOnly) {
  const m=report.selected_module;
  if(!m)return '';
  let html=moduleOnly ? '<div class="card pad" id="repCard"><h2>Диагностический отчёт</h2><p>'+esc(report.generator)+'</p><h3>'+esc(moduleName(m.module))+'</h3>'+kvTable([
    ['Серия',m.series],['Контакт',m.contact || 'не подтверждён'],['Идентификационный ответ',m.identified ? m.ident.join(' ') : 'нет данных'],
    ['Память неисправностей',m.faults_valid ? m.faults.map(f=>diagCode(f.code)).join(', ') || 'Неисправностей нет' : 'чтение не завершено']
  ]) : '';
  const sample=report.hht_screen;
  if(sample && HHT_SCREENS[sample.screen]) {
    const [title,fields]=HHT_SCREENS[sample.screen];
    html+='<h3>'+esc(title)+'</h3>'+kvTable(fields.map(f=>[f[1] || title,describeHht(f,sample.words[f[0]])]));
  }
  if(moduleOnly && m.raw?.length)html+='<h3>Запись '+esc(m.at)+'</h3><p class="mono">'+esc(m.raw.join(' '))+'</p>';
  if(report.before_module_clear) {
    const previous=report.before_module_clear;
    html+='<h3>Перед стиранием</h3><p>'+esc(moduleName(previous.module))+' · '+esc(previous.codes.map(diagCode).join(', '))+'</p>';
  }
  if(report.scan?.length)html+='<h3>Результаты опроса</h3>'+kvTable(report.scan.map(x=>[
    moduleName(x.module)+' · контакт '+x.contact,x.complete ? x.codes.map(diagCode).join(', ') || 'Неисправностей нет' : x.message
  ]));
  return html+(moduleOnly ? '</div>' : '');
}
R.scanner=async () => {
  const token=S.token; const here=()=>token===S.token;
  page('Сканер блоков','<div id="scanForm" class="card pad scan-flow"><label>Серия автомобиля<select id="scanSeries"></select></label><label>Электронный блок<select id="scanModule"></select></label><div id="scanRoute"></div></div><div id="scanResults" aria-live="polite"></div><div id="scanSummary"></div>');
  const [series,state]=await Promise.all([api('scanner/catalog'),api('scanner')]);if(!here())return;
  const seriesBox=$('#scanSeries'),moduleBox=$('#scanModule');
  let routes=[],latest=state,active=false,reading=false,manualStop=false,listVersion=0;
  let backgroundPoll=null;
  let screen=Number(sessionStorage.getItem('hht-screen')||1);
  if(!HHT_SCREENS[screen])screen=1;
  let sample=null,sampleEpoch=0,lastRead=0,averageCount=0,totals=[];
  const resetAverage=()=>{averageCount=0;totals=[];};
  seriesBox.replaceChildren(...series.map(s=>new Option(s,s)));
  const savedSeries=state.selection.series || sessionStorage.getItem('hht-series');
  if(series.includes(savedSeries))seriesBox.value=savedSeries;
  function chosen(){return routes.find(r=>r.id===Number(moduleBox.value));}
  function report() {
    const rows=latest.scan || [];
    $('#scanSummary').innerHTML=rows.length ? '<h2 class="section">Результаты опроса</h2><div class="list">'+rows.map(r=>
      '<div class="row"><div class="grow"><div class="t">'+esc(moduleName(r.module))+'</div><div class="small muted">Контакт '+r.contact+' · '+esc(r.complete ? (r.codes.length ? 'Коды '+r.codes.map(diagCode).join(', ') : 'Неисправностей нет') : r.identified ? 'Блок опознан, память не прочитана' : r.message)+'</div></div></div>').join('')+'</div><a class="btn sec" href="/api/report?dl=1" download>Скачать отчёт</a>' : '';
  }
  function routeForm() {
    const r=chosen();if(!r)return;
    const selected=latest.selection.id===r.id,confirmed=selected && latest.selection.confirmed;
    const desc=esc(r.protocol)+(r.variant ? ' · '+esc(r.variant) : '');
    let html='<p class="muted small">'+desc+'</p>';
    if(!r.available)html+='<p class="note">В каталоге HHT есть этот вариант. Его сценарий связи на этом адаптере пока недоступен.</p>';
    else if(!selected)html+='<button class="btn" data-scan="select">Выбрать блок и приостановить обмен</button>';
    else if(!confirmed)html+='<label>Контакт диагностического разъёма<input id="scanPin" type="number" min="1" max="38" '+(r.contact ? 'value="'+r.contact+'" readonly' : 'placeholder="По схеме автомобиля"')+'></label>'+
      (['KS-1','KS-6'].includes(r.protocol) && !r.fixed_baud ? '<label>Скорость после синхронизации<select id="scanBaud"><option value="0">9600 бод</option><option value="1">10400 бод</option><option value="2">4800 бод</option><option value="3">19200 бод</option></select></label>' : '')+
      '<label class="scan-confirm"><input type="checkbox" id="scanConfirm">Провод K-Line подключён к указанному контакту</label><button class="btn" data-scan="attach">Подключиться</button>';
    else html+='<p>Подключение через контакт <strong>'+latest.selection.contact+'</strong></p><div class="btns">'+
      (r.dtc ? '<button class="btn" data-scan="faults">Прочитать неисправности</button>' : '')+
      '<button class="btn sec" data-scan="scan">Опросить блоки на этом контакте</button><button class="btn sec" data-scan="pause">Остановить связь</button></div>';
    if(confirmed && r.clear && latest.selection.identified)html+='<button class="btn sec" data-scan="clear">Стереть память неисправностей</button>';
    if(confirmed && r.module==='HFM') html+='<label>Параметр двигателя<select id="scanScreen">'+Object.entries(HHT_SCREENS).map(([id,[title]])=>'<option value="'+id+'" '+(Number(id)===screen ? 'selected' : '')+'>'+esc(title)+' · '+id+'</option>').join('')+'</select></label><div class="btns"><button class="btn sec" data-scan="sample">Считать параметр</button><button class="btn sec" data-scan="monitor">'+(reading ? 'Остановить график' : 'Запустить график')+'</button></div>';
    if(confirmed && r.measures.length)html+='<label>Блок данных ЭБУ<select id="scanMeasure">'+r.measures.map((at,i)=>'<option value="'+i+'">'+(r.module==='EGS' && (at===21 || at===23) ? (at===21 ? 'Счётчик масла' : 'Счётчик двигателя') : 'Запись '+at)+'</option>').join('')+'</select></label><button class="btn sec" data-scan="measure">Считать данные</button>';
    $('#scanRoute').innerHTML=html;
  }
  function results() {
    const r=chosen(), s=latest.selection;
    let html='';
    if(r && r.id===s.id && r.module!=='HFM') {
      if(s.identified)html+='<div class="card pad"><h3>Идентификатор блока</h3><div class="mono scan-bytes">'+s.ident.map(esc).join(' ')+'</div></div>';
      if(s.faults_valid)html+='<div class="card pad"><h3>Память неисправностей</h3>'+(s.faults.length ? s.faults.map(d=>'<div>Код '+diagCode(d.code)+(r.protocol==='GL_ISO2' ? ' · статус '+d.status : '')+'</div>').join('') : 'Неисправностей нет')+'</div>';
      if(s.raw.length)html+='<div class="card pad"><h3>Запись '+s.at+'</h3><div class="mono scan-bytes">'+s.raw.map(esc).join(' ')+'</div></div>';
    }
    if(r && r.id===s.id && s.confirmed && r.module==='HFM' && sample && sample.screen===screen && sampleEpoch===s.epoch) {
      const [title,fields]=HHT_SCREENS[screen];
      if([1,7,13].includes(screen))html+='<p class="note info">При неисправности датчика ЭБУ может подставить резервное значение. Проверьте память неисправностей.</p>';
      html+='<h2 class="section">'+esc(title)+'</h2><div class="grid2">'+fields.map(f=>{
        const raw=sample.words[f[0]],text=describeHht(f,raw);
        return '<div class="card pad"><div class="small muted">'+esc(f[1]||title)+'</div><strong>'+esc(text)+'</strong>'+
          (averageCount && (screen===125 || screen===126) ? '<div class="small muted">Среднее '+Math.floor(totals[f[0]]/averageCount)+' об/мин · замеров '+averageCount+'</div>' : '')+
          (raw!=null && !f[5].length ? '<canvas class="scan-chart" id="scPlot'+f[0]+'" aria-label="График измерений"></canvas>' : '')+'</div>';
      }).join('')+'</div>';
    }
    $('#scanResults').innerHTML=html;
    if(r && sample) for(const f of HHT_SCREENS[screen][1])scannerChart($('#scPlot'+f[0]),scannerHistory.get([s.epoch,r.id,screen,f[0]].join('/'))||[]);
  }
  async function refresh() {
    const status=await api('scanner');if(!here())return;
    const changed=status.selection.epoch!==latest.selection.epoch || status.selection.confirmed!==latest.selection.confirmed;
    latest=status;if(changed){reading=false;sample=null;resetAverage();scannerHistory.clear();routeForm();}
    results();report();
  }
  async function list() {
    const seq=++listVersion,series=seriesBox.value;sessionStorage.setItem('hht-series',series);
    const rs=await api('scanner/catalog?series='+encodeURIComponent(series));if(!here() || seq!==listVersion)return;
    routes=rs;routes.sort((a,b)=>Number(b.available)-Number(a.available)||moduleName(a.module).localeCompare(moduleName(b.module),'ru'));
    moduleBox.replaceChildren(...routes.map(r=>new Option(moduleName(r.module)+' · '+r.module+(r.variant ? ' ('+r.variant+')' : ''),r.id)));
    if(routes.some(r=>r.id===latest.selection.id))moduleBox.value=latest.selection.id;
    reading=false;sample=null;routeForm();results();report();
  }
  async function readScreen() {
    const r=chosen(),requested=screen,epoch=latest.selection.epoch;
    if(!r || r.id!==latest.selection.id || !latest.selection.confirmed)return;
    if(!await job('hht_screen',{screen:requested,route:r.id,epoch},null,'silent')) {sample=null;results();throw Error(job.msg);}
    const result=await api('hht/screen');
    if(!here() || chosen()?.id!==r.id || latest.selection.epoch!==epoch || screen!==requested || result.screen!==requested || result.epoch!==epoch)return;
    sample=result;sampleEpoch=epoch;
    if((screen===125 || screen===126) && averageCount<65535) {
      const fields=HHT_SCREENS[screen][1];
      if(fields.every(f=>sample.words[f[0]]!=null)){
        for(const f of fields){const i=f[0];totals[i]=((totals[i]||0)+sample.words[i])>>>0;}
        ++averageCount;
      }
    }
    for(const f of HHT_SCREENS[screen][1]){
      const raw=sample.words[f[0]];if(raw==null || f[5].length)continue;
      const key=[epoch,r.id,screen,f[0]].join('/'),arr=scannerHistory.get(key)||[];arr.push(raw);if(arr.length>60)arr.shift();scannerHistory.set(key,arr);
    }
    results();
  }
  seriesBox.onchange=()=>list().catch(e=>toast(e.message,true));
  moduleBox.onchange=()=>{sample=null;reading=false;routeForm();results();};
  $('#scanRoute').onchange=e=>{if(e.target.id==='scanScreen'){screen=Number(e.target.value);sessionStorage.setItem('hht-screen',screen);sample=null;resetAverage();results();}};
  $('#scanRoute').onclick=async e=>{
    const btn=e.target.closest('[data-scan]'),r=chosen();if(!btn||!r)return;
    const action=btn.dataset.scan;
    if(action==='pause' && (active || backgroundPoll)){manualStop=true;reading=false;try{await scannerCommand('pause',r.id);}catch(err){toast(err.message,true);}return;}
    if(active)return;
    active=true;btn.disabled=true;
    try {
      if(backgroundPoll)await backgroundPoll;
      if(!here() || chosen()?.id!==r.id)return;
      if(action==='clear'){
        if(latest.selection.id!==r.id || !latest.selection.confirmed || !latest.selection.identified)return;
        reading=false;
        await serviceFlow('dtc_clear',{route:r.id,epoch:latest.selection.epoch});
        return;
      }
      let extra={};
      if(action==='attach'){
        if(!$('#scanConfirm').checked)throw Error('Подтвердите контакт подключения');
        extra={contact:$('#scanPin').value,speed:$('#scanBaud')?.value||0,confirmed:1};
      }
      if(action==='monitor'){reading=!reading;if(reading)resetAverage();routeForm();if(reading)await readScreen();}
      else if(action==='sample')await readScreen();
      else {
        if(action==='measure')extra.index=$('#scanMeasure').value;
        reading=false;manualStop=false;
        const msg=await scannerCommand(action,r.id,extra);
        if(!here())return;await refresh();routeForm();
        if(action==='faults' && r.module==='HFM')go('dtc');else if(msg && !manualStop)toast(msg);
      }
    } catch(err){reading=false;if(here())toast(err.message,true);}
    finally {active=false;btn.disabled=false;}
  };
  await list();
  async function poll() {
    try {
      await refresh();
      if(here() && !active && reading && Date.now()-lastRead>900){
        lastRead=Date.now();
        await readScreen();
      }
    } catch(e) {
      if(here() && reading){reading=false;routeForm();toast(e.message,true);}
    }
  }
  loop(async()=>{
    if(active || backgroundPoll || !here())return;
    backgroundPoll=poll();
    try {await backgroundPoll;}
    finally {backgroundPoll=null;}
  },1500);
};
