#ifdef ARDUINO
#include "DiagEngine.h"
#include "Config.h"
#include <esp_random.h>
using scanner::FrameError;

void DiagEngine::selectRoute(int id,char* msg,size_t n,bool& ok) {
  const auto* route=scanner::route(id);
  ok=false;
  if (!route) {snprintf(msg,n,"Блок отсутствует в каталоге");return;}
  if (guard_.state()!=guard::State::Idle && guard_.state()!=guard::State::Prepared) {
    snprintf(msg,n,"Сначала завершите сервисную операцию");return;
  }
  if (st_.recording) stopRecording();
  guard_.cancel(); userDisconnected_=true;
  session_->disconnect(); moduleDriver_->close(); hhtReader_.reset();
  const auto* prior=scanner::route(st_.module.route);
  lock();
  if (!prior || strcmp(prior->series,route->series)) {st_.scanCount=0;st_.beforeModuleClear={};}
  moduleFaultMs_=0;
  ++st_.module.epoch;
  trace_.setRoute(id,0);
  st_.module.route=id;st_.module.confirmed=false;st_.module.contact=0;
  st_.module.error=FrameError::Ok;st_.module.readout={};
  st_.ident=EcuIdent();st_.dtc.invalidate();st_.adapt={};st_.coding={};st_.vin={};st_.impulse={};
  st_.liveActive=false;st_.live={};st_.snap={};st_.hhtScreen={};st_.liveCycles=0;
  st_.plan=guard::Plan();st_.lastServiceResult[0]=0;st_.purgeOn=false;
  for(auto& feat:st_.feat) feat={};
  st_.session=SessionState::Disconnected;st_.link=LinkPhase::Paused;st_.connectedAtMs=0;
  snprintf(st_.linkMsg,sizeof st_.linkMsg,"Блок выбран. Подключите провод и подтвердите контакт");
  prof::ResolveOptions options;prof::resolve(st_.ident,options,st_.profile);
  session_->setVdoReads(false);prevLive_={};planTried_=0;extDisabled_=false;everConnected_=false;
  unlock();
  snprintf(msg,n,"Выбран %s. Обмен приостановлен до подтверждения контакта",route->module);
  ok=true;
}

void DiagEngine::storeScan(int id,const scanner::Readout& read,FrameError error) {
  lock();
  unsigned i=0;
  while(i<st_.scanCount && st_.scans[i].route!=id) ++i;
  if(i<sizeof st_.scans/sizeof st_.scans[0]) {
    if(i==st_.scanCount) ++st_.scanCount;
    auto& rec=st_.scans[i];
    rec.route=id;rec.contact=st_.module.contact;rec.time=millis();rec.error=error;
    rec.identified=read.identityValid;rec.complete=read.faultsValid;
    rec.count=read.faultsValid ? read.codeCount : 0;
    memcpy(rec.codes,read.codes,rec.count*sizeof(rec.codes[0]));
  }
  unlock();
}

