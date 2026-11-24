# DJ Tools

DJ Tools puts club-mixer style effects in your DAW: one-knob COLOR effects and 14 beat-synced effects from echo to roll and brake.

Product page: https://99hertz.com/dj-tools

DJ Tools is an audio plugin (VST3 and AU on macOS, VST3 on Windows) built with [JUCE](https://juce.com) 8.

Copyright (c) 2026 99 Hertz Limited

## Licence

DJ Tools is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License version 3 (AGPLv3). See [LICENSE](LICENSE).

Third-party parts:

- **JUCE** 8.0.13, used under its AGPLv3 open-source licence. Not included in this repo; CMake downloads it at build time.
- **TweetNaCl** (`Source/Licence/tweetnacl.c`, `tweetnacl.h`), public domain.
- **Barlow Condensed** and **Share Tech Mono** fonts (`Resources/Fonts/`), SIL Open Font License 1.1. The licence texts are included next to the fonts (`OFL-*.txt`).

## Building

You need CMake 3.22 or newer. JUCE 8.0.13 is fetched automatically on the first configure (or put a JUCE checkout at `./JUCE` to use that instead).

### macOS (Xcode command line tools)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDJTOOLS_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build --config Release --target DJTools_VST3 DJTools_AU
```

The plugins end up in `build/DJTools_artefacts/Release/`.

### Windows (Visual Studio 2022)

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DDJTOOLS_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build --config Release --target DJTools_VST3
```

Copy `build\DJTools_artefacts\Release\VST3\DJ Tools.vst3` to `C:\Program Files\Common Files\VST3\`.

The GitHub Actions workflow in `.github/workflows/windows.yml` builds the same VST3 and wraps it in an NSIS installer (`scripts/installer.nsi`).
