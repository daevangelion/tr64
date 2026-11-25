#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
import tempfile
from pathlib import Path
import unittest
from input import assemble, write_masks


class InputTests(unittest.TestCase):
    def test_masks_and_tap(self):
        self.assertEqual(assemble('# comment\nhold FORWARD+WALK 2\ntap JUMP\nhold NONE 1\n'),
                         (None, [72, 72, 32, 0]))

    def test_placement(self):
        start, masks = assemble('start 14 40448 4608 60928 -16384\ntap ROLL\n')
        self.assertEqual(start, [14, 40448, 4608, 60928, -16384])
        self.assertEqual(masks, [24])
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'input'
            write_masks(path, masks, start)
            self.assertEqual(path.read_text(), 'start 14 40448 4608 60928 -16384\n24\n')

    def test_setup_directives(self):
        setup = []
        start, masks = assemble('give 126 1\nitem 20 100\nstart 3 1 2 3 4\ntap ROLL\n', setup)
        self.assertEqual((start, masks, setup), ([3, 1, 2, 3, 4], [24], [('give', 126, 1), ('item', 20, 100)]))
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'input'
            write_masks(path, masks, start, setup)
            self.assertEqual(path.read_text(), 'start 3 1 2 3 4\ngive 126 1\nitem 20 100\n24\n')
        for script in ('give 126 1', 'tap NONE\nitem 1 2'):
            with self.subTest(script=script), self.assertRaises(ValueError):
                assemble(script)

    def test_invalid(self):
        for script in ('hold FORWARD 0', 'hold FORWARD 601', 'hold WHAT 1',
                       'tap', 'hold FORWARD -1', 'start 0 0 0 0 0\nstart 0 0 0 0 0',
                       'tap NONE\nstart 0 0 0 0 0', 'start 0 2147483648 0 0 0',
                       'tap START', 'hold FORWARD+START 1'):
            with self.subTest(script=script), self.assertRaises(ValueError):
                assemble(script)


if __name__ == '__main__':
    unittest.main()
