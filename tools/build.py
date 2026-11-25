#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build a playable tr64 N64 ROM from a Steam install or CD-ROM layout.

Supports Tomb Raider I (PC CD-ROM, Steam GAME.GOG, PlayStation CD-ROM / BIN+CUE),
Tomb Raider II (Steam / PC CD-ROM), and Tomb Raider III (Steam / PC CD-ROM).
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

REPO_ROOT = Path(__file__).resolve().parents[1]
COOK_DIR = REPO_ROOT / 'tools' / 'cook'

TR1_ORDER = (
    'title', 'gym', 'level1', 'level2', 'level3a', 'level3b', 'cut1',
    'level4', 'level5', 'level6', 'level7a', 'level7b', 'cut2',
    'level8a', 'level8b', 'level8c', 'level10a', 'cut3',
    'level10b', 'cut4', 'level10c',
)
TR2_DEFAULT = ('title', 'wall', 'boat', 'venice', 'opera', 'assault')
TR2_ALL = (
    'title', 'wall', 'boat', 'venice', 'opera', 'rig', 'platform',
    'unwater', 'keel', 'living', 'deck', 'skidoo', 'monastry',
    'catacomb', 'icecave', 'emprtomb', 'floating', 'xian', 'house', 'assault',
)
TR3_DEFAULT = ('title', 'jungle', 'temple')
TR3_ALL = (
    'title', 'jungle', 'temple', 'quadchas', 'tonyboss', 'house',
    'nevada', 'compound', 'area51', 'shore', 'crash', 'rapids', 'triboss',
    'roofs', 'sewer', 'tower', 'office', 'stpaul', 'antarc', 'mines',
    'city', 'chamber', 'vict',
)


# ---------------------------------------------------------------------------
# Pure-Python ISO9660 reader (standard 2048, raw Mode-1/2352 GAME.GOG,
# and raw Mode-2/2352 PlayStation BIN/CUE).
# ---------------------------------------------------------------------------

