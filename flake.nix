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
          cmakeFlags = [ "-GNinja" ]
            ++ pkgs.lib.optional isWindows "-DLOGOS_LIDL_BUILD_TESTS=OFF";
          doCheck = !isWindows;
          checkPhase = ''
            runHook preCheck
            ctest --output-on-failure
            runHook postCheck
          '';
          meta.platforms = pkgs.lib.platforms.unix ++ pkgs.lib.platforms.windows;
        };
    in
    {
      packages = forAllTargets ({ system, pkgs }:
        let lidl = mkLidl { inherit system pkgs; };
        in
        {
          logos-lidl = lidl;
          default = lidl;
          tests = lidl;
        }
      );

      # Native only: a Windows `check` would have to run a PE on the builder.
      checks = forAllSystems ({ system, pkgs }: {
        tests = self.packages.${system}.logos-lidl;
      });

      devShells = forAllSystems ({ pkgs, ... }: {
        default = pkgs.mkShell {
          nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
          buildInputs = [ pkgs.gtest pkgs.nlohmann_json ];
        };
      });
    };
}
