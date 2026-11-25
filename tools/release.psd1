# release.psd1 - THPS 2X's names for the toolkit's release scripts (option-c tools/release/port_release.ps1,
# port_release_zip.ps1; their headers list every key). Used through tools\release.ps1 / tools\release_zip.ps1, which
# run with the repository as the current folder (relative paths below are relative to it). Model: Panzer Dragoon Orta
# Recompiled's tools/release.psd1.
# Release layout (from 2.0): only thps2x_launcher.exe on top, the game data\thps2x.exe (named in launcher/main.c);
# GameExe here is the DEV build's output whose link line the payload copies (THPS2X.exe stays).
# THPS lifts with its own steps (seeds, hooks, recomp_manual.c): LiftFile + LifterFiles. No APU module (THPS plays
# its sound through the DSound HLE mixer; the build has no xbox_apu.dll).
@{
    Who           = "THPS 2X release"
    Title         = "THPS 2X Recomp"
    ExeName       = "thps2x_launcher.exe"
    Target        = "thps2x_recomp"
    GameExe       = "THPS2X.exe"
    GameName      = "THPS2X"
    GenMatch      = '[\\/]thps2x[^\\/]*[\\/]gen$'
    Rc            = "assets\thps2x.rc"
    RcFiles       = @("assets\icon.ico")
    Config        = @()
    Licence       = "LICENSE-THPS2X-recomp-GPL-3.0.txt"
    PublicRepo    = "THPS2X-Recompiled"
    LiftFile      = "tools\lift.txt"
    LifterFiles   = @{ "port\hooks.toml" = "config\hooks.toml"; "port\seeds" = "seeds\*_seeds.json" }
    # the build of tools\build.ps1 -Variant v200 -ForRelease, relative to the repo (release.ps1 runs the toolkit script
    # in a new process with the repo as its folder, so .NET's GetFullPath resolves it the same way); without the
    # variant leave the key out: port_release.ps1 then takes <private>\build\<repo folder>
    Build         = "..\private\build\thps2x_recomp_v200-v200"
    ZipPrefix     = "THPS2X-Recompiled"
    Readme        = "tools\release_readme.txt"
    # RecompLauncher's manifest, at the top of the zip next to the launcher (maintainer 2026-11-24: in every release)
    RecompLauncher = "recomplauncher.json"
    # the LGPL source bundle (licence option C) from a clone of the public toolkit xboxrecomp-option-c at the release
    # tag: the option-c snapshot made for this THPS release, tagged thps2x-v<version> (commit "xboxrecomp-option-c
    # (THPS 2X Recompiled v<version>)"); until that tag exists port_release.ps1 stops with "has no tag"
    PublicToolkit = "..\..\..\tools\xboxrecomp_option_c_public"
    PublicTag     = "thps2x-v2.0.2"
}
