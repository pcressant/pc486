"""Garde-fous de l'installation ; aucun support Microsoft requis pour ces tests."""
import importlib.util
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
spec = importlib.util.spec_from_file_location('installation', ROOT / 'scripts/installation.py')
install = importlib.util.module_from_spec(spec)
spec.loader.exec_module(install)


class MediaTests(unittest.TestCase):
    def test_local_images_are_copied_and_unchanged(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            source = p / 'supports'
            source.mkdir()
            original = source / 'Disk01.IMA'
            original.write_bytes(b'a' * install.FLOPPY_SIZE)
            images = install.collect_media(source, 1, p / 'copie')
            self.assertNotEqual(images[0], original)
            self.assertEqual(install.digest(images[0]), install.digest(original))
            images[0].write_bytes(b'modification du support de travail')
            self.assertEqual(original.stat().st_size, install.FLOPPY_SIZE)
            self.assertEqual(original.read_bytes()[:10], b'a' * 10)

    def test_zip_never_extracts_paths_or_non_media_files(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            source = p / 'media.zip'
            with zipfile.ZipFile(source, 'w', compression=zipfile.ZIP_DEFLATED) as z:
                z.writestr('../../DISK1.IMG', b'\0' * install.FLOPPY_SIZE)
                z.writestr('../../ailleurs.txt', b'non')
                z.writestr('DOS/INSTALL.EXE', b'non')
            images = install.collect_media(source, 1, p / 'supports')
            self.assertEqual(images, [p / 'supports/disk1.img'])
            self.assertEqual(set(p.iterdir()), {source, p / 'supports'})

    def test_supplemental_disk_found_by_content_in_dos_zip(self):
        def only_suppl(path):
            if not path.read_bytes().startswith(b'SUPPL'):
                raise install.InstallError('pas le disque supplémentaire')
        blob = lambda tag: tag + b'\0' * (install.FLOPPY_SIZE - len(tag))
        with tempfile.TemporaryDirectory() as d, patch.object(install, 'validate_suppl', only_suppl):
            p = Path(d)
            source = p / 'msdos622fr.zip'
            with zipfile.ZipFile(source, 'w') as z:
                z.writestr('msdos622fr/Disk1.IMA', blob(b'DISK1'))
                z.writestr('msdos622fr/Dsuppl.IMA', blob(b'SUPPL'))
                z.writestr('msdos622fr/win3xorg.txt', b'texte')
            out = install.collect_suppl(source, p / 'suppl.img')
            self.assertTrue(out.read_bytes().startswith(b'SUPPL'))
            self.assertEqual(set(p.iterdir()), {source, out})
            image = p / 'SUPPL.IMG'                 # image seule : comme avant
            image.write_bytes(blob(b'SUPPL'))
            self.assertEqual(install.collect_suppl(image, p / 'copie.img').read_bytes(), image.read_bytes())
            with zipfile.ZipFile(p / 'deux.zip', 'w') as z:
                z.writestr('a.ima', blob(b'SUPPL'))
                z.writestr('b.ima', blob(b'SUPPL'))
            with self.assertRaisesRegex(install.InstallError, 'plusieurs'):
                install.collect_suppl(p / 'deux.zip', p / 'x.img')
            with zipfile.ZipFile(p / 'aucun.zip', 'w') as z:
                z.writestr('Disk1.IMA', blob(b'DISK1'))
            with self.assertRaisesRegex(install.InstallError, 'aucun'):
                install.collect_suppl(p / 'aucun.zip', p / 'y.img')
            self.assertFalse((p / 'y.img').exists())

    def test_missing_duplicate_and_wrong_size_are_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            source = p / 'media'
            source.mkdir()
            with self.assertRaisesRegex(install.InstallError, 'manquantes'):
                install.collect_media(source, 1, p / 'vide')
            (source / 'DISK1.IMG').write_bytes(b'\0' * install.FLOPPY_SIZE)
            (source / 'disk01.ima').write_bytes(b'\0' * install.FLOPPY_SIZE)
            with self.assertRaisesRegex(install.InstallError, 'ambigu'):
                install.collect_media(source, 1, p / 'doublon')
            (source / 'disk01.ima').unlink()
            (source / 'DISK1.IMG').write_bytes(b'erreur')
            with self.assertRaisesRegex(install.InstallError, '1,44'):
                install.collect_media(source, 1, p / 'taille')


class DiskSafetyTests(unittest.TestCase):
    def test_existing_file_and_broken_symlink_are_never_overwritten(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            image = p / 'disque.img'
            image.write_bytes(b'travail utilisateur')
            link = p / 'lien.img'
            link.symlink_to(p / 'absent')
            with patch.object(install, 'prerequisites'), patch.object(install, 'collect_media') as collect:
                for target in (image, link):
                    self.assertEqual(install.main(['--dos', d, '--windows', d,
                                                   '--output', str(target)]), 1)
                collect.assert_not_called()
            self.assertEqual(image.read_bytes(), b'travail utilisateur')
            self.assertTrue(link.is_symlink())

    def test_raw_disk_cannot_be_published_with_a_qcow2_extension(self):
        with tempfile.TemporaryDirectory() as d:
            output = Path(d) / 'new.qcow2'
            with patch.object(install, 'prerequisites'), patch.object(install, 'collect_media') as collect:
                self.assertEqual(install.main(['--dos', d, '--windows', d,
                                               '--output', str(output)]), 1)
                collect.assert_not_called()
            self.assertFalse(output.exists())

    def test_publication_is_atomic_and_refuses_a_racing_writer(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            disk, output = p / 'stage.img', p / 'final.img'
            disk.write_bytes('disque vérifié'.encode())
            output.write_bytes(b'autre processus')
            with self.assertRaises(FileExistsError):
                install.publish(disk, output)
            self.assertEqual(output.read_bytes(), b'autre processus')
            self.assertTrue(disk.exists())
            output.unlink()
            install.publish(disk, output)
            self.assertEqual(output.read_bytes(), 'disque vérifié'.encode())
            self.assertFalse(disk.exists())

    def test_invalid_partition_is_not_used_as_an_mtools_offset(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'disk.img'
            with p.open('wb') as f:
                f.truncate(2 * 1024 * 1024)
            with self.assertRaises(install.InstallError):
                install.disk_partition(p)
            mbr = bytearray(512)
            mbr[510:] = b'\x55\xaa'
            mbr[446], mbr[450] = 0x80, 6
            struct.pack_into('<II', mbr, 454, 63, 2048)
            with p.open('r+b') as f:
                f.write(mbr)
            self.assertEqual(install.disk_partition(p), f'{p}@@32256')
            struct.pack_into('<II', mbr, 454, 63, 999999)
            with p.open('r+b') as f:
                f.write(mbr)
            with self.assertRaisesRegex(install.InstallError, 'hors du disque'):
                install.disk_partition(p)

    def test_failure_before_boot_validation_never_publishes_the_disk(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            binary = p / 'bin'
            binary.mkdir()
            for name in ('EPAULARD.EXE', 'EPAUHOOK.DLL', 'EPAUDOS.COM'):
                (binary / name).write_bytes(b'MZ-test')
            out = p / 'final.img'
            with patch.object(install, 'prerequisites'), \
                 patch.object(install, 'collect_media', return_value=[]), \
                 patch.object(install, 'validate_media'), patch.object(install, 'install_dos'), \
                 patch.object(install, 'disk_partition', return_value='offset'), \
                 patch.object(install, 'stage_windows'), patch.object(install, 'install_windows'), \
                 patch.object(install, 'configure'), \
                 patch.object(install, 'verify_boot', side_effect=install.InstallError('échec démarrage')):
                self.assertEqual(install.main(['--dos', d, '--windows', d, '--binaries', str(binary),
                                               '--output', str(out)]), 1)
            self.assertFalse(out.exists())
            stages = list(p.glob('.pc486-install-*'))
            self.assertEqual(len(stages), 1)
            self.assertTrue((stages[0] / 'installation.img').exists())


class ConfigurationTests(unittest.TestCase):
    def test_group_aliases_preserve_the_installed_group_data(self):
        with tempfile.TemporaryDirectory() as d:
            stage = Path(d)
            ini = '[Groups]\r\nGroup3=C:\\WINDOWS\\RÉSEAU.GRP\r\nGroup5=C:\\WINDOWS\\DÉMARRAG.GRP\r\n'
            original = {'PROGMAN.INI': ini.encode('cp1252'),
                        'RÉSEAU.GRP': b'PMCC-group-network', 'DÉMARRAG.GRP': b'PMCC-group-startup'}
            writes = {}

            def fake_run(*args):
                if args[0] == 'mtype':
                    return original[args[-1].split('/')[-1]]
                if args[0] == 'mcopy':
                    writes[args[-1]] = Path(args[-2]).read_bytes()
                    return b''
                raise AssertionError(args)

            with patch.object(install, 'run', side_effect=fake_run):
                install.configure_groups(stage, 'disk@@32256')
            self.assertEqual(writes['::/WINDOWS/RESEAU.GRP'], original['RÉSEAU.GRP'])
            self.assertEqual(writes['::/WINDOWS/DEMARRAG.GRP'], original['DÉMARRAG.GRP'])
            text = writes['::/WINDOWS/PROGMAN.INI'].decode('cp1252')
            self.assertIn('RESEAU.GRP', text)
            self.assertIn('DEMARRAG.GRP', text)
            self.assertNotIn('É', text)

    def test_ascii_group_files_from_codepage_850_are_only_renamed_in_progman(self):
        with tempfile.TemporaryDirectory() as d:
            ini = '[Groups]\r\nGroup3=C:\\WINDOWS\\RÉSEAU.GRP\r\nGroup5=C:\\WINDOWS\\DÉMARRAG.GRP\r\n'
            writes = {}

            def fake_run(*args):
                if args[0] == 'mtype' and args[-1] == '::/WINDOWS/PROGMAN.INI':
                    return ini.encode('cp1252')
                if args[0] == 'mcopy':
                    writes[args[-1]] = Path(args[-2]).read_bytes()
                    return b''
                raise AssertionError(args)

            with patch.object(install, 'run', side_effect=fake_run), \
                 patch.object(install, 'mexists', return_value=True):
                install.configure_groups(Path(d), 'disk@@32256')
            self.assertEqual(list(writes), ['::/WINDOWS/PROGMAN.INI'])
            self.assertNotIn('É', writes['::/WINDOWS/PROGMAN.INI'].decode('cp1252'))

    def test_machine_cleanup_survives_a_closed_qmp_stream(self):
        machine = install.Machine.__new__(install.Machine)
        machine.proc = Mock()
        machine.proc.poll.return_value = 0
        machine.q = Mock()
        machine.q.f.close.side_effect = BrokenPipeError('socket fermée')
        machine.log = Mock()
        machine.runtime = Mock()
        machine.close()
        machine.proc.wait.assert_called_once()
        machine.q.s.close.assert_called_once()
        machine.log.close.assert_called_once()
        machine.runtime.cleanup.assert_called_once()


class SetupTests(unittest.TestCase):
    def test_dos_actions_only_accept_known_prompts(self):
        self.assertEqual(install.dos_action('Bienvenue à Install.'), ('enter', 'Bienvenue à Install.'))
        self.assertEqual(install.dos_action('Insérez la disquette dans le lecteur A:\nDisquette 2'), ('floppy', 2))
        self.assertEqual(install.dos_action('Retirez les disquettes de tous vos lecteurs'), ('done', None))
        self.assertIsNone(install.dos_action('Veuillez patienter. 47% effectuée'))
        self.assertIsNone(install.dos_action('Attention : un fichier est déjà installé'))
        self.assertIsNone(install.dos_action('Insérez la disquette dans le lecteur\nDisquette 8'))

    def test_braille_is_configured_unless_refused(self):
        args = ['--dos', 'd', '--windows', 'w']
        self.assertTrue(install.parser().parse_args(args).braille)
        self.assertFalse(install.parser().parse_args(args + ['--sans-braille']).braille)
        self.assertTrue(install.parser().parse_args(args + ['--braille']).braille)

    def test_user_names_cannot_inject_setup_sections(self):
        for value in ('a\n[endinstall]', 'x"\r\n[options]', 'a;b', '\0'):
            with self.assertRaises(argparse.ArgumentTypeError):
                install.user_text(value)
        self.assertEqual(install.user_text('Équipe'), 'Équipe')
        text = install.answer_file('Équipe', '')
        self.assertIn('"Équipe"\n""', text)
        self.assertIn('network=nonet', text)
        self.assertIn('mouse=ps2mouse', text)
        self.assertNotIn('tutorial', text)
        self.assertNotIn('Microsoft Corporation', text)


class ReferenceProfileTests(unittest.TestCase):
    def test_boot_files_follow_the_reference_templates(self):
        config, autoexec = install.final_boot_files()
        self.assertEqual(config, (ROOT / 'dos/CONFIG.SYS').read_bytes().replace(b'\n', b'\r\n'))
        self.assertEqual(autoexec, (ROOT / 'dos/AUTOEXEC.BAT').read_bytes().replace(b'\n', b'\r\n'))
        self.assertIn(b':DOS\r\nC:\\EPAUDOS.COM\r\n', autoexec)
        self.assertIn(b'MENUDEFAULT=DOS,10', config)
        self.assertNotIn(b'SNL', autoexec)
        self.assertNotIn(b'SONOLECT', autoexec.upper())
        self.assertNotIn(b'\r\r', autoexec)

    def test_sonolect_option_is_gone(self):
        with self.assertRaises(SystemExit), patch('sys.stderr'):
            install.parser().parse_args(['--dos', 'd', '--windows', 'w', '--sonolect', 's'])

    def test_supplemental_setup_answers_only_known_prompts(self):
        self.assertEqual(install.suppl_action('Which components?\nAll, Selected, Exit [A,S,X]?'), ('key', 'a'))
        menu = 'Enter your display type by pressing the appropriate function key.\nF9  -  8514\nF10 -  Quit'
        self.assertEqual(install.suppl_action(menu), ('key', 'f5'))
        self.assertEqual(install.suppl_action('You selected "VGA". Is this correct?\nYes, No [Y,N]?'),
                         ('key', 'y'))
        self.assertEqual(install.suppl_action('You selected "CGA". Is this correct?\nYes, No [Y,N]?'), None)
        self.assertEqual(install.suppl_action('Do you want to create it?\nYes, Exit [Y,X]?'), None)
        self.assertEqual(install.suppl_action('There is already a file named X in C:\\DOS.\n'
                                              'Do you want to overwrite it?  Yes, No [Y,N]?'), ('key', 'n'))
        self.assertEqual(install.suppl_action(install.SUPPL_MARKER), ('done', None))

    def test_word_dos_adds_only_its_directory_to_the_path(self):
        config, autoexec = install.final_boot_files(True)
        self.assertIn(b'PATH C:\\WINDOWS;C:\\DOS;C:\\WORD\r\n', autoexec)
        self.assertEqual(config, install.final_boot_files()[0])
        self.assertEqual(autoexec.replace(b';C:\\WORD', b''), install.final_boot_files()[1])

    def test_word_dos_screens_choose_text_mode_and_never_touch_boot_files(self):
        self.assertEqual(install.word_dos_action('BIENVENUE AU PROGRAMME INIT DE MICROSOFT WORD 6.0.'), 'ret')
        self.assertEqual(install.word_dos_action('Exécuter Word en mode Texte\nExécuter Word en mode Graphique'),
                         'Exécuter Word en mode Texte')
        self.assertEqual(install.word_dos_action('Mettre à jour les fichiers système\n'
                                                 'Ne pas mettre à jour les fichiers système'),
                         'Ne pas mettre à jour les fichiers système')
        # L'écran de confirmation du nom contient aussi le nom saisi : pas de seconde saisie.
        self.assertEqual(install.word_dos_action('Voulez-vous changer le nom UTILISATEUR ?\n'
                                                 'Ne pas changer le nom\nChanger le nom'), 'Ne pas changer le nom')
        self.assertIsNone(install.word_dos_action('Copie de C:\\WORD\\WORD.EXE'))

    def test_word_dos_screen_quoting_another_question_uses_the_listed_choice(self):
        # Écran réel d'INIT (28 octobre 2026) : il cite la question de la souris.
        screen = ("Vous avez choisi qu'INIT ne modifie pas vos fichiers de\n"
                  "configuration. Afin que Word fonctionne correctement,\n"
                  "Si vous avez choisi de mettre à jour le gestionnaire de souris\n"
                  "Microsoft, il peut ne pas être installé correctement.\n"
                  "INIT peut enregistrer les changements effectués sur les fichiers\n"
                  "┌──────────────────────────────────────────┐\n"
                  "│  Enregistrer les fichiers système        │\n"
                  "│  Ne pas enregistrer les fichiers système │\n"
                  "└──────────────────────────────────────────┘")
        listed = ['Enregistrer les fichiers système', 'Ne pas enregistrer les fichiers système']
        self.assertEqual(install.word_dos_action(screen, listed), 'Ne pas enregistrer les fichiers système')
        # Sans liste encore dessinée, l'ordre de la table s'applique toujours.
        self.assertEqual(install.word_dos_action(screen), 'Ne pas mettre à jour le gestionnaire de souris Microsoft')

    def test_word_dos_media_requests_are_told_from_information(self):
        # Textes de WORD60.INF : demande coupée par le cadre sur deux lignes.
        with self.assertRaisesRegex(install.InstallError, 'demande une disquette'):
            install.word_dos_action('│ Insérez la disquette Programme          │\n'
                                    "│ dans l'unité A:, puis appuyez sur ENTREE. │")
        # Écrans d'information : un mot seul ne suffit pas.
        for screen in ("Si vous insérez un caractère spécial, choisissez Ne pas tester l'affichage",
                       "INIT suppose que vous démarrez votre système à partir de l'unité de C:",
                       'Les fichiers peuvent aussi être copiés depuis le lecteur A: ou B:.',
                       'Insérez vos initiales dans le document.',
                       'Décompression...\nCopie de C:\\WORD\\SCREEN.VID'):
            install.word_dos_action(screen)
        self.assertEqual(install.word_dos_action("Si vous insérez un caractère spécial…\n"
                                                 "Ne pas tester l'affichage du caractère"),
                         "Ne pas tester l'affichage du caractère")
        self.assertIsNone(install.word_dos_action('Décompression...'))
        for error in ('INIT a des problèmes pour lire les fichiers sur la disquette.',
                      "INIT s'est interrompu. Microsoft Word n'est pas installé",
                      "Erreur d'accès au fichier source.",
                      "Pas assez d'espace disque pour décompresser le fichier."):
            with self.assertRaisesRegex(install.InstallError, 'erreur'):
                install.word_dos_action(error)

    @staticmethod
    def video(rows):
        """Mémoire texte 80x25 : (texte, attribut) par ligne."""
        data = bytearray(b'\x20\x17' * 2000)
        for r, (text, attr) in enumerate(rows):
            for c, ch in enumerate(text.encode('cp850')):
                data[(r * 80 + c) * 2] = ch
                data[(r * 80 + c) * 2 + 1] = attr
        return bytes(data)

    def test_word_dos_menu_follows_the_highlight(self):
        video = self.video([('   Sélectionnez une option dans la liste suivante:', 0x17),
                            ('     ┌────────────────────────────┐', 0x70),
                            ('     │  ■  IBM PC XT, AT          │', 0x0f),
                            ('     │     Tandy Modèle 1000      ░', 0x70),
                            ('     │     Gestionnaire de clavier pour résidents     \x19', 0x70),
                            ('     └────────────────────────────┘', 0x70)])
        items = install.menu_rows(video)
        self.assertEqual(items, [('IBM PC XT, AT', True), ('Tandy Modèle 1000', False),
                                 ('Gestionnaire de clavier pour résidents', False)])
        self.assertEqual(install.menu_key(items, 'Gestionnaire de clavier pour résidents'), 'down')
        self.assertEqual(install.menu_key(items, 'IBM PC XT, AT'), 'ret')
        self.assertIsNone(install.menu_key([(t, False) for t, _ in items], 'IBM PC XT, AT'))
        self.assertIsNone(install.menu_key(items, 'Exécuter Word en mode Graphique'))

    LIST = ('     ┌────────────────────────────┐', 0x70)
    END = ('     └────────────────────────────┘', 0x70)

    def rows_with(self, *rows):
        video = bytearray(self.video([('   Sélectionnez une option :', 0x0f), self.LIST,
                                      *[(f'     │     {t:<23}│', 0x70) for t, _ in rows], self.END]))
        for r, (text, attr) in enumerate(rows, 2):
            for c in range(len(text)):
                video[(r * 80 + 11 + c) * 2 + 1] = attr
            if attr != 0x70:            # barre : jusqu'au cadre, pas seulement les lettres
                for c in range(6, 34):
                    video[(r * 80 + c) * 2 + 1] = attr
        return bytes(video)

    def test_word_dos_highlight_is_a_bar_not_a_colour(self):
        # Titre blanc brillant au-dessus du cadre : jamais un élément.
        items = install.menu_rows(self.rows_with(('Un', 0x70), ('Deux', 0x1f), ('Trois', 0x70)))
        self.assertEqual(items, [('Un', False), ('Deux', True), ('Trois', False)])
        self.assertEqual(install.menu_key(items, 'Trois'), 'down')
        self.assertEqual(install.menu_key(items, 'Un'), 'up')
        # Lettres blanc brillant sans barre : pas une surbrillance.
        video = bytearray(self.rows_with(('Un', 0x70), ('Deux', 0x70), ('Trois', 0x70)))
        for c in range(11, 15):
            video[(3 * 80 + c) * 2 + 1] = 0x0f
        items = install.menu_rows(bytes(video))
        self.assertEqual(items, [('Un', False), ('Deux', False), ('Trois', False)])
        self.assertIsNone(install.menu_key(items, 'Trois'))
        # Deux barres : ambigu, aucune touche.
        items = install.menu_rows(self.rows_with(('Un', 0x0f), ('Deux', 0x70), ('Trois', 0x0f)))
        self.assertIsNone(install.menu_current(items))
        self.assertIsNone(install.menu_key(items, 'Deux'))
        # Aucun cadre : aucune liste.
        self.assertEqual(install.menu_rows(self.video([('  Un', 0x0f), ('  Deux', 0x70)])), [])

    def test_word_dos_menu_walk_checks_each_move(self):
        names = ('Un', 'Deux', 'Trois', 'Quatre')

        def at(i):
            return [(t, j == i) for j, t in enumerate(names)]
        walk = install.MenuWalk()
        self.assertTrue(walk.ready('Trois', at(0), 'down', 0))
        self.assertFalse(walk.ready('Trois', at(0), 'down', 1))      # pas encore redessiné
        self.assertTrue(walk.ready('Trois', at(1), 'down', 1.5))
        self.assertTrue(walk.ready('Trois', at(2), 'ret', 2))
        # Écran suivant, même libellé (« France » deux fois) : compteurs remis à zéro.
        for i in range(3):
            self.assertTrue(walk.ready('Trois', at(i), 'down', 3 + i))
        walk = install.MenuWalk()
        walk.ready('Quatre', at(0), 'down', 0)
        with self.assertRaisesRegex(install.InstallError, 'ne suit pas'):
            walk.ready('Quatre', at(0), 'down', 0 + install.MenuWalk.SETTLE)
        walk = install.MenuWalk()
        walk.ready('Quatre', at(1), 'down', 0)
        with self.assertRaisesRegex(install.InstallError, 'au lieu de'):
            walk.ready('Quatre', at(0), 'down', 1)                   # recul inattendu
        # Liste qui défile : la barre reste au même rang, le libellé suit.
        walk = install.MenuWalk()
        walk.ready('Trois', [('Un', False), ('Deux', True), ('Trois', False)], 'down', 0)
        self.assertTrue(walk.ready('Trois', [('Deux', False), ('Trois', True), ('Quatre', False)],
                                   'ret', 1))
        # Alternance : bornée par la taille de la liste.
        walk = install.MenuWalk()
        with self.assertRaisesRegex(install.InstallError, 'jamais atteint'):
            for step in range(10):
                i = step % 2
                walk.ready('Quatre', at(i), 'down' if i == 0 else 'up', step)

    def test_word_dos_owner_name_is_typed_without_accents(self):
        self.assertEqual(install.ascii_name('Hélène Crès'), 'Helene Cres')
        self.assertEqual(install.ascii_name('ßßß'), 'Utilisateur')

    @unittest.skipUnless(shutil.which('mformat') and shutil.which('mlabel'), 'mtools absent')
    def test_word_dos_disks_are_recognised_by_volume_label(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            source = p / 'WORD6'
            source.mkdir()
            for label in install.WORD_DOS_VOLUMES + ('GRAMMAR_60',):
                image = source / f'{label.lower()}.ima'
                image.write_bytes(b'\0' * 1228800)
                subprocess.run(['mformat', '-f', '1200', '-v', label, '-i', image, '::'], check=True)
            media = install.collect_word_dos(source, p / 'copie')
            self.assertEqual([install.volume_label(m) for m in media], list(install.WORD_DOS_VOLUMES))
            with self.assertRaisesRegex(install.InstallError, 'vide'):
                install.validate_word_dos(media)
            for m in media:
                self.mput(m, 'LISEZMOI.TXT', b'x')
            for name in ('INIT.EXE', 'INIT1.EXE', 'WORD60.INF'):
                with self.assertRaisesRegex(install.InstallError, name.replace('.', r'\.')):
                    install.validate_word_dos(media)
                self.mput(media[0], name, b'MZ')
            with self.assertRaisesRegex(install.InstallError, r'WORD\.EX\$'):
                install.validate_word_dos(media)
            self.mput(media[1], 'WORD.EX$', b'SZDD')
            install.validate_word_dos(media)
            (source / 'prog_60.ima').unlink()
            with self.assertRaisesRegex(install.InstallError, 'PROG_60'):
                install.collect_word_dos(source, p / 'incomplet')

    @staticmethod
    def mput(image, name, data, directory=None):
        with tempfile.TemporaryDirectory() as d:
            local = Path(d) / name.replace('$', '_')
            local.write_bytes(data)
            if directory:
                subprocess.run(['mmd', '-D', 's', '-i', image, '::/' + directory],
                               capture_output=True)
            target = f'::/{directory}/{name}' if directory else '::/' + name
            subprocess.run(['mcopy', '-o', '-i', image, local, target], check=True)

    @staticmethod
    def floppy(path, label):
        path.write_bytes(b'\0' * 1228800)
        subprocess.run(['mformat', '-f', '1200', '-v', label, '-i', path, '::'], check=True)
        return path

    @unittest.skipUnless(shutil.which('mformat') and shutil.which('mcopy'), 'mtools absent')
    def test_word_dos_staging_keeps_subdirectories_and_checks_the_copy(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            disk = p / 'c.img'
            disk.write_bytes(b'\0' * 1474560)
            subprocess.run(['mformat', '-f', '1440', '-i', disk, '::'], check=True)
            media = [self.floppy(p / f'{v}.ima', v) for v in install.WORD_DOS_VOLUMES[:2]]
            self.mput(media[0], 'INIT.EXE', b'MZinit')
            self.mput(media[0], 'COMMUN.DAT', b'meme')
            self.mput(media[1], 'COMMUN.DAT', b'meme')               # doublon identique : admis
            self.mput(media[1], 'WORD.EX$', b'SZDDword')
            self.mput(media[1], 'PILOTE.PRD', b'sous-dossier', 'IMPR')
            stage = p / 'stage'
            stage.mkdir()
            install.stage_word_dos(stage, str(disk), media)
            listing = subprocess.run(['mdir', '-/', '-b', '-i', disk, '::/INSTALL/WORDDOS'],
                                     capture_output=True, text=True, check=True).stdout
            for name in ('INIT.EXE', 'COMMUN.DAT', 'WORD.EX$', 'IMPR/PILOTE.PRD'):
                self.assertIn('::/INSTALL/WORDDOS/' + name, listing)
            got = subprocess.run(['mtype', '-i', disk, '::/INSTALL/WORDDOS/IMPR/PILOTE.PRD'],
                                 capture_output=True, check=True).stdout
            self.assertEqual(got, b'sous-dossier')

            # Homonymes différents sur deux disquettes : refus, quel que soit le dossier.
            self.mput(media[1], 'COMMUN.DAT', b'autre')
            stage2 = p / 'stage2'
            stage2.mkdir()
            with self.assertRaisesRegex(install.InstallError, 'homonymes'):
                install.stage_word_dos(stage2, str(disk), media)

    @unittest.skipUnless(shutil.which('mformat') and shutil.which('mcopy'), 'mtools absent')
    def test_word_dos_staging_detects_an_incomplete_copy(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            disk = p / 'c.img'
            disk.write_bytes(b'\0' * 1474560)
            subprocess.run(['mformat', '-f', '1440', '-i', disk, '::'], check=True)
            media = [self.floppy(p / 'setup.ima', 'SETUP_60')]
            self.mput(media[0], 'INIT.EXE', b'MZ')
            self.mput(media[0], 'INIT1.EXE', b'MZ')
            stage = p / 'stage'
            stage.mkdir()
            real = install.run

            def lossy(*args):
                # Copie vers C: qui perd silencieusement son dernier fichier.
                if args[0] == 'mcopy' and str(args[-1]).endswith('WORDDOS/') and len(args) > 6:
                    args = args[:-2] + args[-1:]
                return real(*args)
            with patch.object(install, 'run', lossy):
                with self.assertRaisesRegex(install.InstallError, 'absents'):
                    install.stage_word_dos(stage, str(disk), media)

    @unittest.skipUnless(shutil.which('mformat') and shutil.which('mlabel'), 'mtools absent')
    def test_word_dos_zip_is_closed_after_collection(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            archive = p / 'word.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                for v in install.WORD_DOS_VOLUMES:
                    z.write(self.floppy(p / f'{v}.ima', v), f'WORD6/{v}.IMA')
            media = install.collect_word_dos(archive, p / 'copie')
            self.assertEqual(len(media), len(install.WORD_DOS_VOLUMES))
            opened = [os.readlink(f) for f in Path('/proc/self/fd').iterdir()
                      if os.path.exists(f) and os.path.islink(f)]
            self.assertNotIn(str(archive), opened)

    def test_word_dos_owner_parameter_is_not_reused_by_the_final_check(self):
        source = Path(install.__file__).read_text(encoding='utf-8')
        body = source[source.index('def install_word_dos'):source.index('def configure_groups')]
        self.assertNotIn('for name in', body)

    def test_installer_and_build_share_one_load_editor(self):
        epaulard = install.epaulard_ini()
        program = epaulard.PROGRAM
        text = ('[windows]\r\nload=clock.exe C:\\INSTAUTO.EXE "c:/epaulard/epaulard.exe"\r\n'
                'load=calc.exe\r\nrun=notepad.exe\r\n[Word]\r\nload=autre.exe\r\n')
        once = epaulard.configure(text)
        self.assertEqual(once, epaulard.configure(once))             # idempotent
        self.assertEqual(once.count('EPAULARD.EXE') + once.count('epaulard.exe'), 1)
        self.assertIn('load=clock.exe C:\\INSTAUTO.EXE "c:/epaulard/epaulard.exe" calc.exe\r\n', once)
        self.assertIn('run=notepad.exe\r\n[Word]\r\nload=autre.exe\r\n', once)
        cleaned = epaulard.edit_load(once, remove=(r'C:\INSTAUTO.EXE',))
        self.assertIn('load=clock.exe "c:/epaulard/epaulard.exe" calc.exe\r\n', cleaned)
        self.assertEqual(cleaned, epaulard.edit_load(cleaned, remove=(r'C:\INSTAUTO.EXE',)))
        # Rien à retirer ni à ajouter : fichier inchangé, aucune section créée.
        self.assertEqual(epaulard.edit_load('[other]\nx=y\n', remove=('a.exe',)), '[other]\nx=y\n')
        fresh = epaulard.configure('[windows]\nload=\n')
        self.assertEqual(fresh, f'[windows]\nload={program}\n')
        # L'installateur n'a plus sa propre écriture de load= pour Épaulard.
        source = Path(install.__file__).read_text(encoding='utf-8')
        self.assertNotIn("'windows', 'load', r'C:\\EPAULARD", source)
        self.assertIn('epaulard_ini().configure(text)', source)

    def test_epaulard_settings_are_versioned(self):
        text = (ROOT / 'dos/EPAULARD.INI').read_text()
        self.assertIn('[Parole]', text)
        self.assertIn('Debit=5', text)

    def test_sound_blaster_sections_match_the_reference(self):
        text = install.configure_sound('[drivers]\r\ntimer=timer.drv\r\n')
        for line in ('wave=sndblst2.drv', 'midi=msadlib.drv'):
            self.assertIn(line, text)
        # SNDBLST2.DRV lit [sndblst.drv] : sans elle, IRQ 7 et boîte d'erreur au démarrage.
        for section in ('[sndblst.drv]', '[sndblst2.drv]'):
            block = text.split(section, 1)[1].split('[', 1)[0]
            self.assertIn('port=220', block)
            self.assertIn('int=5', block)

    def test_word_helper_section_is_removed_completely(self):
        text = '[windows]\r\nload=C:\\INSTAUTO.EXE\r\n[pc486]\r\ncommande=x\r\netat=fini\r\n[ports]\r\nCOM1:=9600\r\n'
        out = install.remove_section(text, 'pc486')
        self.assertNotIn('pc486', out)
        self.assertNotIn('etat=', out)
        self.assertIn('[ports]\r\nCOM1:=9600\r\n', out)


if __name__ == '__main__':
    unittest.main()

class OfficeMediaTests(unittest.TestCase):
    def test_excel_form_is_chosen_from_the_media(self):
        """5.0c (1,2 Mo) et 5.0 (1,44 Mo) : la forme qui se valide est retenue."""
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            source = p / 'excel'
            source.mkdir()
            for n in range(1, 11):
                (source / f'DISK{n}.IMG').write_bytes(b'x' * install.FLOPPY_SIZE)
            seen = []
            with patch.object(install, 'validate_acme', side_effect=lambda prod, media: seen.append(prod.key)):
                product, media = install.collect_acme(source, p / 'copie', ('excel', 'excel50'))
            self.assertEqual(product.key, 'excel50')
            self.assertEqual(len(media), 10)
            self.assertEqual(seen, ['excel50'])      # 5.0c refusé dès la taille des images
            self.assertFalse((p / 'copie/excel').exists())

    def test_unknown_office_media_lists_every_form(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p / 'vide').mkdir()
            with self.assertRaises(install.InstallError) as e:
                install.collect_acme(p / 'vide', p / 'copie', ('excel', 'excel50'))
            self.assertIn('Excel 5.0c', str(e.exception))
            self.assertIn('Excel 5.0 :', str(e.exception))

    def test_access_y2k_dlls_come_from_the_self_extracting_zip(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            inner = p / 'inner.zip'
            with zipfile.ZipFile(inner, 'w') as z:
                z.writestr('MSABC200.dll', b'MZabc')
                z.writestr('Msaju200.dll', b'MZaju')
                z.writestr('../../evil.dll', b'MZ')
            outer = p / 'acc2date.zip'
            with zipfile.ZipFile(outer, 'w') as z:
                z.writestr('acc2date/ACC2K/ACC2DATE.EXE', b'MZstub' + inner.read_bytes())
            dlls = install.collect_access_y2k(outer, p / 'copie')
            self.assertEqual(sorted(x.name for x in dlls), ['MSABC200.DLL', 'MSAJU200.DLL'])
            self.assertEqual(sorted(x.name for x in (p / 'copie').iterdir()), ['MSABC200.DLL', 'MSAJU200.DLL'])
            self.assertEqual((p / 'copie/MSAJU200.DLL').read_bytes(), b'MZaju')

    def test_netscape_folder_only_short_names_from_its_directory(self):
        """Netscape : seul le dossier N16FR31 du ZIP, fichiers aux noms DOS ; validé par
        SETUP.INI français et SETUP.EXE."""
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            source = p / 'ns.zip'
            with zipfile.ZipFile(source, 'w') as z:
                z.writestr('N16FR31/SETUP.EXE', b'MZsetup')
                z.writestr('N16FR31/SETUP.INI', b'[Intl]\r\nResource Dll Name=nsfrfr16.dll\r\n')
                z.writestr('N16FR31/data.z', b'donnees')
                z.writestr('N16FR31/nom beaucoup trop long.txt', b'x')
                z.writestr('N16FR31/SOUS/AUTRE.EXE', b'MZ')
                z.writestr('N16FR31/../EVIL.EXE', b'MZ')
                z.writestr('win3xorg.txt', b'x')
            product, media = install.collect_acme(source, p / 'copie', ('netscape',))
            self.assertEqual(product.key, 'netscape')
            self.assertEqual(sorted(x.name for x in media[0].iterdir()), ['DATA.Z', 'SETUP.EXE', 'SETUP.INI'])
            self.assertFalse((p / 'EVIL.EXE').exists())
            # version anglaise (autre DLL de ressources) : refusée
            other = p / 'en.zip'
            with zipfile.ZipFile(other, 'w') as z:
                z.writestr('N16FR31/SETUP.EXE', b'MZsetup')
                z.writestr('N16FR31/SETUP.INI', b'[Intl]\r\n')
            with self.assertRaises(install.InstallError):
                install.collect_acme(other, p / 'copie2', ('netscape',))

    def test_netscape_settings_point_to_the_host_services(self):
        with tempfile.TemporaryDirectory() as d:
            stage = Path(d)
            ini = ('[Main]\r\nHome Page=http://home.fr.netscape.com/fr/\r\n\r\n[Proxy Information]\r\n'
                   'Http_ProxyPort=0\r\nHTTP_Proxy=\r\n').encode('cp1252')
            writes = {}

            def fake_run(*args):
                if args[0] == 'mtype':
                    return ini
                if args[0] == 'mcopy':
                    writes[args[-1]] = Path(args[-2]).read_bytes()
                    return b''
                raise AssertionError(args)

            with patch.object(install, 'run', side_effect=fake_run):
                install.configure_product(stage, 'disk@@32256', install.ACME['netscape'], 'Jérôme')
            text = writes['::/NETSCAPE/NETSCAPE.INI'].decode('cp1252')
            for line in ('Home Page=http://pc486/', 'Proxy Type=1', 'HTTP_Proxy=10.0.2.2',
                         'Http_ProxyPort=8080', 'SMTP_Server=10.0.2.2', 'POP_Server=10.0.2.2',
                         'User_Name=Jérôme', 'Warn Insecure Forms=no'):
                self.assertIn(line + '\r\n', text)
            self.assertNotIn('home.fr.netscape.com', text)
            bookmarks = writes['::/NETSCAPE/BOOKMARK.HTM'].decode('cp1252')
            self.assertIn('<A HREF="http://pc486/">', bookmarks)
            self.assertIn('\r\n', bookmarks)

    def test_netscape_cfg_matches_the_file_shipped_with_communicator(self):
        """Le NETSCAPE.CFG vide de Communicator 4.08 est reproduit octet pour octet :
        même décalage, même clé, même empreinte."""
        shipped = bytes((b + 7) & 255 for b in
                        b'// 2d 83 59 03 a4 cc 03 1f 64 3a c9 90 8b 62 f5 ca\n\r')
        self.assertEqual(install.netscape_cfg([]), shipped)

    def test_communicator_defaults_point_to_the_host_services(self):
        cfg = install.communicator_cfg()
        text = bytes((b - 7) & 255 for b in cfg)
        head, rest = text[:51], text[51:]
        digest = hashlib.md5(install.NETSCAPE_CFG_KEY + rest).hexdigest()
        self.assertEqual(head.decode()[3:-1].replace(' ', ''), digest)
        rest = rest.decode('cp1252')
        for line in ('PrefConfig.pref("network.proxy.type", 1);',
                     'PrefConfig.pref("network.proxy.http", "10.0.2.2");',
                     'PrefConfig.pref("network.proxy.http_port", 8080);',
                     'PrefConfig.pref("browser.startup.homepage", "http://pc486/");',
                     'PrefConfig.pref("network.hosts.pop_server", "10.0.2.2");',
                     'PrefConfig.pref("security.warn_submit_insecure", false);'):
            self.assertIn(line + '\r\n', rest)
        self.assertNotIn('lockPref', rest)          # l'utilisateur garde la main
        files = dict(install.ACME['communicator'].files)
        self.assertEqual(files['NETSCAPE/COMM/PROGRAM/NETSCAPE.CFG'], cfg)

    def test_communicator_answers_follow_its_boxes(self):
        titles = [t for t, _ in install.ACME['communicator'].answers]
        self.assertEqual(titles.count('Question'), 2)
        self.assertEqual(dict(install.ACME['communicator'].answers)['Redémarrer Windows'],
                         '{BAS}{ENTREE}')                       # plus tard : INSTAUTO quitte
        second = [a for t, a in install.ACME['communicator'].answers if t == 'Question'][1]
        self.assertEqual(second, '{TAB}{ENTREE}')               # LISEZMOI : Non

    def test_eudora_self_extractor_comes_from_the_zip_root(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            source = p / 'eul306.zip'
            with zipfile.ZipFile(source, 'w') as z:
                z.writestr('eul306.exe', b'MZ...Eudora.ini...win16.z...')
                z.writestr('sous/autre.exe', b'MZ')
            product, media = install.collect_acme(source, p / 'copie', ('eudora',))
            self.assertEqual([x.name for x in media[0].iterdir()], ['EUL306.EXE'])
            # dossier local : la racine, jamais « / » du système
            local = p / 'local'
            local.mkdir()
            (local / 'eul306.exe').write_bytes(b'MZ...Eudora.ini...win16.z...')
            product, media = install.collect_acme(local, p / 'copie2', ('eudora',))
            self.assertEqual([x.name for x in media[0].iterdir()], ['EUL306.EXE'])

    def test_eudora_uses_the_local_mailbox(self):
        settings = {(k, v) for path, section, k, v in install.ACME['eudora'].settings}
        self.assertTrue({('POPAccount', 'pc486@10.0.2.2'), ('SMTPServer', '10.0.2.2'),
                         ('UseACAPServer', '0'), ('RealName', '{NOM}')} <= settings)
        self.assertEqual(install.ACME['eudora'].answers[-1], ('Question', '{TAB}{ENTREE}'))

    def test_y2k_fix_requires_access(self):
        args = install.parser().parse_args(['--dos', 'd', '--windows', 'w', '--access-an2000', 'x'])
        with self.assertRaises(install.InstallError):
            install.prerequisites(args)
