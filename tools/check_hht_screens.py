"""Replay real HHT screen plans in the C++ reader, including the four ROM chunks."""
import argparse
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from run_host_tests import ROOT,FW,compiler

def corpus(kit):
    sys.path.insert(0,str(kit/'03_REFERENCE'))
    import hfm_rom
    from hfm_special_tests import Smoothness
    from hfm_module import HfmModule
    from hfm_runtime import identify_core
    from hfm_decode import convert
    module=HfmModule()
    for variant in 'VB':
        for diag in (0x44,0x45,0x50):
            for cyl in (4,6):
                ident=bytearray(120); ident[6]=ord(variant); ident[10]=ord('M')
                ident[18]=ord('2');ident[22]=ord('0' if cyl==4 else '8')
                ident[106]=diag;ident[110]=0x55;ident[114]=3;ident[118]=228
                identity=identify_core(ident); v=identity['variant_id']
                target=bytearray(780);types=list(module.model.type_map(v))
                for i in range(97):
                    target[i*8+6]=i+1;target[i*8+10]=types[i%len(types)]
                prefix=bytearray(); parts=[]
                for req in hfm_rom.requests(ident):
                    tpl=req['template']; rx=bytearray(len(tpl));cut=req['rollover_index']
                    skip=4*(cut+1+int(req['chunk']==0)) if cut is not None else len(tpl)
                    for i in range(0,len(tpl),2):
                        index=req['destination']+i-(4 if i>=skip+4 else 0)
                        rx[i]=255^tpl[i] if tpl[i+1] else target[index] if index<len(target) else 0
                        prefix.extend([tpl[i],tpl[i+1],rx[i]])
                    parts.append(rx)
                rom=hfm_rom.assemble(ident,parts)
                for screen,s in module.screens.items():
                    special=screen==(126 if cyl==4 else 125)
                    if not s['conversion'] and not special:continue
                    e=s['entries'][variant]
                    if not (e['length'] if diag<0x45 else e['descriptor']):continue
                    req=module.live_request(screen,identity,rom)
                    tpl=req['template'];rx=bytearray(len(tpl));wire=bytearray(prefix if diag>=0x45 else b'')
                    for i in range(0,len(tpl),2):
                        rx[i]=255^tpl[i] if tpl[i+1] else (2+i//4 if special else (i*19+screen+7)&255)
                        wire.extend([tpl[i],tpl[i+1],rx[i]])
                    r=bytearray(rx)+bytes(64-len(rx))
                    mapping=req['mapping']
                    if mapping:
                        for a,b in zip(mapping['source_offsets'],mapping['destination_offsets']): r[b]=rx[a]
                    mask=module.model.validity_mask(e['index'],v,diag,rom,r)
                    if special:
                        vals=Smoothness(cyl).sample(r,variant) or []
                        if mask & ((1<<cyl)-1) != (1<<cyl)-1: vals=[]
                    else: vals=convert(s['conversion'],r,variant,diag,0x55,cyl,s['screen_parameter'])
                    valid=sum(1<<i for i,x in enumerate(vals) if x is not None) & mask
                    yield bytes([screen,v,diag,0x55,cyl,3,228])+struct.pack('<HH',len(wire),0)+wire, (screen,variant,diag,cyl,valid,vals)
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('kit',type=Path);args=parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        exe=Path(tmp)/'reader.exe'
        files=[ROOT/'tests/fixtures/hht_reader_runner.cpp']+[FW/f for f in ('HhtReader.cpp','HhtValues.cpp','HfmSession.cpp','TraceLog.cpp')]
        subprocess.run(compiler()+['-std=c++17','-O2','-I',str(FW),*map(str,files),'-o',str(exe)],check=True)
        data=bytearray();expected=[]
        for wire,exp in corpus(args.kit): data.extend(wire);expected.append(exp)
        ret=subprocess.run([str(exe)],input=data,stdout=subprocess.PIPE,check=True)
        if len(ret.stdout)!=20*len(expected):raise ValueError('incomplete replay')
        for row,exp in zip(struct.iter_unpack('<BBH8h',ret.stdout),expected):
            screen,v,d,cyl,mask,vals=exp
            if row[:3]!=(0,1,mask) or any(mask&(1<<i) and row[3+i]!=x for i,x in enumerate(vals)):
                raise ValueError((screen,v,d,cyl,mask,vals,row))
        print(f'C++ HFM physical-word replay: {len(expected)} screens/contexts, {len(set(x[0] for x in expected))} distinct screens')
if __name__=='__main__':main()
