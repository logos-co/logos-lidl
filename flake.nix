{
  description = "logos-lidl - the language-neutral LIDL frontend (lexer/parser/AST/serializer/validator); .lidl text is the cross-language interchange";

  inputs.logos-nix.url = "github:logos-co/logos-nix";
  inputs.nixpkgs.follows = "logos-nix/nixpkgs";

  outputs = { self, nixpkgs, logos-nix }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        pkgs = import nixpkgs { inherit system; };
      });
    in
    {
      packages = forAllSystems ({ pkgs }:
        let
          lidl = pkgs.stdenv.mkDerivation {
            pname = "logos-lidl";
            version = "0.1.0";
            src = ./.;
            nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
            buildInputs = [ pkgs.gtest ];
            cmakeFlags = [ "-GNinja" ];
            doCheck = true;
            checkPhase = ''
              runHook preCheck
              ctest --output-on-failure
              runHook postCheck
            '';
          };
        in
        {
          logos-lidl = lidl;
          default = lidl;
          tests = lidl;
        }
      );

      checks = forAllSystems ({ pkgs }: {
        tests = self.packages.${pkgs.system}.logos-lidl;
      });

      devShells = forAllSystems ({ pkgs }: {
        default = pkgs.mkShell {
          nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
          buildInputs = [ pkgs.gtest ];
        };
      });
    };
}
