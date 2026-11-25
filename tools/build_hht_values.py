"""Generate HFM numeric expressions, lookup tables and read plans from the verified kit."""
import argparse
import ast
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'firmware/HFMConnect'
MANUAL = {'0042','0cdd','0945','2312','0f5a','1611','1546','15c8','187b','18e8'}

def arithmetic(text):
    node = ast.parse(str(text),mode='eval').body
    def emit(n):
        if isinstance(n,ast.Constant) and type(n.value) is int: return str(n.value)+'LL'
        if isinstance(n,ast.Name) and re.fullmatch(r'x\d+',n.id): return 'r['+n.id[1:]+']'
        if isinstance(n,ast.UnaryOp) and isinstance(n.op,ast.USub): return '(-'+emit(n.operand)+')'
        if isinstance(n,ast.BinOp) and type(n.op) in (ast.Add,ast.Sub,ast.Mult):
            op={ast.Add:'+',ast.Sub:'-',ast.Mult:'*'}[type(n.op)]
            return '('+emit(n.left)+op+emit(n.right)+')'
        raise ValueError(text)
    return emit(node)

def generate(kit):
    sys.path.insert(0,str(kit/'03_REFERENCE'))
    from hfm_decode import base, EXTRA, MASKS
    from hfm_module import HfmModule, templates
    model=HfmModule()
    luts={}
    def lut(values,idx):
        k=tuple(values)
        if k not in luts: luts[k]=len(luts)
        return f'lut{luts[k]}[r[{idx}]]'
    def formula(g):
        f=g['fit']
        if g.get('lut') is not None: return lut(g['lut'],int(g['inputs'][0]))
        if not f: return None
        if f['form']=='const': return str(f['o'])
        if 'word' in f:
            hi,lo=map(int,re.findall(r'in_(\d+)',f['word']))
            x=f'((r[{hi}]<<8)|r[{lo}])'
            if f['signed_w']: x=f'int16_t({x})'
        else:
            x=f'r[{int(g["inputs"][0])}]'
            if f['signed_x']: x=f'int8_t({x})'
        return f'(({x}*{f["m"]}LL+({f["a"]}LL))/{f["d"]}+({f["o"]}))'
    def expressions(entry,v,diag,rev):
        spec=base.DATA[entry]; lo,hi=base._class(entry,v,diag)
        out=[None]*spec['n_out']
        for g in spec['groups']:
            i=g['output']
            if g['variant']!=v or not lo<=g['diag']<=hi or out[i] is not None: continue
            if diag==0x49 and g['id6e'] not in (None,rev): continue
            out[i]=formula(g)
        filled=set()
        for key,g in EXTRA.get(entry,{}).items():
            match=re.match(r'(V|B) diag=(0x\w+) 6e=(0x\w+) out(\d+)',key)
            which,d,rr,i=match.groups(); d=int(d,16); rr=int(rr,16); i=int(i)
            if which!=v or not lo<=d<=hi or i in filled: continue
            if diag==d==0x49 and (rr<0x50,rr>=0x60)!=(rev<0x50,rev>=0x60): continue
            if g['kind']=='table': ex=lut(g['values'],int(g['input'][1:]))
            elif entry.endswith('0633') and v=='V' and 0x42<=diag<0x45: ex='(r[2]>127 ? int(r[6])-256 : r[6])'
            else: ex=f'(({arithmetic(g["numerator"])})/{g["divisor"]}+({arithmetic(g["offset"])}))'
            out[i]=ex; filled.add(i)
        return tuple(out)
    cases=[]; bodies={}
    for entry,spec in base.DATA.items():
        addr=entry.split(':')[1]
        if addr in MANUAL: continue
        cases.append(f'case 0x{addr}:')
        for v in ('V','B'):
            cases.append('  if (!bosch) {' if v=='V' else '  else {')
            if addr=='0ac9' and v=='V': cases.append('    return oxygenRange(r,diag,revision);')
            else:
                for lo,hi in spec['classes'][v]:
                    for a,b in ((lo,min(hi,0x48)),(max(lo,0x49),min(hi,0x49)),(max(lo,0x4a),hi)):
                        if a>b: continue
                        for rev in ([0x40,0x50,0x60] if a==b==0x49 else [0x55]):
                            expr=expressions(entry,v,a,rev)
                            if expr not in bodies: bodies[expr]=len(bodies)
                            conditions=[]
                            if a: conditions.append(f'diag >= {a}')
                            if b<255: conditions.append(f'diag <= {b}')
                            if rev==0x40: conditions.append('revision < 0x50')
                            if rev==0x50: conditions.append('revision >= 0x50 && revision < 0x60')
                            if rev==0x60: conditions.append('revision >= 0x60')
                            condition=' && '.join(conditions) or 'true'
                            cases.append(f'    if ({condition}) return eval({bodies[expr]},r);')
            cases.append('  }')
        cases.append('  break;')
    data=['// Generated. Source: HFMWIN segment 1038, conversion kit.']
    for table,i in luts.items():
        data.append(f'static constexpr int16_t lut{i}[] = {{'+','.join(map(str,table))+'};')
    for key,table in MASKS.items(): data.append(f'static constexpr uint8_t mask{key}[] = {{'+','.join(map(str,table))+'};')
    data.extend(['static Words eval(unsigned id,const uint8_t* r) {','  Words w;','  switch (id) {'])
    for expr,i in bodies.items():
        data.append(f'    case {i}:')
        data.extend(f'      w.put({n},{e});' for n,e in enumerate(expr) if e is not None)
        data.append('      break;')
    data.extend(['  }','  return w;','}'])
    outputs={'HhtNumeric.inc':'\n'.join(data)+'\n','HhtCases.inc':'\n'.join(cases)+'\n'}
    rows=model.model.entries; codes=templates(); arrays=[]; entries=[]
    def seq(values):return '{'+','.join(map(str,values))+'}'
    for at,code in enumerate(codes): arrays.append(f'static const uint8_t at{at}[] = '+seq(code or [0])+';')
    # Only screen references can select a template from this table.
    entries.append('static const Entry entries[][2] = {')
    for i in range(max(k[0] for k in rows)+1):
        pair=[]
        for v in range(2):
            e=rows.get((i,v))
            if e:
                code=codes[e['at']]
                pair.append('{at%d,%d,%d,%d,%d,%d,%s}'%(e['at'],len(code),e['length'],e['descriptor'],e['flags'],len(e['also']),seq(e['also'])))
            else: pair.append('{}')
        entries.append('  {'+','.join(pair)+'},')
    entries.append('};')
    entries.append('static const uint8_t typeMap[2][256] = '+seq([seq([model.model.types[v].get(t,255) for t in range(256)]) for v in range(2)])+';')
    screens=['static const Screen screens[] = {']
    for i in range(max(model.screens)+1):
        s=model.screens.get(i)
        if not s or not s['conversion']: screens.append('  {},'); continue
        screens.append('  {0x%s,%d,%s},'%(s['conversion'].split(':')[1],s['screen_parameter'],seq([s['entries'].get(v,{}).get('index',0) for v in 'VB'])))
    screens.append('};')
    used={e['at'] for e in rows.values()}
    outputs['HhtScreens.inc']='\n'.join([arrays[n] for n in sorted(used)]+entries+screens)+'\n'
    return outputs

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('kit',type=Path);ap.add_argument('--check',action='store_true')
    args=ap.parse_args()
    for name,code in generate(args.kit).items():
        path=OUT/name
        if args.check:
            if path.read_text(encoding='utf8')!=code:raise SystemExit(name+' is stale')
        else:path.write_text(code,encoding='utf8')
