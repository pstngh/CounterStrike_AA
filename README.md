# CounterStrike_AA

CounterStrike_AA is a build of [Kisak-Strike](https://github.com/SwagSoftware/Kisak-Strike), the open-source port of the 2017 CS:GO engine, for Linux x64 and Apple Silicon Macs. It adds an optional gameplay preset that plays like Medal of Honor: Allied Assault, with leaning, Allied Assault movement speeds and scoped sniping, fixed loadouts and free-for-all deathmatch against bots.

The repository contains source code only. You need legally acquired CS:GO game files to play.

## Features

- **Native Apple Silicon build.** arm64 code throughout, no Rosetta. It starts without the Steam desktop client, using an offline Steam API shim built from source.
- **Linux x64 build.** It links Valve's `libsteam_api.so`.
- **Allied Assault gameplay preset.** It's on by default on macOS and off on Linux; see [Gameplay preset](#gameplay-preset).
- **Mac launcher app.** Pick a map, the bot count and difficulty, resolution and graphics settings, then start the game.
- **Open-source physics.** Choose between the Kisak-Strike rebuild of Valve's physics and a Bullet-based implementation, instead of Valve's closed `vphysics` library.
- **RmlUi menus.** The open-source RocketUI replaces the proprietary Scaleform UI.

Play is offline or on LAN listen servers. Valve's online services, such as matchmaking, VAC, inventory and achievements, are not available.

The project is experimental. Some legacy assets and features are incomplete.

## Prebuilt binaries

Every push to `main` builds both platforms in GitHub Actions. Open the latest successful [Build run](https://github.com/pstngh/CounterStrike_AA/actions/workflows/build.yml) and download:

- `csgo-linux-x64` for Linux
- `csgo-macos-arm64` for Apple Silicon Macs

Each artifact is a tarball of the game directory's binaries, without Valve's assets. Unpack it over a game directory prepared as described in [Game files](#game-files).

## Building

The build writes its binaries to `../game`, a directory beside the source tree. So clone into a parent folder that you keep for this purpose:

```sh
mkdir csgo && cd csgo
git clone https://github.com/pstngh/CounterStrike_AA.git src
```

The game then lands in `csgo/game`. Build directories can go anywhere; the commands below use `csgo/build`.

### Linux

Install the dependencies. On Ubuntu, CI uses:

```sh
sudo apt install build-essential cmake ninja-build libsdl2-dev libsdl2-mixer-dev \
  libopenal-dev libcurl4-openssl-dev libssl-dev libfontconfig1-dev libglu1-mesa-dev net-tools
```

The engine calls `ifconfig` from `net-tools` to find its LAN address.

<details>
<summary>Package lists for Fedora, Arch and Gentoo</summary>

Fedora:

```sh
sudo dnf install git SDL2-devel SDL2_mixer-devel openal-soft-devel libcurl-devel openssl-devel \
  fontconfig-devel freetype-devel cmake ninja-build gcc g++ mesa-libGL-devel mesa-libGLU-devel
```

Arch:

```
sdl2 sdl2_mixer openal libcurl-compat openssl fontconfig freetype2 mesa cmake ninja gcc base-devel
```

Gentoo:

```
media-libs/libsdl2 media-libs/sdl2-mixer media-libs/openal net-misc/curl dev-libs/openssl
media-libs/fontconfig media-libs/freetype media-libs/mesa dev-build/cmake dev-build/ninja sys-devel/gcc
```

</details>

Configure and build:

```sh
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_KISAK_PHYSICS=ON
cmake --build build
```

### macOS

You need an Apple Silicon Mac; Intel Macs are not supported. Install the Command Line Tools (`xcode-select --install`), then the remaining dependencies with Homebrew:

```sh
brew install cmake ninja sdl2-compat
```

Configure with the bundled toolchain file, then build:

```sh
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_KISAK_PHYSICS=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/src/cmake/toolchains/macos-arm64.cmake"
cmake --build build
```

[MACOS.md](MACOS.md) covers the macOS port in more detail.

### Windows

Windows is not supported: CMake stops when configuring on Windows. The engine's Windows code paths are still in the source, as a starting point for a port.

### Build options

Pass these to the configure step as `-D<option>=<value>`:

| Option | Default | Effect |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Release` | `Release` or `Debug`. |
| `USE_KISAK_PHYSICS` | `OFF` | Build the open-source rebuild of Valve's physics. CI turns it on. |
| `USE_BULLET_PHYSICS` | `OFF` | Build the Bullet-based physics instead. It is experimental. |
| `USE_BULLET_PHYSICS_THREADED` | `OFF` | Multithreaded Bullet; the `bt_threadcount` console variable sets the thread count. Experimental. |
| `USE_MAC_PRESET` | `ON` on macOS, `OFF` elsewhere | Compile the [gameplay preset](#gameplay-preset). |
| `USE_ROCKETUI` | `ON` | Use the RmlUi menus. |
| `USE_SCALEFORM` | `OFF` | Use Valve's incomplete Scaleform UI instead. It needs Valve's prebuilt `scaleformui_client.so`. Not recommended. |
| `DEDICATED` | `OFF` | Build a dedicated server instead of the game; see [Dedicated server](#dedicated-server). |
| `USE_VALVE_HRTF` | `OFF` | Linux only. Link Valve's Steam Audio HRTF library for 3D sound. Copy `libphonon3d.so` into `game/bin/linux64` before configuring. |
| `KISAK_ARCH_FLAGS` | `-march=x86-64-v2` or `-mcpu=apple-m1` | CPU target. Set `-march=native` to tune for the build machine; the binaries then may not run elsewhere. |
| `RELEASE_DEBUG_INFO` | `ON` | Keep debug info in Release builds for symbolized crash backtraces. |
| `USE_ASAN` | `OFF` | Linux only. Build with AddressSanitizer. |
| `USE_TRACY` | `OFF` | Enable the Tracy profiler. |
| `TRACY_STORE_LOGS` | `OFF` | Make Tracy record from startup and send the data when a viewer connects, instead of only while one is connected. This uses memory quickly. |

If neither physics option is on, the game loads Valve's closed-source physics library instead. On Linux, copy `vphysics_client.so` from Valve's Linux binaries; see [Game files](#game-files).

### Dedicated server

Configure a separate build directory with `-DDEDICATED=ON`. It writes `srcds_linux` (`srcds_osx` on macOS) to the game directory, and its engine modules replace the game's there. Build the game again when you want to play.

## Game files

The repository ships no Valve content. With a Steam account that owns CS:GO, download it with [DepotDownloader](https://github.com/SteamRE/DepotDownloader):

| Content | App | Depot | Manifest |
|---|---|---|---|
| Game assets | 730 | 731 | `7043469183016184477` |
| Linux binaries | 730 | 734 | `4197642562793798650` |

1. Copy everything from the assets depot into the game directory (`csgo/game` in the layout above).
2. From the Linux binaries depot, copy `bin/map_publish/`, which holds VGUI assets. Optionally also copy these files into `game/bin/linux64`:
   - `libphonon3d.so` for `USE_VALVE_HRTF`.
   - `vphysics_client.so` for Valve's original physics.
   - `scaleformui_client.so` for `USE_SCALEFORM`.
3. Copy [Kisak-Strike-Files](https://github.com/SwagSoftware/Kisak-Strike-Files) into the game directory. Its `csgo/rocketui` directory holds the RocketUI menus. Without it, the team and pause menus cannot render.
4. Optional: to give the AK-47 its Asiimov finish, supply `ak47_asiimov.vtf` from your CS:GO content:

   ```sh
   python3 src/tools/install_ak_asiimov.py /path/to/ak47_asiimov.vtf game
   ```

## Running

Run the game from the game directory. The modules find each other relative to their own location, so the directory can live anywhere.

Linux:

```sh
cd game
./csgo_linux64 -insecure -novid +map de_dust2
```

macOS:

```sh
cd game
arch -arm64 ./csgo_osx64 -insecure -novid -windowed +map de_dust2
```

`-insecure` turns off VAC, which offline play cannot use. Leave out `+map` to start at the main menu. On macOS, messages about the Steam API failing to start are expected and harmless.

To build the Mac launcher app into a game directory, run:

```sh
src/tools/install_macos_launcher.sh game
```

Then open `CSGO Launcher.app` from that directory. [MACOS.md](MACOS.md#native-mac-launcher) describes its settings.

## Gameplay preset

`USE_MAC_PRESET` compiles in Allied Assault-style rules for local play:

- **Leaning and movement.** Left Shift leans left and Space leans right, with OpenMoHAA's timing, and F jumps. Control toggles crouch and C toggles walk. Movement uses Allied Assault's run, walk and crouch speeds, and opposing movement and lean keys resolve like nullbinds: the last key pressed wins.
- **Sniping.** The AWP uses a circular 20-degree scope and Allied Assault's camera kick.
- **Recoil and spread.** Recoil and hit reactions are softened, and spread doesn't grow while moving.
- **Loadouts.** CT players spawn with a USP-S, M4A1-S, AK-47, AUG and AWP, and T players with a USP-S, AK-47 and AWP. Grenades, knives and C4 are removed.
- **Matches.** Local matches are free-for-all deathmatch with no warmup or round end. The host has god mode, full ammunition, maximum money, and can buy anywhere.
- **Bots.** Bots carry a random primary, keep hunting, and lean as they strafe.
- **View.** The FOV is fixed at 80, the HUD shows only the crosshair, and the K key cycles the weapon model between hidden, weapon only, and weapon and hands.

The preset changes shared client and server code, including the player network table. A preset client therefore only plays correctly on a server built from the same source with the same setting. [MACOS.md](MACOS.md#mac-gameplay-preset) documents every change and the console variables that tune them.

## Proprietary components

- `lib/public/linux64/libsteam_api.so` is Valve's Steam API library. Linux builds link it and copy it to `game/bin/linux64`. macOS builds use the offline shim in `thirdparty/steam_api_offline` instead.
- The optional files from Valve's Linux binaries depot are listed under [Game files](#game-files).

## Credits

- Kisak-Strike is by lwss and contributors. Its [blog post](https://lwss.github.io/Kisak-Strike/) tells the project's history.
- The Allied Assault lean, movement and camera-kick values follow [OpenMoHAA](https://github.com/openmoh/openmohaa).
- Apple Silicon support translates the engine's SSE code with [sse2neon](https://github.com/DLTcollab/sse2neon).
- Bundled libraries include RmlUi, Bullet, protobuf, Crypto++, Lua, Squirrel, zlib, libpng and Tracy. Each keeps its license file beside its source.

## License

##### Any contributions made to Kisak-Strike will be considered donations to the public domain.

##### The following Inherited License from Source SDK also applies.

SOURCE 1 SDK LICENSE

Source SDK Copyright(c) Valve Corp.  

THIS DOCUMENT DESCRIBES A CONTRACT BETWEEN YOU AND VALVE 
CORPORATION ("Valve").  PLEASE READ IT BEFORE DOWNLOADING OR USING 
THE SOURCE ENGINE SDK ("SDK"). BY DOWNLOADING AND/OR USING THE 
SOURCE ENGINE SDK YOU ACCEPT THIS LICENSE. IF YOU DO NOT AGREE TO 
THE TERMS OF THIS LICENSE PLEASE DON’T DOWNLOAD OR USE THE SDK.  

  You may, free of charge, download and use the SDK to develop a modified Valve game 
running on the Source engine.  You may distribute your modified Valve game in source and 
object code form, but only for free. Terms of use for Valve games are found in the Steam 
Subscriber Agreement located here: http://store.steampowered.com/subscriber_agreement/ 

  You may copy, modify, and distribute the SDK and any modifications you make to the 
SDK in source and object code form, but only for free.  Any distribution of this SDK must 
include this LICENSE file and thirdpartylegalnotices.txt.  
 
  Any distribution of the SDK or a substantial portion of the SDK must include the above 
copyright notice and the following: 

    DISCLAIMER OF WARRANTIES.  THE SOURCE SDK AND ANY 
    OTHER MATERIAL DOWNLOADED BY LICENSEE IS PROVIDED 
    "AS IS".  VALVE AND ITS SUPPLIERS DISCLAIM ALL 
    WARRANTIES WITH RESPECT TO THE SDK, EITHER EXPRESS 
    OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, IMPLIED 
    WARRANTIES OF MERCHANTABILITY, NON-INFRINGEMENT, 
    TITLE AND FITNESS FOR A PARTICULAR PURPOSE.  

    LIMITATION OF LIABILITY.  IN NO EVENT SHALL VALVE OR 
    ITS SUPPLIERS BE LIABLE FOR ANY SPECIAL, INCIDENTAL, 
    INDIRECT, OR CONSEQUENTIAL DAMAGES WHATSOEVER 
    (INCLUDING, WITHOUT LIMITATION, DAMAGES FOR LOSS OF 
    BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF 
    BUSINESS INFORMATION, OR ANY OTHER PECUNIARY LOSS) 
    ARISING OUT OF THE USE OF OR INABILITY TO USE THE 
    ENGINE AND/OR THE SDK, EVEN IF VALVE HAS BEEN 
    ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.  
 
       
If you would like to use the SDK for a commercial purpose, please contact Valve at 
sourceengine@valvesoftware.com.
