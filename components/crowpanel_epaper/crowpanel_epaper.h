#pragma once

#include "esphome/components/display/display_buffer.h"
#include "esphome/components/spi/spi.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace crowpanel_epaper {

/// Driver for the JD79661 controller on the Elecrow CrowPanel ESP32 2.13" e-paper board (122x250).
class CrowPanelEPaper : public display::DisplayBuffer,
                        public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                              spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_4MHZ> {
 public:
  void set_dc_pin(GPIOPin *dc_pin) { this->dc_pin_ = dc_pin; }
  void set_reset_pin(GPIOPin *reset_pin) { this->reset_pin_ = reset_pin; }
  void set_busy_pin(GPIOPin *busy_pin) { this->busy_pin_ = busy_pin; }
  void set_power_pin(GPIOPin *power_pin) { this->power_pin_ = power_pin; }
  void set_full_update_every(uint32_t full_update_every) { this->full_update_every_ = full_update_every; }
  void set_mirror_x(bool mirror_x) { this->mirror_x_ = mirror_x; }
  void set_mirror_y(bool mirror_y) { this->mirror_y_ = mirror_y; }

  void setup() override;
  void dump_config() override;
  void update() override;

  float get_setup_priority() const override { return setup_priority::PROCESSOR; }
  display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_BINARY; }

  void fill(Color color) override;

  /// Push the current buffer to the panel and refresh it.
  void display();

  /// Refresh the panel to plain white and drop whatever is in the buffer.
  void clear_screen();

 protected:
  void draw_absolute_pixel_internal(int x, int y, Color color) override;
  int get_width_internal() override { return WIDTH; }
  int get_height_internal() override { return HEIGHT; }

  void command_(uint8_t value);
  void data_(uint8_t value);
  void command_with_data_(uint8_t command, const uint8_t *data, size_t length);
  void write_frame_(uint8_t command, const uint8_t *data);
  void write_constant_frame_(uint8_t command, uint8_t value);

  void power_on_();
  void reset_();
  void init_panel_();
  void refresh_();
  void deep_sleep_();
  bool wait_until_idle_();

  /// Load the greyscale-clear (full refresh) waveform.
  void write_lut_gc_();
  /// Load the direct-update (partial refresh) waveform.
  void write_lut_du_();
  void write_lut_(const uint8_t *r20, const uint8_t *r21, const uint8_t *r22, const uint8_t *r23, const uint8_t *r24);

  static constexpr int WIDTH = 128;  // 122 visible columns, padded to a whole byte
  static constexpr int VISIBLE_WIDTH = 122;
  static constexpr int HEIGHT = 250;
  static constexpr size_t BUFFER_LENGTH = WIDTH * HEIGHT / 8;

  GPIOPin *dc_pin_{nullptr};
  GPIOPin *reset_pin_{nullptr};
  GPIOPin *busy_pin_{nullptr};
  GPIOPin *power_pin_{nullptr};

  bool mirror_x_{false};
  bool mirror_y_{false};
  uint32_t full_update_every_{1};
  uint32_t at_update_{0};
  // The controller needs the previous image in RAM 0x10 to compute a partial refresh.
  uint8_t *previous_buffer_{nullptr};
  // The GC and DU waveforms swap the 0x22/0x23 channels on every refresh.
  bool lut_swapped_{false};
};

}  // namespace crowpanel_epaper
}  // namespace esphome
