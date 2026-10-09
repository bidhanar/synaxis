# Synaxis

An offline media aggregator: a streaming-style home screen for the videos
already on your computer or phone. Synaxis finds your movies and shows, groups
episodes into series, makes artwork from the videos themselves, and plays
everything in [VLC](https://www.videolan.org/vlc/), remembering where each
person stopped. There's a Linux desktop app and an Android app; each works
with the videos stored on its own device.

## Install

Both come from the [latest release](https://github.com/bidhanar/synaxis/releases/latest).

**Linux** (Ubuntu 24.04 or newer, amd64): download the `.deb` and run

```sh
sudo apt install ./synaxis_0.3.0_amd64.deb
```

then open **Synaxis** from your app menu. `apt` pulls in VLC and the libraries
it needs.

**Android** (10 or newer): download `synaxis_0.3.0_android.apk` on the phone
and open it, allowing your browser to install apps if asked. Videos play in
[VLC for Android](https://play.google.com/store/apps/details?id=org.videolan.vlc);
Synaxis offers to fetch it the first time you press Play.

### Publishing a release

Set the version in `project(Synaxis VERSION ...)` in `CMakeLists.txt` (the
Android app reads it too), commit, and push a matching tag:

```sh
git tag v0.3.0 && git push origin v0.3.0
```

The [release workflow](.github/workflows/release.yml) builds and tests both
packages and attaches them to a GitHub Release. The APK is signed with the
release key in `~/.android/synaxis-release.jks`; upload it to the repository's
Actions secrets once with `scripts/set-android-signing-secrets.sh` (needs
`gh auth login`). Keep a backup of the key and `android/keystore.properties`:
phones only install updates signed with the same key.

## Features

- **Profiles**: "Who's watching?" with separate history and lists per person.
- **Continue Watching**: resumes right where you stopped; after an episode
  ends, its row moves on to the next one. You can remove a title from the row.
- **Autoplay**: the rest of the season is queued in VLC, so the next episode
  starts on its own.
- **Home rows**: Recently Added, Top 10 in Your Library, New to You, Watch It
  Again, Bingeworthy Series, Stunning in 4K, Epic Movies, Quick Watches, and
  decade rows, all built from your files.
- **Title pages**: season picker, episode list with progress bars and
  watched marks, Start Over, and every copy of a title when you have more than
  one (best quality first).
- **My List**, **search** (titles and episode names), and a TV Shows/Movies split.
- **Automatic artwork**: frames taken from each video, skipping dark or blank
  frames and cropping letterbox bars.
- **Settings**: which folders to scan and the minimum file size (default
  200 MB, to skip samples and clips).
- **On Android**: finds every video on the phone over the size limit (camera
  and screen recordings left out unless you include them), resumes through
  VLC for Android, and counts down to the next episode when one finishes.

Everything runs locally and nothing goes over the network. Both apps share one
web UI (`web/`): on Linux it's served on `127.0.0.1` and shown in a WebKitGTK
window; on Android it's bundled in the APK and talks to the app through an
in-process bridge, with no network socket at all.

## Build

Dependencies: CMake 3.20+, a C++20 compiler, FFmpeg development libraries
(`libavformat-dev libavcodec-dev libswscale-dev libavutil-dev` on
Debian/Ubuntu), WebKitGTK for the app window (`libwebkit2gtk-4.1-dev`; without
it the UI opens in your browser instead), and VLC to play. nlohmann_json, cpp-httplib and GoogleTest are
used from the system when installed and downloaded automatically otherwise.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
cd build && ctest --output-on-failure
```

### Debian/Ubuntu package

```sh
cmake -S . -B build-pkg -DCMAKE_BUILD_TYPE=Release -DSYNAXIS_BUNDLED_FFMPEG=ON
cmake --build build-pkg --target package     # -> build-pkg/synaxis_<version>_amd64.deb
```

`SYNAXIS_BUNDLED_FFMPEG` builds a minimal static FFmpeg into Synaxis (needs
`nasm`) instead of linking the system's, whose library versions differ between
Ubuntu releases; that's what lets one package install on all of them. The
package installs `synaxis` and `synaxis_cli` to `/usr/bin`, adds the app-menu
entry and icon, and pulls in VLC and WebKitGTK.
Remove it with `sudo apt remove synaxis`; your history and settings in
`~/.config` and `~/.local/share` are left alone.

Without packaging, `cmake --install build --prefix ~/.local` installs the same
files for your user only.

### Android app

Needs JDK 17+ and the Android SDK (platform 37). From `android/`:

```sh
./gradlew testDebugUnitTest assembleDebug    # -> app/build/outputs/apk/debug/
./gradlew assembleRelease                    # signed when keystore.properties exists
```

The Kotlin code (`android/app/src/main/java/io/synaxis/app/`) is a port of the
desktop logic (filename parser, catalog, watch history, home rows) and is
tested against the same fixtures. The web UI comes straight from `web/`.

## Usage

```sh
synaxis                 # open the Synaxis window (scans in the background)
synaxis --browser       # show the UI in your web browser instead
synaxis --no-browser    # no UI; just serve http://127.0.0.1:8484/
synaxis --port 9000
synaxis --quit          # stop the running app (also in the account menu)
```

Launching it again while it's open brings the existing window to the front.
In the window, **F11** toggles fullscreen, **Ctrl+R** reloads and **Ctrl+Q**
quits. Closing the window quits the app. The first run
scans your home folder; change that under **Settings**.

Files:

- `~/.config/synaxis/config.json`: folders, minimum size, port, VLC options
- `~/.local/share/synaxis/`: library cache, watch history (`watch_state.json`)
  and thumbnails

### Command-line tool

`synaxis_cli` is the original scriptable interface. It scans a single
directory into `./library.json` and plays a match in VLC:

```sh
synaxis_cli -d <directory> [-x mp4,mkv]
synaxis_cli -p -f "Inception" [-y 2010]
synaxis_cli -p -f "The Silent Orbit" -s 1 -e 3
synaxis_cli --version
```

## How it fits together

| Piece | Role |
|---|---|
| `FilenameParser` | Title, season/episode, year, source, tags and episode name from scene-style filenames |
| `ScanVideoFiles` / `Catalog` | Finds video files over the size limit and groups them into shows and movies |
| `ProbeMedia` / `WriteThumbnail` | Duration, resolution, codec and HDR via libavformat; JPEG artwork via libavcodec |
| `LibraryService` | Background rescans, cached probe results, thumbnail rendering |
| `WatchState` | Profiles, resume points, watched marks and My List, saved as JSON |
| `ComputeUpNext` / `ContinueWatching` | Decides between resume, next episode and rewatch |
| `VlcSession` / `PlaybackManager` | Launches VLC with its HTTP interface on a random localhost port and password, then polls it to record progress |
| `Server` + `web/` | JSON API and the single-page UI, compiled into the binary |

When working on the UI, `SYNAXIS_WEB_DIR=$PWD/web synaxis` serves the files
from disk, so a browser refresh picks up changes without a rebuild.

## License

GPLv3-or-later. See [LICENSE](LICENSE).
