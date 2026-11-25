"""Emit bounded read-only profile commands directly from recovered PKW descriptors."""
from pathlib import Path
import argparse
import json
from build_scanner_catalog import OUTPUT, read, asr_addresses

def generate(kit):
    base=kit/'02_DATA'; banks=read(base,'protocols/ks_proto1_frames')['modules']
    tables=[]; profiles=[]; options={}
    def timing(tx,er):
        at=tx['atp'];rx=er['er_params']
        return '{%d,%d,%d,%d}'%(at['t_a_ms'],at['t_b_ms'],rx['t_first_ms'],rx['t_inter_ms'])
    for module,variants in sorted(banks.items()):
        for variant,t in variants.items():
            rows=t['at'];ers={a['er']:a for a in t['er']}; init=next(a for a in rows if a.get('kind')=='5-baud address')
            def find(cmd):
                return next(a for a in rows if a.get('cmd')==cmd)
            def times(row,code): return timing(row,next(e for e in ers.values() if e.get('answer_code')==code))
            status=find('50'); fault=find('10')
            addr=bytes.fromhex(status['line'])[0]
            assert bytes.fromhex(fault['line'])[0]==addr
            measures=[]
            for row in rows:
                if row.get('kind')!='frame' or row.get('cmd')!='30': continue
                wire=bytes.fromhex(row['line'])
                if len(wire)!=9 or wire[3]!=1 or not 1<=wire[6]<=64 or wire[0]!=addr:continue
                if int.from_bytes(wire[-2:],'little')!=sum(wire[:-2])&65535:raise ValueError(row)
                if not any(e.get('answer_code')=='b0' for e in ers.values()):continue
                measures.append('{%d,%d,%d,%d,%s}'%(row['at'],wire[4],wire[5],wire[6],times(row,'b0')))
            symbol='m'+str(len(profiles)) if measures else 'nullptr'
            if measures:tables.append(f'static const Measure {symbol}[] = {{'+','.join(measures)+'};')
            profiles.append((module,variant,addr,0,False,False,timing(init,ers[0]),times(status,'d0'),times(fault,'90'),symbol,len(measures)))
    # Classical frames inside CDB. Token data below must be present in the
    # source before enabling a profile. EGS uses the same 8F identification response.
    for module in ('EGS','EDW','IF1'):
        desc=read(base,'cdb/pkw/'+module+'/descriptor_tables')['tables']['ATD']
        byname={row['name']:bytes.fromhex(row['variants'][0]['bytes'])[6:] for row in desc if row['variants']}
        status=byname['status_blk' if module=='EGS' else 'erhalt_iso1']; addr=status[0]
        assert status[1:4]==bytes([0x50,6,1])
        if module!='EGS':assert byname['fs_lesen_iso1'][0:4]==bytes([addr,0x10,6,0])
        symbol='nullptr'; measure_count=0
        if module=='EGS':
            records=[]
            for row in desc:
                if row['name'] not in ('read_oel','read_mot'): continue
                wire=byname[row['name']]
                assert len(wire)==9 and wire[:4]==bytes([addr,0x30,9,1]) and wire[6]==4
                records.append('{%d,%d,%d,4,{30,10,1995,1995}}'%(row['index'],wire[4],wire[5]))
            assert len(records)==2
            symbol='egsCounters';measure_count=len(records)
            tables.append('static const Measure egsCounters[] = {'+','.join(records)+'};')
            assert byname['9600Bd']==bytes([0x20])
            options[module,'CDB-KS1']=',0,255,false,true'
            init,stat,fault='{300,10,1995,1000}','{1000,10,1995,1995}','{30,10,1995,1995}'
        else:
            init,stat,fault='{500,200,4500,300}','{10,3,1000,300}','{10,3,1000,300}'
        profiles.append((module,'CDB-KS1',addr,0,False,False,init,stat,fault,symbol,measure_count))
    # Only direct physical GL_ISO2 addressing whose G53 parameters in the
    # actual bytecode agree with the module passport; no emulator TX is exported.
    for d in sorted((base/'cdb/pkw').iterdir()):
        pp=d/'passport.json'
        if not pp.exists():continue
        passport=read(d,'passport'); c=passport['connection']; address=c.get('ecu_address')
        if not address or c['status']!='end' or c.get('gateway'):continue
        if not c.get('init'): continue
        if d.name in ('D2B','AID','LWR'):continue
        addr=int(address,16)
        af=read(d,'af_programs')['af']
        setup=[i['args']['params'] for prog in af if prog['clean'] and 'AUFBAU' in prog['signals']
               for i in prog['insns'] if i['name']=='CALLAF' and i['args']['af']==['G',53]]
        g53=next((p for p in setup if len(p)>=5 and p[1][0]=='imm' and p[2:4]==[['imm',addr],['imm',addr]] and p[4][0]=='imm' and p[0][0]=='imm'),None)
        if not g53:continue
        tester=g53[4][1]
        if c.get('tester_address') and int(c['tester_address'],16)!=tester:continue
        baud=g53[0][1] or 10400
        if baud not in (9600,10400):continue
        modes={i['args']['params'][2][1] for prog in af if prog.get('clean') and 'AUFBAU' in prog['signals']
               for i in prog['insns'] if i['name']=='CALLAF' and i['args']['af']==['G',26] and
               len(i['args']['params'])>=3 and i['args']['params'][2][0]=='imm'}
        if len(modes)!=1 or not modes<={0x80,0x81,0xc0,0xc1}: continue
        mode=modes.pop(); slow=bool(mode&1)
        options[d.name,'GL_ISO2']=f',0,{g53[1][1]},{str(bool(mode&0x40)).lower()},false,{baud}'
        if d.name in ('KLA','KI','EPH','SH2','APS'):
            clear=any(i['args']['params']==[['imm',0]] for prog in af if prog['clean'] and 'FS_LOESCHEN' in prog['signals']
                      for i in prog['insns'] if i['name']=='CALLAF' and i['args']['af']==['G',12])
            if clear: options[d.name,'GL_ISO2']+=',true'
        ops=passport.get('functions',{})
        if not all(any(row.get('signal')==name for row in ops.get(key,[])) for key,name in [('1_ident','ID_LESEN'),('2_read_faults','FS_LESEN')]):continue
        profiles.append((d.name,'GL_ISO2',addr,tester,True,slow,'{}','{}','{}','nullptr',0))
    tree=read(base,'vehicles/vehicle_tree')['tree']
    asr=sorted({addr for series in tree for addr in asr_addresses(base,series['id'])})
    for addr in asr:
        profiles.append(('ASR',f'GL_ISO2@{addr:02X}',addr,0xf2,True,False,'{}','{}','{}','nullptr',0))
    tables.append('static const Profile profiles[] = {')
    for m,v,a,t,iso,slow,ini,stat,fault,sym,n in profiles:
        tables.append(f'  {{"{m}","{v}",{a},{t},{str(iso).lower()},{str(slow).lower()},{ini},{stat},{fault},{sym},{n}{options.get((m,v),str())}}},')
    for module in ('DM','DM2','EZ1','LH1','PMS'):
        for variant,t in read(base,'classic/pkw/'+module+'/ks_tables')['sg_tables'].items():
            if t['header']['protocol']!=3: continue
            tpl=t['at_templates']
            if bytes.fromhex(tpl[0]['bytes'])!=bytes([0,0,0xaa,0]): continue
            lengths=[]
            for row in tpl[:6]:
                data=bytes.fromhex(row['bytes']); n=0
                while data[4*n:4*n+4]==bytes([0xf0,1,n,0]):n+=1
                lengths.append(n)
            size=min(28,max(lengths))
            if size>=6: tables.append(f'  {{"{module}","{variant}",0,0,false,false,{{}},{{}},{{}},nullptr,0,{size}}},')
    tables.append('};')
    return '\n'.join(tables)+'\n'

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('kit',type=Path);ap.add_argument('--check',action='store_true')
    args=ap.parse_args();out=generate(args.kit);p=OUTPUT/'ModuleProfiles.inc'
    if args.check:
        if p.read_text(encoding='utf8')!=out:raise SystemExit('profiles differ from source')
    else:p.write_text(out,encoding='utf8')
