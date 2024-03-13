{
  inputs = {
    nixpkgs.url = github:NixOS/nixpkgs/nixos-unstable;
    flake-utils.url = "github:numtide/flake-utils";

    f-alpaca-api-client.url = git+ssh://wentam.net/mnt/NAS/git-host/alpaca-api-client.git;
    f-alpaca-api-client.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs = {self, nixpkgs, flake-utils, f-alpaca-api-client, ...}:
  (flake-utils.lib.eachDefaultSystem (system: let pkgs = nixpkgs.legacyPackages.${system}; in rec {

    # Shell
    devShells.default = pkgs.gcc13Stdenv.mkDerivation {
      name = "build";
      buildInputs = with pkgs; [
        # For mkTestData
        f-alpaca-api-client.packages.${pkgs.system}.default
        openssl
        nlohmann_json
        libpqxx
      ];
    };

    # Package
    packages.default = pkgs.stdenv.mkDerivation rec {
      name = "simBroker";

      src = ./.;

      buildInputs = with pkgs; [ gcc13 ];

      dontConfigure = true;
      makeFlags = [ "-j12" ];

      doCheck = false;
      checkPhase = ''
        make test
      '';

      installPhase = ''
        mkdir -p $out/lib
        mkdir -p $out/include
        cp build/libsimbroker.so $out/lib/libsimbroker.so
        cp include/simBroker.hpp $out/include/simBroker.hpp
      '';
    };
  }));
}
