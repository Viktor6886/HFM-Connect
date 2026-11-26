"""Read the linked firmware registry and export the available operations by chassis."""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import tempfile
from run_host_tests import compiler, FW, ROOT

def coverage():
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "catalogue.exe"
        units = ["ModuleDriver.cpp", "ScannerCatalog.cpp", "ObdSession.cpp", "EngineSignals.cpp", "FrameSession.cpp", "HfmSession.cpp", "TraceLog.cpp"]
        sources = [ROOT/"tests/fixtures/scanner_catalogue.cpp"] + [FW/name for name in units]
        subprocess.run(compiler()+["-std=c++17", "-O2", "-I", str(FW), *map(str,sources), "-o", str(exe)], check=True)
        lines = subprocess.check_output([str(exe)], encoding="utf8").splitlines()
    routes = []
    for number,series,module,variant,protocol,pin,mux,enabled,dtc,clear,id_bytes,reads in csv.reader(lines,delimiter="\t"):
        assert int(number) == len(routes)
        assert not int(clear) or int(dtc)
        assert int(enabled) or not (int(dtc) or reads)
        routes.append(dict(id=int(number),series=series,module=module,variant=variant,protocol=protocol,
                           socket_contact=int(pin) or None,hht_mux=int(mux) or None,available=bool(int(enabled)),
                           dtc_read=bool(int(dtc)),module_guarded_clear=bool(int(clear)),ks3_id_bytes=int(id_bytes) or None,
                           fixed_read_records=[int(i) for i in reads.split(",") if i]))
    active=[r for r in routes if r["available"]]
    engines=[r for r in routes if r["module"] in ("ME-OBD","KE")]
    hht=[r for r in routes if r not in engines]
    def distinct(key, rows): return sorted({r[key] for r in rows})
    counts = dict(hht_routes=len(hht), hht_enabled_routes=sum(r["available"] for r in hht),
                  supplemental_routes=len(engines),series=len(distinct("series",routes)), candidates=len(distinct("module",routes)), routes=len(routes),
                  enabled_routes=len(active), enabled_module_codes=len(distinct("module",active)),
                  enabled_module_variants=len({(r["module"],r["variant"]) for r in active}),
                  dtc_modules=distinct("module",[r for r in active if r["dtc_read"]]),
                  only_ident_modules=sorted(set(distinct("module",active))-set(distinct("module",[r for r in active if r["dtc_read"]]))),
                  fixed_read_modules=distinct("module",[r for r in active if r["fixed_read_records"]]),
                  non_hfm_guarded_clear_modules=distinct("module",[r for r in active if r["module_guarded_clear"]]))
    return {"schema":"hfm.coverage.v1","counts":counts,"routes":routes}

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path)
    args=parser.parse_args()
    data=coverage()
    if args.out:
        args.out.write_text(json.dumps(data, ensure_ascii=False, indent=1)+"\n",encoding="utf8")
    print(json.dumps(data["counts"], ensure_ascii=False))
