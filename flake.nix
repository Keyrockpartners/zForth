{
  description = "zForth for BlueStreak IoT devices, and the ZGo transpiler";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in {
      devShells = forAllSystems (pkgs:
        let
          # On macOS, binaries built with nixpkgs' clang and -fsanitize=address
          # hang at startup, so keep using Xcode's compiler there: mkShellNoCC
          # adds no C toolchain or SDK. On Linux, nixpkgs' gcc works.
          mkShell = if pkgs.stdenv.hostPlatform.isDarwin then pkgs.mkShellNoCC else pkgs.mkShell;
        in {
          default = mkShell {
            packages = with pkgs; [ go gopls gnumake ]
              ++ pkgs.lib.optional pkgs.stdenv.hostPlatform.isLinux readline;
          };
        });
    };
}
