# SPDX-License-Identifier: MIT
"""Independent SN64 byte decoder; does not import the writer or source reader."""
import struct


class Section:
    def __init__(self, data):
        self.data = data
        if len(data) < 64:
            raise ValueError('SN64 short header')
        h = struct.unpack_from('>4s15I', data)
        magic, version, total, metadata, count, stride, offset, missing, missing_offset, block = h[:10]
        if magic != b'SN64' or version != 1 or total != len(data) or any(h[10:]):
            raise ValueError('SN64 header')
        if stride != 32 or offset != 64 or block != 4096 or count > 256 or missing > 256:
            raise ValueError('SN64 directory limits')
        if missing_offset != 64 + count * 32 or metadata != (missing_offset + missing * 8 + 15) & -16:
            raise ValueError('SN64 metadata layout')
        if metadata > len(data) or metadata > 16384:
            raise ValueError('SN64 metadata bounds')
        self.samples, self.unavailable = [], []
        cursor = metadata
        for i in range(count):
            row = struct.unpack_from('>8I', data, 64 + i * 32)
            start, frames, rate, encoding, loop, size, flags, zero = row
            if start != cursor or not frames or rate not in (11025, 22050) or encoding not in (1, 2, 3):
                raise ValueError('SN64 sample descriptor')
            expected = frames // 28 * 16 if encoding == 3 else frames * encoding
            if (encoding == 3 and (frames % 28 or rate != 11025)) or size != expected or loop >= frames or flags & ~1 or zero:
                raise ValueError('SN64 sample shape')
            if size > len(data) - start:
                raise ValueError('SN64 sample bounds')
            cursor = (start + size + 15) & -16
            if cursor > len(data) or any(data[start + size:cursor]):
                raise ValueError('SN64 sample padding')
            self.samples.append(row)
        if cursor != len(data):
            raise ValueError('SN64 trailing bytes')
        for i in range(missing):
            sound, sample = struct.unpack_from('>2I', data, missing_offset + i * 8)
            if sound >= 256 or sample < count or sample > 65535:
                raise ValueError('SN64 unavailable reference')
            if (sound, sample) in self.unavailable:
                raise ValueError('SN64 duplicate unavailable reference')
            self.unavailable.append((sound, sample))
        if self.unavailable != sorted(self.unavailable):
            raise ValueError('SN64 unavailable order')
        if any(data[missing_offset + missing * 8:metadata]):
            raise ValueError('SN64 metadata padding')

    def pcm(self, index):
        start, frames, _, encoding, _, size, _, _ = self.samples[index]
        encoded = self.data[start:start + size]
        if encoding == 1:
            return [n * 256 for n in struct.unpack(str(frames) + 'b', encoded)]
        if encoding == 2:
            return list(struct.unpack('>' + str(frames) + 'h', encoded))
        # Independent byte walk of the SPU recurrence; never calls source
        # decoder/writer. Nibbles are signed4bit, low half first; rounded
        # prediction is saturated before entering the next history pair.
        coefficients = ((0,0),(60,0),(115,-52),(98,-55),(122,-60))
        out, history = [], [0,0]
        for block in struct.iter_unpack('16B', encoded):
            predictor, shift = divmod(block[0], 16)
            if predictor > 4 or shift > 12 or block[1] > 7:
                raise ValueError('SN64 SPU block')
            for n in range(28):
                nibble = (block[2 + n//2] >> ((n & 1)*4)) & 15
                signed = (nibble ^ 8) - 8
                a,b = coefficients[predictor]
                prediction = (history[0]*a + history[1]*b + 32)//64
                sample = max(-32768,min(32767,(signed*4096)//(1 << shift)+prediction))
                out.append(sample)
                history = [sample,history[0]]
        if not encoded[-15] & 1:
            raise ValueError('SN64 SPU terminal flag')
        return out
