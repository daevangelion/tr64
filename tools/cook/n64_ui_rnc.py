# SPDX-License-Identifier: MIT
"""Original bounded RNC2 decoder from PSX-SPX's published bit-prefix tables.
Reference: https://problemkaputt.de/psxspx-cdrom-file-compression-rnc-rob-northen-compression.htm
No retail executable or other engine implementation is used.
"""
import struct


def crc16(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
    return crc


class Bits:
    def __init__(self, data):
        self.data = data
        self.offset = 0
        self.value = self.left = 0

    def byte(self):
        if self.offset >= len(self.data):
            raise ValueError('RNC2 truncated byte stream')
        value = self.data[self.offset]
        self.offset += 1
        return value

    def bit(self):
        if not self.left:
            self.value = self.byte()
            self.left = 8
        self.left -= 1
        return (self.value >> self.left) & 1

    def bits(self, count):
        value = 0
        for _ in range(count):
            value = value * 2 + self.bit()
        return value

    def code(self, table):
        prefix = ''
        for _ in range(6):
            prefix += str(self.bit())
            if prefix in table:
                return table[prefix]
        raise ValueError('RNC2 invalid bit prefix')


LENGTH = {'0': -1, '1000': 4, '10010': 6, '10011': 7, '1010': 5,
          '10110': 8, '10111': -2, '110': 2, '1110': 3, '1111': -3}
DISTANCE = {'0': 0, '110': 0x100, '1000': 0x200, '1001': 0x300,
            '10101': 0x400, '10111': 0x500, '11101': 0x600,
            '11111': 0x700, '101000': 0x800, '101001': 0x900,
            '101100': 0xa00, '101101': 0xb00, '111000': 0xc00,
            '111001': 0xd00, '111100': 0xe00, '111101': 0xf00}


def decode(data, max_bytes=1024*1024):
    if len(data) < 18 or data[:4] != b'RNC\2':
        raise ValueError('expected RNC method 2 header')
    size, packed, raw_crc, packed_crc = struct.unpack_from('>IIHH', data, 4)
    if not size or size > max_bytes or packed > len(data)-18:
        raise ValueError('RNC2 input/output size')
    payload = data[18:18+packed]
    if crc16(payload) != packed_crc:
        raise ValueError('RNC2 packed CRC')
    bits = Bits(payload)
    bits.bits(2)
    out = bytearray()
    while True:
        length = bits.code(LENGTH)
        if length == -3:
            length = bits.byte()
            if not length:
                if bits.bit():
                    continue
                break
            length += 8
        if length in (-1, -2):
            length = 1 if length == -1 else bits.bits(4)*4+12
            if length > size-len(out):
                raise ValueError('RNC2 literal output overflow')
            for _ in range(length):
                out.append(bits.byte())
            continue
        distance_high = 0 if length == 2 else bits.code(DISTANCE)
        distance = distance_high + bits.byte() + 1
        if distance > len(out) or length > size-len(out):
            raise ValueError('RNC2 invalid backreference')
        for _ in range(length):
            out.append(out[-distance])
    if len(out) != size or crc16(out) != raw_crc:
        raise ValueError('RNC2 output length/CRC')
    return bytes(out)
