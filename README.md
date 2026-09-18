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
    update_interval: 60s
    lambda: |-
      it.print(5, 5, id(my_font), "Hello");
```

- **`power_pin`** switches the panel's 3.3 V rail. Without it the panel never responds.
- **`busy_pin`** takes the pin as-is — do not set `inverted: true`. The component knows BUSY is
  low while the panel is busy.
- **`rotation: 270`** gives a 250 × 122 landscape canvas with the origin at the top left, matching
  the orientation of Elecrow's own firmware. Use `90` to flip it end for end.
- **`full_update_every`** (default `1`) refreshes with the fast partial waveform in between full
  refreshes. `1` means every update is a full refresh, which is the safe setting; higher values
  trade image quality for speed.

Drawing coordinates in the unrotated frame are 128 wide, but only columns 0–121 are physically
present. With `rotation: 270` that is the vertical axis, so keep drawing within `y < 122`.

## Refresh timing

E-paper degrades with excessive refreshing. Keep `update_interval` at 60 s or more, and drive the
display from `on_...` triggers with `component.update` if you need it to react to events instead.
