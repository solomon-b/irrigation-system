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

    arduino-nix.url = github:bouk/arduino-nix;

    arduino-index = {
      url = "github:bouk/arduino-indexes";
      flake = false;
    };

    moore-arduino.url = github:solomon-b/moore-arduino;
  };

  outputs = {
      self,
      nixpkgs,
      flake-utils,
      hasql-interpolate-src,
      hasql-src,
      tmp-postgres-src,
      arduino-nix,
      arduino-index,
      moore-arduino
  }:
    flake-utils.lib.eachSystem [ "x86_64-linux" ]
      (system:
        let
          pkgs = import nixpkgs {
            inherit system;
            config.allowUnfree = true;
            overlays = [
              (arduino-nix.overlay)
              (arduino-nix.mkArduinoPackageOverlay (arduino-index + "/index/package_index.json"))
              (arduino-nix.mkArduinoLibraryOverlay (arduino-index + "/index/library_index.json"))
            ];
          };

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
              pkgs.graphviz
            ];
          };

          formatter = pkgs.nixpkgs-fmt;

          packages = flake-utils.lib.flattenTree rec {
            arduino-cli = pkgs.wrapArduinoCLI {
              libraries = [
                # Include MooreArduino library
                moore-arduino.packages.${system}.moore-arduino
                # HTTP client for API requests
                pkgs.arduinoLibraries.ArduinoHttpClient."0.6.1"
                # JSON parsing library  
                pkgs.arduinoLibraries.ArduinoJson."7.2.1"
              ];
              packages = with pkgs.arduinoPackages; [
                platforms.arduino.mbed_giga."4.2.4"
              ];
            };

            arduino-build = pkgs.writeShellScriptBin "build" ''
              SKETCH="''${1:-MySketch}"
              ${arduino-cli}/bin/arduino-cli compile --warnings all --fqbn arduino:mbed_giga:giga --libraries ${moore-arduino.packages.${system}.moore-arduino} $SKETCH
            '';

            arduino-upload = pkgs.writeShellScriptBin "upload" ''
              SKETCH="''${1:-controller}"
              PORT="''${2:-/dev/ttyACM0}"
              
              echo "Compiling sketch: $SKETCH"
              ${arduino-cli}/bin/arduino-cli compile --warnings all --fqbn arduino:mbed_giga:giga --libraries ${moore-arduino.packages.${system}.moore-arduino} $SKETCH
              
              if [ $? -eq 0 ]; then
                echo "Uploading to port: $PORT"
                ${arduino-cli}/bin/arduino-cli upload --fqbn arduino:mbed_giga:giga --port $PORT $SKETCH
              else
                echo "Compilation failed. Upload aborted."
                exit 1
              fi
            '';

            arduino-monitor = pkgs.writeShellScriptBin "monitor" ''
              PORT="''${1:-/dev/ttyACM0}"
              BAUD="''${2:-115200}"
              
              echo "Monitoring serial port: $PORT at $BAUD baud"
              echo "Press Ctrl+C to exit"
              ${arduino-cli}/bin/arduino-cli monitor --port $PORT --config baudrate=$BAUD
            '';

            generate-state-diagram = pkgs.writeShellScriptBin "generate-state-diagram" ''
              INPUT_FILE="''${1:-irrigation-controller-state-diagram.dot}"
              OUTPUT_FILE="''${2:-irrigation-controller-state-diagram.png}"
              
              if [ ! -f "$INPUT_FILE" ]; then
                echo "Error: Input file $INPUT_FILE not found"
                exit 1
              fi
              
              echo "Generating state diagram: $INPUT_FILE -> $OUTPUT_FILE"
              ${pkgs.graphviz}/bin/dot -Tpng "$INPUT_FILE" -o "$OUTPUT_FILE"
              
              if [ $? -eq 0 ]; then
                echo "State diagram generated successfully: $OUTPUT_FILE"
              else
                echo "Error generating state diagram"
                exit 1
              fi
            '';
            irrigation-web-server = hsPkgs.irrigation-web-server;
            default = hsPkgs.irrigation-web-server;
          };

          apps = {
            arduino-build = flake-utils.lib.mkApp { drv = self.packages.${system}.arduino-build; };
            arduino-upload = flake-utils.lib.mkApp { drv = self.packages.${system}.arduino-upload; };
            arduino-monitor = flake-utils.lib.mkApp { drv = self.packages.${system}.arduino-monitor; };
            generate-state-diagram = flake-utils.lib.mkApp { drv = self.packages.${system}.generate-state-diagram; };
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
