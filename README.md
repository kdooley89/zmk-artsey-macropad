# ZMK firmware: ARTSEY Paintbrush as a Claude Code macropad

ZMK config for an [ARTSEY](https://artsey.io) "The Paintbrush" 2x4 board with a
**nice!nano v2** and a 128x32 OLED, set up as an 8-key macropad for
[Claude Code](https://claude.com/claude-code) rather than an ARTSEY chording keyboard.
The OLED shows status while you type and an animated pixel critter when idle.

## Why ZMK and not QMK

The nice!nano is an nRF52840. QMK does not support that chip, so the QMK ARTSEY
files cannot be built for it. ZMK does, and it produces the `.uf2` file the
nice!nano bootloader accepts.

## Layout

Positions are 0-3 on the top row and 4-7 on the bottom row.

| Layer | Top row | Bottom row |
|---|---|---|
| Claude (default) | Esc, Shift+Tab, Esc Esc, Dictate | Ctrl+C*, 1, 2, Enter |
| F-keys | F13 F14 F15 F16 | F17* F18 F19 F20 |
| Media (hold bottom-left) | Play/Pause, Vol-, Vol+, Mute | -, Prev, Next, BT* |
| BT (hold bottom-left + bottom-right) | BT 0, BT 1, BT 2, USB/BT toggle | -, BT clr, Bootloader, - |

`*` = hold for the next layer.

What the Claude keys do:

- **Esc**: stop Claude mid-response.
- **Shift+Tab**: cycle normal / auto-accept edits / plan mode.
- **Esc Esc**: rewind to an earlier message.
- **Dictate**: double-presses Globe/Fn, the default macOS dictation shortcut.
  If it does nothing, check System Settings > Keyboard > Dictation > Shortcut.
- **Ctrl+C** (tap bottom-left): clear the input. Twice exits Claude Code.
- **1 / 2**: answer a permission prompt ("Yes" / "Yes, and don't ask again").
- **Enter**: send.

Combos (press two top-row keys together):

| Keys | Action |
|---|---|
| top 1 + top 2 | types `/clear` and runs it |
| top 3 + top 4 | types `/compact` and runs it |
| top 1 + top 4 | switch between the Claude and F-keys layers |

F13-F20 do nothing on their own; bind them in macOS Shortcuts, Keyboard Maestro,
BetterTouchTool, or per-app settings.

## OLED screen

The screen is a custom ZMK status screen in
`boards/shields/paintbrush_pad/status_screen.c`.

- **While typing:** output (USB/BT) top-left, battery top-right, layer name
  bottom-left, and a small pixel critter bottom-right. The critter blinks now
  and then and hops on every key press.
- **Idle (5 s without a key press):** the status text hides and the critter
  wanders the screen. It walks, looks around, hops, and blinks at random.
- **Idle 2 minutes:** it walks to the middle and naps, with floating Zs.
- **Idle 30 minutes:** the screen turns off to avoid OLED burn-in.
- **Deep sleep** (15 idle minutes, battery only): the screen turns off before
  the board sleeps.
- **Any key** brings back the status view.

ZMK normally blanks the OLED when idle. This config turns that off
(`CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE=n`) so the critter can play, so on battery
the screen stays lit until deep sleep.

## Change the screen

| What | Where |
|---|---|
| Seconds before the critter comes out | `CONFIG_ZMK_IDLE_TIMEOUT` (ms) in `config/paintbrush_pad.conf` |
| Nap and screen-off times, frame speed | `#define`s at the top of `status_screen.c` |
| Critter shape | `build_mascot()` in `status_screen.c` (built from 2x2-pixel blocks) |
| Animations | `pick_next_action()` and `tick_idle()` in `status_screen.c` |
| Go back to ZMK's stock screen | add `CONFIG_ZMK_DISPLAY_STATUS_SCREEN_BUILT_IN=y` and remove `CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE=n` in the `.conf` |

The shield's `Kconfig.defconfig` selects the custom screen and enables the
status widgets and fonts it uses. `CMakeLists.txt` in the same folder compiles
`status_screen.c`.

## Change the keymap

Edit `config/paintbrush_pad.keymap`, commit, and push. GitHub Actions builds a
new firmware in a few minutes. Keycode names: https://zmk.dev/docs/keymaps/list-of-keycodes

## Build locally (optional)

GitHub Actions is the normal build. To build on your machine instead, with
Docker running:

```sh
mkdir -p /tmp/zmkbuild && cd /tmp/zmkbuild
docker run --rm -v "$OLDPWD":/repo:ro -v "$PWD":/w -w /w zmkfirmware/zmk-build-arm:stable bash -c '
  [ -d zmk ] || { mkdir -p config && cp /repo/config/west.yml config/ && west init -l config && west update; }
  west zephyr-export
  rm -rf cfg && cp -r /repo/config cfg
  west build -p -s zmk/app -d build -b nice_nano//zmk -- \
    -DSHIELD=paintbrush_pad -DZMK_CONFIG=/w/cfg -DZMK_EXTRA_MODULES=/repo'
```

Run it from the repo folder. The first run downloads ZMK and Zephyr and takes
several minutes. The firmware is `/tmp/zmkbuild/build/zephyr/zmk.uf2`.

## Get the firmware

1. Open the repo's **Actions** tab, pick the latest green run, download the
   `firmware` artifact (or run `gh run download -n firmware`).
2. Unzip. You want `paintbrush_pad-nice_nano__zmk-zmk.uf2`.

## Flash

1. Double-tap the reset button on the board. A `NICENANO` drive appears.
2. Copy the `.uf2` onto it. The drive ejects itself and the board reboots.

```sh
cp paintbrush_pad-nice_nano__zmk-zmk.uf2 /Volumes/NICENANO/
```

Once this firmware is on the board you can also enter the bootloader from the
keys: hold bottom-left, hold bottom-right, press the third key on the bottom row.

`boards/shields/paintbrush_pad/paintbrush_pad.keymap` is a fallback that ZMK
ignores while `config/paintbrush_pad.keymap` exists. Edit the one in `config/`.

## Troubleshooting

- **Keys come out mirrored** (F16 where F13 should be): reverse the order of the
  `input-gpios` list in `boards/shields/paintbrush_pad/paintbrush_pad.overlay`.
- **Bluetooth acting strange / won't pair**: flash `settings_reset-nice_nano__zmk-zmk.uf2`
  once, then flash the normal firmware again.
- **Critter is a dark shape on a lit screen** (or the colors look reversed):
  add `CONFIG_ZMK_DISPLAY_INVERT=y` to `config/paintbrush_pad.conf`.
- **Screen stays blank when idle**: check that `CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE=n`
  is still in `config/paintbrush_pad.conf`. It must be set there; a shield
  default is not enough.
- **Build fails after a ZMK update**: this repo tracks ZMK `main`. Pin
  `revision:` in `config/west.yml` to a tag if needed.

## Hardware reference

Pin wiring comes from the official
[zmk-artsey Paintbrush shield](https://github.com/artseyio/zmk-artsey/tree/main/config/boards/shields/the_paintbrush):
top row A3, A2, A1, A0 and bottom row D15, D14, D16, D10, direct-wired to ground.
