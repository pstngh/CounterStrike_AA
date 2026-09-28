# Native macOS arm64 build

This branch ports the Kisak-Strike client, listen server, renderer, VGUI, VScript, and Kisak physics implementation to native Apple Silicon. It does not use Rosetta.

## Current scope

- Native arm64 Mach-O executables and dynamic libraries
- Cocoa/SDL video and Apple's legacy OpenGL compatibility layer
- Offline and LAN listen-server play with `-insecure`
- Startup without the Steam desktop client
- No Steam matchmaking, VAC, inventory, achievements, or other online Steam services in standalone mode

## Requirements

- An Apple Silicon Mac (Intel Macs are not supported)
- macOS Command Line Tools
- CMake and Ninja
- Homebrew's SDL 2 compatibility package
- Legally acquired CS:GO game content

Install the build dependencies with Homebrew:

```sh
brew install cmake ninja sdl2-compat
```

## Configure and build

Kisak-Strike writes runtime binaries to a sibling `game` directory. Clone this repository inside a parent working directory, then configure from the repository root:

```sh
cmake -S . -B ../build-macos-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/macos-arm64.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DDEDICATED=OFF \
  -DUSE_KISAK_PHYSICS=ON \
  -DUSE_ROCKETUI=ON \
  -DUSE_SCALEFORM=OFF

cmake --build ../build-macos-arm64
```

