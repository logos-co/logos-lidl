{
  description = "logos-lidl - the language-neutral LIDL frontend (lexer/parser/AST/serializer/validator); .lidl text is the cross-language interchange";

  inputs.logos-nix.url = "github:logos-co/logos-nix";
  inputs.nixpkgs.follows = "logos-nix/nixpkgs";

  outputs = { self, nixpkgs, logos-nix }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        inherit system;
        pkgs = import nixpkgs { inherit system; };
      });

      # Adds the "x86_64-windows" pseudo-system on top of the native ones. A
      # cross derivation's `system` attribute is its BUILD platform, so
      # `packages.x86_64-windows.*` evaluates anywhere but realises on Linux.
      forAllTargets = logos-nix.lib.forAllTargets;

      # Reported by `lidl --version`; a path: flake has no revision.
      gitRev = self.shortRev or self.dirtyShortRev or "unknown";

      mkLidl = { system, pkgs }:
        let
          # The test binary is a PE when cross-compiling, and
          # gtest_discover_tests RUNS it at build time to enumerate cases —
          # which the build host cannot do. Build the libraries only.
          isWindows = pkgs.stdenv.hostPlatform.isWindows;
        in
        pkgs.stdenv.mkDerivation {
          pname = "logos-lidl";
          version = "0.1.0";
          src = ./.;
          nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
          buildInputs = [ pkgs.nlohmann_json ]
            ++ pkgs.lib.optional (!isWindows) pkgs.gtest;
          # The Windows build has no CLI, so its hash stays independent of the revision.
          cmakeFlags = [ "-GNinja" ]
            ++ (if isWindows
                then [ "-DLOGOS_LIDL_BUILD_TESTS=OFF" "-DLOGOS_LIDL_BUILD_CLI=OFF" ]
                else [ "-DLOGOS_LIDL_GIT_REV=${gitRev}" ]);
          doCheck = !isWindows;
          checkPhase = ''
            runHook preCheck
            ctest --output-on-failure
            runHook postCheck
          '';
          meta.platforms = pkgs.lib.platforms.unix ++ pkgs.lib.platforms.windows;
        };

      # Just bin/lidl. Not named `lidl`: module flakes use packages.<sys>.lidl for a contract.
      mkLidlCli = { pkgs, lidl }:
        pkgs.runCommandLocal "lidl-cli-${lidl.version}"
          {
            meta = {
              description = "The lidl command-line tool (json, check, fmt)";
              mainProgram = "lidl";
            };
          }
          ''
            mkdir -p $out/bin
            ln -s ${lidl}/bin/lidl $out/bin/lidl
          '';
    in
    {
      packages = forAllTargets ({ system, pkgs }:
        let lidl = mkLidl { inherit system pkgs; };
        in
        {
          logos-lidl = lidl;
          default = lidl;
          tests = lidl;
        } // pkgs.lib.optionalAttrs (!pkgs.stdenv.hostPlatform.isWindows) {
          lidl-cli = mkLidlCli { inherit pkgs lidl; };
        }
      );

      # Native only: a Windows `check` would have to run a PE on the builder.
      checks = forAllSystems ({ system, pkgs }: {
        tests = self.packages.${system}.logos-lidl;
      });

      apps = forAllSystems ({ system, ... }:
        let
          lidl = {
            type = "app";
            program = "${self.packages.${system}.lidl-cli}/bin/lidl";
            meta.description = "The lidl command-line tool";
          };
        in
        {
          inherit lidl;
          default = lidl;
        });

      devShells = forAllSystems ({ pkgs, ... }: {
        default = pkgs.mkShell {
          nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
          buildInputs = [ pkgs.gtest pkgs.nlohmann_json ];
        };
      });
    };
}
