#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed regression + negative controls for TR2 spider/disc item-slot reuse.

Verifies three invariants that previously caused a hard hang/crash when reaching
the Great Wall disc throwers after killing a spider:
  1. ItemObj_remove() never pushes a placed TR2 item (slot < itemsCount) onto
     gs->firstFree; the slot stays retired so dynamic spawns (discs, flares,
     weapon poses) cannot overwrite placed-item identity or break save/load.
  2. ItemObj_add() resets recycled slots via ItemObj_resetSlot() so stale
     ITEM_FLAG_MASK / ITEM_FLAG_ONCE bits on a freed slot cannot cause
     initBase() to auto-activate or hide the newly spawned item.
  3. ItemObj_activateBase() is idempotent and never creates a self-cycle in
     gs->firstActive if called on an already-active item.

Also runs two negative controls (mutants reverting each fix in scratch copies)
to prove the directed harness catches both failure modes.

usage: tr2-disc-reuse-test.py HOST_BUILD_DIR WALL_PACK SCRATCH
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]

CONTROL_C = r'''/* SPDX-License-Identifier: MIT */
#include "game/common/internal.h"
#include "game/tr2/objects.h"
#include "game/common/save_world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void __real_game_tr2_tick(uint32 mask);

static const char *mode(void)
{
    const char *m = getenv("TR64_DISC_REUSE_MODE");
    if (!m)
        sys_error("disc-reuse control: missing TR64_DISC_REUSE_MODE");
    return m;
}

static void verify_active_acyclic(const char *where)
{
    unsigned seen = 0;
    for (ItemObj *it = gs->firstActive; it; it = it->nextActive) {
        if (++seen > MAX_ITEMS)
            sys_error("disc-reuse: cycle in gs->firstActive (%s)", where);
    }
}

static ItemObj *find_placed_spider(void)
{
    for (uint32 i = 0; i < gs->level.itemsCount; ++i) {
        ItemObj *it = gs->items + i;
        if (it->type == TR2_ITEM_SPIDER && it->room)
            return it;
    }
    return NULL;
}

void __wrap_game_tr2_tick(uint32 mask)
{
    static int injected = 0;
    static unsigned spider_slot = UINT32_MAX;
    static unsigned discs_seen = 0;
    const char *m = mode();

    if (!injected && gs->tick == 1) {
        if (!strcmp(m, "spider-placed")) {
            ItemObj *spider = find_placed_spider();
            if (!spider)
                sys_error("disc-reuse: no placed spider found");
            spider_slot = (unsigned)(spider - gs->items);
            spider->flags |= ITEM_FLAG_MASK;
            if (!game_tr2_activate_item(spider))
                sys_error("disc-reuse: failed to activate spider");
            /* Drive the real spider death -> game_tr2_spider_breakup path. */
            spider->health = 0;
        } else if (!strcmp(m, "dirty-free")) {
            if (!gs->firstFree)
                sys_error("disc-reuse: no free item slot");
            gs->firstFree->flags = ITEM_FLAG_MASK | ITEM_FLAG_ONCE;
        } else {
            sys_error("disc-reuse: unknown mode %s", m);
        }
        injected = 1;
    }

    __real_game_tr2_tick(mask);
    verify_active_acyclic("post-tick");

    if (!strcmp(m, "spider-placed") && gs->tick == 2) {
        ItemObj *spider = gs->items + spider_slot;
        for (ItemObj *f = gs->firstFree; f; f = f->nextItem) {
            if ((unsigned)(f - gs->items) == spider_slot)
                sys_error("disc-reuse: placed spider slot %u entered firstFree", spider_slot);
        }
        if (spider->room || !(spider->flags & TR2_FLAG_RETIRED))
            sys_error("disc-reuse: broken spider not cleanly retired");
    }

    for (ItemObj *it = gs->firstActive; it; it = it->nextActive) {
        if (it->type == TR2_ITEM_DISC) {
            ++discs_seen;
            if (it->flags & ITEM_FLAG_REVERSE)
                sys_error("disc-reuse: spawned disc inherited ITEM_FLAG_REVERSE from stale flags");
            if ((it->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INVISIBLE)
                sys_error("disc-reuse: spawned disc inherited ITEM_FLAG_STATUS_INVISIBLE");
        }
    }

    if (gs->tick == 115) {
        if (!discs_seen)
            sys_error("disc-reuse: no discs ever launched");
        if (!strcmp(m, "spider-placed")) {
            ItemObj *spider = gs->items + spider_slot;
            if (spider->type != TR2_ITEM_SPIDER || spider->room != NULL)
                sys_error("disc-reuse: placed spider slot %u corrupted (type=%u room=%p)",
                          spider_slot, (unsigned)spider->type, (void *)spider->room);
            static uint8_t buf[32768];
            uint32_t len = game_save(buf, (uint32_t)sizeof(buf));
            if (!len)
                sys_error("disc-reuse: game_save failed after spider breakup + discs");
            if (!game_load(buf, len))
                sys_error("disc-reuse: game_load rejected save after spider breakup + discs");
        }
        printf("PASS disc-reuse mode=%s discs_seen=%u\n", m, discs_seen);
        exit(0);
    }
}
'''