The build compiles the [Mac gameplay preset](#mac-gameplay-preset) by default. Add `-DUSE_MAC_PRESET=OFF` for stock CS:GO gameplay; Linux builds default to OFF.

The build compiles an offline Steam API shim from source into `bin/osx64`, where the game modules find it through their rpath. The Steam desktop client and its `libsteam_api.dylib` are not required for this standalone build. Builds made before this change also copied the shim to `csgo/bin/osx64/libsteam_offline.dylib`; that copy is no longer needed and can be deleted.

The port vendors the MIT-licensed [sse2neon](https://github.com/DLTcollab/sse2neon) compatibility headers used to translate Source's SSE intrinsics to ARM NEON.

## Game content

This repository does not contain Valve game assets. Follow the upstream acquisition instructions for app 730, depot 731, manifest `7043469183016184477`, and place that content in the sibling `game` directory. You must have the legal right to use the content.

RocketUI also needs the separate [Kisak-Strike-Files](https://github.com/SwagSoftware/Kisak-Strike-Files) GUI files. Copy that repository's `csgo/rocketui` directory into `../game/csgo/rocketui` before launching. Without it, the team-selection and pause menus cannot render.

To use the AK-47 Asiimov finish, supply its `ak47_asiimov.vtf` from legally acquired CS:GO content and run:

```sh
python3 tools/install_ak_asiimov.py /path/to/ak47_asiimov.vtf ../game
```

The script installs the texture only in the local game directory. No game texture files are committed to this source repository.

## Launch standalone

Run from the game directory:

```sh
arch -arm64 ./csgo_osx64 -insecure -novid -windowed
```

To start a local map directly:

```sh
arch -arm64 ./csgo_osx64 -insecure -novid -windowed +map de_dust2
```

Standalone listen servers intentionally fall back to LAN mode when Steam services are unavailable. Console messages from failed Steam API initialization may still appear; they are non-fatal in this mode.

## Native Mac launcher

Build the launcher app in a playable game directory:

```sh
./tools/install_macos_launcher.sh ../game
```

Open `CSGO Launcher.app` in that directory to choose an installed map, bot count
and difficulty, and windowed or fullscreen resolution. The overall Quality menu
offers Low, Medium, High, and Very High presets; Custom appears when individual
graphics choices differ from a preset. Graphics Settings still offers texture
detail, texture filtering, anti-aliasing, shadows, shader detail, and VSync, and
the saved individual choices remain authoritative. Graphics choices apply on the
next launch; anti-aliasing and VSync are also passed at startup so the video
mode uses them immediately. The launcher saves the choices, writes
`csgo/cfg/mac_launcher.cfg`, and starts the game with that config after the map
loads. Game output goes to `launcher-game.log`. If the game crashes, macOS
writes a report in `~/Library/Logs/DiagnosticReports`. The launcher keeps the
original gameplay bindings and free-for-all defaults.

The bot selector supports 0–28 bots, excluding you from the count. The launcher
reserves enough slots for both teams and reapplies its settings after the mode
and map configs load, including on map changes. This prevents the deathmatch
defaults from replacing the requested count and difficulty. Local deathmatch
reuses matching bot personalities when all names at a difficulty are in use;
additional bots receive numbered names.
Use `status` in the developer console to check the actual connected bot count.
Update both the launcher and `server.dylib` when installing this change.

## Mac gameplay preset

These gameplay changes are compiled in when `USE_MAC_PRESET` is ON, the default on macOS.

Backtick opens the developer console. The preset locks the normal world FOV to 80, the saved Desktop-preset weapon viewmodel FOV of 60, mouse sensitivity to 1.029863, and `m_pitch` to 0.018. The persistent in-game HUD is crosshair-only; the sniper scope overlay and deliberately opened buy/team menus remain available. On-screen gameplay hints, objective lessons, and local weapon-drop messages are disabled. Every weapon uses the native CS:GO crosshair, including unscoped sniper rifles; configure it with the `cl_crosshair*` console variables. The AWP uses Allied Assault's 20-degree sniper FOV, one-step right-click toggle, immediate FOV change, and a circular Allied Assault-style scope mask. The scroll wheel cycles weapons without showing a selection HUD.

Use `cg_drawviewmodel 0` to hide the first-person weapon and hands, `cg_drawviewmodel 1` to show only the weapon, or `cg_drawviewmodel 2` for the normal weapon-and-hands view. K cycles through all three values. The default is 2, and the setting is saved. K replaces the previous voice-record binding.

Left Shift leans left, Space leans right, and F jumps. Lean uses OpenMoHAA's
Allied Assault multiplayer timing, 40-degree limit, camera pivot, and roll.
The weapon and hands follow that camera with a small additional drop. The
original four-unit drop cropped too much of the CS:GO rig at its 60-degree
weapon FOV, so the default drop is now 1 unit. `cl_viewmodel_lean_lower`
controls the drop at full lean: 0 disables it, 1 is the default, and 4 restores
the original amount. The drop follows the existing smooth lean transition.
No extra sideways movement or weapon tilt is applied. CS:GO's stock bob,
sway, running pose, and landing dip are unchanged, as are lean camera movement,
aim, collision, and FOV.
Other players see the lean as Allied Assault draws it in third person: the hips
roll 0.8 of the lean angle, the chest and shoulders the full angle, and the head
0.6, while both feet stay where the animation plants them. Server hitboxes and
lag compensation follow the same pose. CS:GO's acceleration lean, which tipped
bodies into speed changes, is disabled so bodies lean only with the lean input.
Left or right Control toggles crouch; C toggles walk.
W/S and A/D use nullbind-style SOCD: the most recently pressed direction wins
while both are held, and releasing it resumes the other held direction.
The preset uses Allied Assault deathmatch's 275 run speed, 0.6 walk
and crouch modifiers (165 each), and a combined 99 crouch-walk speed. Backward
input is 0.8 of forward and strafe input is 0.85, as in AA. The AWP uses AA's
0.8 sniper movement multiplier, giving 220 while running and 132 while walking
or crouching. Other local weapons use the full movement speed.

All grenade types, including flashbangs, and all knives are unavailable: they
cannot be bought, granted, picked up, or spawned on maps. C4 cannot be granted
or picked up, and bomb sites do not become objectives. Local matches start in
free-for-all deathmatch, with respawns enabled and every player a valid target.
Set `mp_teammates_are_enemies 0` in the console for team deathmatch. Weapon
inaccuracy uses each weapon's first-shot standing or crouching baseline while
running, jumping, climbing, or spraying. Shots retain their normal random
first-shot spread. Automatic-weapon recoil remains visible and affects aim,
but each shot samples a different recoil table entry instead of following a
fixed spray sequence. The view tracks recoil so the crosshair remains centered
on the recoil-adjusted shot direction. Guns using aim recoil retain each
shot's original impulse and 75% of the previous recoil velocity. The combined
velocity is capped at 32 degrees/second, preserving noticeable kick and spray
movement while softening the opening climb. This applies to automatic,
burst, and single-shot weapons on both the client and server; stock firing
animations and weapon bob are preserved. The AWP instead uses OpenMoHAA's
camera-only sniper kick: CTs use the Allied Springfield profile and Ts use the
Axis scoped Kar98 profile. `cl_viewkick_scale` sets its camera strength
(default 0.25, 75% less than the original kick). Enemy damage applies
OpenMoHAA's directional pitch, yaw, and roll response at 10% of its original
camera strength (`cl_damagekick_scale 0.1`, a 90% reduction). Both settings
allow 0–1: 0 disables that camera effect, and 1 restores its original strength.
The original camera-kick recenter and decay timing, weapon bob, and lean
framing are preserved.

For the local listen-server host, the preset keeps `sv_cheats` enabled, god mode active, hit-tagging slowdown disabled, the account at the server's maximum balance, and the active weapon's clip full. Timed respawn immunity is disabled, so bots are vulnerable as soon as they spawn. In classic and deathmatch games, each CT spawn gives a USP-S, silenced M4A1-S, AK-47, AUG and AWP; each T spawn gives a USP-S, AK-47 and AWP. Other players on either team also spawn with a USP-S by default. Each fixed primary weapon has a separate scroll-wheel position. While alive, the host can open the buy menu and buy anywhere throughout the round, regardless of buy zones, buy time or mode-specific buy locks. The local host can carry the fixed primary weapons together; ordinary inventory limits still apply to other players. Bots on the local server retain vest armor but receive no helmet protection. These server-side benefits do not override a remote server's rules or apply to other human players. A remote server may also impose its own mouse-pitch limit. The preset is compiled into the build rather than stored in `config.cfg`; editing that file will not change the locked values.

The preset changes shared client and server code, including the player network
table, so a preset client only plays correctly on a listen server built from
the same source with the same setting.

The AWP uses its native Asiimov paint kit, and the AK-47 uses its official
model-specific Asiimov texture and paint kit when installed from the acquired
content. The M4A1-S uses its native Mecha Industries finish and the USP-S uses
its native Cyrex finish. Chickens are suppressed on local maps. Warmup, the
round-start freeze countdown, the round-restart delay, and timed spawn immunity
are disabled. The loading screen's placeholder text is hidden. Once a map
finishes loading, the local player joins CT and spawns automatically without
opening the team menu. The pause menu's **Choose Team** action remains available
for manually joining CT, T, or Spectator. Bots may join and spawn before the
local player. Local matches never end: rounds ignore their timer, and the HUD
clock counts up how long the round has run. If a match does end, for example
after `mp_ignore_round_win_conditions 0`, and the map cycle has no other
installed map, the match restarts on the same map and every player keeps their
team.

Deathmatch's automatic random buy and automatic rebuy are disabled so they
cannot replace the fixed spawn loadout after it is granted.

In local classic and deathmatch games, bots spawn with one randomly selected
AK-47, M4A1-S, or AWP, plus a backup USP-S and vest armor. They keep using their
primary even at close range, with replenished reserve ammo and normal reloads.
Bots keep hunting instead of buying, camping, or holding a position, and move
while aiming, scoping, and reloading. AWP bots can fire while moving with the
preset's existing movement accuracy. Movement preserves navigation, crouching
through low passages, and ladder climbing; fallback sidesteps check for walls
and drops. Collisions can still briefly interrupt movement. Bots lean toward
whichever side they strafe, as Allied Assault players do, and aim from the
leaned eye their shots leave from; `bot_strafe_lean 0` turns the bot lean off
(default 1). Explicit bot debug
stops and freeze controls remain available. This behavior does not apply to
dedicated servers, cooperative/training modes, or builds without the preset.

## Status

The port is experimental. On an Apple Silicon Mac, the native client has loaded `de_dust2`, shown the RocketUI team menu, joined a local match, and run combat with bots without Steam. Some legacy assets and features are still incomplete. This client build does not support headless map loading; without a display, SDL/OpenGL initialization fails.
