# external

A minimal x64 external for Roblox: attaches to `RobloxPlayerBeta.exe`, reads the game
through `ReadProcessMemory`, and draws an ESP + aimbot on a transparent, click-through
ImGui/GLFW overlay. No injection into the game process.

## Layout

```
external/
  external.sln / external.vcxproj
  offsets.ini              <- per-Roblox-version values (edit this)
  src/
    main.cpp
    core/process.*         attach, module base, typed read/write, pattern scan
    core/crash.*           unhandled-exception logger (crash.txt)
    game/math.hpp          Vec2/Vec3/Mat4 + world-to-screen
    game/offsets.*         loads offsets.ini, resolves globals
    game/roblox.*          instance tree, players, parts, view matrix, --dump
    features/features.*    ESP (box styles, skeleton, health, names, snaplines) + aimbot
    overlay/overlay.*      transparent topmost window, menu toggle, click-through routing
    ui/menu.*              luna's tabbed ImGui menu (ESP / Aimbot / Misc)
  deps/
    imgui/                 Dear ImGui (from the nord archive)
    glfw/                  GLFW 3.3.8 x64 (import lib + dll + headers)
```

## Build

Open `external.sln` in Visual Studio and build **x64 / Release**, or:

```
MSBuild external.sln /m /p:Configuration=Release /p:Platform=x64
```

Output: `x64\Release\external.exe` (plus `glfw3.dll` and `offsets.ini` copied next to it).

## Configure (required)

`offsets.ini` holds every Roblox-specific value. They change on **every Roblox update**.
Fill them for your exact `RobloxPlayerBeta.exe` build from a dumper or your own reversing,
or provide byte signatures with the matching `<name>_sig` key.

Then verify before using the overlay:

```
external.exe --dump
```

`--dump` attaches, resolves the globals, and prints the DataModel / services / local
player / character parts / viewport. If anything is `0x0` or garbage, the corresponding
offset is wrong. Fix one at a time; `--dump` is the ground truth.

Notes:
- Numeric values are hex.
- `fake_data_model` / `visual_engine` accept a module-relative offset or a `_sig` pattern
  (IDA style, `?` = wildcard).
- Signature examples in the file are illustrative; verify them against your build.

## Use

1. Launch Roblox and join a place.
2. Run `external.exe`. The **luna** menu opens immediately.
3. Only the menu rectangle captures the mouse; everywhere else clicks, camera drags and
   keyboard input go straight to Roblox, so you can play with the menu on screen.
4. **Insert** or **F1** hides/shows the menu. The overlay never takes focus from the game.
5. Aimbot is hold-to-aim on **mouse 5** by default (changeable in the Aimbot tab).

## Menu

Three tabs, tuned live while the game keeps running:

| Tab | What's there |
|---|---|
| **ESP** | enable, names, distance, health bar, skeleton, snaplines, team check; box style (full / corners / filled), box thickness, text scale, max distance; colours for box, name, distance, skeleton |
| **Aimbot** | enable, aim-at-head, torso fallback, sticky target, FOV, smoothing, deadzone, aim key (mouse 4/5, alt, shift, ctrl), FOV circle toggle + colour |
| **Misc** | debug HUD, keybind reference |

The FOV circle is drawn at the centre of the viewport in aim space pixels, so what you see
is exactly the radius the aimbot searches.

### Aimbot behaviour

- **Target selection** picks the player closest to the crosshair inside the FOV, with world
  distance as a tie-break, so overlapping targets resolve predictably.
- **Sticky target** keeps the locked player for 350 ms while they stay inside the FOV, which
  stops the crosshair flicking between two nearby players.
- **Torso fallback** aims at the head and falls back to `UpperTorso`/`Torso` when the head
  cannot be projected (character half off screen, R6 rig, missing head).
- **Fractional movement** carries the sub-pixel remainder between frames, so high smoothing
  no longer stalls the crosshair a pixel short of the target.
- **Deadzone** stops mouse movement once the target is within that many pixels.

## Diagnostics

`crash.txt` is written next to the exe if the process ever dies from an unhandled
exception or `std::terminate`. It contains the exception code, the faulting
`module+offset`, registers and the image addresses found on the faulting stack, which is
enough to map the fault back to a function with `dumpbin /DISASM`.

## Reality check

- **Offsets are the whole maintenance burden.** When Roblox updates, `--dump` will start
  returning nulls and you re-fill `offsets.ini`. That is normal for externals.
- **Byfron/Hyperion:** current Roblox clients are protected. External `OpenProcess` +
  `ReadProcessMemory` may be blocked or detected depending on the build; if attach fails
  with access denied or reads return garbage, that is the anti-cheat, not this code.
- **Fullscreen exclusive** will hide the overlay; use windowed or borderless.
- Using this violates Roblox's ToS and can get accounts banned. Point it at a throwaway.
