"""Compile PKW vehicle tree and protocol-table variants into a flash-only registry."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'firmware' / 'HFMConnect'
# Automatic HHT dispatchers retain all candidates. A listed candidate never
# authorizes sending its frames before the physical routing is known.
ALIASES = {'CV': 'CVD', 'RV': 'CVD', 'Traktion': 'ABS',
           'PTS': 'EPH', 'STH': 'SH1', 'TELE AID': 'AID'}

def read(base, name):
    return json.loads((base / (name + '.json')).read_text(encoding='utf-8-sig'))

def asr_addresses(base,series_id):
    """Evaluate only ASR AUFBAU's address selection for one chassis and failed attempts."""
    programs=read(base,'cdb/pkw/ASR/af_programs')['af']
    setup=next(p for p in programs if p['clean'] and 'AUFBAU' in p['signals'])
    insns={i['off']:i for i in setup['insns']}
    chassis=['sys0','S_824A']; address=['var','B',1,22]; last_init=['var','L',1,0]
    values={tuple(chassis):int(series_id,16)}
    choices=[]; pc=0
    for _ in range(150):
        insn=insns[pc]; op=insn['name']; a=insn['args']; pc+=insn['len']
        if op=='MOVE':
            if a['dst']==address:
                atom=a['src'][1]; assert len(atom)==1 and atom[0][0]=='imm'
                values[tuple(address)]=atom[0][1]
        elif op in ('SWITCH_LIST','SWITCH_RANGE'):
            pc=dict(a['cases']).get(values[tuple(a['v'])],a['default'])
        elif op=='JNOT_CMP':
            assert a['a']==chassis and a['op']=='==' and a['b'][0]=='imm'
            if values[tuple(chassis)]!=a['b'][1]: pc=a['target']
        elif op=='JNE_IMM':
            assert a['a']==last_init and a['imm']==0
            # Zero takes the retry path. Its addresses are alternate profiles.
        elif op=='JMP': pc=a['target']
        elif op=='CALLAF':
            call=a['af']
            if call==['G',53]:
                assert a['params'][:5]==[['imm',0],address,address,address,['imm',242]]
                choices.append(values[tuple(address)])
            elif call==['G',26]: assert a['params'][1:]==[last_init,['imm',128]]
            else: assert call==['L',3]
        elif op in ('EXIT_CODE','END'):
            if not choices or len(set(choices))!=len(choices):raise ValueError(choices)
            return choices
        else:raise ValueError(op)
    raise ValueError('ASR address selection did not terminate')

def generate(kit):
    base = kit / '02_DATA'
    tree = read(base, 'vehicles/vehicle_tree')['tree']
    frames = read(base, 'protocols/ks_proto1_frames')['modules']
    classic = read(base, 'protocols/ks_modules_summary')
    cdb = {m['module'] for m in read(base, 'catalog')['modules']
           if m['namespace'] == 'pkw' and m['kind'] != 'classic'}
    rows = {}
    for series in tree:
        for group in series['systems'].values():
            for entry in group:
                direct = entry['entry'].removesuffix('MAIN')
                candidates = entry['candidates_de'] or [direct]
                if direct == 'ABS':
                    candidates = ['ABS','ASR']
                elif direct in classic or direct in cdb:
                    candidates = [direct]
                elif direct == 'ALLAB':
                    candidates = ['ZAE', 'AB2']
                for candidate in candidates:
                    module = ALIASES.get(candidate, candidate)
                    variants = dict(frames.get(module) or classic.get(module, {}).get('tables') or {})
                    if module == 'HFM':
                        variant = {'0x8010':'c850','0x4008':'c832'}.get(series['id'],'c814')
                        variants = {variant:variants[variant]}
                    if module=='ASR':
                        for addr in asr_addresses(base,series['id']): variants[f'GL_ISO2@{addr:02X}']={}
                    elif module in cdb: variants['GL_ISO2'] = {}
                    if module in ('EGS','EDW','IF1'): variants['CDB-KS1'] = {'protocol':1}
                    if not variants: variants[''] = {}
                    for variant, details in variants.items():
                        proto = {1:'Ks1', 3:'Ks3', 6:'Ks6'}.get(details.get('protocol'),
                                'Cdb' if module in cdb else 'Unresolved')
                        contact = entry['contact'] or 0
                        key = (series['series'], module, variant, proto, contact, details.get('mux_pin') or 0)
                        rows[key] = None
    lines = ['// Generated from vehicle_tree, ks_modules_summary and ks_proto1_frames.', 'static const Route routes[] = {']
    for series, module, variant, proto, contact, mux in rows:
        names = ', '.join(json.dumps(v, ensure_ascii=False) for v in (series,module,variant))
        lines.append(f'  {{{names}, Protocol::{proto}, {contact}, {mux}}},')
    return '\n'.join(lines + ['};',''])

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kit', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    data = generate(args.kit)
    path = OUTPUT / 'ScannerRoutes.inc'
    if args.check:
        if path.read_text(encoding='utf8') != data:
            raise SystemExit('ScannerRoutes.inc differs from kit')
    else:
        path.write_text(data, encoding='utf8')
