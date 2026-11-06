"""QEMM 8.0 sous 86Box, sur une copie : mémoire relevée par MEM /C (DOS), démarrage de
Windows avec Épaulard (Apollo II, COM1) et parole de Synthé3 sous Windows (son enregistré).

PC486_QEMM_86BOX=1 PC486_QEMM_SOURCE=disque.img python3 -m unittest tests.test_qemm_86box -v

Le disque source (arrêté, jamais modifié) doit contenir C:\\QEMM (scripts/installer
--qemm). La copie reçoit CONFIG.SYS et AUTOEXEC.BAT de final_boot_files(qemm=True),
branche DOS : S3RES, KEYB, Épaudos et SMARTDRV dans l'ordre de l'installation. 86Box
isolé (Xvfb, D-Bus privé, sans accessibilité), copie sous work/vmtest/qemm/.
"""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'synthe3'))
spec = importlib.util.spec_from_file_location('installation', ROOT / 'scripts/installation.py')
install = importlib.util.module_from_spec(spec)
spec.loader.exec_module(install)
SOURCE = Path(os.environ.get('PC486_QEMM_SOURCE', ROOT / 'vm/win311.img'))


@unittest.skipUnless(os.environ.get('PC486_QEMM_86BOX') == '1', 'PC486_QEMM_86BOX=1 non demandé')
class Qemm86BoxTests(unittest.TestCase):
    def test_dos_memory_with_qemm(self):
        import banc86box
        if not SOURCE.exists():
            self.skipTest(f'disque absent : {SOURCE}')
        listing = subprocess.run(['mdir', '-i', f'{SOURCE}@@32256', '::/QEMM/QEMM386.SYS'],
                                 capture_output=True)
        if listing.returncode:
            self.skipTest(f'QEMM absent de {SOURCE}')
        config, autoexec = install.final_boot_files(qemm=True)
        mem = b'MEM /C > C:\\MEM.TXT\r\nC:\\QEMM\\QEMM.COM > C:\\QEMM.TXT\r\n'
        batch = autoexec.replace(b'GOTO %CONFIG%', b'GOTO DOS').replace(b'GOTO FIN', mem + b'GOTO FIN')
        work, _ = banc86box.run_batch('dx4-100', (), passes=0, source=SOURCE, timeout=300,
                                      extra_autoexec=batch, config=config,
                                      base=ROOT / 'work/vmtest/qemm',
                                      outputs_extra=('MEM.TXT', 'QEMM.TXT'))
        text = (work / 'MEM.TXT').read_bytes().decode('cp850').replace('\r', '')
        modules = install.mem_modules(text)
        self.assertIn('QEMM386', modules, text)
        self.assertIn('EPAUDOS', modules, text)
        self.assertGreater(modules.get('S3RES', (0, 1))[1], 0, text)    # S3RES en mémoire supérieure
        free = modules['Libre'][0]
        print(f'\n86Box + QEMM : {free} octets de mémoire conventionnelle libre ({work})')
        # 614 528 octets mesurés le 6 novembre 2026 (EMM386 : 521 680)
        self.assertGreater(free, 580 * 1024, text)

    def windows(self, synthese):
        """Branche WIN sur une copie : EPAUDOS.INI choisit la synthèse ; INSTAUTO lance
        une attente DOS de 15 s (fenêtre DOS de Windows), puis quitte Windows ; S3END
        écrit la fin sur COM1. Renvoie (dossier, transcription COM1)."""
        import banc86box
        if not SOURCE.exists():
            self.skipTest(f'disque absent : {SOURCE}')
        image = f'{SOURCE}@@32256'
        if subprocess.run(['mdir', '-i', image, '::/QEMM/QEMM386.SYS'], capture_output=True).returncode:
            self.skipTest(f'QEMM absent de {SOURCE}')
        win = subprocess.run(['mtype', '-i', image, '::/WINDOWS/WIN.INI'], capture_output=True,
                             check=True).stdout.decode('cp1252')
        ini = install.load_ini_editor()
        win = install.epaulard_ini().edit_load(win, add=r'C:\INSTAUTO.EXE')
        win = ini.setting(win, 'pc486', 'commande', r'C:\DOS\CHOICE.COM /C:N /T:N,15')
        win = ini.setting(win, 'pc486', 'etat', '')
        win = ini.setting(win, 'sounds', 'SystemStart', 'none')     # pas de carillon
        win = ini.setting(win, 'sounds', 'SystemExit', 'none')
        base = ROOT / 'work/vmtest/qemm'
        base.mkdir(parents=True, exist_ok=True)
        files = base / f'fichiers-{synthese}'
        files.mkdir(exist_ok=True)
        (files / 'WINQ.INI').write_bytes(win.replace('\r\n', '\n').replace('\n', '\r\n').encode('cp1252'))
        (files / 'EPAUDOSQ.INI').write_bytes(f'Synthese={synthese}\r\nBraille=0\r\n'.encode())
        config, autoexec = install.final_boot_files(qemm=True)
        prepare = (b'COPY C:\\WINQ.INI C:\\WINDOWS\\WIN.INI > NUL\r\n'
                   b'COPY C:\\EPAUDOSQ.INI C:\\EPAULARD\\EPAUDOS.INI > NUL\r\n')
        batch = (prepare + autoexec).replace(b'GOTO %CONFIG%', b'GOTO WIN')
        work, _ = banc86box.run_batch(
            'dx4-100', (), passes=0, source=SOURCE, timeout=600, extra_autoexec=batch,
            config=config, base=base, sound=synthese == 'Synthe3',
            extra_files=(files / 'WINQ.INI', files / 'EPAUDOSQ.INI', ROOT / 'epaulard/build/INSTAUTO.EXE'))
        return work, (work / 'com1.txt').read_bytes()

    def test_windows_and_epaulard_with_qemm(self):
        # Apollo II : la parole d'Épaulard passe par COM1, lue ici
        work, com1 = self.windows('Apollo')
        self.assertIn(b'paulard pr', com1, work)
        self.assertIn(b'Gestionnaire de programmes', com1, work)

    def test_windows_synthe3_with_qemm(self):
        # Synthé3 : S3RES rend son IRQ à Windows (1605h) et EPAUS3.DLL parle par la
        # Sound Blaster ; Windows se referme ensuite (S3END atteint).
        import banc86box
        work, com1 = self.windows('Synthe3')
        self.assertNotIn(b'paulard pr', com1, work)             # rien sur COM1
        samples, rate = banc86box.load_wav(work / 'son.wav')
        spans = banc86box.utterances(samples, rate)
        print(f'\nSynthé3 sous Windows (86Box + QEMM) : énoncés (ms) {spans} ({work})')
        late = [s for s in spans if s[0] > spans[0][1] + 5000] if spans else []
        # Épaudos parle au chargement (DOS) ; Épaulard plusieurs secondes après, sous Windows
        self.assertTrue(late, f'aucune parole sous Windows ; {work}')
        self.assertGreater(sum(b - a for a, b in late), 2000, work)


if __name__ == '__main__':
    unittest.main()
