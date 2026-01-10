{
  description = "libneurosdk";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in {
        packages.libneurosdk = pkgs.stdenv.mkDerivation {
          pname = "libneurosdk";
          version = "0.2.1";

          src = ./.;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
          ];

          cmakeFlags = [
            "-DNEURO_BUILD_STATIC=OFF"
          ];
        };

        packages.default = self.packages.${system}.libneurosdk;

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            clang-tools
            lldb
            cmake
            ninja

            curl
          ];
        };
      }
    );
}