bool DiagEngine::scannerJob(Job j,uint32_t arg,const char* text,char* msg,size_t n,bool& ok) {
  if(j==Job::SelectRoute) {selectRoute(arg,msg,n,ok);return true;}
  const auto* route=scanner::route(st_.module.route);
  if(!route) return false;
  const bool hfm=!strcmp(route->module,"HFM");
  const auto* p=scanner::profile(*route);
  if(j==Job::AttachRoute) {
    uint16_t id=arg>>16; uint8_t pin=arg&63; unsigned speed=(arg>>6)&3;
    if(id!=st_.module.route || !pin || pin>38 || (route->contact && pin!=route->contact) || (!hfm && !p)) {
      ok=false;snprintf(msg,n,"Проверьте выбранный блок, контакт и поддержку подключения");return true;
    }
    static const uint32_t rates[]={9600,10400,4800,19200};
    lock();st_.module.confirmed=true;st_.module.contact=pin;st_.module.baud=rates[speed];unlock();
    trace_.setRoute(id,pin);
    userDisconnected_=false;
    if(hfm) {
      doConnect(msg,n,ok,1); if(ok) doIdentify(msg,n,ok);
      if(ok && !st_.ident.hhtCylinders) { ok=false;snprintf(msg,n,"Ответил блок с другим идентификатором HFM"); }
      if(!ok) {session_->disconnect();userDisconnected_=true;lock();st_.module.confirmed=false;unlock();}
      return true;
    }
    j=Job::Connect;
  }
  if(j==Job::ScanContact) {
    if(!st_.module.confirmed || userDisconnected_ || (!hfm && !p) || guard_.state()!=guard::State::Idle || st_.recording) {
      ok=false;snprintf(msg,n,"Для опроса подтвердите контакт и завершите запись и сервисные операции");
    } else scanContact(msg,n,ok);
    return true;
  }
  if(j==Job::Stop || j==Job::Disconnect) {
    if(hfm && j==Job::Stop) return false;
    if(guard_.state()==guard::State::Active || guard_.state()==guard::State::Restoring) {
      ok=false;snprintf(msg,n,"Сначала остановите исполнительный тест");return true;
    }
    guard_.cancel();
    lock();++st_.module.epoch;st_.module.confirmed=false;st_.hhtScreen={};st_.plan=guard_.plan();unlock();
    if(hfm) return false;
    userDisconnected_=true;moduleDriver_->close();
    lock();st_.link=LinkPhase::Paused;st_.session=SessionState::Disconnected;
    st_.module.readout={};strlcpy(st_.linkMsg,"Обмен остановлен",sizeof st_.linkMsg);unlock();
    snprintf(msg,n,"Обмен остановлен");return true;
  }
  if(!st_.module.confirmed) {
    ok=false;snprintf(msg,n,"Подтвердите диагностический контакт выбранного блока");return true;
  }
  if(hfm) return false;
  if(!p) {ok=false;snprintf(msg,n,"Для этого варианта требуется отдельный сценарий подключения");return true;}
  if(j==Job::AckFault) {
    guard_.acknowledgeFault();lock();st_.faultStop=guard_.locked();st_.plan=guard_.plan();unlock();
    snprintf(msg,n,"Сервисная блокировка квитирована");return true;
  }
  if(j==Job::SvcPrepare || j==Job::SvcExecute) {
    if((arg&255)!=unsigned(guard::Op::DtcClear) || !p->clearFaultMemory) {
      ok=false;snprintf(msg,n,"Эта операция отсутствует в профиле блока");return true;
    }
    moduleClear(j,arg,text,*p,msg,n,ok);return true;
  }
  FrameError err=FrameError::Ok;
  if(j==Job::Connect || j==Job::Identify) {
    userDisconnected_=false;err=moduleDriver_->connect(*p,st_.module.baud);
  } else if(j==Job::ReadDtc || j==Job::ModuleMeasure) {
    if(!moduleDriver_->ready()) err=FrameError::Offline;
    else if(j==Job::ReadDtc) err=moduleDriver_->faults(*p);
    else err=moduleDriver_->measurement(*p,arg);
  } else {
    ok=false;snprintf(msg,n,"Эта операция отсутствует в профиле %s",route->module);return true;
  }
  ok=err==FrameError::Ok;nextModuleMs_=millis()+(ok ? 1400 : 4000);
  snprintf(msg,n,"%s",scanner::message(err));
  lock();st_.module.readout=moduleDriver_->result();st_.module.error=err;
  st_.session=moduleDriver_->ready() ? SessionState::Ready : SessionState::Error;
  st_.link=moduleDriver_->ready() ? LinkPhase::Ready : LinkPhase::Recovering;
  strlcpy(st_.linkMsg,msg,sizeof st_.linkMsg);unlock();
  if(j==Job::ReadDtc) {moduleFaultMs_=ok ? millis() : 0;storeScan(st_.module.route,moduleDriver_->result(),err);}
  return true;
}

