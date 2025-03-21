{
  lib,
  stdenv,
  pkg-config,
  cmake,
  cairo,
  fribidi,
  libdatrie,
  libGL,
  libjpeg,
  libselinux,
  libsepol,
  libthai,
  libxkbcommon,
  libinput,
  pango,
  expect,
  pcre,
  pcre2,
  utillinux,
  wayland,
  wayland-protocols,
  wayland-scanner,
  libXdmcp,
  debug ? false,
  version ? "git",
}:
stdenv.mkDerivation {
  pname = "Trackpad-Color-Picker" + lib.optionalString debug "-debug";
  inherit version;

  src = ../.;

  cmakeBuildType =
    if debug
    then "Debug"
    else "Release";

  nativeBuildInputs = [
    cmake
    pkg-config
  ];

  buildInputs = [
    cairo
    fribidi
    libdatrie
    libGL
    libjpeg
    libselinux
    libsepol
    libthai
    libinput
    expect
    pango
    pcre
    pcre2
    wayland
    wayland-protocols
    wayland-scanner
    libXdmcp
    libxkbcommon
    utillinux
  ];

  outputs = [
    "out"
  ];

  meta = with lib; {
    homepage = "https://github.com/Liam-Weitzel/Trackpad-Color-Picker";
    description = "A wlroots-compatible Wayland screen magnifier and color picker daemon that activates on trackpad pinch gesture with basic customization options.";
    license = licenses.bsd3;
    platforms = platforms.linux;
    mainProgram = "Trackpad-Color-Picker";
  };
}