ROUTE_TEXT = """scene tr2-disc-reuse
start boot
seed 1
load level wall
game
hash-schema 21
place 45 11776 3584 42496 0
ticks 120
at 0 pad 8 0 0
observe tr2-world
"""


def run_cmd(cmd, **kwargs):
    return subprocess.run(['rtk', *map(str, cmd)], capture_output=True, text=True, **kwargs)


def compile_obj(build, src, out):
    gen = build / 'generated'
    r = run_cmd([
        'cc', '-m32', '-std=c99', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
        '-Wno-unused-function', f'-I{gen}/host', f'-I{ROOT}', f'-I{ROOT}/include',
        f'-I{ROOT}/game', f'-I{gen}', '-ffp-contract=off', '-fno-fast-math',
        '-DTR64_GAME=2', '-DTR64_PLAY=0', '-DTR64_TEST=0', '-DTR64_LEVEL_LOADER=1',
        '-D_POSIX_C_SOURCE=200809L', '-DTR64_HOST=1', '-c', src, '-o', out,
    ], timeout=120)
    if r.returncode:
        raise RuntimeError(f'compile {src} failed:\n{r.stderr[-2000:]}')


def link_host(build, scratch, name, control_obj, replacements):
    skip = {build / 'host' / rel for rel in replacements}
    objs = sorted(str(p) for p in (build / 'host').rglob('*.o') if p not in skip)
    out = scratch / name
    r = run_cmd([
        'cc', '-m32', '-Wl,--wrap=game_tr2_tick', '-o', out,
        *objs, control_obj, *replacements.values(),
    ], timeout=120)
    if r.returncode:
        raise RuntimeError(f'link {name} failed:\n{r.stderr[-2000:]}')
    return out


