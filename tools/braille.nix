# nix build --impure -f tools/braille.nix -o tools/braille
{
  pkgs ? import (builtins.getFlake "nixpkgs") { system = builtins.currentSystem; },
}:
let
  brltty = pkgs.brltty.overrideAttrs (old: {
    buildInputs = (old.buildInputs or [ ]) ++ [ pkgs.polkit ];
  });
  source = pkgs.lib.cleanSourceWith {
    src = ../scripts;
    filter =
      path: type:
      builtins.baseNameOf path != "__pycache__" && (type == "directory" || pkgs.lib.hasSuffix ".py" path);
  };
  python = pkgs.python3.withPackages (ps: [
    ps.pyatspi
    ps.pygobject3
  ]);
in
pkgs.writeShellApplication {
  name = "pc486-braille";
  runtimeInputs = [ python ];
  text = ''
    export GI_TYPELIB_PATH="${pkgs.at-spi2-core}/lib/girepository-1.0:${pkgs.gobject-introspection}/lib/girepository-1.0:${pkgs.glib}/lib/girepository-1.0:''${GI_TYPELIB_PATH:-}"
    export PYTHONPATH="${source}:${brltty}/${pkgs.python3.sitePackages}:''${PYTHONPATH:-}"
    exec python3 -m braille.gateway "$@"
  '';
}
