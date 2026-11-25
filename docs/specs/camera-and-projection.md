# M1 posed-camera, lighting and comparison contract

OpenLara citations are relative to `src/` at `2333adef30ae2a5f3b32c5355e6e343d6aa0531a`.
**M1 choice** means an explicit reference/engine requirement, not a claim about
a retail executable. None was inspected. Main's review decisions D-a through
D-d (2026-11-17) select the brightness, water tint and two-step gate below.
Permitted sources: [PROVENANCE](../PROVENANCE.md), section1.

## Coordinates, posed camera and projection

- X/Z are horizontal, Y increases downward. World coordinates are original TR
  units: sectors1024, height steps256. IR room X/Z are local, Y is already
  world Y; add only `room.info.x,z`. [IR `../formats/ir.md:13-22`;
  fixed `draw.h:902-907`, `fmt/phd.h:346-385`]
- Fixed rotations have14 fractional bits; one revolution is65536 angle units.
  Zero yaw looks +Z, positive yaw turns toward +X. [fixed `common.h:32,552-562`;
  `common.cpp:1430-1455`; `camera.h:110-120`]
- **M1 choice:** JSON `camera` contains numeric `room`, `position:[x,y,z]`,
  `target:[x,y,z]`, optional `flipped:0|1`. Forward=normalize(target-position),
  right=normalize(cross([0,1,0],forward)), down=cross(forward,right). A vertical
  or zero look vector is invalid. Pixel centers are(x+.5,y+.5). [continuous
  interpretation of fixed `common.cpp:1430-1455`]
- Follow MODEHW/MODE13: square pixels, focal256 on both axes, center(160,120),
  viewport[0,320)x[0,240), aspect4:3; project x=160+256X/Z, y=120+256Y/Z.
  Continuous horizontal FOV=64.0108°, vertical=50.2297°. The reciprocal table
  macro is the integer approximation, not a different FOV. [fixed
  `common.h:2345-2355`; architecture§3; `test/oracle/host.patch` MODEHW]
- Near64 is geometrically clipped. Far10240 is a rejection/cue boundary:
  reject a triangle only when ALL vertices exceed it; otherwise draw it whole.
  **Main's tiny3d renderer-contract fact:** there is no geometric far clipping.
  The fixed behaviour oracle uses the same distance
  for M1. The earlier N64 hardware port used16384; that is recorded as a fact,
  not adopted as a design. Revisit draw distance with measurements in M6.
  [fixed `common.h:510-519,262-267`; **M1 choice** Z-buffer per architectureD3]
- GL1 uses `gluPerspective(60,aspect,.1,16384)`, whereas its portal focal also
  multiplies by aspect and disagrees with its draw projection. We do not mix
  that projection with MODEHW. [fixed `render/gl1.cpp:97-107`;
  `common.h:2337-2355`]
- Split quads(0,1,2),(0,2,3); obtain UV with `ir.face_uv`, never pair raw source
  UV slots with decoded vertices. Clockwise in the Y-down image is front;
  face bit15 disables culling. [IR `ir.md:76-86`; fixed
  `render/gl1.cpp:69-75`; GBA `render.iwram.cpp:159-162`]
