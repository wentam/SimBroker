with import <nixpkgs> {}; {
  fooEnv = gcc11Stdenv.mkDerivation {
    name = "build";
    nativeBuildInputs =
    let
      wentampkgs = import /home/wentam/NAS/my-nix-pkgs/default.nix  {inherit pkgs;};
    in
    [
      wentampkgs.AlpacaApiClient # for mkTestData
      openssl # for mkTestData
      nlohmann_json # for mkTestData
      libpqxx # for mkTestData
    ];
  };
}
