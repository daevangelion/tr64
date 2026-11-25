#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The cook's stream checks (n64_video_h264.check via n64_video.validate) and the
slice-size margin, with real libx264 streams made by the cook's own flags.

PYTHONPATH=tools/cook python3 test/media/test_video_cook.py
Needs ffmpeg with libx264 (skipped without).
"""
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
import n64_video as cook
import n64_video_h264 as h264

FFMPEG = shutil.which('ffmpeg')
START = b'\x00\x00\x00\x01'


def encode(root, source, *, scale='320:240', fps=15, params=None, refs=1, bframes=0, extra=()):
    """Same command as cook.encode_video with one property changed."""
    out = root / 'out.h264'
    p = dict(item.split('=') for item in cook.X264.split(':'))
    p.update(params or {})
    vf = (f'scale={scale}:flags=lanczos:out_color_matrix=bt709:out_range=pc,'
          f'fps={fps}:eof_action=pass,format=yuv420p')
    cmd = ['ffmpeg', '-v', 'error', '-y', '-i', str(source), '-an', '-vf', vf,
           '-c:v', 'libx264', '-profile:v', 'main', '-pix_fmt', 'yuv420p',
           '-colorspace', 'bt709', '-color_range', 'pc', '-color_primaries', 'bt709',
           '-color_trc', 'bt709', '-crf', '32', '-maxrate', '480k', '-bufsize', '240k',
           '-bf', str(bframes), '-refs', str(refs), '-preset', 'veryfast', '-g', '10',
           '-x264-params', ':'.join(f'{k}={v}' for k, v in p.items()), *extra, '-f', 'h264', str(out)]
    subprocess.run(cmd, check=True)
    return out.read_bytes()


def model_window(stream, advertised):
    """The SDK's input window over `stream` (h264.c decode_next_slice with
    h264bsdExtractNalUnit, read from the source): returns the byte offsets at which
    the decoder starts a NAL and whether any is not a true NAL start."""
    true_starts = {s for s, _, _ in h264.nals(stream)}
    pos = idx = buf_len = 0
    buf = b''
    starts = []
    while True:
        left = buf_len - idx
        if left <= advertised:
            fresh = stream[pos:pos + h264.INPUT_WINDOW - left]
            buf = buf[idx:buf_len] + fresh
            base = pos - left
            pos += len(fresh)
            buf_len, idx = left + len(fresh), 0
            if buf_len == 0:
                return starts, True
        offset = base + idx
        starts.append(offset)
        if offset not in true_starts:
            return starts, False
        view = buf[idx:buf_len]
        n = len(view)
        # h264bsdExtractNalUnit, byte stream branch
        zero = count = 2
        p = 2
        no_start = False
        while True:
            byte = view[p]
            p += 1
            count += 1
            if count == n:
                no_start = True
                break
            if not byte:
                zero += 1
            elif byte == 1 and zero >= 2:
                break
            else:
                zero = 0
        if no_start:
            idx += n
            continue
        init = count
        zero = 0
        while True:
            byte = view[p]
            p += 1
            count += 1
            if not byte:
                zero += 1
            if byte == 1 and zero >= 2:
                size = count - init - zero - 1
                zero -= min(zero, 3)
                break
            elif byte:
                zero = 0
            if count == n:
                size = count - init - zero
                break
        idx += size + init + zero
        if idx >= buf_len and pos >= len(stream):
            return starts, True


def nal(kind, size):
    """A NAL of exactly `size` bytes (start code included): no zero bytes inside."""
    return START + bytes([0x60 | kind]) + b'\xa5' * (size - 5)


class CookChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not FFMPEG:
            raise unittest.SkipTest('ffmpeg not installed')
        cls.dir = tempfile.TemporaryDirectory()
        cls.root = Path(cls.dir.name)
        cls.source = cls.root / 'source.nut'
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i', 'testsrc2=s=320x240:r=15:d=4',
                        '-c:v', 'ffv1', str(cls.source)], check=True)
        cls.good, _ = cook.embed_slice_metadata(encode(cls.root, cls.source))

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def reject(self, data, text):
        with self.assertRaises(ValueError) as caught:
            cook.validate(cook.embed_slice_metadata(data)[0])
        self.assertIn(text, str(caught.exception))

    def test_cooked_stream_passes_and_is_summarised(self):
        info = h264.check(self.good)
        self.assertEqual(info['pictures'], 60)
        self.assertGreaterEqual(info['keyframes'], 6)  # an IDR every 10 pictures: SPS repeats
        self.assertEqual(info['max_slice'], info['max_nal'] + h264.START_CODE_MARGIN)
        self.assertEqual((info['sps']['width'], info['sps']['height'], info['sps']['fps']), (320, 240, 15))
        self.assertGreater(len(list(n for n in h264.nals(self.good) if n[2] == 7)), 1)

    def test_resolution_is_rejected_by_name(self):
        self.reject(encode(self.root, self.source, scale='640:480'), '640x480')

    def test_frame_rate_is_rejected_by_name(self):
        self.reject(encode(self.root, self.source, fps=25), 'frame rate')

    def test_cabac_is_rejected_by_name(self):
        self.reject(encode(self.root, self.source, params={'cabac': 1}), 'CABAC')

    def test_b_frames_are_rejected_by_name(self):
        # x264 gives a B-frame stream more than one reference frame in the SPS, which is
        # named first; a stream with B slices and one reference is named by its slice.
        self.reject(encode(self.root, self.source, bframes=2, params={'b-adapt': 0, 'b-pyramid': 'none'}),
                    'num_ref_frames')
        spans = list(h264.nals(self.good))
        pps = h264.parse_pps(h264.payload(self.good, next(s for s in spans if s[2] == 8)))
        sps = h264.parse_sps(h264.payload(self.good, next(s for s in spans if s[2] == 7)))
        # first_mb ue(0) = 1, slice_type ue(1) = 010, pps_id ue(0) = 1
        b_slice = bytes([0b10101000]) + bytes(8)
        header = h264.parse_slice(b_slice, 1, 1, sps, pps)
        self.assertEqual(header['type'], 1)

    def test_two_slices_per_picture_are_rejected_by_name(self):
        self.reject(encode(self.root, self.source, params={'slices': 2}), 'more than one slice')

    def test_deblocking_is_rejected_by_name(self):
        self.reject(encode(self.root, self.source, params={'no-deblock': 0}), 'deblocking')

    def test_reference_frames_are_rejected_by_name(self):
        self.reject(encode(self.root, self.source, refs=4), 'num_ref_frames')

    def test_8x8_transform_is_rejected_by_name(self):
        data = encode(self.root, self.source, params={'8x8dct': 1}, extra=('-profile:v', 'high'))
        self.reject(data, 'profile')

    def test_different_parameter_sets_are_rejected_by_name(self):
        # The same stream with a second, different SPS in the middle (another level).
        spans = list(h264.nals(self.good))
        sps = next(s for s in spans if s[2] == 7)
        raw = bytearray(self.good[sps[0]:sps[1]])
        raw[7] ^= 0x01  # level_idc
        later = [s for s in spans if s[2] == 7][1]
        data = self.good[:later[0]] + bytes(raw) + self.good[later[1]:]
        with self.assertRaises(ValueError) as caught:
            cook.validate(data)
        self.assertIn('different SPS', str(caught.exception))

    def test_color_range_and_matrix_are_rejected_by_name(self):
        data = subprocess.run(
            ['ffmpeg', '-v', 'error', '-y', '-i', str(self.source), '-an', '-vf', 'scale=320:240,fps=15,format=yuv420p',
             '-c:v', 'libx264', '-profile:v', 'main', '-bf', '0', '-refs', '1', '-colorspace', 'smpte170m',
             '-color_range', 'tv', '-x264-params', cook.X264, '-f', 'h264', '-'],
            check=True, capture_output=True).stdout
        self.reject(data, 'BT.709 full range')

    def test_missing_slice_size_is_rejected(self):
        with self.assertRaises(ValueError):
            cook.validate(b''.join(self.good[s:e] for s, e, t in h264.nals(self.good) if t != 6))

    def test_slice_size_without_margin_is_rejected(self):
        spans = list(h264.nals(self.good))
        sei = next(s for s in spans if s[2] == 6)
        biggest = max(e - s for s, e, _ in spans)
        data = self.good[:sei[0]] + cook.slice_sei(biggest) + self.good[sei[1]:]
        with self.assertRaises(ValueError) as caught:
            cook.validate(data)
        self.assertIn('start code', str(caught.exception))


class WindowMargin(unittest.TestCase):
    """The SDK refills its 64 KiB window when `left <= advertised size` and cuts a
    NAL at the end of the window: a window that ends 2 or 3 bytes into the next
    start code loses the stream up to the next window's end."""

    def stream(self, big, left):
        """NALs of 3,000 bytes, then `big`, placed so that exactly `left` bytes of the
        64 KiB window remain when the big NAL begins."""
        head = nal(7, 30) + nal(8, 12) + nal(6, 40)
        pieces = [head]
        used = len(head)
        target = h264.INPUT_WINDOW - left
        while target - used >= 3000 + 5:
            pieces.append(nal(1, 3000))
            used += 3000
        pieces.append(nal(1, target - used))   # the last filler lands exactly
        pieces.append(nal(1, big))
        pieces += [nal(1, 3000)] * 40
        return b''.join(pieces)

    def test_old_advertised_size_loses_the_stream_new_one_does_not(self):
        big = 20000
        stream = self.stream(big, big + 2)
        spans = list(h264.nals(stream))
        biggest = max(e - s for s, e, _ in spans)
        self.assertEqual(biggest, big)
        old, whole_old = model_window(stream, biggest)
        new, whole_new = model_window(stream, biggest + h264.START_CODE_MARGIN)
        self.assertFalse(whole_old, 'the old advertised size must hit the case')
        self.assertTrue(whole_new)
        self.assertEqual(new, [s for s, _, _ in spans])
        print(f'\nwindow model: advertised {biggest} -> NAL at {old[-1]} is not a NAL start '
              f'(stream lost from there); advertised {biggest + h264.START_CODE_MARGIN} -> '
              f'all {len(spans)} NALs found')

    def test_margin_is_enough_for_every_phase(self):
        """Every distance of the window end from the biggest NAL: without the margin
        the window ends inside the next start code for 1 to 3 bytes of room, with it never."""
        big = 12000
        lost = []
        for room in range(0, 12):
            stream = self.stream(big, big + room)
            biggest = max(e - s for s, e, _ in h264.nals(stream))
            self.assertEqual(biggest, big)
            self.assertTrue(model_window(stream, biggest + h264.START_CODE_MARGIN)[1], room)
            if not model_window(stream, biggest)[1]:
                lost.append(room)
        self.assertEqual(lost, [1, 2, 3])
        print(f'\nwindow model: room after the biggest NAL 0..11 bytes: lost without the margin at {lost}, '
              'never with it')

    def test_largest_nal_plus_margin_must_fit_the_window(self):
        limit = h264.INPUT_WINDOW - h264.START_CODE_MARGIN
        ok = nal(7, 30) + nal(8, 12) + nal(1, limit)
        self.assertEqual(cook.embed_slice_metadata(ok)[1], h264.INPUT_WINDOW)
        with self.assertRaises(ValueError):
            cook.embed_slice_metadata(nal(7, 30) + nal(8, 12) + nal(1, limit + 1))


if __name__ == '__main__':
    unittest.main()
