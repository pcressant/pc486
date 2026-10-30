{
  description = "pc486 : Épaulard, PC 486 accessible et outils de développement";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    apolo-emu = {
      url = "github:pcressant/apolo-emu";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      apolo-emu,
    }:
    let
      # Les binaires Win16 et les bancs Watcom sont validés sur Linux x86-64.
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        config.allowUnfree = true;
      };
      lib = pkgs.lib;
      watcom = pkgs.open-watcom-v2;
      box = import ./tools/86box.nix { inherit pkgs; };
      braille = import ./tools/braille.nix { inherit pkgs; };
      apolo = apolo-emu.packages.${system}.default;
      dependencies = with pkgs; [
        bash
        coreutils
        findutils
        gnugrep
        gnused
        procps
        util-linux
        python3
        qemu
        mtools
        socat
        glibc.bin
        speechd
        espeak-ng
        apolo
        braille
        box
      ];
      environment = ''
        export WATCOM=${watcom}
        export PC486_86BOX="''${PC486_86BOX:-${box}/bin/86Box}"
        export PC486_SOCAT="''${PC486_SOCAT:-${pkgs.socat}/bin/socat}"
        export PC486_BRAILLE_CMD="''${PC486_BRAILLE_CMD:-${braille}/bin/pc486-braille}"
      '';
      # Les applications opèrent sur une copie de travail, jamais dans /nix/store.
      checkout = ''
        root="''${PC486_ROOT:-$PWD}"
        if [[ ! -f "$root/epaulard/src/epaulard.c" || ! -f "$root/scripts/pc486" ]]; then
          echo "Se placer à la racine du dépôt pc486 ou définir PC486_ROOT." >&2
          exit 1
        fi
        root=$(realpath "$root")
        cd "$root"
      '';
      run = pkgs.writeShellApplication {
        name = "pc486-run";
        runtimeInputs = dependencies;
        text =
          environment
          + checkout
          + ''
            if [[ $# -eq 0 || "$1" == --* ]]; then
              set -- start "$@"
            fi
            exec bash "$root/scripts/pc486" "$@"
          '';
      };
      prepare = pkgs.writeShellApplication {
        name = "pc486-prepare";
        runtimeInputs = dependencies ++ [ pkgs.nix ];
        text =
          environment
          + checkout
          + ''
            if [[ "''${1:-}" == --help ]]; then
              echo 'Usage : nix run .#prepare [options de scripts/installer]'
              echo 'Sans option : prépare les outils et compile Épaulard, sans modifier les disques.'
              echo 'Avec options : prépare les outils puis lance une installation depuis les supports locaux.'
              exec bash "$root/scripts/installer" --help
            fi
            mkdir -p tools
            # Refuser tout fichier ordinaire avant de toucher aux liens existants.
            for name in 86box braille socat watcom apolo-emu; do
              if [[ -e "tools/$name" && ! -L "tools/$name" ]]; then
                echo "tools/$name existe et ne correspond pas à un lien ; préparation interrompue." >&2
                exit 1
              fi
            done
            nix-store --realise ${box} --add-root "$root/tools/86box" --indirect >/dev/null
            nix-store --realise ${braille} --add-root "$root/tools/braille" --indirect >/dev/null
            nix-store --realise ${pkgs.socat} --add-root "$root/tools/socat" --indirect >/dev/null
            nix-store --realise ${watcom} --add-root "$root/tools/watcom" --indirect >/dev/null
            nix-store --realise ${apolo} --add-root "$root/tools/apolo-emu" --indirect >/dev/null
            bash "$root/epaulard/build.sh"
            bash "$root/epaudos/build.sh"
            if [[ $# -gt 0 ]]; then
              exec bash "$root/scripts/installer" "$@"
            fi
            echo 'Outils et binaires prêts. Aucun disque modifié.'
            if [[ ! -f "$root/vm/win311.img" ]]; then
              echo 'Pour créer C:, relancer prepare avec --dos et --windows (voir --help).'
            fi
          '';
      };
      # ISO des supports décompressés : écrite dans la copie de travail, jamais dans
      # /nix/store (logiciels tiers, à garder locaux).
      iso = pkgs.writeShellApplication {
        name = "pc486-iso";
        runtimeInputs = with pkgs; [
          python3
          mtools
          xorriso
          _7zz
          unar
        ];
        text = checkout + ''
          exec python3 "$root/scripts/iso_supports.py" "$@"
        '';
      };
      epaulard = pkgs.stdenvNoCC.mkDerivation {
        pname = "epaulard";
        version = "0.1.0";
        # Liste explicite : aucun support Microsoft ni image locale dans le paquet.
        src = lib.fileset.toSource {
          root = ./.;
          fileset = lib.fileset.unions [
            ./epaulard/src
            ./epaulard/build.sh
            ./LICENSE
            ./LICENSES
          ];
        };
        nativeBuildInputs = [
          pkgs.bash
          pkgs.glibc.bin
        ];
        WATCOM = watcom;
        buildPhase = "bash epaulard/build.sh";
        installPhase = ''
          mkdir -p "$out/share/epaulard"
          cp epaulard/build/EPAULARD.EXE epaulard/build/EPAUHOOK.DLL "$out/share/epaulard/"
          mkdir -p "$out/share/licenses/epaulard"
          cp LICENSE LICENSES/* "$out/share/licenses/epaulard/"
        '';
        meta = {
          description = "Lecteur d’écran Windows 3.1, binaires Win16";
          license = lib.licenses.gpl3Plus;
          platforms = [ system ];
        };
      };
      epaudos = pkgs.stdenvNoCC.mkDerivation {
        pname = "epaudos";
        version = "0.1.0";
        src = lib.fileset.toSource {
          root = ./.;
          fileset = lib.fileset.unions [
            ./epaudos/src
            ./epaudos/build.sh
            ./epaulard/src/brfr.h
            ./LICENSE
            ./LICENSES
          ];
        };
        nativeBuildInputs = [
          pkgs.bash
          pkgs.glibc.bin
          pkgs.python3
        ];
        WATCOM = watcom;
        buildPhase = "bash epaudos/build.sh";
        installPhase = ''
          mkdir -p "$out/share/epaudos" "$out/share/licenses/epaudos"
          cp epaudos/build/EPAUDOS.COM "$out/share/epaudos/"
          cp LICENSE LICENSES/* "$out/share/licenses/epaudos/"
        '';
        meta = {
          description = "Épaudos, lecteur d'écran TSR pour MS-DOS";
          license = lib.licenses.gpl3Plus;
          platforms = [ system ];
        };
      };
      app = package: {
        type = "app";
        program = lib.getExe package;
        meta.description = package.meta.description or package.name;
      };
    in
    {
      packages.${system} = {
        inherit
          epaulard
          epaudos
          prepare
          run
          iso
          ;
        default = epaulard;
        "86box" = box;
        inherit braille;
        apolo-emu = apolo;
      };
      apps.${system} = {
        prepare = app prepare;
        run = app run;
        iso = app iso;
        default = app run;
      };
      devShells.${system} = {
        default = pkgs.mkShell {
          packages = dependencies ++ [
            watcom
            pkgs.brltty
            pkgs.git
            pkgs.nixfmt
          ];
          shellHook = environment + ''
            echo 'pc486 : outils disponibles ; nix run .#prepare pour compiler et préparer les liens locaux.'
          '';
        };
        develop = self.devShells.${system}.default;
      };
      checks.${system} = {
        inherit
          epaulard
          epaudos
          prepare
          run
          ;
        regression =
          pkgs.runCommand "pc486-regression"
            {
              nativeBuildInputs = dependencies ++ [
                pkgs.brltty
                pkgs.xorriso
              ];
              WATCOM = watcom;
              source = lib.fileset.toSource {
                root = ./.;
                fileset = lib.fileset.unions [
                  ./tests
                  ./scripts
                  ./epaulard/src
                  ./epaudos/src
                  ./epaudos/build.sh
                ];
              };
            }
            ''
              cp -r "$source" project
              chmod -R u+w project
              patchShebangs project/scripts
              cd project
              mkdir -p tools
              ln -s ${pkgs.socat} tools/socat
              ln -s ${braille} tools/braille
              export HOME="$TMPDIR"
              python3 -m unittest discover -s tests -v
              touch "$out"
            '';
      };
      formatter.${system} = pkgs.nixfmt;
    };
}
