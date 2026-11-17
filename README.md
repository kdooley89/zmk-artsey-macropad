# ZMK firmware: ARTSEY Paintbrush as a Claude Code macropad

ZMK config for an [ARTSEY](https://artsey.io) "The Paintbrush" 2x4 board with a
**nice!nano v2** and a 128x32 OLED, set up as an 8-key macropad for
[Claude Code](https://claude.com/claude-code) rather than an ARTSEY chording keyboard.

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

## Change the keymap

Edit `config/paintbrush_pad.keymap`, commit, and push. GitHub Actions builds a
new firmware in a few minutes. Keycode names: https://zmk.dev/docs/keymaps/list-of-keycodes

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
- **Build fails after a ZMK update**: this repo tracks ZMK `main`. Pin
  `revision:` in `config/west.yml` to a tag if needed.

## Hardware reference

Pin wiring comes from the official
[zmk-artsey Paintbrush shield](https://github.com/artseyio/zmk-artsey/tree/main/config/boards/shields/the_paintbrush):
top row A3, A2, A1, A0 and bottom row D15, D14, D16, D10, direct-wired to ground.
