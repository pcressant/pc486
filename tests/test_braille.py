"""Protocole Baum, arbitrage BrlAPI et configuration Win16, sans plage physique."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from braille.baum import Baum, packet
from braille.gateway import Output, Focus
sys.path.insert(0, str(ROOT / 'tests'))
import native
spec = importlib.util.spec_from_file_location('configure_braille', ROOT / 'scripts/configure-braille.py')
config = importlib.util.module_from_spec(spec)
spec.loader.exec_module(config)


def request(command, data):
    return b'\x1b' + bytes((command,)) + bytes(data).replace(b'\x1b', b'\x1b\x1b')


class ProtocolTests(unittest.TestCase):
    def test_all_fragmentation_boundaries_and_literal_escape(self):
        cells = bytes((0, 0x1b, 255, 0x1b))
        wire = request(0x15, [1]) + request(1, cells)
        for split in range(len(wire) + 1):
            output = []
            protocol = Baum(4, output.append)
            reply = protocol.feed(wire[:split]) + protocol.feed(wire[split:])
            self.assertEqual(output, [cells])
            self.assertTrue(reply.startswith(b'\x1b\x01\x04\x1b\x84'))
            self.assertEqual(len(reply), 31)

    def test_byte_by_byte_and_full_80_cells(self):
        output = []
        protocol = Baum(80, output.append)
        for b in request(0x15, [1]) + request(1, [0x1b] * 80):
            protocol.feed(bytes((b,)))
        self.assertEqual(output, [b'\x1b' * 80])

    def test_disable_and_reconnect(self):
        output = []
        protocol = Baum(1, output.append)
        protocol.feed(request(1, [255]))
        self.assertEqual(output, [])
        protocol.feed(request(0x15, [1]))
        self.assertTrue(protocol.key('FWINLT'))
        self.assertEqual(protocol.feed(request(0x15, [0])), b'')
        self.assertEqual(protocol.key('FWINLT'), b'')
        protocol.feed(request(0x15, [1]))
        protocol.feed(request(1, [0]))
        self.assertEqual(output, [b'\0'])

    def test_truncated_frame_resync_and_unknown_commands(self):
        output = []
        protocol = Baum(4, output.append)
        protocol.feed(request(0x15, [1]))
        protocol.feed(b'garbage\x1b\xff\x1b\x01\xff' + request(0x15, [1]))
        protocol.feed(request(1, [1, 2, 3, 4]))
        self.assertEqual(output, [bytes((1, 2, 3, 4))])

    def test_keys_press_release_and_routing_boundaries(self):
        protocol = Baum(40, lambda _: None)
        protocol.feed(request(0x15, [1]))
        self.assertEqual(protocol.key('FWINLT'), b'\x1b\x24\x02\x1b\x24\0')
        self.assertEqual(protocol.key('ROUTE', 39), b'\x1b\x27\x28\x1b\x27\0')
        self.assertEqual(protocol.key('ROUTE', 40), b'')
        # Réponses brutes : une valeur ESC n'est pas doublée.
        self.assertEqual(protocol.key('ROUTE', 26), b'\x1b\x27\x1b\x1b\x27\0')
        for width in (0, 81):
            with self.assertRaises(ValueError):
                Baum(width, lambda _: None)


class FakeConnection:
    displaySize = (40, 1)
    def __init__(self):
        self.events = []
        self.queue = []
    def enterTtyModeWithPath(self): self.events.append('enter')
    def setParameter(self, *args): self.events.append(('priority', args[-1]))
    def ignoreKeys(self, *args): self.events.append(('ignore', args))
    def acceptKeys(self, *args): self.events.append(('accept', args))
    def writeDots(self, cells): self.events.append(('dots', cells))
    def readKey(self, wait): return self.queue.pop(0) if self.queue else None
    def closeConnection(self): self.events.append('close')


class HostTests(unittest.TestCase):
    def setUp(self):
        self.connection = FakeConnection()
        names = ('FWINLT', 'FWINRT', 'LNUP', 'LNDN', 'HOME', 'ROUTE')
        self.api = types.SimpleNamespace(Connection=lambda: self.connection,
            PARAM_CLIENT_PRIORITY=1, rangeType_all=0, rangeType_command=2,
            KEY_TYPE_CMD=0x20000000, expandKeyCode=lambda k: k,
            **{'KEY_CMD_' + name: i for i, name in enumerate(names)})

    def test_focus_releases_display_and_restores_latest_cells(self):
        with patch.dict(sys.modules, brlapi=self.api):
            output = Output()
        accepted = next(event for event in self.connection.events if event[0] == 'accept')
        self.assertTrue(all(code & self.api.KEY_TYPE_CMD for code in accepted[1][1]))
        cells = bytes(range(40))
        output.display(cells)
        self.assertNotIn(('dots', cells), self.connection.events)
        output.activate(True)
        self.assertEqual(self.connection.events[-2:], [('priority', 50), ('dots', cells)])
        output.activate(False)
        output.display(b'\xff' * 40)
        self.assertEqual(self.connection.events[-1], ('priority', 0))
        output.activate(True)
        self.assertEqual(self.connection.events[-1], ('dots', b'\xff' * 40))
        output.close()
        self.assertEqual(self.connection.events[-2:], [('priority', 0), 'close'])

    def test_keys_are_ignored_while_inactive(self):
        with patch.dict(sys.modules, brlapi=self.api):
            output = Output()
        key = dict(type=self.api.KEY_TYPE_CMD, command=self.api.KEY_CMD_ROUTE, argument=7)
        self.connection.queue.append(key)
        self.assertEqual(list(output.keys()), [])
        output.activate(True)
        self.connection.queue.append(key)
        self.assertEqual(list(output.keys()), [('ROUTE', 7)])

    def test_init_failure_closes_connection(self):
        self.connection.displaySize = (81, 1)
        with patch.dict(sys.modules, brlapi=self.api), self.assertRaises(ValueError):
            Output()
        self.assertEqual(self.connection.events[-1], 'close')

    def test_terminal_title_cannot_claim_the_display(self):
        window = types.SimpleNamespace(name='pc486 — terminal',
            getApplication=lambda: types.SimpleNamespace(name='Ghostty'))
        self.assertFalse(Focus.matches(window))
        window.getApplication = lambda: types.SimpleNamespace(name='qemu-system-i386')
        self.assertTrue(Focus.matches(window))

    def test_refresh_recovers_missing_window_event(self):
        window = types.SimpleNamespace(name='86box - 86Box 5.3',
            getApplication=lambda: types.SimpleNamespace(name='86Box'),
            getState=lambda: types.SimpleNamespace(contains=lambda state: True))
        focus = object.__new__(Focus)
        focus.active = False
        focus.api = types.SimpleNamespace(STATE_ACTIVE=1,
            Registry=types.SimpleNamespace(getDesktop=lambda index: [[window]]))
        focus.refresh()
        self.assertTrue(focus.active)
        window.getState = lambda: types.SimpleNamespace(contains=lambda state: False)
        focus.refresh()
        self.assertFalse(focus.active)

    def test_delayed_unrelated_deactivation_preserves_focus(self):
        focus = object.__new__(Focus)
        focus.active = False
        vm = types.SimpleNamespace(name='QEMU (pc486)', getApplication=lambda: types.SimpleNamespace(name='qemu'))
        other = types.SimpleNamespace(name='Terminal', getApplication=lambda: types.SimpleNamespace(name='terminal'))
        focus.event(types.SimpleNamespace(type='window:activate', source=vm))
        self.assertTrue(focus.active)
        focus.event(types.SimpleNamespace(type='window:deactivate', source=other))
        self.assertTrue(focus.active)
        focus.event(types.SimpleNamespace(type='window:activate', source=other))
        self.assertFalse(focus.active)


class ConfigTests(unittest.TestCase):
    def test_idempotent_preserves_other_sections_and_crlf(self):
        original = '[windows]\r\nload=C:\\EPAULARD\\EPAULARD.EXE\r\n; réglages\r\n'
        result = config.setting(original, 'epaulard', 'BraillePort', '3')
        self.assertTrue(result.startswith(original))
        self.assertEqual(result, config.setting(result, 'epaulard', 'BraillePort', '3'))
        self.assertNotIn('\n', result.replace('\r\n', ''))

    def test_duplicates_and_case_insensitive_sections(self):
        result = config.setting('[386enh]\nCOM3IRQ=4\ncom3irq=4\nfoo=bar\n[next]\nCOM3IRQ=4\n',
                                '386Enh', 'COM3Irq', '9')
        self.assertEqual(result.count('COM3Irq=9'), 1)
        self.assertIn('[next]\nCOM3IRQ=4', result)
        self.assertIn('foo=bar', result)


@unittest.skipUnless(native.available(), 'ni Open Watcom (tools/watcom) ni compilateur C')
class GuestTests(unittest.TestCase):
    def test_guest_serial_and_french_translation(self):
        native.build_and_run('braille-test.c')

if __name__ == '__main__':
    unittest.main()
