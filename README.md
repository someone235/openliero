# Open Liero

Earthworm simulation game based on a real physical model. Features: 2 worms,
40 weapons, great playability. Four game modes: Kill'em All, Game of Tag,
Capture the Flag and Simple CtF!

This is a continuation of the openliero project, originally located at
[github.com/gliptic/liero][0], also known as Liero 1.36.

The main ambition is to keep Liero running as faithfully as possible on
modern machines, although we are not opposed to making careful changes
and improvements to the original game (especially if they are optional).

Patches are welcome!

## Online multiplayer

See [multiplayer.md](multiplayer.md) for setup, firewall ports, and controls.

Choose **Host Online** to create a lobby and share its room code. Each guest
chooses **Join Online**, enters the code, and presses Enter when ready. The host
presses Enter to start once everyone is ready. Online rosters have no fixed
player limit; local and direct-connect modes retain their two-player layout.

Each player receives five independently randomized weapons every round. During
a match, Escape pauses/resumes, Q leaves while paused, and E lets the host end the match. Hold Tab to show the
player list; PgUp/PgDn pages through long lists. After a match, Enter on the host
returns the room to the lobby for a rematch. A departing guest returns the
remaining players to the lobby.

The server in `server/` must be upgraded along with the clients. Run it with
`cd server && go build -o openliero-relay . && ./openliero-relay` (UDP 19533).
To use your own server, set
`OPENLIERO_SIGNALING_SERVER=<server-address>` on every client;
`OPENLIERO_SIGNALING_PORT` overrides the default port. ICE/STUN/TURN still carries
game traffic between each guest and the host. Online play uses host-coordinated
lockstep and waits for every player's input before advancing a frame.

## Linux launchers for this checkout

Run `./start_serrver.sh` to start the server and `./play_liero.sh` to launch
the game in `LIERO/`. Both load and export settings from the project root's
`.env` file, even when launched from another directory. Copy `.env.example`
to `.env` if needed; use shell-style `KEY=value` assignments.

Set `OPENLIERO_SIGNALING_SERVER` to the server's reachable address and
`OPENLIERO_SIGNALING_PORT` to its UDP port (default `19533`). Open that UDP
port on the server's firewall/router. The server launcher rebuilds when Go is
available, otherwise it uses the existing `server/openliero-relay` executable.
Both scripts forward command-line arguments to their respective programs.

## Windows launcher for this checkout

Double-click `Play Liero.bat` in the project root. It launches an installed
Windows build from `install/windows-x64`, `install/windows-arm64`, or
`install/windows-x64-ci`, `install/windows-x64-cross`, or a portable build
extracted into `LIERO`.
If `LIERO` supplies `openliero.cmd`, the batch file can use that launcher too,
preserving its ruleset selection and nested runtime location.
Command-line arguments can also be passed through the batch file.

All Windows batch launchers load the project root's `.env` file. Copy
`.env.example` to `.env` if needed, then set `OPENLIERO_SIGNALING_SERVER` to the
relay hostname and `OPENLIERO_SIGNALING_PORT` to its UDP port. The supplied
configuration uses `lliero.duckdns.org:19533`; `.env` stays out of Git.

To run a server on this PC, double-click `Start Server.bat` and keep its window
open. For local clients, set `OPENLIERO_SIGNALING_SERVER=127.0.0.1` in `.env`
and launch each player with `Play Liero.bat`. The server launcher uses the
configured port and rebuilds when Go is on PATH, otherwise it uses
`server/openliero-relay.exe` if already built.

### Building a complete Windows bundle

Run `cmake --workflow --preset windows-x64`, then
`cmake --install build/windows-x64 --config Release`. The install directory
contains the game, assets, and required dependency and compiler-runtime DLLs.
Copy that complete directory to another Windows PC; it does not need a compiler
or vcpkg. Install Debug builds with `--config Debug` into a separate directory.

The Windows installer scans every enabled executable, including optional tools,
and fails on missing or conflicting DLLs. It takes libraries from the configured
build dependencies and compiler, excluding Windows system DLLs. For dependencies
in nonstandard locations, configure `OPENLIERO_RUNTIME_LIBRARY_DIRECTORIES` with
a semicolon-separated list of DLL directories. CI smoke-tests the installed game
with development directories removed from `PATH`.

Windows-hosted LLVM-MinGW builds require CMake 3.30 or newer for DLL scanning.

### Building Windows artifacts on Linux

Install CMake (3.25+), Ninja, Git, curl, tar, Python 3, a native C/C++ compiler,
pkg-config, zip/unzip, and a MinGW-w64 cross-toolchain with C++23 support.
[LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw/releases) is supported: unpack
its Linux-hosted UCRT release and add its `bin/` directory to `PATH`, or set
`LLVM_MINGW_ROOT` to its installation directory. All Windows dependencies are
built with the selected cross-toolchain; Linux host tools use the native compiler.

```bash
export LLVM_MINGW_ROOT=/opt/llvm-mingw
cmake --workflow --preset windows-x64-cross
cmake --install build/windows-x64-cross --config Release
```

The complete Windows bundle is in `install/windows-x64-cross/`, including `.exe`
files, game data, and required DLLs. Copy the entire directory to Windows to play.
Use `windows-x64-cross-debug` to build Debug, then install with `--config Debug`
and a separate `--prefix`. The cross-build does not need Windows, Wine, or a
Windows `SystemRoot`; Windows system DLLs are excluded explicitly. Missing
non-system DLLs still fail installation. Run the installed executable on Windows
or through Wine to smoke-test startup.

