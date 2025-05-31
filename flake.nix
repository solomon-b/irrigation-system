{
  description = "Irrigation Control System";

  inputs = {
    nixpkgs.url = github:NixOS/nixpkgs/nixpkgs-unstable;
    flake-utils.url = github:numtide/flake-utils;

    hasql-interpolate-src = {
      url = github:awkward-squad/hasql-interpolate;
      flake = false;
    };
    hasql-src = {
      url = github:JonathanLorimer/hasql/expose-hasql-encoders-params;
      flake = false;
    };

    tmp-postgres-src = {
      url = github:jfischoff/tmp-postgres;
      flake = false;
    };
  };

  outputs = { self, nixpkgs, flake-utils, hasql-interpolate-src, hasql-src, tmp-postgres-src }:
    flake-utils.lib.eachSystem [ "x86_64-linux" ]
      (system:
        let
          pkgs = import nixpkgs { inherit system; config.allowUnfree = true; };
          hsPkgs = pkgs.haskellPackages.override {
            overrides = hfinal: hprev: {
              hasql = pkgs.haskell.lib.dontCheck (pkgs.haskell.lib.dontCheck (hfinal.callHackageDirect
                {
                  pkg = "hasql";
                  ver = "1.9.1.2";
                  sha256 = "sha256-W2pAC3wLIizmbspWHeWDQqb5AROtwA8Ok+lfZtzTlQg=";
                }
                {}));

              hasql-pool = pkgs.haskell.lib.dontCheck (pkgs.haskell.lib.dontCheck (hfinal.callHackageDirect
                {
                  pkg = "hasql-pool";
                  ver = "1.3.0.2";
                  sha256 = "sha256-3tADBDSR7MErgVLzIZdivVqyU99/A7jsRV3qUS7wWns=";
                }
                { }));

              hasql-transaction = pkgs.haskell.lib.dontCheck (pkgs.haskell.lib.dontCheck (hfinal.callHackageDirect
                {
                  pkg = "hasql-transaction";
                  ver = "1.2.0.1";
                  sha256 = "sha256-gXLDMlD6E3degEUJOtFCiZf9EAsWEBJqsOfZK54iBSA=";
                }
                { }));

              irrigation-web-server = pkgs.haskell.lib.dontCheck (hfinal.callCabal2nix "irrigation-web-server" ./web-server { });

              web-server-core = pkgs.haskell.lib.dontCheck (hfinal.callCabal2nix "web-server-core"
                (pkgs.fetchFromGitHub
                  {
                    owner = "solomon-b";
                    repo = "web-server";
                    rev = "daeb48a31652bb1a3652aadc320abe689934347a";
                    sha256 = "sha256-yUT1NKytqh0sIX5Z2iDeUkf+2E3Ucc85lQ9IARRZlyQ=";
                  } + "/web-server-core")
                { });

              tmp-postgres = pkgs.haskell.lib.dontCheck (hfinal.callCabal2nix "tmp-postgres" tmp-postgres-src { });

              text-builder = pkgs.haskell.lib.dontCheck pkgs.haskellPackages.text-builder_1_0_0_3;
            };
          };
        in
        rec {
          devShell = hsPkgs.shellFor {
            packages = p: [ p.irrigation-web-server ];
            buildInputs = [
              pkgs.cabal-install
              pkgs.haskellPackages.ghc
              pkgs.haskellPackages.haskell-language-server
              pkgs.haskellPackages.hlint
              pkgs.just
              pkgs.nixpkgs-fmt
              pkgs.ormolu
              pkgs.openssl
              pkgs.postgresql
              pkgs.pkg-config
              pkgs.shellcheck
              pkgs.sqlx-cli
              pkgs.zlib
              pkgs.zlib.dev
            ];
          };

          formatter = pkgs.nixpkgs-fmt;

          packages = flake-utils.lib.flattenTree rec {
            irrigation-web-server = hsPkgs.irrigation-web-server;
            default = hsPkgs.irrigation-web-server;
          };

          apps = {
            irrigation-web-server = flake-utils.lib.mkApp { drv = self.packages.${system}.irrigation-web-server; };
            default = self.apps.${system}.irrigation-web-server;
          };
        }) // {
      nixosModules = {
        irrigation-web-server = import ./nixos-module.nix { inherit self; };
        default = self.nixosModules.irrigation-web-server;
      };
    };
}
