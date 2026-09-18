# CrowPanel ESP32 2.13" e-paper — ESPHome component

An ESPHome display platform for the Elecrow CrowPanel ESP32 2.13" HMI board (122 × 250).

The panel is driven by a **JD79661**, not an SSD1680. ESPHome's built-in `waveshare_epaper`
platform speaks the SSD1680/SSD1608 command set, so none of its models drive this board.

Source from [Elecrow-RD](https://github.com/Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250/).

## Installing

Copy the `components/` directory next to your ESPHome YAML and reference it:

```yaml
external_components:
  - source:
      type: local
      path: components
```

`crowpanel-2.13.yaml` is a complete working configuration.

## Board pinout

| Signal | Pin | Note |
| ---- | ---- | ---- |
| SPI CLK | GPIO12 | |
| SPI MOSI | GPIO11 | |
| CS | GPIO14 | |
| DC | GPIO13 | |
| RESET | GPIO10 | |
| BUSY | GPIO9 | low while the panel is refreshing |
| Panel power | GPIO7 | `IO7_LCD_3.3_CTL`, must be driven high |
| Power LED | GPIO19 | |
| Menu / Exit / Up / Down / Confirm | GPIO2 / 1 / 6 / 4 / 5 | active low |

## Configuration

```yaml
display:
  - platform: crowpanel_epaper
    cs_pin: GPIO14
    dc_pin: GPIO13
    reset_pin: GPIO10
    busy_pin: GPIO9
    power_pin: GPIO7
    rotation: 270
    mirror_y: true
    update_interval: 60s
    lambda: |-
      it.print(5, 5, id(my_font), "Hello");
```

- **`power_pin`** switches the panel's 3.3 V rail. Without it the panel never responds.
- **`busy_pin`** takes the pin as-is — do not set `inverted: true`. The component knows BUSY is
  low while the panel is busy.
- **`rotation: 270`** gives a 250 × 122 landscape canvas with the origin at the top left. Use `90`
  to turn it end for end.
- **`mirror_x` / `mirror_y`** reflect the image along the panel's own axes, before rotation is
  applied. With `rotation: 270`, `mirror_y` reverses the long (reading) axis and `mirror_x` the
  short one. The panel's gate scan runs opposite to the row order in RAM, so `mirror_y: true` is
  what you want for upright, readable text; without it everything comes out reversed left to right.
- **`full_update_every`** (default `1`) refreshes with the fast partial waveform in between full
  refreshes. `1` means every update is a full refresh, which is the safe setting; higher values
  trade image quality for speed.

Drawing coordinates in the unrotated frame are 128 wide, but only columns 0–121 are physically
present. With `rotation: 270` that is the vertical axis, so keep drawing within `y < 122`.

## Home Assistant values

`crowpanel-2.13.yaml` pulls its values over the native API with the `homeassistant` sensor,
text sensor and time platforms, and renders them in the display lambda. Swap the `entity_id`s for
your own; nothing else needs changing.

A display is a `PollingComponent`, so its first draw would otherwise land one whole
`update_interval` after boot. The example triggers a draw from `api.on_client_connected` instead,
which puts real values on the panel a few seconds after Home Assistant connects. Use
`esphome.on_boot` instead if you want something on screen before the network is up — it will be
placeholders, since no values have arrived yet.

Guard every numeric read with `has_state()` — Home Assistant values arrive a second or two after
the API connects, and a `printf` of an unset sensor prints `nan`.

The degree sign is in ESPHome's default glyph set. Any other non-ASCII character needs an explicit
`glyphs:` list on the font.

## Refresh timing

A full refresh inverts the whole panel a couple of times before the new image settles. That flashing
is the GC waveform doing its job. Speckle or noise during the flash is not — it means the
controller's previous-frame RAM held garbage.

A refresh blocks for about a second, so ESPHome logs
`Component display took a long time for an operation`. That is inherent to e-paper — the panel
holds BUSY low for the whole waveform — and can be ignored.

E-paper degrades with excessive refreshing. Keep `update_interval` at 60 s or more, and drive the
display from `on_...` triggers with `component.update` if you need it to react to events instead.