def run_case(exe, rbc, shot, mode, expect_pass):
    env = dict(os.environ, TR64_DISC_REUSE_MODE=mode)
    try:
        r = subprocess.run([str(exe), '--route', str(rbc), '--shot', str(shot)],
                           env=env, capture_output=True, text=True, timeout=15)
        out = r.stdout + '\n' + r.stderr
        timed_out = False
        rc = r.returncode
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b'').decode('utf-8', 'replace') + '\n' + (e.stderr or b'').decode('utf-8', 'replace')
        timed_out = True
        rc = 124
    passed = (not timed_out) and rc == 0 and f'PASS disc-reuse mode={mode}' in out
    if expect_pass and not passed:
        raise RuntimeError(f'case {mode} on {exe.name} FAILED (rc={rc}, timeout={timed_out}):\n{out[-2000:]}')
    if not expect_pass and passed:
        raise RuntimeError(f'negative control {mode} on {exe.name} unexpectedly PASSED:\n{out[-1000:]}')
    return rc, timed_out, out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('build', type=Path)
    ap.add_argument('pack', type=Path)
    ap.add_argument('scratch', type=Path)
    args = ap.parse_args()

    build, scratch = args.build.resolve(), args.scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=True)

    control_c = scratch / 'disc_reuse_control.c'
    control_c.write_text(CONTROL_C)
    control_o = scratch / 'disc_reuse_control.o'
    compile_obj(build, control_c, control_o)

    route_path = scratch / 'disc-reuse.route'
    route_path.write_text(ROUTE_TEXT)
    rbc_path = scratch / 'disc-reuse.rbc'
    r = run_cmd([sys.executable, ROOT / 'tools/routec/routec.py', route_path, '-o', rbc_path], timeout=30)
    if r.returncode:
        raise RuntimeError(f'routec failed: {r.stderr}')

    # 1. Production build: both positive tests must pass.
    prod = link_host(build, scratch, 'host-prod', control_o, {})
    for m in ('spider-placed', 'dirty-free'):
        run_case(prod, rbc_path, scratch / f'{m}.ppm', m, expect_pass=True)
        print(f'PASS production {m}')

    # 2. Negative control A: remove ItemObj_resetSlot(self) from ItemObj_add() and
    #    idempotency guard from ItemObj_activateBase() -> dirty-free MUST fail.
    game_src = (ROOT / 'game/common/game.c').read_text()
    item_src = (ROOT / 'game/common/item.c').read_text()

    old_add = '    gs->firstFree = self->nextItem;\n    ItemObj_resetSlot(self);\n'
    new_add = '    gs->firstFree = self->nextItem;\n'
    if game_src.count(old_add) != 1:
        raise RuntimeError('negative control A: expected ItemObj_resetSlot in ItemObj_add')
    mut_game_c = scratch / 'mut_game.c'
    mut_game_c.write_text(game_src.replace(old_add, new_add, 1))
    mut_game_o = scratch / 'mut_game.o'
    compile_obj(build, mut_game_c, mut_game_o)

    old_act = '    if (self->flags & ITEM_FLAG_ACTIVE)\n        return;\n'
    if item_src.count(old_act) != 1:
        raise RuntimeError('negative control A: expected active guard in ItemObj_activateBase')
    mut_item_a_c = scratch / 'mut_item_a.c'
    mut_item_a_c.write_text(item_src.replace(old_act, '', 1))
    mut_item_a_o = scratch / 'mut_item_a.o'
    compile_obj(build, mut_item_a_c, mut_item_a_o)

    mut_a = link_host(build, scratch, 'host-mut-a', control_o, {
        'game/common/game.o': mut_game_o,
        'game/common/item.o': mut_item_a_o,
    })
    rc, timed_out, _ = run_case(mut_a, rbc_path, scratch / 'mut-a.ppm', 'dirty-free', expect_pass=False)
    print(f'PASS negative-control-A (dirty-free rejected: rc={rc}, timeout={timed_out})')

    # 3. Negative control B: remove placed-item retirement guard from ItemObj_remove()
    #    -> spider-placed MUST fail.
    old_rem = '    if (TR64_SECOND_GAME && (uint32)(self - gs->items) < gs->level.itemsCount)\n'
    new_rem = '    if (0 && TR64_SECOND_GAME && (uint32)(self - gs->items) < gs->level.itemsCount)\n'
    if item_src.count(old_rem) != 1:
        raise RuntimeError('negative control B: expected placed-item guard in ItemObj_remove')
    mut_item_b_c = scratch / 'mut_item_b.c'
    mut_item_b_c.write_text(item_src.replace(old_rem, new_rem, 1))
    mut_item_b_o = scratch / 'mut_item_b.o'
    compile_obj(build, mut_item_b_c, mut_item_b_o)

    mut_b = link_host(build, scratch, 'host-mut-b', control_o, {
        'game/common/item.o': mut_item_b_o,
    })
    rc, timed_out, _ = run_case(mut_b, rbc_path, scratch / 'mut-b.ppm', 'spider-placed', expect_pass=False)
    print(f'PASS negative-control-B (spider-placed rejected: rc={rc}, timeout={timed_out})')


if __name__ == '__main__':
    main()
