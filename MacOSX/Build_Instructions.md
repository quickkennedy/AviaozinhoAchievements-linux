# macOS Build

Produces a Universal app bundle compatible with Intel macOS 10.9+ and Apple Silicon macOS 11.0+.

Tested on `macOS 10.15.7, Xcode 12.4, x86_64` and `macOS 12.5, Xcode 14.01, arm64`.

1. Prerequisites:

    - vcpkg requires `brew install pkg-config`

2. Run `setup-vcpkg.sh` (fetches + builds x86_64 and arm64 vorbis, opus, and zlib from vcpkg, and produces universal static libraries.)

   The Opus .dylibs included with QuakeSpasm lack the encoder, which is needed by `snd_voip.c`, necessitating building it from source.

3. For a release build, run `xcodebuild -project QuakeSpasm.xcodeproj -target QSS-M`


## Limitations

- SDL 1.2 is no longer supported.
    - Some code assumes SDL 2.0+ (e.g. `VID_UpdateCursor`).
- Minimum macOS version is 10.9 for x64, and 11.0 for arm64.
    - These are the lowest versions that can be targeted with recent releases of Xcode.