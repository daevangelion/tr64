# SPDX-License-Identifier: MIT
"""Game-free binding metadata for the ordinary TR64_LEVEL_PACK build input."""
import argparse
import hashlib
import json
from pathlib import Path
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--pack',default='')
ap.add_argument('--out',type=Path,required=True)
ns=ap.parse_args()
path=Path(ns.pack).resolve() if ns.pack else None
blob=path.read_bytes() if path else b''
if len(blob)>64*1024*1024-0x101000:raise SystemExit('level pack exceeds cart range')
text='/* SPDX-License-Identifier: MIT */\n/* Generated input binding, no game data. */\n'
text+=f'#define TR64_LEVEL_PACK_PATH {json.dumps(str(path) if path else "")}\n'
text+=f'#define TR64_LEVEL_PACK_BYTES {len(blob)}u\n#define TR64_LEVEL_PACK_OFFSET 0x101000u\n'
text+=f'#define TR64_LEVEL_PACK_SHA256 "{hashlib.sha256(blob).hexdigest() if path else ""}"\n'
ns.out.parent.mkdir(parents=True,exist_ok=True)
if not ns.out.exists() or ns.out.read_text()!=text:ns.out.write_text(text)
