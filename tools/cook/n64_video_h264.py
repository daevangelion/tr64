# SPDX-License-Identifier: MIT
"""Parse a cooked H.264 elementary stream and check what the cart player assumes.

The player (engine/media/film.c over the SDK decoder) is sized and tuned for one
stream shape; a stream outside it fails on the cart late or plays wrong. `check`
reads the SPS, PPS and every slice header of the stream itself (not the
encoder's flags) and raises ValueError naming the first property that breaks:

  320x240, 15 frames a second from the VUI timing, Main profile (no 8x8
  transform), CAVLC, frame pictures only, one reference frame on every SPS,
  weighted prediction at most for P slices, no B slices, one slice per
  picture, deblocking off, BT.709 full range, one SPS and one PPS repeated
  unchanged (x264 repeats them at every keyframe), the LDSZ slice-size SEI
  right after them with room for the SDK's input window (`START_CODE_MARGIN`).

Arena: the decoder's arena use is fixed by the picture size, the reference frame
count and the number of different parameter sets it meets (every different SPS or
PPS stays allocated), and all three are pinned here, so a stream that passes needs
the 1,006,592 B measured on the cart (docs/formats/n64-video.md) of the 1,507,328 B
borrowed arena whatever its length. Equal parameter sets repeated at every
keyframe are freed and their memory reused by the player (engine/media/film_arena.h).
"""
import struct

WIDTH, HEIGHT, FPS = 320, 240, 15
INPUT_WINDOW = 64 * 1024        # the SDK's input buffer (H264_BUF_SIZE)
LD_UUID = b'LIBDRAGON' + bytes(7)
# The SDK decoder refills its window when `left <= max_slice_size` and cuts a NAL
# at the end of the window if the next start code is not wholly inside it. A NAL's
# span here ends before the next NAL's start code; finding that code takes up to 4
# more bytes (zero_byte + 00 00 01), so a window holding only 1 to 3 of them ends
# inside the start code: the extractor (`h264bsdExtractNalUnit`) swallows the zeros
# it saw, the next window begins mid start code and is parsed as one raw NAL, and up
# to 64 KiB of stream is lost. Advertising the largest NAL plus the start code length
# (3 would do, as the refill test is `<=`) makes the refill come first.
START_CODE_MARGIN = 4
# The only NAL types a conforming stream holds: slice, IDR slice, SEI, SPS, PPS.
ALLOWED = (1, 5, 6, 7, 8)


def nals(data):
    """Yield (start, end, type) for every start-code delimited NAL, prefix included.
    The span ends where the next NAL's start code (zero_byte included) begins."""
    starts = []
    i = 0
    while True:
        i = data.find(b'\x00\x00\x01', i)
        if i < 0:
            break
        starts.append(i - 1 if i and data[i - 1] == 0 else i)
        i += 3
    for k, start in enumerate(starts):
        end = starts[k + 1] if k + 1 < len(starts) else len(data)
        prefix = 4 if data[start + 2] == 0 else 3
        yield start, end, data[start + prefix] & 31


def unescaped(data):
    out = bytearray()
    zeros = 0
    for b in data:
        if zeros == 2 and b == 3:
            zeros = 0
            continue
        out.append(b)
        zeros = zeros + 1 if b == 0 else 0
    return bytes(out)


def payload(data, span):
    """RBSP of a NAL (header byte dropped, emulation prevention removed)."""
    start, end, _ = span
    prefix = 4 if data[start + 2] == 0 else 3
    return unescaped(data[start + prefix + 1:end])


class Bits:
    def __init__(self, rbsp):
        self.value = int.from_bytes(rbsp, 'big')
        self.size = len(rbsp) * 8
        self.pos = 0

    def u(self, n):
        if self.pos + n > self.size:
            raise ValueError('H.264 stream: header runs past its NAL')
        shift = self.size - self.pos - n
        self.pos += n
        return (self.value >> shift) & ((1 << n) - 1)

    def ue(self):
        zeros = 0
        while self.u(1) == 0:
            zeros += 1
            if zeros > 32:
                raise ValueError('H.264 stream: malformed Exp-Golomb code')
        return (1 << zeros) - 1 + (self.u(zeros) if zeros else 0)

    def se(self):
        k = self.ue()
        return (k + 1) // 2 if k & 1 else -(k // 2)

    def more_rbsp_data(self):
        """True if anything but the rbsp_stop_one_bit and its alignment remains."""
        last_one = self.size - 1 - ((self.value & -self.value).bit_length() - 1) if self.value else -1
        return self.pos < last_one


def _hrd(b):
    count = b.ue() + 1
    b.u(8)
    for _ in range(count):
        b.ue()
        b.ue()
        b.u(1)
    b.u(20)