The DLL collector has a standalone regression fixture. On Linux, with the same
toolchain installed, run:

```bash
cmake -S tools/cmake/tests/windows-runtime -B tmp/windows-runtime-tests -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/tools/cmake/toolchains/windows-x64-cross.cmake"
cmake --build tmp/windows-runtime-tests
ctest --test-dir tmp/windows-runtime-tests --output-on-failure
```

## Downloads

Download the latest release from the [releases page][releases].

### Portable archives

The portable archives are the easiest way to run OpenLiero. Extract the archive
anywhere and run `openliero`. All saves, replays, profiles, and settings stay
inside the extracted folder — nothing is written elsewhere.

| Archive | Platform |
|---|---|
| `openliero-linux-x64-portable.tar.gz` | Linux x86_64 |
| `openliero-macos-portable.tar.gz` | macOS (arm64 + x86_64 universal) |
| `openliero-windows-x64-portable.zip` | Windows x86_64 |
| `openliero-web.tar.gz` | Web (self-hosted Emscripten/WebAssembly) |

Each archive ships with a `portable.txt` file next to the binary. When that
file is present, OpenLiero reads and writes everything from the same folder. To
switch to per-user save locations (so saves survive reinstalls or live in your
home directory), simply delete `portable.txt`.

### Windows MSIX installer

The MSIX package provides a Start Menu entry and clean uninstall via
Settings → Apps. Because Windows MSIX install directories are read-only,
OpenLiero saves to `%APPDATA%\openliero\openliero\` instead of the install
directory.

#### Step 1 — Verify and trust the publisher certificate

Download `openliero-publisher.cer` from the release page and verify its
thumbprint before importing it:

```powershell
$cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 "openliero-publisher.cer"
$cert.Thumbprint
# Expected: 3E2C2090E1D6A075DD8A480B1FA858E503730498
```

If the thumbprint matches, import it to the Trusted People store:

```powershell
certutil -addstore "TrustedPeople" openliero-publisher.cer
```

#### Step 2 — Install the MSIX

Double-click `openliero.msix`, or from PowerShell:

```powershell
Add-AppxPackage openliero.msix
```

#### Uninstalling

```powershell
Get-AppxPackage *openliero* | Remove-AppxPackage
```

Or use Settings → Apps → Installed apps → OpenLiero → Uninstall.

#### Why self-signed?

OpenLiero is a free, open-source project and does not have a paid code-signing
certificate. We publish the certificate thumbprint above so you can verify it
before importing. Build provenance can be verified independently with the
[GitHub CLI][5]:

```
gh attestation verify <artifact-file> --repo openliero/openliero
```

## Extracting game data for total conversions

For copyright reasons, this repository does not contain the original Liero sound
files. Included instead is the original ruleset together with the lierolibre
sound effects.

To use the original data, or any total conversion, run `tctool` on the game
data. The examples below assume you are running from inside the extracted
portable archive, where `portable.txt` causes `tctool` to write the TC into
the same folder.

### Windows

```powershell
Invoke-WebRequest https://www.liero.be/download/liero-1.36-bundle.zip -OutFile liero-1.36-bundle.zip
Expand-Archive -LiteralPath .\liero-1.36-bundle.zip .
.\tctool.exe liero-1.36-bundle
Rename-Item .\TC\liero-1.36-bundle "Liero v1.33"
Remove-Item .\liero-1.36-bundle.zip
Remove-Item -Recurse .\liero-1.36-bundle
```

### Linux/macOS

```bash
curl https://www.liero.be/download/liero-1.36-bundle.zip -O
unzip liero-1.36-bundle.zip
./tctool liero-1.36-bundle
mv TC/liero-1.36-bundle TC/"Liero v1.33"
rm -rf liero-1.36-bundle.zip liero-1.36-bundle
```

## Level Authoring

Custom terrain levels can be created in classic palette-indexed format or in a
modern true-color format with optional per-pixel animation. See
[docs/modern-level-authoring.md](docs/modern-level-authoring.md) for a
step-by-step guide, including a Krita workflow and a Python script for
generating `.lev` files from painted images.

## Building

See [docs/building.md](docs/building.md) for full build instructions (native,
Emscripten/web, videotool, desync fuzzer, clang-format, clang-tidy).

## License

Open Liero is licensed with the [BSD-2-Clause](LICENSE). Since the copyright
for the default sounds in Liero 1.33 is unknown, this project uses the sound
pack from LibreLiero, which is licensed with the
[WTFPL](data/TC/openliero/sounds/LICENSE).

This project depends on various other open source libraries which are licensed
under their own respective licenses:

* [SDL + SDL_image][1] ([zlib][3])
* [miniz][2] ([MIT][4])

The serialization layer in `src/game/serialization/` is adapted from
[gvl](https://github.com/gliptic/gvl) by Erik Lindroos and Martin Erik Werner
(BSD-2-Clause).

[0]: https://github.com/gliptic/liero
[1]: https://www.libsdl.org
[2]: https://github.com/richgel999/miniz
[3]: https://www.libsdl.org/license.php
[4]: https://github.com/richgel999/miniz/blob/master/LICENSE
[5]: https://cli.github.com/
[releases]: https://github.com/openliero/openliero/releases
