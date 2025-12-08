{
  description = "ConnectTool Qt build with Steamworks + Qt6";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];

      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      nixpkgsFor = forAllSystems (system: import nixpkgs { inherit system; });
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgsFor.${system};
          repoRoot = toString ./.;
          steamworksEnv = builtins.getEnv "STEAMWORKS_PATH_HINT";
          steamworksSdkEnv = builtins.getEnv "STEAMWORKS_SDK_DIR";
          steamworksEnvPath =
            if steamworksEnv != "" then
              builtins.path {
                path = steamworksEnv;
                name = "steamworks-hint";
              }
            else
              null;
          steamworksSdkEnvPath =
            if steamworksSdkEnv != "" then
              builtins.path {
                path = steamworksSdkEnv;
                name = "steamworks-sdk";
              }
            else
              null;
          steamworksHint =
            if steamworksEnvPath != null then
              steamworksEnvPath
            else if steamworksSdkEnvPath != null then
              steamworksSdkEnvPath
            else
              repoRoot + "/steamworks";
          steamworksSdkHint =
            if steamworksSdkEnvPath != null then
              steamworksSdkEnvPath
            else if steamworksEnvPath != null then
              steamworksEnvPath
            else
              repoRoot + "/sdk";
        in
        {
          default = pkgs.stdenv.mkDerivation rec {
            pname = "connecttool-qt";
            version = "1.5.8";

            # Keep entire working tree (including untracked) so new sources are present.
            src = ./.;

            nativeBuildInputs =
              with pkgs;
              [
                cmake
                ninja
                pkg-config
                qt6.wrapQtAppsHook
                git
              ]
              ++ lib.optionals stdenv.isLinux [ patchelf ];

            buildInputs =
              (with pkgs.qt6; [
                qtbase
                qtdeclarative
                qtsvg
                qttools
                qt5compat
              ])
              ++ (if pkgs.stdenv.isLinux then [ pkgs.qt6.qtwayland ] else [ ])
              ++ [ pkgs.boost ]
              ++ (if pkgs.stdenv.isLinux then [ pkgs.SDL2 ] else [ ]);

            cmakeFlags = [
              "-DSTEAMWORKS_PATH_HINT=${steamworksHint}"
              "-DSTEAMWORKS_SDK_DIR=${steamworksSdkHint}"
              "-DCMAKE_BUILD_TYPE=Release"
            ];

            configurePhase = ''
              CONNECTTOOL_VERSION=${version} cmake -B build -S . -G Ninja $cmakeFlags
            '';
            buildPhase = "cmake --build build";
            installPhase = "cmake --install build --prefix $out";

            postFixup = pkgs.lib.optionalString pkgs.stdenv.isLinux ''
              patchelf --force-rpath --set-rpath "\$ORIGIN" $out/bin/libsteam_api.so
              wrapProgram $out/bin/connecttool-qt --prefix LD_LIBRARY_PATH : "$out/bin"
            '';
          };
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgsFor.${system};
        in
        {
          default = pkgs.mkShell {
            name = "connecttool-qt-shell";
            inputsFrom = [ self.packages.${system}.default ];
            packages = with pkgs; [
              gdb
              clang-tools
            ];
            CMAKE_EXPORT_COMPILE_COMMANDS = "1";
            shellHook = ''
              export CONNECTTOOL_VERSION=${self.packages.${system}.default.version}
              echo "Qt: $(qmake --version | grep -o 'Qt version [0-9.]*' | cut -d ' ' -f 3)"
              ln -sf build/compile_commands.json .
            '';
          };
        }
      );
    };
}
