# Utiliser pc486 avec Nix

Le flake prend en charge **Linux x86-64**. Activer les fonctionnalités Nix
`nix-command` et `flakes` si elles ne le sont pas déjà. Exécuter les commandes
ci-dessous depuis la racine du dépôt cloné ; `PC486_ROOT` permet de désigner cette
racine depuis un autre répertoire.

`flake.lock` fixe Nixpkgs et [Apolo-emu](https://github.com/pcressant/apolo-emu).
Apolo-emu utilise la même entrée Nixpkgs que pc486 : aucun dépôt voisin n'est requis.
Nix récupère les outils libres, Open Watcom et les ROM de 86Box ; la configuration
autorise les dépendances non libres nécessaires à ces ROM et au compilateur.
**Aucun support Microsoft ou Word n'est téléchargé ni inclus dans un
paquet Nix.** Les disques restent dans la copie de travail et hors du magasin Nix.

Le catalogue local des supports (`media/sources-medias.md` et son manifeste JSON,
non versionnés) consigne leurs empreintes et jeux de disquettes ; il prépare un
éventuel constructeur d'ISO privée. Aucune cible du flake n'en dépend.

## Développer

```sh
nix develop                  # équivalent : nix develop .#develop
python3 -m unittest discover -s tests -v
epaulard/build.sh
```

Le shell fournit Python, Bash, QEMU, mtools, socat, Open Watcom, 86Box corrigé,
Apolo-emu, la passerelle braille, Speech Dispatcher et eSpeak NG. Il définit
`WATCOM` et les chemins des outils de lancement sans modifier les fichiers locaux.
Pour les scripts qui utilisent directement les liens `tools/`, lancer d'abord
`prepare`.

## Préparer les outils et une installation

```sh
nix run .#prepare
```

Cette commande crée ou actualise les liens `tools/86box`, `tools/braille`,
`tools/socat`, `tools/watcom` et `tools/apolo-emu`, les enregistre comme racines du ramasse-miettes Nix,
puis compile Épaulard dans `epaulard/build/`. Un fichier ordinaire à la place d'un
lien provoque un refus. Elle ne démarre aucune VM et ne modifie aucun disque.
Elle peut être relancée ; elle n'installe pas les binaires compilés sur un C: existant.

Pour créer un **nouveau** C: à partir de supports locaux :

```sh
nix run .#prepare -- --dos media/msdos622 --windows media/wfw311 \
  --suppl media/msdos622/SUPPL.IMG \
  --output vm/win311.img --name 'Utilisateur'
```

`--suppl` est facultatif. Les arguments sont transmis à [l'installateur](installation-automatique.md) après
préparation des outils. Un disque existant n'est jamais remplacé. `--check`
vérifie les supports sans installer ; `--help` affiche les options sans préparer.
L'installation se déroule dans une VM QEMU privée, sans D: partagé.

## Démarrer et contrôler

```sh
nix run .#run                         # 86Box par défaut
nix run .#run -- --qemu               # QEMU, notamment après une installation neuve
nix run .#run -- status
nix run .#run -- gateway restart
nix run .#run -- stop                 # après fermeture de Windows et retour au DOS
```

`nix run` est un alias de `nix run .#run`. Sans commande, les options sont celles
de `scripts/pc486 start`. Les variables `PC486_*` habituelles restent disponibles,
notamment pour les copies de développement et la synthèse simulée. `run` fournit
les dépendances sans exiger les liens créés par `prepare`.

L'hôte doit disposer d'une session audio et d'un serveur Speech Dispatcher
fonctionnels. Le braille utilise le BRLTTY de l'hôte, avec les permissions BrlAPI
et la session graphique appropriées ; consulter [le guide braille](braille.md).
Le flake ne configure pas les services système.

Une installation neuve crée uniquement C:. Démarrer d'abord avec `--qemu`, qui
accepte l'absence de D:. Le modèle 86Box versionné décrit la machine de référence,
avec D: `vm/transfert.img` ; `pc486` ne l'attache que si l'image existe, et l'état
CMOS conservé localement doit alors aussi en être privé : voir le README. Ne pas lancer deux émulateurs sur le
même disque inscriptible.

## Construire, vérifier et actualiser

```sh
nix build .#epaulard          # result/share/epaulard/{EPAULARD.EXE,EPAUHOOK.DLL}
nix flake check              # compilation Win16, applications et régressions sans supports
nix fmt
nix flake update apolo-emu    # actualisation volontaire du verrou Apolo-emu
```

Les régressions utilisent les bancs Win16, une synthèse simulée, un faux 86Box
et un serveur BRLTTY privé avec une plage virtuelle. Aucun accès à la plage physique.
Les cinq scénarios Windows restent explicites : ils nécessitent le disque local,
qui ne doit jamais entrer dans le magasin Nix. Ils apparaissent donc ignorés dans
`nix flake check`. Pour les lancer sur une copie jetable, VM principale arrêtée :

```sh
PC486_VM_TESTS=1 nix develop --command python3 -m unittest tests.test_vm_scenarios -v
```

La copie est `work/vmtest/win311.img` ; les échanges série, le braille et la synthèse
sont isolés. Le disque principal reste inchangé.

Après une mise à jour du verrou, relancer `prepare` pour actualiser les liens locaux. Conserver `flake.lock` dans les sources publiées.