- **M1 choice:** perspective-correct UV interpolation and screen-affine vertex
  gain/tint/fog interpolation (RDP shade attributes), point sample
  nearest integer texel (`floor(uv+.5)`), source-rectangle clamp, Z-buffer
  nearest wins. Colour-key writes neither RGB nor depth. PC index0 and PSX
  CLUT word0000 are key; PSX8000 is opaque black here, no semitransparent
  blending. [fixed GL1 nearest/alpha/depth: `render/gl1.cpp:69-81,123-136`;
  [PSX-SPX texture colour format](https://psx-spx.consoledev.net/graphicsprocessingunitgpu/#gpu-video-memory-vram)]

## Common scalar brightness (engine requirement, Main D11)

Both sources map into13-bit darkness d (low=bright): PC d=`pc_raw`, PSX
d=`8191-(psx_raw<<5)`. The gated reference uses **one model for both**:
vertex gain=clamp((8191-d)/4096,0,1.5). PSX value128 is neutral; PC d4096 is
neutral to the original1/4096 encoding quantum. Support gain above1.0 rather
than clamping highlights to unlit texel brightness. [Main ruling;
IR `ir.md:69-75`; [PSX-SPX GPU Modulation](https://psx-spx.consoledev.net/graphicsprocessingunitgpu/#modulation-also-known-as-texture-blending)]

PSX-SPX explicitly gives `texel*vertexColour/128`, says0x808080 is neutral,
and values above128 brighten. PC's measured light map also demonstrates
neutral row16 and brighter rows0-15, but it is not evaluated per pixel by the
N64 engine. The scalar choice follows the hardware-compatible PSX model,
not a claim that PC's nonlinear palette ramps are linear.

**Main D11 clamp/fog order:** vertex f=clamp((Z-8192)/2048,0,1), interpolate
gain, tint and f across the triangle as shade attributes, then
`colour = clamp(texel * gain * tint, 0,255) * (1-f)`.
Clamp the lit colour **before** the cue to black. Fog is per vertex for both
rooms and static meshes; it is not recalculated from interpolated pixel Z.
At vertex Z10240, f=1 even for brightest input. [Main's RDP-fog decision]

Main's renderer-contract investigation caps gain at1.5: the selected
two-cycle combiner's9-bit clamp maps codes384-511 to zero, so a larger
full-scale multiplication can wrap to black. Cook shade=min(gain,1.5)/1.5
into8-bit channels, and use a1.5x combiner factor. This hardware constraint
is deliberately stricter than PSX's almost2x modulation capability.

There is no triangle far-plane split. Fog f remains clamped per original
vertex and screen-affine across crossing triangles, so its interpolated
picture beyond far is whatever that interpolation produces. Hardware Z
saturates, but reference/host maps retain true camera Z. Don't-care is decided
ONLY by covered REFERENCE pixels with Z>10240, dilated by one pixel; uncovered
infinity is not a far pixel. Candidate Z>10240 where the reference is covered
and in range is an error on coverage, room and texel agreement, not a way to
mask a bad picture. All image/map comparisons use the reference-only mask.
The NPZ additionally requires `visible_rooms`, a1D integer vector of logical
room indices, so wholly far rooms incorrectly reached by traversal can still
be rejected even though their pixels would be don't-care. A wholly far room
must not enter the set, and a visible room must not be omitted.

The fixed software code instead adds2*max(0,Z-8192) to darkness and clamps8191;
static fog uses the instance origin. An initial d0 remains gain~1 at far under
that additive rule, whereas D11's black cue fades its lit result. Keep this
difference explicit. [fixed `common.h:515-519`; `draw.h:130-140`;
GBA `render.iwram.cpp:198-205,271-276`]

PC software behaviour is retained as a **second, informational output**:
`render(...,lighting_model='pc_lut')` / `--lighting-model pc_lut` uses
row=clamp(floor(d/256),0,31), RGB=`palette[light_map[row,source_index]]*4`,
with the fixed additive cue. Palette channels are six-bit scaled by4, not
stretched by255/63. [IR `ir.md:49-53`; fixed `fmt/phd.h:1191-1201`;
GBA `render.iwram.cpp:229,295,305`, `rasterizer.h:266,494-504`]
The calibrator writes `pc-lut/*.png` plus per-pose normalized RGB MAE/RMSE and
changed-pixel counts against the scalar picture. This is information for Main
and the user, **never a gate or a source of thresholds**.

PSX page indices are low nibble first, descriptors select their own CLUT,
and BGR555 channels expand by bit replication. [IR `ir.md:49-53,134-148`]

Equivalence residual=`pc_raw-(8191-(psx_raw<<5))`, not exact equality.
LEVEL1 has8,349 coordinate-matched room vertices, LEVEL6 has11,504; residuals
span[-31,0] in both. Keep originals. Full histograms are in
`m1-ref/calibration.json.gz`, matching M0's per-level lighting evidence.
[M0 `cook-m0/equivalence.md`; IR `ir.md:69-75`]

- **M1 choice:** keep per-vertex mesh darkness; add instance darkness-4096
  (normals-only meshes use4096 baseline). This is richer than the fixed game's
  single mesh intensity: the PHD reader sets4096; the GBA packer averages mesh
  vertex values, or uses0 with normals. This reference is not claiming that
  either averaging rule equals per-vertex lighting. [fixed
  `fmt/phd.h:655`; GBA packer `out_GBA.h:656-665`; `draw.h:130-140`]
- Flat PC material uses the low8-bit palette index and the same scalar gain.
  Flat PSX material names one of the first256 texture descriptors: take its
  first texel from its own page/CLUT, then the same brightness gain as a
  material colour (**M1 choice**). [permitted `format.h:6521-6541`]
- **Main D-b water choice:** water-flag bit0 tints RGB by(0.5,0.8,0.8), following
  the fixed 3DO packer's concrete underwater PLUT transform. Apply this to
  source-shaded material RGB before final RGB8 clamp; exclude animated
  caustics. Game draw requests palette1 for a water room; GL1's setter is
  empty and GBA's is TODO, so neither implements a tint to follow. The 3DO
  packer does write a second palette bank. [fixed `common.h:939`,
  `draw.h:909`, `render/gl1.cpp:151-154`; GBA `render.iwram.cpp:106-109,264-265`;
  3DO packer `out_3DO.h:1667-1688`]
- Draw only static definitions with flags&2. Use the definition's mesh-pointer
  index, including aliases; quantize instance angle to a quarter turn as the
  fixed PHD reader does, then rotate Y and translate world XYZ. [fixed
  `common.h:399`; `draw.h:925-975`; `fmt/phd.h:561`; IR `ir.md:98,166-167`]

### Measured PC lightmap response (both level inputs)

Mean of per-entry output/input luminance ratios over nonblack palette entries,
Y=.2126R+.7152G+.0722B; these are **measurements, not an approximate replacement
for the LUT**. Row r covers darkness[256r,256r+255]. Equal-colour identity
entries in row16: LEVEL1=256 (239 equal *indices*), LEVEL6=256 (246 equal indices).
Saturation is palette-ramp dependent, not one darkness cutoff: row0 already
has119/256 entries with at least one RGB channel252 in both levels. Counts
per row and both mean-ratio/summed-luminance-ratio definitions are recorded by
`lighting_evidence` in the calibration manifest. [read-only PC IR tables]

Independent review measured scalar gain against the PC LUT by darkness-row
centres: rows0-15 agree within0.4-2.2%; rows17-30 are4-15% darker under the
linear model (e.g. row23 gain0.531 versus LUT0.624 LEVEL1 /0.596 LEVEL6).
Palette-entry mean RGB error is3.5-8.0 levels. These reviewer numbers compare
the original UNCAPPED linear model to the LUT; the later1.5 hardware cap adds
another known highlight difference. They are informational, not a reason to
substitute the LUT in the gate; final per-pose differences are remeasured.

| Row | LEVEL1 mean ratio | LEVEL6 mean ratio |
|---|---:|---:|
| 0 | 1.719181 | 1.717531 |
| 1 | 1.677608 | 1.675939 |
| 2 | 1.650220 | 1.643322 |
| 3 | 1.614051 | 1.611558 |
| 4 | 1.574643 | 1.577899 |
| 5 | 1.531546 | 1.534441 |
| 6 | 1.492946 | 1.491905 |
| 7 | 1.450765 | 1.456988 |
| 8 | 1.406841 | 1.412900 |
| 9 | 1.359110 | 1.371207 |
| 10 | 1.319788 | 1.327182 |
| 11 | 1.261687 | 1.277626 |
| 12 | 1.218703 | 1.223068 |
| 13 | 1.152677 | 1.162586 |
| 14 | 1.100196 | 1.103865 |
| 15 | 1.031757 | 1.037577 |
| 16 | 1.000000 | 1.000000 |
| 17 | 0.965141 | 0.958694 |
| 18 | 0.892942 | 0.889160 |
| 19 | 0.826515 | 0.820105 |
| 20 | 0.771735 | 0.761391 |
| 21 | 0.745115 | 0.722021 |
| 22 | 0.680631 | 0.656427 |
| 23 | 0.623620 | 0.595813 |
| 24 | 0.544075 | 0.516805 |
| 25 | 0.444860 | 0.439323 |
| 26 | 0.389173 | 0.379583 |
| 27 | 0.327492 | 0.315346 |
| 28 | 0.263480 | 0.251898 |
| 29 | 0.200325 | 0.178015 |
| 30 | 0.141053 | 0.110584 |
| 31 | 0.055039 | 0.037575 |

## Portal visibility and alternate rooms

Start the camera room with the full viewport, all others empty. For each
portal add room X/Z; reject dot(normal,corner0-camera)>=0. Transform corners;
reject allZ<=0 or allZ>far. Project positive-Z vertices (clamp Z to far for
bounds). If an edge crosses Z=0, expand the corresponding X/Y bound to the
viewport edge according to both endpoint signs (both edges if signs differ).
Intersect with the parent clip; reject empty; union into the destination clip
and traverse. [fixed `room.h:374-511`]

The original DFS appends descendants before parents and camera last: not a
nearest-room sort. **M1 choice:** work queue to rectangle fixed point;
reprocess when any clip grows, no recursive cyclic revisit and no list cap.
No N64-specific2-pixel inflation. Draw reachable geometry clipped to its union
rectangle, with Z-buffer final occlusion. [fixed `room.h:486-560`;
`draw.h:992-1015`; architectureD3]

At flip1 build reciprocal mates from `alternate_room`, swap each pair only
once, and select the mate's data at the unchanged logical room index. At
flip0 use original data. This changes geometry/portals/flags, not source room
identity. [fixed `object.h:1979-2011`, `world_state.h:276-309`]

## Two-step gate and auxiliary-map ABI (Main D-c)

1. **Reference vs HOST engine:** all five auxiliary arrays below are mandatory.
   Missing/malformed maps are an error, never a reduced-metric pass. The cook's
   manifest must map native material descriptors back to the source texture
   index for each face; native deduplication must not change this domain.
2. **HOST engine vs CART:** RGB picture only. Same engine/native content and
   native geometry setup. Host must model the RDP's point-sampling convention,
   not freely choose a half-pixel projection shift or bilinear filtering.
   Cart gating is unavailable until measured host-engine/cart picture pairs
   establish its tolerance; synthetic colour controls are diagnostic only.
   No RDP pixel parity or provisional cart acceptance limit is claimed.

Files share a stem: RGB PNG320x240, compressed numpy NPZ containing ordinary
numeric arrays (load with `allow_pickle=False`), JSON metadata/counts. Row0 is
image top. No normalized-device depth or cooked room/material IDs:

| NPZ name | Shape/type | Domain/units |
|---|---|---|
| `coverage` |240x320 boolean/uint8|1 iff a non-key depth-winning fragment wrote |
| `room` |240x320 signed integer|logical/source room index; -1 uncovered, including flip1 |
| `texture` |240x320 signed integer|source object-texture index for that face; -2 flat; -1 uncovered |
| `depth` |240x320 float32/64|TRUE camera Z in world units, including >far; +infinity uncovered; no hardware saturation |
| `texel` |240x320x2 float32/64|continuous perspective-correct U,V in SOURCE PAGE texel coordinates; (0,0) flat, NaN uncovered |

Integer source-page UV coordinates are **texel centres**; `floor(uv+.5)` selects
the nearest center. `texel` is before sampling, not a rounded palette index.
For split/repacked native rectangles, the cook manifest must provide the
descriptor's source object-texture index AND source page rectangle origin;
the engine converts local native UV back to source-page UV. Internal
cross-edition controls subtract each face's source origin because atlas
placement differs, but the emitted engine map is always SOURCE PAGE space.
Animated textures MUST remain frozen at their stored descriptor in M1 scenes.
Candidate maps must come from the engine's actual
draw path. JSON names source SHA256, posed state, visible room list, submitted
visible-definition static count, covered pixel count and faces that wrote at
least one non-key depth-winning fragment (not triangle or final-face counts).

### Metrics and calibration policy

**PROVISIONAL until engine pictures exist.** Existing calibration-file limits
are retained, not claimed as a finished acceptance gate. Open items are the
independent activity/minimum-coverage rule, shade-floor versus shade-round
combined precision bounds, and five precision-limited overbright poses:
PC LEVEL1 hall, PC LEVEL6 worst-set, PSX LEVEL1 hall, PSX LEVEL6 sightline,
PSX LEVEL6 worst-set. Their measured errors and bounds remain in the external
report. Main explicitly deferred further calibration work in favour of the
verified reference renderer, pictures and scenes.

`m1_compare.py --stage host|cart --case source/scene` selects a per-pose bound.
The CLI verifies the reference RGB hash for the host stage; it does not claim
to authenticate candidate provenance. Calibration never consumes an engine
picture and **no negative participates in selecting a limit**.

Host positives per pose/source: other edition, bilinear RGB+alpha, half-pixel
center, whole/quarter-pixel vertex snapping, 8-bit gain/1.5 shade floor and
round followed by1.5x combiner gain, actual cooked CI4 textures independently
decoded by `n64_tex_view.Section` for BOTH sources, and undithered RGB555
framebuffer quantisation. Existing content-keyed native sections are reused;
their source/section hashes are checked and recorded. PSX native CI4 is
lossless, PC is the cook's actual quantisation, not an invented substitute.
SGI's [RDP Command Summary](https://ultra64.ca/files/documentation/silicon-graphics/SGI_RDP_Command_Summary.pdf),
pp13-16 describes quarter-pixel Y/subpixel edge setup and affine RGBA shade;
8-bit shade gain/1.5 plus1.5x combiner is the M1 backend representation (Main's
hardware contract), with floor/nearest rounding tested. The combined positive
applies quarter-pixel snapping,8-bit shade, actual cooked CI4 and RGB555 at
once: limits must accommodate the real joint pipeline, not just its separate
factors. See also
the [Nintendo colour-combiner tutorial](https://ultra64.ca/files/documentation/online-manuals/man-v5-1/tutorial/graphics/4/4_5.htm).
Cross-source controls canonicalize the two atlas origins and omit only
source-domain texture-ID equality; the host-engine CLI has no such exception.

Coverage and silhouette use area error and symmetric one-pixel-tolerant mask
agreement. Room/texture labels are symmetrically matched within one pixel.
Texel error finds the closest matching room/material UV in a3x3 pixel window,
normalized by local UV slope (at least1texel), capped at16; depth compares at
that correspondence. Colour/chroma compare RGB means grouped by material and
nearest source texel, rather than spatial8x8 block averages; cross-source colour
uses2x2 RGB blocks because palettes/material tables differ. Global mean
luminance difference separately measures overall brightness without texel-bin
sampling bias. `shading_loss` measures loss of illumination variation among
repeated copies of the same material texel: within-group luminance variance
(groups of at least4 pixels), count-weighted RMS divided by group mean,
then max(0,log((reference+.001)/(candidate+.001))). Material colour cancels;
neutral lighting cannot reproduce the source's spatial shading. This is
one-sided because bilinear texture mixing can add variance, not remove the
underlying illumination. The .001 floor is0.1-percent relative luminance.
Detail compares
luminance-Laplacian energy on interior coverage; SSIM8x8 is also reported as a
structure diagnostic. Far error compares the visible-room sets, not saturated
or don't-care depth pixels. These are
independent signals, not a single loose screenshot score.

Limits are max(positive maximum*1.05, declared precision floor), frozen before
negatives. Geometry label/coverage/far floors are one full-image pixel
(1/76800); mean brightness/colour/chroma and depth-band brightness floors are
one RGB8 level(1/255). UV error floor1e-4, relative-depth1e-5, logarithmic
shading/detail floors.01 prevent a zero-observation bound from becoming exact
equality. `distance_brightness` compares mean luminance in1024-unit camera-Z
bands (at least16 pixels), detecting missing fog without dilution by near
surfaces. Every negative with at least one changed RGB8 pixel must fail for
that pose. Map-only changes are additionally
required to fail (covered depth changes>1e-5 or texel changes>1e-6). Inert cases
are labelled, never claimed as a passed rejection. If the intended metric is
inert, the report names every other metric that rejects it. Per-case errors,
thresholds and signed margins are in `calibration.json.gz`/`calibration.md.gz`.

Negatives run the real renderer with rotated quad corners, largest and
smallest occupied non-camera rooms omitted (never camera fallback),
neutralized lighting, disabled fog,5-percent wrong FOV, shuffled texture
descriptors, far disabled and far halved.
A gain-clamped-at1.0 render is an overbright-path negative. If its difference
is below the combined native precision floor, report the exact pose and
metric margins as a detection limitation; never loosen a bound to hide it.

Known detection floor: the earlier static pose's missing fog touched about
110 pixels with a depth-band mean difference about1.9RGB levels, below the
cooked-CI4 floor. The final static pose exposes substantial far geometry
instead; that tiny-patch effect is not claimed to be distinguishable.
Further picture negatives keep correct maps: greyscale, twice-bright, Gaussian
blur radius3, spatial8x8 averaging, flat mean colour. Thus correct geometry
maps cannot make a wrong picture pass unnoticed.

Cart metrics report normalized mean absolute RGB error and changed-pixel
fraction, with **no synthetic acceptance thresholds**. `--stage cart` refuses
unless calibration contains `cart_calibration.kind=\"measured_host_cart\"`,
a nonempty `pairs` evidence ledger and per-case measured cart limits.
Once the engine draws, Main records same-content host/cart images, executable
and source hashes, scene state, ares/hardware configuration and screenshot
conversion. Calibrate from those real pairs: 5-bit floor/round/dither controls
must be accepted and point-versus-bilinear must be rejected where observable.
Only then publish the measured file consumed by the cart gate. Current
synthetic dither/5-bit variants remain labelled diagnostics, never passes.

## Scenes, source differences and evidence ceiling

There are six baseline poses per level, plus a supplemental LEVEL6 room25
flip1 pose that draws data selected from alternate76/77. LEVEL1 has no water
rooms; LEVEL6 room0 provides water coverage. Start-room views are moved toward
the exit so a non-camera room is actually occupied on screen. Near poses
show a portal jamb crossing the64-unit near plane (minimum written Z about
64.57 LEVEL1 /64.00 LEVEL6). Long sightlines use LEVEL1 room17 and LEVEL6 room45,
portal chains5 and4, visible sets6 and11. LEVEL6 worst-room44 and worst-set
seed9 remain covered. [read-only IR plus generated pose/count/depth evidence]

LEVEL1 static definitions contain no flat face. LEVEL6 static19 contains one
flat base quad at local Y=+1, placed in rooms3,11,13 exactly one world unit
below their floor planes. It is occluded from valid above-floor cameras;
probes record zero flat pixels. **Main decision:** no planted below-floor pose;
retain analytic flat-material tests and add a real entity flat-face scene in
M2. [source mesh/instance/sector inspection; Main's review decision]

PC/PSX geometry, corner mapping and most room data agree for these levels.
M0 records room-flag0x100 and animation-table differences, not room geometry
edits; those are not silently normalized. Visible colour differences are
expected from the PC palette versus PSX16-colour CLUTs under the same scalar
gain, plus the measured sub32-unit lighting residuals and
occasional colour-key edge differences. The report retains per-pose colour,
chroma, coverage, room, texel and depth differences rather than attributing
all differing pixels to an unspecified edition difference.

**No pixel-level anchor to OpenLara's own renderer is established.** Fixed GL
context/projection setup is WGL-only (`fixed/render/gl1.cpp:5-6,10-109`);
`platform/win_fixed` is a Visual Studio project. The Linux recipe
`platform/nix/build.sh:2` builds the other/non-fixed `game.h` renderer. A fixed
Linux context plus posed export is not a quick unmodified Xvfb/Mesa build.
An existing unkeyed `bin/OpenLara` was not used. Coordinate transforms,
projection/planes, winding, portal/alternate selection, static placement and
fixed additive fog rest on cited source reading and analytic tests; scalar
brightness and post-clamp vertex fog are Main D11 choices grounded in the
hardware model, and water follows the explicit3DO-based choice. The PC-LUT
secondary pictures preserve its different response. No original-renderer
pixel parity is claimed.

No moving items, dynamic lights, animated texture time, sprites, caustics or
camera collision are implemented by this posed-reference slice.

## Commands (from repository root)

Prefix with `rtk` on this host. Python3, numpy and Pillow are reused; no install:

```
env PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=tools/cook:test/ref:test/run python3 -m unittest discover -s test/ref -v
env PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=test/run python3 -m unittest discover -s test/run -p test_m1_compare.py -v
env PYTHONDONTWRITEBYTECODE=1 python3 test/ref/mutations.py --scratch $TR64_WORK/tmp/RefRender/mutations
env PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=tools/cook:test/ref:test/run python3 test/ref/calibrate.py --data $TR64_DATA --output $TR64_WORK/evidence/m1-ref
python3 test/run/m1_compare.py reference.png host.png --stage host --case pc/m1-level1-start --reference-maps reference.npz --candidate-maps host.npz --thresholds $TR64_WORK/evidence/m1-ref/calibration.json.gz
python3 test/run/m1_compare.py host.png cart.png --stage cart --case pc/m1-level1-start --thresholds $TR64_WORK/evidence/m1-ref/calibration.json.gz
make provcheck
```

All derived pictures, maps and reports stay outside the repository. Manifest
records source, scene and tool hashes/runtime versions. Contact sheet is
`m1-ref/contact.png`; individual references are `m1-ref/{pc,psx}/*.png`.

`mutations.py` generates isolated AST mutants and requires a named analytic
assertion failure for each: fog removal, doubled PC darkness, inverted PSX
intensity, static rotation sign, missing near-portal expansion, missing parent
intersection, omitted static fog, omitted all-far rejection, and missing clip-growth
repropagation. Import/setup errors do not count as successful rejection.

### Picture inspection

Inspected the final two-column contact sheet and full-size PC LEVEL1 start,
PSX LEVEL6 near, and PC LEVEL6 flipped references. LEVEL1 now shows bright
white snow/footprints rather than half-bright grey; bridge statics and vine
textures are coherently oriented. The near-water view shows the turquoise
source tiles and the clipped close jamb; the flipped pose shows the selected
upper stone chamber. No inside-out faces, mirrored textures, missing placed
statics or unexplained enclosed holes were seen. Near-plane cuts and distant
rejected/fogged geometry follow the declared rules, not a filled-pixel fit.
