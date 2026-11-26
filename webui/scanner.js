'use strict';
const MODULE_NAMES = {
  'ME-OBD':'ME-SFI · диагностика OBD-II', KE:'KE-Jetronic',
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
const OBD_PIDS={
  4:['Нагрузка двигателя','%',a=>100*a/255],5:['Температура охлаждающей жидкости','°C',a=>a-40],
  11:['Абсолютное давление во впуске','кПа',a=>a],12:['Частота вращения','об/мин',(a,b)=>(256*a+b)/4],
  13:['Скорость автомобиля','км/ч',a=>a],15:['Температура воздуха на впуске','°C',a=>a-40],
  16:['Массовый расход воздуха','г/с',(a,b)=>(256*a+b)/100],17:['Положение дросселя','%',a=>100*a/255]
};
const CFI_N3={
  2:'Контакт полной нагрузки',3:'Температура охлаждающей жидкости',4:'Потенциометр расходомера',
  5:'Сигнал лямбда-зонда',7:'Сигнал частоты вращения',8:'Высотная коррекция',9:'Ток электрогидравлического регулятора',
  10:'Контакт закрытого дросселя',11:'Система вторичного воздуха',12:'Давление во впуске от блока зажигания',
  13:'Температура воздуха на впуске',14:'Сигнал скорости',16:'Клапан рециркуляции',17:'Замыкание сигнального провода лямбда-зонда',
  18:'Ток клапана холостого хода',22:'Напряжение подогрева лямбда-зонда',23:'Замыкание цепи клапана продувки на плюс',
  25:'Замыкание цепи пусковой форсунки на плюс',26:'Замыкание цепи задержки переключения на плюс',
  27:'Связь между впрыском и зажиганием',28:'Прерывистый контакт датчика температуры ОЖ',
  29:'Расхождение температуры ОЖ в двух блоках',31:'Прерывистый контакт датчика температуры воздуха',
  34:'Температура ОЖ от блока зажигания'
};
const isEngineMode=r=>r.module==='ME-OBD' || r.module==='KE';
const isDuty=r=>r.module==='KE' && r.variant==='X11 PWM';
const obdCode=code=>'PCBU'[code>>>14]+((code>>>12)&3)+diagCode(code).slice(1);
const hasPid=(mask,pid)=>((Number(mask) >>> (32-pid)) & 1)!==0;
function engineConnector(r) {
  if(r.module==='ME-OBD')return 'OBD-II (J1962), контакт 7. Для автомобилей с диагностикой OBD-II по K-Line.';
  if(isDuty(r))return 'X11, контакт 3. Измерительный вход с высоким сопротивлением, выход передатчика отключён. Сверьте вывод по схеме автомобиля.';
  return 'X11/4, контакт 3 блока CFI N3 на соответствующих M104/M119. Включите зажигание. Одна команда считывает один импульсный код.';
}
function engineRows(s) {
  const rows=[['Режим',s.variant],['Разъём',engineConnector(s)]];
  if(s.module==='ME-OBD') {
    if(!s.obd_response)return rows.concat([['Связь','Нет ответа OBD-II']]);
    rows.push(['Ответ OBD-II',(s.obd_kwp ? 'ISO 14230-4' : 'ISO 9141-2')+' · адрес 0x'+diagCode(s.obd_peer).slice(-2)]);
    const pid=OBD_PIDS[s.at];
    if(pid && s.raw?.length) {
      const bytes=s.raw.map(x=>parseInt(x,16));
      rows.push([pid[0],pid[2](...bytes).toLocaleString('ru-RU',{maximumFractionDigits:2})+' '+pid[1]]);
    }
    if(s.faults_valid) {
      rows.push(['Сохранённые коды OBD-II',s.faults.length ? String(s.faults.length) : 'Неисправностей нет']);
      for(const f of s.faults){
        const refs=['ME2','M1L','M1R'].filter((_,i)=>f.hht_sources & (1<<i));
        rows.push([obdCode(f.code),refs.length ? 'Встречается в HHT · '+refs.join(', ') : 'Код OBD-II']);
      }
    }
  } else if(isDuty(s)) {
    const d=s.duty;
    const percent=x=>(x/10).toLocaleString('ru-RU')+' %';
    rows.push(['Доля низкого уровня',d ? percent(d.low) : 'Нет достоверного измерения']);
    if(d)rows.push(['Минимум / максимум',percent(d.min)+' / '+percent(d.max)],['Полных периодов',String(d.cycles)]);
  } else {
    rows.push(['Память N3',s.faults_valid ? (s.faults.length ? 'Цикл завершён' : '1 импульс · неисправностей нет') :
      (s.last_blink ? 'Считайте следующий код до повтора первого' : 'Нет завершённого чтения')]);
    for(const f of s.faults || [])rows.push(['Импульсный код '+f.code,CFI_N3[f.code] || 'Нет расшифровки для N3']);
  }
  return rows;
}
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
  const deadline=Date.now()+(action==='scan' ? 10*60000 : 130000);
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
  let html=moduleOnly ? '<div class="card pad" id="repCard"><h2>Диагностический отчёт</h2><p>'+esc(report.generator)+'</p><h3>'+esc(moduleName(m.module))+'</h3>'+kvTable(isEngineMode(m) ? engineRows(m) : [
    ['Серия',m.series],['Контакт',m.contact || 'не подтверждён'],['Идентификационный ответ',m.identified ? m.ident.join(' ') : 'нет данных'],
    ['Память неисправностей',m.faults_valid ? m.faults.map(f=>diagCode(f.code)).join(', ') || 'Неисправностей нет' : 'чтение не завершено']
  ]) : '';
  const sample=report.hht_screen;
  if(sample && HHT_SCREENS[sample.screen]) {
    const [title,fields]=HHT_SCREENS[sample.screen];
    html+='<h3>'+esc(title)+'</h3>'+kvTable(fields.map(f=>[f[1] || title,describeHht(f,sample.words[f[0]])]));
  }
  if(moduleOnly && !isEngineMode(m) && m.raw?.length)html+='<h3>Запись '+esc(m.at)+'</h3><p class="mono">'+esc(m.raw.join(' '))+'</p>';
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
    if(isEngineMode(r))html+='<p>'+esc(engineConnector(r))+'</p>';
    if(['ME2','M1L','M1R'].includes(r.module))html+='<button class="btn sec" data-scan="open-obd">Открыть диагностику ME-SFI по OBD-II</button>';
    if(!r.available)html+='<p class="note">В каталоге HHT есть этот вариант. Его сценарий связи на этом адаптере пока недоступен.</p>';
    else if(!selected)html+='<button class="btn" data-scan="select">Выбрать блок и приостановить обмен</button>';
    else if(!confirmed)html+='<label>Контакт диагностического разъёма<input id="scanPin" type="number" min="1" max="38" '+(r.contact ? 'value="'+r.contact+'" readonly' : 'placeholder="По схеме автомобиля"')+'></label>'+
      (['KS-1','KS-6'].includes(r.protocol) && !r.fixed_baud ? '<label>Скорость после синхронизации<select id="scanBaud"><option value="0">9600 бод</option><option value="1">10400 бод</option><option value="2">4800 бод</option><option value="3">19200 бод</option></select></label>' : '')+
      '<label class="scan-confirm"><input type="checkbox" id="scanConfirm">Я проверил разъём и подключил указанный контакт</label><button class="btn" data-scan="attach">Подключиться</button>';
    else html+='<p>Подключение через контакт <strong>'+latest.selection.contact+'</strong></p><div class="btns">'+
      (r.dtc && (r.module!=='ME-OBD' || hasPid(latest.selection.pid_mask,1)) ? '<button class="btn" data-scan="faults">'+(r.module==='KE' ? 'Прочитать следующий импульсный код' : 'Прочитать неисправности')+'</button>' : '')+
      (isEngineMode(r) ? '' : '<button class="btn sec" data-scan="scan">Опросить блоки на этом контакте</button>')+'<button class="btn sec" data-scan="pause">Остановить связь</button></div>';
    if(confirmed && r.clear && latest.selection.identified)html+='<button class="btn sec" data-scan="clear">Стереть память неисправностей</button>';
    if(confirmed && r.module==='HFM') html+='<label>Параметр двигателя<select id="scanScreen">'+Object.entries(HHT_SCREENS).map(([id,[title]])=>'<option value="'+id+'" '+(Number(id)===screen ? 'selected' : '')+'>'+esc(title)+' · '+id+'</option>').join('')+'</select></label><div class="btns"><button class="btn sec" data-scan="sample">Считать параметр</button><button class="btn sec" data-scan="monitor">'+(reading ? 'Остановить график' : 'Запустить график')+'</button></div>';
    if(confirmed && isEngineMode(r) && r.measures.length) {
      const opts=r.measures.map((at,i)=>[at,i]).filter(([at])=>isDuty(r) || hasPid(latest.selection.pid_mask,at));
      if(opts.length)html+='<label>Параметр<select id="scanMeasure">'+opts.map(([at,i])=>'<option value="'+i+'">'+(isDuty(r) ? 'Доля низкого уровня' : OBD_PIDS[at][0])+'</option>').join('')+'</select></label><button class="btn sec" data-scan="measure">Измерить</button>';
    } else if(confirmed && r.measures.length)html+='<label>Блок данных ЭБУ<select id="scanMeasure">'+r.measures.map((at,i)=>'<option value="'+i+'">'+(r.module==='EGS' && (at===21 || at===23) ? (at===21 ? 'Счётчик масла' : 'Счётчик двигателя') : 'Запись '+at)+'</option>').join('')+'</select></label><button class="btn sec" data-scan="measure">Считать данные</button>';
    $('#scanRoute').innerHTML=html;
  }
  function results() {
    const r=chosen(), s=latest.selection;
    let html='';
    if(r && r.id===s.id && isEngineMode(r)) {
      html='<div class="card pad">'+kvTable(engineRows(s))+'<a class="btn sec" href="/api/report?dl=1" download>Скачать отчёт</a></div>';
    } else if(r && r.id===s.id && r.module!=='HFM') {
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
    const changed=status.selection.epoch!==latest.selection.epoch || status.selection.confirmed!==latest.selection.confirmed || status.selection.pid_mask!==latest.selection.pid_mask;
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
    if(action==='open-obd'){seriesBox.value='ME-SFI · OBD-II';await list();return;}
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
    } catch(err){
      reading=false;if(here()){toast(err.message,true);try {await refresh();routeForm();}catch(_){}}
    }
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
