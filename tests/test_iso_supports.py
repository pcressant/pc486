#!/usr/bin/env python3
import contextlib
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import installation as install  # noqa: E402
import iso_supports as iso  # noqa: E402

MTOOLS = shutil.which('mformat') and shutil.which('mcopy') and shutil.which('mattrib')


def floppy(path, label, files=(), hidden=()):
    path.write_bytes(b'\0' * 1228800)
    subprocess.run(['mformat', '-f', '1200', '-v', label, '-i', path, '::'], check=True)
    with tempfile.TemporaryDirectory() as d:
        for name, data in files:
            local = Path(d) / name
            local.write_bytes(data)
            subprocess.run(['mcopy', '-o', '-i', path, local, '::/' + name], check=True)
            if name in hidden:
                subprocess.run(['mattrib', '+h', '-i', path, '::/' + name], check=True)
    return path


class IsoSupportsTests(unittest.TestCase):
    @unittest.skipUnless(MTOOLS, 'mtools absent')
    def test_merged_disks_keep_hidden_files_and_refuse_conflicts(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            a = floppy(p / 'a.img', 'A', [('COMMUN.TXT', b'x'), ('CACHE.BIN', b'h')], hidden={'CACHE.BIN'})
            b = floppy(p / 'b.img', 'B', [('COMMUN.TXT', b'x'), ('DEUX.TXT', b'2')])
            (p / 'w').mkdir()
            iso.merged([a, b], p / 'OUT', p / 'w')
            self.assertEqual(sorted(f.name for f in (p / 'OUT').iterdir()),
                             ['CACHE.BIN', 'COMMUN.TXT', 'DEUX.TXT'])
            c = floppy(p / 'c.img', 'C', [('COMMUN.TXT', b'autre')])
            with self.assertRaisesRegex(install.InstallError, 'COMMUN.TXT'):
                iso.merged([a, c], p / 'OUT2', p / 'w')
            # Lotus : la dernière disquette l'emporte, comme dans l'installateur.
            iso.merged([a, c], p / 'OUT3', p / 'w', overwrite=True)
            self.assertEqual((p / 'OUT3/COMMUN.TXT').read_bytes(), b'autre')

    def test_names_must_be_dos_8_3(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p / 'WORD6').mkdir()
            (p / 'WORD6/INSTALL.EXE').write_bytes(b'')
            iso.check_names(p)
            (p / 'WORD6/TROPLONGNOM.EXE').write_bytes(b'')
            with self.assertRaisesRegex(install.InstallError, '8.3'):
                iso.check_names(p)

    def test_output_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / 'supports.iso'
            out.write_bytes(b'garde')
            with contextlib.redirect_stderr(io.StringIO()) as err:
                self.assertEqual(iso.main(['--sortie', str(out), '--reseau', str(out)]), 1)
            self.assertIn('déjà existante', err.getvalue())
            self.assertEqual(out.read_bytes(), b'garde')
            with contextlib.redirect_stderr(io.StringIO()) as err:
                self.assertEqual(iso.main(['--sortie', str(Path(d) / 'vide.iso')]), 1)
            self.assertIn('aucun support', err.getvalue())
            self.assertEqual(sorted(f.name for f in Path(d).iterdir()), ['supports.iso'])

    @unittest.skipUnless(MTOOLS and shutil.which('xorriso'), 'mtools ou xorriso absent')
    def test_word_dos_iso_with_grammatik_apart(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            source = p / 'worddos.zip'
            with zipfile.ZipFile(source, 'w') as z:
                for label in install.WORD_DOS_VOLUMES:
                    files = [('LISEZMOI.TXT', b'word'), (label[:8] + '.DAT', label.encode()),
                             (label[:6] + '.EX$', b'SZDD')]
                    z.write(floppy(p / f'{label}.ima', label, files), f'WORD6/{label}.IMA')
                z.write(floppy(p / 'g.ima', 'GRAMMAR_60', [('LISEZMOI.TXT', b'grammatik'),
                                                         ('INSTALLE.EXE', b'MZ')]), 'WORD6/GRAMMATIK.IMA')
            out = p / 'iso/supports.iso'
            with patch.object(install, 'validate_word_dos'), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(iso.main(['--sortie', str(out), '--word-dos', str(source)]), 0)
            names = set(iso.iso_names(out))     # noms ISO 9660 vus par MSCDEX
            self.assertIn('/WORDDOS/SETUP_60.DAT', names)
            self.assertIn('/WORDDOS/PRINT_.EX$', names)
            self.assertIn('/WORDDOS/PRINT2_6.DAT', names)
            self.assertIn('/WORDDOS/GRAMMAT/INSTALLE.EXE', names)
            self.assertIn('/LISEZMOI.TXT', names)
            subprocess.run(['xorriso', '-osirrox', 'on', '-indev', out, '-extract',
                            '/WORDDOS/LISEZMOI.TXT', p / 'lu.txt'], capture_output=True, check=True)
            self.assertEqual((p / 'lu.txt').read_bytes(), b'word')
            # Aucun dossier de travail laissé à côté de l'ISO.
            self.assertEqual([f.name for f in out.parent.iterdir()], ['supports.iso'])

    def test_folder_labels_keep_version_and_language(self):
        self.assertEqual(iso.short_label('communicator405-en'), 'COM405EN')
        self.assertEqual(iso.short_label('qemm-7.5-fr-win3x'), 'QEMM75FR')
        self.assertEqual(iso.short_label('netscape3-en'), 'NETSC3EN')
        self.assertEqual(iso.short_label('sonolect'), 'SONOLECT')
        taken = set()
        self.assertEqual(iso.dos_name('NBWIN.EX!', taken), 'NBWIN.EX!')
        self.assertEqual(iso.dos_name('WORD.EX$', taken), 'WORD.EX$')
        self.assertEqual(iso.dos_name('media-disk01.jpg', taken), 'MEDIA-DI.JPG')
        self.assertEqual(iso.dos_name('media-disk02.jpg', taken), 'MEDIA-_1.JPG')
        self.assertEqual(iso.dos_name('FD14LITE.vmdk', taken), 'FD14LITE.VMD')

    @unittest.skipUnless(MTOOLS and shutil.which('xorriso'), 'mtools ou xorriso absent')
    def test_other_archives_are_unpacked_except_products_given(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            media = p / 'media'
            (media / 'outil-2.0-fr').mkdir(parents=True)
            (media / 'deja').mkdir()
            nested = io.BytesIO()
            with zipfile.ZipFile(nested, 'w') as z:
                z.writestr('src/a.c', 'int a;')
            disk = floppy(p / 'd.ima', 'OUTIL', [('NBWIN.EX!', b'nb'), ('SETUP.EXE', b'MZ')])
            with zipfile.ZipFile(media / 'outil-2.0-fr/outil.zip', 'w') as z:
                z.write(disk, 'Disque Un.IMA')
                z.writestr('Notice tres longue.txt', 'lisez')
                z.writestr('SOURCES.ZIP', nested.getvalue())
            with zipfile.ZipFile(media / 'deja/produit.zip', 'w') as z:
                z.writestr('SECRET.TXT', 'ne pas reprendre')
            out = p / 'supports.iso'
            with patch.object(iso.options_vm, 'tcpip32', return_value={'OEMSETUP.INF': b'x'}), \
                    contextlib.redirect_stdout(io.StringIO()) as log:
                self.assertEqual(iso.main(['--sortie', str(out), '--reseau', str(media / 'deja/produit.zip'),
                                           '--autres', str(media)]), 0)
            self.assertIn('AUTRES\\OUTI20FR', log.getvalue())
            names = {n for n in iso.iso_names(out) if n.startswith('/AUTRES/') and '.' in n}
            self.assertEqual(names, {
                '/AUTRES/OUTI20FR/DISQUEUN/NBWIN.EX!', '/AUTRES/OUTI20FR/DISQUEUN/SETUP.EXE',
                '/AUTRES/OUTI20FR/NOTICETR.TXT', '/AUTRES/OUTI20FR/SOURCES/SRC/A.C',
                '/AUTRES/OUTI20FR/NOMS.TXT'})


if __name__ == '__main__':
    unittest.main()
