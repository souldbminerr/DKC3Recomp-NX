# Nintendo Switch port (libnx + SDL2 + OpenGL ES2)

Homebrew target for DKC3Recomp. Boots straight into the game — no
pre-boot launcher, no overlay, no diagnostics bundles.

## Build

Prerequisites: devkitPro with devkitA64, libnx, and switch portlibs SDL2.

```sh
# 1. Generate the private recomp units once (needs the USA v1.0 ROM;
#    never committed — generated/ is gitignored):
python scripts/generate_snesrecomp.py --rom <path>/dkc3.sfc

# 2. Build the homebrew:
make -f Makefile.switch            # -> dkc3.nro + dkc3.nacp
#    make -f Makefile.switch DEBUG=1  # + L3/R3 quick savestates
```

Without step 1 the link stops at undefined `g_dispatch_table` /
`g_ram_routine_guards` symbols; everything else compiles and links.

## Install

- Copy `dkc3.nro` to `sdmc:/switch/dkc3/`.
- Copy the legally obtained ROM to `sdmc:/switch/dkc3/rom.smc`
  (`rom.sfc` also accepted; a 512-byte copier header is stripped and the
  payload is verified as USA v1.0 before boot).
- First boot writes defaults; `saves/` holds SRAM and savestates.
  A missing ROM writes `MISSING_ROM.txt` next to the NRO.
- Debugging without nxlink: create an empty `sdmc:/switch/dkc3/log.flag`
  and stderr is redirected, unbuffered, to `sdmc:/switch/dkc3/debug.log`.

## Controls (fixed, seamless)

Players 1 and 2 autodetect pads in order (a lone pad drives player 1).

| Switch | SNES |
|---|---|
| A / B / X / Y | A / B / X / Y |
| ZL / ZR | L / R |
| Plus (+) | Start |
| Minus (-) | Select |
| DPad, left stick | DPad |
| L3 / R3 (`DEBUG=1` builds only) | Quick-save / quick-load slot 0 |

## Presentation

- Fixed 1280x720 window on the GLES2-accelerated renderer (docked
  1080p follows the DKC2 pattern when validated); the framebuffer fills
  the window. Aspect is forced to 16:9 like the DKC2 port — report
  margin/scroll artifacts per scene.
- VSync is on; frame deadlines stay on the host 60 Hz clock.

## Deliberately excluded on Switch

Pre-boot launcher, Dear ImGui overlay (no-op stub), diagnostics
reports/bundles (stub), TCP debug server, co-sim, oracle, mods, GLSL
shaders, post-mortem minidumps, tier-2 JSON manifests. `log.flag`
file logging and the `[saves]` SRAM log lines stay.

## Files

| Path | Role |
|---|---|
| `Makefile.switch` | devkitA64 makefile (includes the framework fragment, forces SDL2/no-launcher defines, `--gc-sections`) |
| `runner/sdl_main.c` (`__SWITCH__`) | SD bring-up, `sdmc:/switch/dkc3/rom.smc` resolution, forced handheld settings (P1+P2 gamepad, 16:9, Switch binds), applet tick, SRAM seed + 30 s writer, L3/R3 debug |
| `runner/switch_gamepad.c` | HID-order raw joystick reader (A/B/X/Y, ZL/ZR triggers, +/-/Start/Select, stick-as-DPad) |
| `runner/desktop_present_switch.c` | `Dkc3SdlPresenter` over `SDL_Renderer` |
| `runner/desktop_overlay_switch.c` | no-op overlay (NULL-tolerant, settings round-trip) |
| `runner/diagnostics_switch.c` | no-op diagnostics |
| `snesrecomp/runner/src/switch/` | framework: SD bring-up, applet tick, exit/focus hooks, ROM resolver (new files in the pin) |
| `snesrecomp/runner/switch.mk` | framework source fragment for make builds |
