"""Differential test of the firmware's C++ converter against original HFMWIN vectors."""
import argparse
import csv
import importlib.util
import random
import sys
from pathlib import Path
import struct
import subprocess
import tempfile
from run_host_tests import compiler, FW

BRIDGE = r'''
#include "HhtValues.h"
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main() {
#ifdef _WIN32
  _setmode(_fileno(stdin),_O_BINARY); _setmode(_fileno(stdout),_O_BINARY);
#endif
  uint8_t item[72],r[64]; uint8_t out[18];
  while (fread(item,1,sizeof item,stdin)==sizeof item) {
    memcpy(r,item+8,64);
    auto w=(item[0]==255 && item[1]==255) ? hht::crankingSpeeds(r,item[2],item[5]) : hht::convert(item[0]|(item[1]<<8),r,item[2],item[3],item[4],item[5],item[6]|(item[7]<<8));
    out[0]=uint8_t(w.valid); out[1]=uint8_t(w.valid>>8);
    for(unsigned i=0;i<8;++i){ out[2+2*i]=uint8_t(w.values[i]); out[3+2*i]=uint8_t(uint16_t(w.values[i])>>8); }
    if(fwrite(out,1,sizeof out,stdout)!=sizeof out)return 2;
  }
  return ferror(stdin) ? 1 : 0;
}
'''
def check_cranking(exe,kit):
    spec=importlib.util.spec_from_file_location('hfm_special_tests',kit/'03_REFERENCE/hfm_special_tests.py')
    ref=importlib.util.module_from_spec(spec)
    sys.modules[spec.name]=ref
    spec.loader.exec_module(ref)
    rng=random.Random(0x0a36)
    data=bytearray()
    expected=[]
    for bosch in (False,True):
        for cylinders in (4,6):
            for i in range(1024):
                raw=bytearray(rng.randrange(256) for _ in range(64))
                if i<48:
                    pos=(i%cylinders)*(4 if bosch else 6)+(0 if bosch else 2)
                    edge=(0,1,12,37,0x52e2,0x52e3,0x52e4,65535)[i//6]
                    raw[pos],raw[pos+2]=divmod(edge,256)
                values=list(ref.cylinder_speeds(raw,'B' if bosch else 'V',cylinders,True))
                data+=struct.pack('<HBBBBH',65535,bosch,0,0,cylinders,0)+raw
                expected.append(((1<<len(values))-1,*values,*([0]*(8-len(values)))))
    out=subprocess.run([str(exe)],input=data,stdout=subprocess.PIPE,check=True).stdout
    actual=list(struct.iter_unpack('<9H',out))
    if actual!=expected:raise ValueError('cranking speed, short sample or channel order mismatch')
    print(f'C++ cranking: {len(actual)} reference comparisons passed')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kit',type=Path)
    kit=parser.parse_args().kit
    with tempfile.TemporaryDirectory() as temp:
        t=Path(temp); src=t/'bridge.cpp'; src.write_text(BRIDGE,encoding='utf8'); exe=t/'bridge.exe'
        subprocess.run(compiler()+['-O2','-std=c++17','-I',str(FW),str(src),str(FW/'HhtValues.cpp'),'-o',str(exe)],check=True)
        total=0
        vectors=sorted((kit/'05_VALIDATION/conversion_vectors').glob('*.csv'))
        for file in vectors:
            entry=int(file.stem.split('_')[1],16)
            data=bytearray(); expected=[]
            with file.open(encoding='utf-8-sig',newline='') as f:
                for row in csv.DictReader(f):
                    raw=bytearray(64); values=[]
                    for k,v in row.items():
                        if k.startswith('in_'):raw[int(k[3:])]=int(v)
                        if k.startswith('out_'):values.append(int(v))
                    data.extend(struct.pack('<HBBBBH',entry,row['variant']=='B',int(row['diag_0x6a'],0),int(row['ident_0x6e'],0),4,0)+raw)
                    expected.append(values)
            result=subprocess.run([str(exe)],input=data,stdout=subprocess.PIPE,check=True).stdout
            if len(result)!=len(expected)*18:raise ValueError('short converter output')
            for i,(actual,exp) in enumerate(zip(struct.iter_unpack('<H8h',result),expected)):
                if actual[0]!=(1<<len(exp))-1 or list(actual[1:len(exp)+1])!=exp:
                    raise ValueError((file.name,i+2,exp,actual))
            total+=len(expected)
        check_cranking(exe,kit)
        if len(vectors)!=51 or total!=442331:raise ValueError('incomplete corpus')
        print(f'C++ HFMWIN differential: {len(vectors)} converters, {total} vectors passed')
if __name__=='__main__':main()
