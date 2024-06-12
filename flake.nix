{
  description = "Hydra's builtin hydra-eval-jobs as a standalone";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  inputs.flake-parts.url = "github:hercules-ci/flake-parts";
  inputs.flake-parts.inputs.nixpkgs-lib.follows = "nixpkgs";
  inputs.treefmt-nix.url = "github:numtide/treefmt-nix";
  inputs.treefmt-nix.inputs.nixpkgs.follows = "nixpkgs";
  inputs.nix-github-actions.url = "github:nix-community/nix-github-actions";
  inputs.nix-github-actions.inputs.nixpkgs.follows = "nixpkgs";
  inputs.lix = {
    url = "git+https://git@git.lix.systems/lix-project/lix";
    inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs = inputs @ { flake-parts, ... }:
    let
      inherit (inputs.nixpkgs) lib;
      inherit (inputs) self;
    in
    flake-parts.lib.mkFlake { inherit inputs; }
      {
        systems = inputs.nixpkgs.lib.systems.flakeExposed;
        imports = [ inputs.treefmt-nix.flakeModule ];

        flake.githubActions = inputs.nix-github-actions.lib.mkGithubMatrix {
          checks = {
            inherit (self.checks) x86_64-linux;
            x86_64-darwin = builtins.removeAttrs self.checks.x86_64-darwin [ "treefmt" ];
          };
        };

        perSystem = { pkgs, self', inputs', ... }:
          let
            drvArgs = {
              srcDir = self;
              nix = inputs'.lix.packages.default;
            };
          in
          {
            treefmt.imports = [ ./dev/treefmt.nix ];
            packages.nix-eval-jobs = pkgs.callPackage ./default.nix drvArgs;
            packages.clangStdenv-nix-eval-jobs = pkgs.callPackage ./default.nix (drvArgs // { stdenv = pkgs.clangStdenv; });
            packages.default = self'.packages.nix-eval-jobs;
            devShells.default = pkgs.callPackage ./shell.nix drvArgs;

            checks = builtins.removeAttrs self'.packages [ "default" ] // {
              shell = self'.devShells.default;
            };
          };
      };
}