def parse_sps(rbsp):
    b = Bits(rbsp)
    sps = {'profile': b.u(8)}
    b.u(8)
    sps['level'] = b.u(8)
    if sps['profile'] not in (66, 77, 88):
        raise ValueError(f"H.264 stream: profile {sps['profile']}: only Baseline/Main/Extended "
                         '(no 8x8 transform) are supported')
    sps['id'] = b.ue()
    sps['log2_max_frame_num'] = b.ue() + 4
    sps['poc_type'] = b.ue()
    if sps['poc_type'] == 0:
        sps['log2_max_poc_lsb'] = b.ue() + 4
    elif sps['poc_type'] != 2:
        raise ValueError('H.264 stream: unsupported picture order count type')
    sps['num_ref_frames'] = b.ue()
    b.u(1)
    sps['width'] = (b.ue() + 1) * 16
    map_units = b.ue() + 1
    sps['frame_mbs_only'] = b.u(1)
    sps['height'] = map_units * 16 * (1 if sps['frame_mbs_only'] else 2)
    if not sps['frame_mbs_only']:
        b.u(1)
    b.u(1)
    sps['cropping'] = b.u(1)
    if sps['cropping']:
        for _ in range(4):
            b.ue()
    sps['full_range'] = None
    sps['matrix'] = 2
    sps['fps'] = None
    if b.u(1):  # vui_parameters_present_flag
        if b.u(1):
            if b.u(8) == 255:
                b.u(32)
        if b.u(1):
            b.u(1)
        if b.u(1):
            b.u(3)
            sps['full_range'] = b.u(1)
            if b.u(1):
                b.u(16)
                sps['matrix'] = b.u(8)
        if b.u(1):
            b.ue()
            b.ue()
        if b.u(1):
            units, scale = b.u(32), b.u(32)
            b.u(1)
            sps['fps'] = scale / (2 * units) if units else None
        nal_hrd = b.u(1)
        if nal_hrd:
            _hrd(b)
        vcl_hrd = b.u(1)
        if vcl_hrd:
            _hrd(b)
        if nal_hrd or vcl_hrd:
            b.u(1)
        b.u(1)
        if b.u(1):
            b.u(1)
            for _ in range(5):
                b.ue()
    return sps


def parse_pps(rbsp):
    b = Bits(rbsp)
    pps = {'id': b.ue(), 'sps_id': b.ue(), 'cabac': b.u(1), 'bottom_poc': b.u(1),
           'slice_groups': b.ue() + 1}
    if pps['slice_groups'] != 1:
        raise ValueError('H.264 stream: slice groups (FMO) are unsupported')
    b.ue()
    b.ue()
    pps['weighted_pred'] = b.u(1)
    pps['weighted_bipred'] = b.u(2)
    b.se()
    b.se()
    b.se()
    pps['deblocking_control'] = b.u(1)
    b.u(1)
    pps['redundant_pic_cnt'] = b.u(1)
    pps['transform_8x8'] = 1 if b.more_rbsp_data() else 0
    return pps


def parse_slice(rbsp, nal_type, ref_idc, sps, pps):
    """First fields of a slice header, through disable_deblocking_filter_idc."""
    b = Bits(rbsp)
    h = {'first_mb': b.ue(), 'type': b.ue() % 5, 'pps_id': b.ue()}
    b.u(sps['log2_max_frame_num'])
    if nal_type == 5:
        b.ue()
    if sps['poc_type'] == 0:
        b.u(sps['log2_max_poc_lsb'])
        if pps['bottom_poc']:
            b.se()
    if pps['redundant_pic_cnt']:
        b.ue()
    if h['type'] in (1, 3, 4):
        return h  # B, SP, SI: rejected by the caller before anything more is needed
    active = 1
    if h['type'] == 0:
        if b.u(1):
            active = b.ue() + 1
        if b.u(1):  # ref_pic_list_modification_flag_l0
            while b.ue() != 3:
                b.ue()
        if pps['weighted_pred']:
            b.ue()
            b.ue()
            for _ in range(active):
                if b.u(1):
                    b.se()
                    b.se()
                if b.u(1):
                    for _ in range(4):
                        b.se()
    if ref_idc:
        if nal_type == 5:
            b.u(2)
        elif b.u(1):
            while True:
                op = b.ue()
                if op == 0:
                    break
                for _ in range({1: 1, 2: 1, 3: 2, 4: 1, 5: 0, 6: 1}.get(op, 0)):
                    b.ue()
    b.se()  # slice_qp_delta
    h['deblocking'] = b.ue() if pps['deblocking_control'] else 0
    return h