void DiagEngine::moduleClear(Job job,uint32_t arg,const char* text,const scanner::Profile& p,char* msg,size_t n,bool& ok) {
  ok=false;
  constexpr auto op=guard::Op::DtcClear;
  const auto& result=moduleDriver_->result();
  const bool prepare=job==Job::SvcPrepare;
  if(prepare) {
    guard_.cancel();
    svcConfirmStopped_=(arg&0x100)!=0;
    if(arg&0x400) settings_->batteryManualConfirm=(arg&0x200)!=0;
    auto e=moduleDriver_->faults(p);
    moduleFaultMs_=e==FrameError::Ok ? millis() : 0;
    storeScan(st_.module.route,result,e);
  } else if(guard_.state()==guard::State::Prepared) {
    if(moduleDriver_->verifyIdentity(p)!=FrameError::Ok) {guard_.cancel();moduleFaultMs_=0;}
  }
  readHardware();
  lock();
  st_.module.readout=result;
  guard::Facts facts;fillFacts(facts);
  facts.sessionReady=moduleDriver_->ready();
  facts.faultMemAgeMs=result.faultsValid && moduleFaultMs_ ? millis()-moduleFaultMs_ : UINT32_MAX;
  facts.faultMemNonZero=result.faultsValid && result.codeCount;
  EcuIdent identity;
  identity.present=result.identityValid;
  snprintf(identity.fingerprint,sizeof identity.fingerprint,"%d-%lu",st_.module.route,(unsigned long)st_.module.epoch);
  prof::EcuProfile capabilities{};
  capabilities.caps[prof::F_DTC_CLEAR]={p.clearFaultMemory,Confidence::REK,"Стирание памяти неисправностей"};
  if(prepare) {
    ok=guard_.prepare(op,capabilities,identity,facts,millis(),esp_random()|1,st_.plan);
    if(ok) {
      auto& before=st_.beforeModuleClear;
      before.route=st_.module.route;before.contact=st_.module.contact;before.time=millis();before.error=scanner::FrameError::Ok;
      before.identified=true;before.complete=true;before.count=result.codeCount;
      memcpy(before.codes,result.codes,result.codeCount*sizeof(result.codes[0]));
    }
    snprintf(msg,n,"%s",ok ? "Условия выполнены, введите пароль" : (st_.plan.blockReason ? st_.plan.blockReason : "Условия не выполнены"));
    unlock();return;
  }
  const char* separator=strchr(text,'|');
  auto auth=guard_.authorize(op,strtoul(text,nullptr,16),separator ? separator+1 : "",
                      settings_->apPass[0] ? settings_->apPass : HFM_AP_DEFAULT_PASS,capabilities,identity,facts,millis());
  st_.pwLockS=guard_.passwordLockS(millis());st_.plan=guard_.plan();
  if(auth!=guard::AuthResult::Ok) {
    snprintf(msg,n,"%s",auth==guard::AuthResult::BadPassword ? "Неверный пароль точки доступа" :
             auth==guard::AuthResult::PasswordLocked ? "Ввод пароля временно заблокирован" : "Требуется новая проверка условий");
    strlcpy(st_.lastServiceResult,msg,sizeof st_.lastServiceResult);unlock();return;
  }
  guard_.begin(op);st_.serviceActive=true;unlock();
  auto err=moduleDriver_->clearFaults(p);
  bool ack=err==FrameError::Ok;
  if(ack) err=moduleDriver_->faults(p);
  ok=err==FrameError::Ok;moduleFaultMs_=ok ? millis() : 0;
  if(ok) snprintf(msg,n,"Стирание подтверждено. При повторном чтении записей %u",result.codeCount);
  else snprintf(msg,n,"%s. %s",ack ? "Стирание подтверждено, повторное чтение прервано" : "Результат стирания неизвестен",scanner::message(err));
  guard_.finish(ok);
  lock();st_.module.readout=result;st_.module.error=err;st_.plan=guard_.plan();st_.serviceActive=false;
  st_.faultStop=guard_.locked();strlcpy(st_.lastServiceResult,msg,sizeof st_.lastServiceResult);unlock();
  storeScan(st_.module.route,result,err);
}
void DiagEngine::scannerTick() {
  if(!st_.module.confirmed || userDisconnected_ || int32_t(millis()-nextModuleMs_)<0) return;
  const auto* route=scanner::route(st_.module.route);
  const auto* p=route ? scanner::profile(*route) : nullptr;
  if(!p) return;
  bool connected=moduleDriver_->ready();
  if(!connected && !settings_->autoConnect) return;
  FrameError e=connected ? moduleDriver_->keepAlive(*p) : moduleDriver_->connect(*p,st_.module.baud);
  if(e!=FrameError::Ok) {
    moduleDriver_->close();moduleFaultMs_=0;guard_.cancel();
    lock();st_.plan=guard_.plan();if(connected) ++st_.module.epoch;unlock();
  }
  nextModuleMs_=millis()+(e==FrameError::Ok ? 1400 : 5000);
  lock();st_.module.error=e;st_.module.readout=moduleDriver_->result();
  st_.session=e==FrameError::Ok ? SessionState::Ready : SessionState::Error;
  st_.link=e==FrameError::Ok ? LinkPhase::Ready : adapterPresent() ? LinkPhase::Recovering : LinkPhase::NoAdapter;
  strlcpy(st_.linkMsg,scanner::message(e),sizeof st_.linkMsg);unlock();
}