class IsoImage:
    def __init__(self, image_path: Path):
        self.path = image_path
        self.size = image_path.stat().st_size
        self.sector_size, self.payload_off = self._detect_geometry()
        self.sector_count = self.size // self.sector_size

    def _detect_geometry(self):
        with self.path.open('rb') as f:
            for sector_size, off, mode_byte in ((2048, 0, None), (2352, 16, 1), (2352, 24, 2)):
                pos = 16 * sector_size
                if pos + sector_size > self.size:
                    continue
                f.seek(pos)
                raw = f.read(sector_size)
                if mode_byte is not None and raw[15] != mode_byte:
                    continue
                if raw[off:off + 7] == b'\x01CD001\x01':
                    return sector_size, off
        raise ValueError(f'{self.path}: not a recognized ISO9660 or raw 2352-byte disc image')

    def _read_sector(self, fh, lba: int) -> bytes:
        if not 0 <= lba < self.sector_count:
            raise ValueError(f'{self.path}: sector {lba} out of range ({self.sector_count})')
        fh.seek(lba * self.sector_size + self.payload_off)
        return fh.read(2048)

    def list_files(self):
        entries = []
        with self.path.open('rb') as fh:
            pvd = self._read_sector(fh, 16)
            root_rec = pvd[156:156 + 34]
            root_lba = struct.unpack_from('<I', root_rec, 2)[0]
            root_size = struct.unpack_from('<I', root_rec, 10)[0]
            visited = set()

            def walk(lba, size, prefix):
                if lba in visited:
                    return
                visited.add(lba)
                n_sec = (size + 2047) // 2048
                raw = b''.join(self._read_sector(fh, lba + i) for i in range(n_sec))[:size]
                off = 0
                while off < size:
                    rec_len = raw[off]
                    if rec_len == 0:
                        off = (off // 2048 + 1) * 2048
                        continue
                    rec = raw[off:off + rec_len]
                    off += rec_len
                    if len(rec) < 33:
                        continue
                    flags = rec[25]
                    name_len = rec[32]
                    raw_name = rec[33:33 + name_len]
                    if raw_name in (b'\x00', b'\x01'):
                        continue
                    name = raw_name.decode('ascii', errors='replace').split(';', 1)[0].rstrip('.')
                    rel = f'{prefix}/{name}' if prefix else name
                    e_lba = struct.unpack_from('<I', rec, 2)[0]
                    e_size = struct.unpack_from('<I', rec, 10)[0]
                    if flags & 2:
                        walk(e_lba, e_size, rel)
                    else:
                        entries.append((rel, e_lba, e_size))

            walk(root_lba, root_size, '')
        return entries

    def extract(self, wanted_entries, dest_dir: Path):
        dest_dir.mkdir(parents=True, exist_ok=True)
        extracted = {}
        with self.path.open('rb') as fh:
            for rel, lba, size in wanted_entries:
                out_path = dest_dir / rel
                if out_path.is_file() and out_path.stat().st_size == size:
                    extracted[rel.lower()] = out_path
                    continue
                out_path.parent.mkdir(parents=True, exist_ok=True)
                tmp = out_path.with_suffix(out_path.suffix + '.tmp')
                remaining = size
                cur = lba
                with tmp.open('wb') as out:
                    while remaining > 0:
                        chunk = self._read_sector(fh, cur)
                        take = min(remaining, 2048)
                        out.write(chunk[:take])
                        remaining -= take
                        cur += 1
                tmp.replace(out_path)
                extracted[rel.lower()] = out_path
        return extracted


def cue_to_bin(cue_path: Path) -> Path:
    for line in cue_path.read_text(errors='replace').splitlines():
        m = re.match(r'\s*FILE\s+"([^"]+)"\s+BINARY', line, re.I)
        if m:
            bin_path = cue_path.parent / m.group(1)
            if bin_path.is_file():
                return bin_path
            # Case-insensitive fallback in the same directory
            target = m.group(1).lower()
            for child in cue_path.parent.iterdir():
                if child.name.lower() == target and child.is_file():
                    return child
            raise FileNotFoundError(f'BIN referenced by {cue_path} not found: {m.group(1)}')
    raise ValueError(f'{cue_path}: no FILE "..." BINARY entry found')


# ---------------------------------------------------------------------------
# Case-insensitive layout detection for Steam / PC CD-ROM / PSX CD-ROM
# ---------------------------------------------------------------------------

def index_directory(root: Path, max_depth: int = 3):
    """Return dict[lowercase_rel_posix] -> Path for files up to max_depth."""
    files = {}
    root = root.resolve()
    for dirpath, dirnames, filenames in os.walk(root):
        rel_dir = Path(dirpath).relative_to(root)
        depth = 0 if rel_dir == Path('.') else len(rel_dir.parts)
        if depth >= max_depth:
            dirnames[:] = []
        for fn in filenames:
            p = Path(dirpath) / fn
            rel = (rel_dir / fn).as_posix() if rel_dir != Path('.') else fn
            files[rel.lower()] = p
    return files


def find_by_candidates(index, candidates):
    for c in candidates:
        c_low = c.lower()
        if c_low in index:
            return index[c_low]
    return None


def find_level_map(index, ext: str, preferred_dirs=()):
    """Map lowercase stem -> Path for files with extension `ext`."""
    ext = ext.lower()
    levels = {}
    # Sort so preferred directories win if duplicates exist.
    def priority(rel):
        parts = rel.split('/')
        parent = parts[0] if len(parts) > 1 else ''
        if parent in preferred_dirs:
            return (0, preferred_dirs.index(parent), rel)
        if len(parts) == 1:
            return (1, 0, rel)
        return (2, len(parts), rel)

    for rel in sorted(index.keys(), key=priority):
        if not rel.endswith(ext):
            continue
        stem = Path(rel).stem.lower()
        if stem not in levels:
            levels[stem] = index[rel]
    return levels


def find_donor_file(path: Path, stem: str) -> Path:
    path = path.expanduser().resolve()
    if path.is_file():
        return path
    if not path.is_dir():
        raise FileNotFoundError(f'donor path not found: {path}')
    idx = index_directory(path, max_depth=3)
    hit = find_by_candidates(idx, (f'data/{stem}.tr2', f'{stem}.tr2'))
    if hit is None:
        for rel, p in idx.items():
            if Path(rel).name == f'{stem}.tr2':
                hit = p
                break
    if hit is None:
        raise FileNotFoundError(f'could not find {stem.upper()}.TR2 under {path}')
    return hit


def detect_layout(input_path: Path, build_dir: Path, forced_game=None, want_fmv=False):
    """Detect game (1/2/3), source ('pc'/'psx'), levels, sfx, title image, cue, fmv."""
    input_path = input_path.expanduser().resolve()
    cue_path = None
    image_path = None

    if input_path.is_file():
        suf = input_path.suffix.lower()
        if suf == '.cue':
            cue_path = input_path
            image_path = cue_to_bin(cue_path)
        elif suf in ('.gog', '.iso', '.bin', '.img'):
            image_path = input_path
        else:
            raise ValueError(f'unsupported input file {input_path} (pass a directory, .cue, .gog, or .iso)')
    elif input_path.is_dir():
        idx = index_directory(input_path, max_depth=3)
        # Check whether this is a TR1 Steam folder containing GAME.GOG without loose PHDs
        has_loose_levels = any(k.endswith(('.phd', '.psx', '.tr2')) for k in idx)
        if not has_loose_levels:
            gog = find_by_candidates(idx, ('game.gog',))
            if gog is not None:
                image_path = gog
            else:
                cues = sorted(p for k, p in idx.items() if k.endswith('.cue') and '/' not in k)
                if cues:
                    cue_path = cues[0]
                    image_path = cue_to_bin(cue_path)
    else:
        raise FileNotFoundError(f'input path not found: {input_path}')

    if image_path is not None:
        iso = IsoImage(image_path)
        iso_entries = iso.list_files()
        wanted = []
        for rel, lba, size in iso_entries:
            low = rel.lower()
            if low.endswith(('.phd', '.psx', '.tr2', '.sfx', '.pcx', '.bmp', '.raw')):
                wanted.append((rel, lba, size))
            elif want_fmv and low.endswith('.rpl'):
                wanted.append((rel, lba, size))
        if not wanted:
            raise ValueError(f'no Tomb Raider data files found inside {image_path}')
        extract_dir = build_dir / 'extracted'
        print(f'Extracting {len(wanted)} files from {image_path.name} -> {extract_dir} ...')
        iso.extract(wanted, extract_dir)
        idx = index_directory(extract_dir, max_depth=3)
        data_root = extract_dir
    else:
        data_root = input_path

    psx_levels = find_level_map(idx, '.psx', preferred_dirs=('psxdata',))
    phd_levels = find_level_map(idx, '.phd', preferred_dirs=('data',))
    tr2_levels = find_level_map(idx, '.tr2', preferred_dirs=('data', 'cuts'))

    detected_game = None
    source = None
    levels = None
    main_sfx = None
    title_image = None
    fmv_dir = None

    if psx_levels and (forced_game in (None, 1)):
        detected_game = 1
        source = 'psx'
        levels = psx_levels
        title_image = find_by_candidates(idx, (
            'deldata/psxtit.raw', 'deldata/amertit.raw', 'psxtit.raw', 'amertit.raw',
        ))
    elif phd_levels and (forced_game in (None, 1)):
        detected_game = 1
        source = 'pc'
        levels = phd_levels
        title_image = find_by_candidates(idx, ('data/titleh.pcx', 'titleh.pcx'))
        for rel, p in idx.items():
            if rel.endswith('.rpl'):
                fmv_dir = p.parent
                break
    elif tr2_levels:
        is_tr3 = ('jungle' in tr2_levels or 'temple' in tr2_levels)
        is_tr2 = ('wall' in tr2_levels or 'boat' in tr2_levels or 'assault' in tr2_levels)
        if forced_game == 3 or (forced_game is None and is_tr3 and not is_tr2):
            detected_game = 3
        elif forced_game == 2 or (forced_game is None and is_tr2):
            detected_game = 2
        else:
            raise ValueError(
                f'found .TR2 files under {data_root} but could not distinguish TR2 vs TR3; pass --game 2 or --game 3'
            )
        source = 'pc'
        levels = tr2_levels
        main_sfx = find_by_candidates(idx, ('data/main.sfx', 'main.sfx'))
        if main_sfx is None:
            raise FileNotFoundError(f'MAIN.SFX not found under {data_root} (required for Tomb Raider {detected_game})')
        if detected_game == 2:
            title_image = find_by_candidates(idx, ('data/title.pcx', 'pix/title.pcx', 'title.pcx'))
        else:
            title_image = find_by_candidates(idx, ('pix/titleuk.bmp', 'pix/titleus.bmp', 'titleuk.bmp', 'titleus.bmp'))
            if title_image is None:
                for rel, p in sorted(idx.items()):
                    if Path(rel).name.startswith('title') and rel.endswith('.bmp'):
                        title_image = p
                        break
    else:
        raise ValueError(
            f'no Tomb Raider level files (.PHD, .PSX, .TR2, or GAME.GOG) found under {input_path}'
        )

    if forced_game is not None and forced_game != detected_game:
        raise ValueError(f'--game {forced_game} requested, but {input_path} looks like Tomb Raider {detected_game}')

    return dict(
        game=detected_game,
        source=source,
        levels=levels,
        main_sfx=main_sfx,
        title_image=title_image,
        cue=cue_path,
        fmv_dir=fmv_dir,
        data_root=data_root,
    )


def select_levels(game: int, available: dict, spec: str | None, include_title: bool):
    if game == 1:
        default_order = TR1_ORDER
        all_order = TR1_ORDER
    elif game == 2:
        default_order = TR2_DEFAULT
        all_order = TR2_ALL
    else:
        default_order = TR3_DEFAULT
        all_order = TR3_ALL

    if not spec:
        wanted = [name for name in default_order if name in available]
        if not wanted:
            wanted = sorted(available.keys())
    elif spec.strip().lower() == 'all':
        wanted = [name for name in all_order if name in available]
        for name in sorted(available.keys()):
            if name not in wanted:
                wanted.append(name)
    else:
        wanted = []
        for token in spec.split(','):
            name = token.strip().lower()
            if not name:
                continue
            if name.endswith(('.phd', '.psx', '.tr2')):
                name = Path(name).stem.lower()
            if name not in available:
                raise ValueError(f'requested level "{name}" not found (available: {", ".join(sorted(available))})')
            if name not in wanted:
                wanted.append(name)

    if include_title and 'title' in available and 'title' not in wanted:
        wanted.insert(0, 'title')
    if not include_title:
        wanted = [w for w in wanted if w != 'title']
    if not wanted:
        raise ValueError('no levels selected to cook')
    return wanted


def run_cmd(cmd, *, env=None, dry_run=False, quiet=False):
    printable = ' '.join(str(x) for x in cmd)
    if dry_run:
        print(f'[dry-run] {printable}')
        return
    if not quiet:
        print(f'+ {printable}', flush=True)
    subprocess.run([str(x) for x in cmd], check=True, env=env)


def build_spu_encoder(build_dir: Path, dry_run: bool) -> Path:
    enc = build_dir / 'spu_enc'
    if not enc.is_file():
        cc = os.environ.get('HOST_CC', 'cc')
        run_cmd([cc, '-O2', COOK_DIR / 'n64_music_spu.c', '-o', enc], dry_run=dry_run)
    return enc


def main():
    p = argparse.ArgumentParser(
        description='Build a playable tr64 N64 ROM directly from a Steam install or CD-ROM layout.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            'Examples:\n'
            '  python3 tools/build.py --input "/path/to/Steam/steamapps/common/Tomb Raider (I)" --out tr64-tr1.z64\n'
            '  python3 tools/build.py --input "/path/to/Steam/steamapps/common/Tomb Raider (II)" --out tr64-tr2.z64\n'
            '  python3 tools/build.py --input "/path/to/Steam/steamapps/common/Tomb Raider (III)" --out tr64-tr3.z64\n'
            '  python3 tools/build.py --input /path/to/tr1-psx.cue --music --fmv --out tr64-tr1-psx.z64\n'
        ),
    )
    p.add_argument('--input', required=True, type=Path,
                   help='Steam install directory, CD-ROM directory, GAME.GOG, .iso, or PSX .cue')
    p.add_argument('--out', type=Path, default=None,
                   help='output N64 ROM path (.z64), or .pack path with --pack-only')
    p.add_argument('--game', type=int, choices=(1, 2, 3), default=None,
                   help='Tomb Raider game (1, 2, or 3); auto-detected if omitted')
    p.add_argument('--levels', default=None,
                   help='comma-separated level stems (e.g. wall,boat,venice,opera) or "all"')
    p.add_argument('--no-title', action='store_true',
                   help='omit TITLE level from the pack')
    p.add_argument('--title-image', type=Path, default=None,
                   help='override title backdrop image (.PCX, .BMP, or PSX .RAW)')
    p.add_argument('--psx-encoding', choices=('spu', 'pcm'), default='spu',
                   help='sound encoding for PSX TR1 levels (default: spu)')
    p.add_argument('--lara-tr2-donor', type=Path, default=None,
                   help='TR2 WALL.TR2 or TR2 Steam/CD directory (unlocks TR2 Lara Moves in TR1)')
    p.add_argument('--lara-tr3-donor', type=Path, default=None,
                   help='TR3 JUNGLE.TR2 or TR3 Steam/CD directory (unlocks TR3 Lara Moves in TR1/TR2)')
    p.add_argument('--music', action='store_true',
                   help='cook TR1 CD-DA music tracks (requires .cue via --input or --cue)')
    p.add_argument('--cue', type=Path, default=None,
                   help='explicit BIN/CUE sheet for TR1 --music / PSX --fmv')
    p.add_argument('--music-mapping', type=Path, default=None,
                   help='request-id -> CD track mapping JSON (required for PC CD-DA --music)')
    p.add_argument('--fmv', action='store_true',
                   help='cook TR1 FMV cutscenes (requires ffmpeg; uses PC FMV/*.RPL or PSX .cue)')
    p.add_argument('--build-dir', type=Path, default=None,
                   help='scratch/build directory outside the repository (default: $TMPDIR/tr64-build-tr<N>)')
    p.add_argument('--n64-inst', type=Path, default=None,
                   help='path to pinned N64 SDK (overrides N64_INST)')
    p.add_argument('--n64-gccprefix', type=Path, default=None,
                   help='path to mips64-elf GCC toolchain (overrides N64_GCCPREFIX)')
    p.add_argument('--jobs', '-j', type=int, default=min(4, os.cpu_count() or 1),
                   help='parallel jobs for level cooking and make (default: min(4, CPUs))')
    p.add_argument('--pack-only', action='store_true',
                   help='stop after producing the merged .pack (do not invoke make rom)')
    p.add_argument('--dry-run', action='store_true',
                   help='detect layout and print planned steps without cooking or building')
    args = p.parse_args()

    prelim_dir = args.build_dir or (Path(tempfile.gettempdir()) / f'tr64-build-tr{args.game or "auto"}')
    prelim_dir = prelim_dir.expanduser().resolve()
    if not args.dry_run:
        prelim_dir.mkdir(parents=True, exist_ok=True)

    info = detect_layout(args.input, prelim_dir, forced_game=args.game, want_fmv=args.fmv)
    game = info['game']
    source = info['source']
    build_dir = args.build_dir.expanduser().resolve() if args.build_dir else (
        Path(tempfile.gettempdir()) / f'tr64-build-tr{game}'
    ).resolve()
    if build_dir != prelim_dir and not args.dry_run:
        build_dir.mkdir(parents=True, exist_ok=True)

    selected = select_levels(game, info['levels'], args.levels, include_title=not args.no_title)
    title_image = args.title_image.expanduser().resolve() if args.title_image else info['title_image']
    cue_path = args.cue.expanduser().resolve() if args.cue else info['cue']

    tr2_donor = find_donor_file(args.lara_tr2_donor, 'wall') if args.lara_tr2_donor else None
    tr3_donor = find_donor_file(args.lara_tr3_donor, 'jungle') if args.lara_tr3_donor else None
    if tr2_donor and game != 1:
        print('Note: --lara-tr2-donor only applies to Tomb Raider I; ignoring.', file=sys.stderr)
        tr2_donor = None
    if tr3_donor and game == 3:
        print('Note: --lara-tr3-donor only applies to Tomb Raider I/II; ignoring.', file=sys.stderr)
        tr3_donor = None

    default_ext = '.pack' if args.pack_only else '.z64'
    out_path = (args.out or Path(f'tr64-tr{game}{default_ext}')).expanduser().resolve()

    print(f'Detected: Tomb Raider {game} ({source.upper()}) from {args.input}')
    print(f'Levels ({len(selected)}): {", ".join(selected)}')
    if info['main_sfx']:
        print(f'SFX bank: {info["main_sfx"]}')
    if title_image:
        print(f'Title backdrop: {title_image}')
    if tr2_donor:
        print(f'TR2 Lara donor: {tr2_donor}')
    if tr3_donor:
        print(f'TR3 Lara donor: {tr3_donor}')
    print(f'Build dir: {build_dir}')
    print(f'Output:    {out_path}')

    packs_dir = build_dir / 'packs'
    cache_dir = build_dir / 'cache'
    if not args.dry_run:
        packs_dir.mkdir(parents=True, exist_ok=True)
        cache_dir.mkdir(parents=True, exist_ok=True)

    # Optional TR1 music catalog
    music_manifest = None
    if args.music:
        if game != 1:
            p.error('--music is currently supported for Tomb Raider I only')
        if cue_path is None:
            p.error('--music requires a single-file BIN/CUE via --input or --cue')
        spu_enc = build_spu_encoder(build_dir, args.dry_run)
        music_dir = build_dir / 'music'
        cmd = [
            sys.executable, COOK_DIR / 'n64_music.py',
            '--source', source, '--cue', cue_path,
            '--spu-encoder', spu_enc, '--out', music_dir,
        ]
        if args.music_mapping:
            cmd += ['--mapping', args.music_mapping.expanduser().resolve()]
        elif source == 'pc':
            p.error('PC CD-DA --music requires --music-mapping <mapping.json>')
        run_cmd(cmd, dry_run=args.dry_run)
        music_manifest = music_dir / 'manifest.json'

    # Pre-warm host LZ4 compressor once before spawning parallel cook workers
    if not args.dry_run:
        sys.path.insert(0, str(COOK_DIR))
        import n64_pack
        n64_pack.compressor(cache_dir)

    # 1. Cook each level
    level_packs = []
    cook_jobs = []
    for stem in selected:
        src_file = info['levels'][stem]
        pack_file = packs_dir / f'{stem}.pack'
        level_packs.append(pack_file)
        cmd = [
            sys.executable, COOK_DIR / 'pack_level.py',
            '--source', source,
            '--input', src_file,
            '--cache', cache_dir,
            '--out', pack_file,
        ]
        if info['main_sfx']:
            cmd += ['--main-sfx', info['main_sfx']]
        if source == 'psx':
            cmd += ['--psx-encoding', args.psx_encoding]
        if stem == 'title':
            if title_image:
                cmd += ['--title-image', title_image]
            if music_manifest:
                cmd += ['--music-catalog', music_manifest]
        else:
            if tr2_donor:
                cmd += ['--lara-tr2-donor', tr2_donor]
            if tr3_donor:
                cmd += ['--lara-tr3-donor', tr3_donor]
        cook_jobs.append((stem, cmd))

    if args.dry_run:
        for _, cmd in cook_jobs:
            run_cmd(cmd, dry_run=True)
    else:
        print(f'\n[1/3] Cooking {len(cook_jobs)} level(s) with {max(1, args.jobs)} worker(s)...', flush=True)
        with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            futs = {
                pool.submit(subprocess.run, [str(x) for x in cmd], check=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True): stem
                for stem, cmd in cook_jobs
            }
            for fut in as_completed(futs):
                stem = futs[fut]
                try:
                    fut.result()
                    size_kb = (packs_dir / f'{stem}.pack').stat().st_size / 1024.0
                    print(f'  cooked {stem:10s} ({size_kb:7.1f} KiB)', flush=True)
                except subprocess.CalledProcessError as exc:
                    print(f'ERROR cooking {stem}:\n{exc.stdout}', file=sys.stderr)
                    sys.exit(1)

    # 2. Merge packs into one campaign pack
    merge_game_tag = 0 if game == 1 else game
    campaign_pack = build_dir / f'tr{game}-campaign.pack'
    print('\n[2/3] Merging campaign pack...', flush=True)
    merge_cmd = [
        sys.executable, COOK_DIR / 'pack_merge.py',
        '--game', str(merge_game_tag),
        '--cache', cache_dir,
        '--out', campaign_pack,
        '--inputs', *level_packs,
    ]
    run_cmd(merge_cmd, dry_run=args.dry_run, quiet=True)

    # Optional TR1 FMV videos
    if args.fmv:
        if game != 1:
            p.error('--fmv is currently supported for Tomb Raider I only')
        spu_enc = build_spu_encoder(build_dir, args.dry_run)
        video_dir = build_dir / 'video'
        vcmd = [
            sys.executable, COOK_DIR / 'n64_video.py',
            '--source', source, '--spu-encoder', spu_enc, '--out', video_dir,
        ]
        if source == 'pc':
            if info['fmv_dir'] is None:
                p.error('PC FMV/*.RPL directory not found under input')
            vcmd += ['--input', info['fmv_dir']]
        else:
            if cue_path is None:
                p.error('PSX --fmv requires a single-file BIN/CUE via --input or --cue')
            vcmd += ['--cue', cue_path]
        run_cmd(vcmd, dry_run=args.dry_run)
        video_pack = build_dir / f'tr{game}-campaign-video.pack'
        run_cmd([
            sys.executable, COOK_DIR / 'pack_add_video.py',
            '--base', campaign_pack,
            '--video-catalog', video_dir / 'manifest.json',
            '--cache', cache_dir,
            '--out', video_pack,
        ], dry_run=args.dry_run)
        campaign_pack = video_pack

    if not args.dry_run:
        pack_mb = campaign_pack.stat().st_size / (1024 * 1024)
        print(f'  pack ready: {campaign_pack} ({pack_mb:.2f} MiB)', flush=True)

    if args.pack_only:
        if not args.dry_run and out_path != campaign_pack:
            out_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(campaign_pack, out_path)
        print(f'\nDone: {out_path}')
        return

    # 3. Compile & link the N64 ROM
    print('\n[3/3] Building N64 ROM...', flush=True)
    rom_build_dir = build_dir / 'rom'
    make_cmd = [
        'make', '-C', REPO_ROOT,
        f'BUILD_DIR={rom_build_dir}',
        f'TR64_GAME={game}',
        'TR64_PLAY=1',
        'TR64_TEST=0',
        f'TR64_LEVEL_PACK={campaign_pack}',
        f'-j{max(1, args.jobs)}',
        'rom',
    ]
    if args.n64_inst:
        make_cmd.append(f'N64_INST={args.n64_inst.expanduser().resolve()}')
    if args.n64_gccprefix:
        make_cmd.append(f'N64_GCCPREFIX={args.n64_gccprefix.expanduser().resolve()}')
    run_cmd(make_cmd, dry_run=args.dry_run)

    built_rom = rom_build_dir / 'tr64.z64'
    if not args.dry_run:
        out_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(built_rom, out_path)
        rom_mb = out_path.stat().st_size / (1024 * 1024)
        print(f'\nBuilt Tomb Raider {game} N64 ROM: {out_path} ({rom_mb:.2f} MiB)')
        print('Target hardware: stock 4 MiB Nintendo 64 (no Expansion Pak required; 256 Kibit SRAM).')


if __name__ == '__main__':
    main()
