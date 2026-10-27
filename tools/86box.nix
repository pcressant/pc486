# 86Box de nixpkgs (avec ROM) + correctif pc486 : capture clavier Wayland fonctionnelle
# à l'ouverture et bascule par Ctrl+Alt+G. Construction :
#   nix build --impure -f tools/86box.nix -o tools/86box   (version d origine : tools/86box-orig)
{
  pkgs ? import (builtins.getFlake "nixpkgs") {
    system = builtins.currentSystem;
    config.allowUnfree = true; # ROM des cartes mères
  },
}:
pkgs._86box-with-roms.overrideAttrs (old: {
  patches = (old.patches or [ ]) ++ [ ./86box-pc486.patch ];
})
