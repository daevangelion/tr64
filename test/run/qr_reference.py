# SPDX-License-Identifier: MIT
"""Reference QR matrix for the fatal-screen symbol: segno, version 13, level M,
mask 4, alphanumeric mode (needs numpy and segno).

segno quirk: when the terminator ends exactly on a byte boundary it appends a
whole extra zero byte (segno/encoder.py write_padding_bits), where ISO/IEC
18004 7.4.10 adds padding bits only if the stream does not end on a codeword
boundary. `fixed=True` corrects that one function for the call; `fixed=False`
is segno as shipped.
"""
import numpy
import segno
import segno.encoder


def aligned(length):
    """Terminator ends on a codeword boundary: 4 mode + 11 count + data + 4."""
    bits = 15 + 11 * (length // 2) + 6 * (length % 2) + 4
    return bits < 2672 and bits % 8 == 0


def matrix(text, fixed=True, mask=4):
    original = segno.encoder.write_padding_bits
    if fixed:
        segno.encoder.write_padding_bits = lambda buff, version, length: buff.extend([0] * (-length % 8))
    try:
        code = segno.make(text, version=13, error="M", mask=mask, boost_error=False, micro=False, mode="alphanumeric")
    finally:
        segno.encoder.write_padding_bits = original
    return numpy.array([[1 if bit else 0 for bit in row] for row in code.matrix], dtype=numpy.uint8)