void DiagEngine::scanContact(char* msg,size_t n,bool& ok) {
  const int selected=st_.module.route;const auto* current=scanner::route(selected);
  unsigned total=0,found=0,answeredCount=0;bool stopped=false;
  const scanner::Profile* answered[64]{};
  lock();
  unsigned kept=0;
  for(unsigned j=0;j<st_.scanCount;++j) if(st_.scans[j].contact!=st_.module.contact) {
    if(kept!=j)st_.scans[kept]=st_.scans[j];
    ++kept;
  }
  st_.scanCount=kept;unlock();
  const auto pin=st_.module.contact;
  session_->disconnect();moduleDriver_->close();hhtReader_.reset();
  for(size_t pass=0;pass<=scanner::routeCount();++pass) {
    size_t i=pass ? pass-1 : size_t(selected);
    if(pass && i==size_t(selected)) continue;
    const auto& r=*scanner::route(i);
    if(strcmp(current->series,r.series) || (int(i)!=selected && (!r.contact || r.contact!=pin))) continue;
    const auto* p=scanner::profile(r);bool hfm=!strcmp(r.module,"HFM");
    if(!hfm && !p) continue;
    bool seen=false;
    for(unsigned j=0;p && j<answeredCount;++j) {
      const auto& q=*answered[j];
      if(q.iso==p->iso && q.address==p->address && bool(q.classicIdBytes)==bool(p->classicIdBytes)) seen=true;
    }
    if(seen) continue;
    lock();
    for(unsigned k=0;k<qCount_;++k) if(q_[k].job==Job::Stop || q_[k].job==Job::Disconnect) stopped=true;
    unlock();if(stopped) break;
    ++total; trace_.setRoute(i,pin);
    // Allow the ECU on this contact to leave the preceding diagnostic session.
    link_->sleepMs(2600);
    if(hfm) {
      scanner::Readout read;bool good=false;char tmp[200];
      doConnect(tmp,sizeof tmp,good,1);
      if(good) doIdentify(tmp,sizeof tmp,good);
      read.identityValid=good && st_.ident.hhtCylinders;
      if(read.identityValid && readDtc(tmp,sizeof tmp)) {
        read.faultsValid=true;
        for(unsigned j=0;j<st_.dtc.count;++j) if(st_.dtc.items[j].code) {
          if(read.codeCount==64) {read.faultsValid=false;break;}
          read.codes[read.codeCount++]=st_.dtc.items[j].code;
        }
      }
      storeScan(i,read,read.faultsValid ? FrameError::Ok : read.codeCount==64 ? FrameError::Limit : FrameError::Reply);
      found+=read.identityValid;session_->disconnect();
    } else {
      auto err=moduleDriver_->connect(*p,st_.module.baud);
      if(err==FrameError::Ok && !p->classicIdBytes) err=moduleDriver_->faults(*p);
      if(moduleDriver_->result().identityValid) {
        ++found;
        if(answeredCount<64) answered[answeredCount++]=p;
      }
      storeScan(i,moduleDriver_->result(),err);
      moduleDriver_->close();
    }
  }
  // End paused on the originally selected route. Moving the lead requires
  // a fresh contact confirmation. Only the per-route history is retained.
  selectRoute(selected,msg,n,ok);
  ok=!stopped;
  snprintf(msg,n,"%s. Контакт %u, проверено %u, ответили %u",stopped ? "Опрос остановлен" : "Опрос завершён",pin,total,found);
}
#endif
