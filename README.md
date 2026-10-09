# Synaxis

An offline media aggregator: a streaming-style home screen for the videos
already on your disk, in its own desktop window. Synaxis finds your movies and shows, groups episodes into
series, makes artwork from the videos themselves, and plays everything in
[VLC](https://www.videolan.org/vlc/), remembering where each person stopped.

## Install

Download the `.deb` for your Ubuntu version from the
[latest release](https://github.com/bidhanar/synaxis/releases/latest) and run:

```sh
sudo apt install ./synaxis_0.2.0_ubuntu26.04_amd64.deb
```

Then open **Synaxis** from your app menu. `apt` pulls in VLC and the libraries
it needs. Packages are built for Ubuntu 26.04 and 24.04 (amd64).

To publish a release, set the version in `project(Synaxis VERSION ...)` in
`CMakeLists.txt`, commit, and push a matching tag (`git tag v0.2.0 && git
push origin v0.2.0`). The [release workflow](.github/workflows/release.yml)
builds, tests and attaches the packages.

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

Everything runs locally. The UI is web-based, shown in a native WebKitGTK window
and served on `127.0.0.1` only. Nothing goes over the network.

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
cmake --build build --target package     # -> build/synaxis_<version>_amd64.deb
sudo apt install ./build/synaxis_0.2.0_amd64.deb
```

The package installs `synaxis` and `synaxis_cli` to `/usr/bin`, adds the
app-menu entry and icon, and pulls in VLC and the FFmpeg libraries it needs.
Remove it with `sudo apt remove synaxis`; your history and settings in
`~/.config` and `~/.local/share` are left alone.

Without packaging, `cmake --install build --prefix ~/.local` installs the same
files for your user only.

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