def check(data):
    """Raise ValueError naming the first broken property; else return a summary:
    {'pictures', 'keyframes', 'max_nal', 'sps', 'pps', 'max_slice'} where max_slice
    is the LDSZ value the stream advertises."""
    spans = list(nals(data))
    if len(spans) < 4 or spans[0][0] != 0 or spans[0][2] != 7 or spans[1][2] != 8:
        raise ValueError('H.264 stream: SPS/PPS must open it')
    if spans[2][2] != 6:
        raise ValueError('H.264 stream: the LDSZ SEI must follow the SPS and PPS')
    bad = sorted({t for _, _, t in spans} - set(ALLOWED))
    if bad:
        raise ValueError(f'H.264 stream: NAL type {bad[0]} is unsupported (data partitioning, '
                         'filler, access unit delimiters, extensions)')
    sps_all = {payload(data, s) for s in spans if s[2] == 7}
    pps_all = {payload(data, s) for s in spans if s[2] == 8}
    if len(sps_all) != 1 or len(pps_all) != 1:
        raise ValueError(f'H.264 stream: {len(sps_all)} different SPS and {len(pps_all)} different PPS; '
                         'exactly one of each, repeated unchanged, is supported (the decoder keeps '
                         'every different set it meets in the arena)')
    sps = parse_sps(next(iter(sps_all)))
    pps = parse_pps(next(iter(pps_all)))
    if sps['id'] or pps['id'] or pps['sps_id']:
        raise ValueError('H.264 stream: SPS and PPS id must be 0')
    if sps['width'] != WIDTH or sps['height'] != HEIGHT:
        raise ValueError(f"H.264 stream: {sps['width']}x{sps['height']}, expected {WIDTH}x{HEIGHT}")
    if sps['cropping'] or not sps['frame_mbs_only']:
        raise ValueError('H.264 stream: cropping and interlaced coding are unsupported')
    if sps['num_ref_frames'] != 1:
        raise ValueError(f"H.264 stream: num_ref_frames {sps['num_ref_frames']}; the decoder's picture "
                         'buffer is sized for one reference frame (8 overflowed the arena)')
    if sps['fps'] != FPS:
        raise ValueError(f"H.264 stream: frame rate {sps['fps']} from the VUI timing, expected {FPS}")
    if sps['full_range'] != 1 or sps['matrix'] != 1:
        raise ValueError('H.264 stream: expected BT.709 full range in the VUI '
                         f"(matrix {sps['matrix']}, full range {sps['full_range']})")
    if pps['cabac']:
        raise ValueError('H.264 stream: CABAC; only CAVLC is supported (the SDK decoder asserts)')
    if pps['transform_8x8']:
        raise ValueError('H.264 stream: 8x8 transform is unsupported')
    if pps['weighted_bipred']:
        raise ValueError('H.264 stream: bi-predictive weighted prediction (B slices) is unsupported')
    if not pps['deblocking_control']:
        raise ValueError('H.264 stream: deblocking is on (no deblocking filter control in the PPS)')
    pictures = keyframes = 0
    seen_pictures = []
    for span in spans:
        _, _, kind = span
        if kind not in (1, 5):
            continue
        ref_idc = (data[span[0] + (4 if data[span[0] + 2] == 0 else 3)] >> 5) & 3
        h = parse_slice(payload(data, span), kind, ref_idc, sps, pps)
        if h['type'] in (1, 3, 4):
            raise ValueError(f"H.264 stream: picture {pictures} has a "
                             f"{'B' if h['type'] == 1 else 'SP/SI'} slice")
        if h['first_mb']:
            raise ValueError(f'H.264 stream: picture {pictures} has more than one slice '
                             f"(slice starts at macroblock {h['first_mb']})")
        if h['pps_id']:
            raise ValueError(f'H.264 stream: picture {pictures} uses PPS {h["pps_id"]}')
        if h['deblocking'] != 1:
            raise ValueError(f'H.264 stream: picture {pictures} has the deblocking filter on')
        if kind == 5 and h['type'] != 2:
            raise ValueError(f'H.264 stream: IDR picture {pictures} is not intra')
        keyframes += kind == 5
        pictures += 1
        seen_pictures.append(kind)
    if not pictures or seen_pictures[0] != 5:
        raise ValueError('H.264 stream: must start with an IDR picture')
    sei = [s for s in spans if s[2] == 6]
    if len(sei) != 1:
        raise ValueError('H.264 stream: exactly one LDSZ SEI required')
    body = payload(data, sei[0])
    if len(body) < 27 or body[:2] != bytes([5, 25]) or body[2:18] != LD_UUID \
            or body[18:22] != b'LDSZ' or body[22] != 1:
        raise ValueError('H.264 stream: malformed LDSZ SEI')
    limit = struct.unpack('>I', body[23:27])[0]
    biggest = max(e - s for s, e, _ in spans)
    if limit < biggest + START_CODE_MARGIN:
        raise ValueError(f'H.264 stream: LDSZ advertises {limit} bytes; the largest NAL is {biggest} and '
                         f'the SDK window needs {START_CODE_MARGIN} more for the next start code')
    if limit > INPUT_WINDOW:
        raise ValueError(f'H.264 stream: LDSZ {limit} exceeds the {INPUT_WINDOW} byte input window')
    return {'pictures': pictures, 'keyframes': keyframes, 'max_nal': biggest, 'sps': sps, 'pps': pps,
            'max_slice': limit}
