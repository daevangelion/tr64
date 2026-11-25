# SPDX-License-Identifier: MIT
"""Actual logger/allocator/dispatch burst at24frames with31-byte unit names.

One code image for host+cart. Synthetic code/data only, not a game parity or
performance result. No entry flush or altered production flush policy to fit4KiB.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import shlex
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("scene", ROOT / "test/run/scene.py")
scene = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scene)
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--ares", action="store_true")
p.add_argument("--unit-only", action="store_true")
p.add_argument("--max-peak", type=int, default=4096)
a = p.parse_args()
base = Path(os.environ["BUILD_DIR"]).resolve() / "log-burst-tests"
base.mkdir(parents=True, exist_ok=True)
cc = shlex.split(os.environ.get("HOST_CC", "cc"))
scene.command([*cc,"-m32","-std=c99","-O2","-Wall","-Wextra","-Werror","-I",ROOT,"-I",ROOT/"include",
               ROOT/"engine/core/fmt.c",ROOT/"engine/core/log.c",ROOT/"engine/test/log-burst-test.c",
               "-o",base/"log-test"],base/"unit-build.log",timeout=60)
scene.command([base/"log-test"],base/"unit.log",timeout=5)
print((base/"unit.log").read_text().strip())
if a.unit_only:
    sys.exit(0)
inputs=base/"input"
inputs.mkdir(exist_ok=True)
names=["b"+"x"*28+f"{i:02d}" for i in range(24)]
sources=[]
for i,name in enumerate(names):
    source=inputs/(name+".c")
    source.write_text(f'''/* SPDX-License-Identifier: MIT */
#include <stdint.h>
static const uint32_t words[4]={{0x{0x55000000+i:08x}u,0x13579bdfu,0x2468ace0u,0x10203040u}};
uint32_t tr64_unit_{name}(uint32_t value) {{ return words[value&3u]^value; }}
''')
    sources.append(str(source))
fragment=inputs/"burst.def"
fragment.write_text("# SPDX-License-Identifier: MIT\n"+"".join(name+" *"+name+".o\n" for name in names))
probe=inputs/"probe.c"
probe.write_text('''/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "tr64/code.h"
#include "tr64/mem.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
static const char *names[]={'''+",".join('"'+name+'"' for name in names)+'''};
void tr64_scene_load(const char *name)
{
 log_stats_t stats;tr64_mem_stats_t pool;
 if(strcmp(name,"log-burst"))sys_error("log burst identity");
 code_loading_begin();
 /* Do not flush pending boot/other diagnostic bytes at entry. */
 for(unsigned i=0;i<24;++i) {
  code_load(names[i]);
  if(code_call(names[i],0)!=(0x55000000u+i))sys_error("log burst actual unit call %u",i);
 }
 code_log();tr64_mem_stats(TR64_MEM_code,&pool);log_stats(&stats);
 if(pool.used!=24u*4096u || stats.dropped)sys_error("log burst lost loaded bytes");
 log_flush();
 log_printf("log-burst: loaded=24 called=24 frames=24 peak=%lu lost=%llu\\n",(unsigned long)stats.peak_pending,(unsigned long long)stats.dropped);
 log_flush();
 for(unsigned i=0;i<24;++i)code_unload(names[i]);
 code_log();code_loading_end();tr64_mem_stats(TR64_MEM_code,&pool);log_stats(&stats);
 if(pool.used || stats.dropped)sys_error("log burst leaked frames or bytes");
 log_printf("log-burst: released=24 frames=0 peak=%lu lost=%llu\\n",(unsigned long)stats.peak_pending,(unsigned long long)stats.dropped);
 log_flush();log_report();
}
''')
sources.append(str(probe))
route=inputs/"burst.route"
route.write_text("scene log-burst\nstart boot\nload level log-burst\nticks 1\nseed 1\nobserve pools\n")
bytecode=inputs/"burst.rbc"
bytecode.write_bytes(scene.routec.parse(route)[1])
build=base/"build"
options=["TR64_TEST=0","TR64_PLAY=0","TR64_LEVEL_LOADER=0","TR64_LEVEL_PACK=",
         "TR64_ENGINE_SOURCES=","TR64_HOST_SOURCES=","TR64_CART_SOURCES=","TR64_UNIT_SOURCES=",
         "TR64_GENERATED_HEADERS=","TR64_ROM_FILES=","TR64_ROM_DEPS=",
         "TR64_CODE_UNIT_FRAGMENTS="+str(fragment),"TR64_SCENE_TEST_SOURCES="+" ".join(sources)]
scene.command(["make","--no-print-directory","-j1",f"BUILD_DIR={build}",f"SCENE={route}",*options,"host"],
              base/"build.log",timeout=180)
for mode in (["host","cart"] if a.ares else ["host"]):
    out=base/mode
    out.mkdir(exist_ok=True)
    log=out/"engine.log"
    if log.exists():log.unlink()
    if mode=="host":
        scene.command([build/"host/tr64-host","--route",bytecode],log,timeout=20)
    else:
        expected_build = scene.guest_identity.expected_identity(build)
        scene.command([ROOT/"test/run/ares.sh",out,build/"tr64.z64",log,"60"],out/"runner.log",
                      timeout=70,env=dict(os.environ,ARES_EXPANSION_PAK="false"))
    text=log.read_text()
    if mode=="cart":
        scene.guest_identity.require_identity(text,expected_build)
    record=re.findall(r"^log-burst: loaded=24 called=24 frames=24 peak=(\d+) lost=(\d+)$",text,re.M)
    released=re.findall(r"^log-burst: released=24 frames=0 peak=(\d+) lost=(\d+)$",text,re.M)
    if len(record)!=1 or len(released)!=1 or any(int(loss) for _,loss in record+released):
        raise AssertionError("actual maximum code-load burst missing/leaked/dropped: "+text)
    peak=max(int(n) for n,_ in record+released)
    if peak>a.max_peak:
        raise AssertionError(f"4KiB burst proof DOES NOT HOLD: {mode} peak={peak}")
    if "code: frames used=24 free=0 tlb_entries=25 wired=1" not in text or "code: frames used=0 free=24 tlb_entries=1 wired=1" not in text:
        raise AssertionError("code frame/TLB maximum was not exercised/returned")
    print(f"log code burst PASS {mode}:24actualcalls,31-byte names,peak={peak},loss0,frames/TLBreturned; {log}")
