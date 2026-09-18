#include "crowpanel_epaper.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <new>

namespace esphome {
namespace crowpanel_epaper {

static const char *const TAG = "crowpanel_epaper";

static const size_t LUT_LENGTH = 56;

// Full refresh waveform. Trailing bytes of every table are zero.
static const uint8_t LUT_R20_GC[LUT_LENGTH] = {0x01, 0x00, 0x14, 0x14, 0x01, 0x00, 0x00, 0x01};
static const uint8_t LUT_R21_GC[LUT_LENGTH] = {0x01, 0x60, 0x14, 0x14, 0x01, 0x00, 0x00, 0x01};
static const uint8_t LUT_R22_GC[LUT_LENGTH] = {0x01, 0x20, 0x14, 0x14, 0x01, 0x00, 0x00, 0x01};
static const uint8_t LUT_R23_GC[LUT_LENGTH] = {0x01, 0x10, 0x14, 0x14, 0x01, 0x00, 0x00, 0x01};
static const uint8_t LUT_R24_GC[LUT_LENGTH] = {0x01, 0x90, 0x14, 0x14, 0x01, 0x00, 0x00, 0x01};

// Partial refresh waveform, roughly 300 ms.
static const uint8_t LUT_R20_DU[LUT_LENGTH] = {0x01, 0x00, 0x14, 0x01, 0x01};
static const uint8_t LUT_R21_DU[LUT_LENGTH] = {0x01, 0x00, 0x14, 0x01, 0x01};
static const uint8_t LUT_R22_DU[LUT_LENGTH] = {0x01, 0x80, 0x14, 0x01, 0x01};
static const uint8_t LUT_R23_DU[LUT_LENGTH] = {0x01, 0x40, 0x14, 0x01, 0x01};
static const uint8_t LUT_R24_DU[LUT_LENGTH] = {0x01, 0x00, 0x14, 0x01, 0x01};

static const uint8_t CMD_PANEL_SETTING = 0x00;
static const uint8_t CMD_POWER_SETTING = 0x01;
static const uint8_t CMD_DEEP_SLEEP = 0x07;
static const uint8_t CMD_DATA_START_TRANSMISSION_1 = 0x10;  // previous image
static const uint8_t CMD_DATA_START_TRANSMISSION_2 = 0x13;  // new image
static const uint8_t CMD_DISPLAY_REFRESH = 0x17;
static const uint8_t CMD_VCOM_AND_DATA_INTERVAL = 0x50;
static const uint8_t CMD_RESOLUTION_SETTING = 0x61;

void CrowPanelEPaper::setup() {
  this->spi_setup();

  if (this->dc_pin_ != nullptr)
    this->dc_pin_->setup();
  if (this->reset_pin_ != nullptr)
    this->reset_pin_->setup();
  if (this->busy_pin_ != nullptr)
    this->busy_pin_->setup();

  this->power_on_();

  this->init_internal_(BUFFER_LENGTH);
  if (this->buffer_ == nullptr) {
    ESP_LOGE(TAG, "Could not allocate display buffer");
    this->mark_failed();
    return;
  }

  if (this->full_update_every_ > 1) {
    this->previous_buffer_ = new (std::nothrow) uint8_t[BUFFER_LENGTH];  // NOLINT
    if (this->previous_buffer_ == nullptr) {
      ESP_LOGW(TAG, "Could not allocate partial refresh buffer, falling back to full refreshes");
      this->full_update_every_ = 1;
    } else {
      memset(this->previous_buffer_, 0xFF, BUFFER_LENGTH);
    }
  }

  this->clear_screen();
}

void CrowPanelEPaper::dump_config() {
  LOG_DISPLAY("", "CrowPanel 2.13\" e-paper (JD79661)", this);
  LOG_PIN("  CS Pin: ", this->cs_);
  LOG_PIN("  DC Pin: ", this->dc_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  Busy Pin: ", this->busy_pin_);
  LOG_PIN("  Power Pin: ", this->power_pin_);
  ESP_LOGCONFIG(TAG, "  Full update every: %" PRIu32, this->full_update_every_);
  LOG_UPDATE_INTERVAL(this);
}

void CrowPanelEPaper::fill(Color color) {
  // Panel RAM is 1 = white, 0 = black.
  memset(this->buffer_, color.is_on() ? 0x00 : 0xFF, BUFFER_LENGTH);
}

void CrowPanelEPaper::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x >= WIDTH || y >= HEIGHT || x < 0 || y < 0)
    return;

  const uint32_t pos = (x + y * WIDTH) / 8u;
  const uint8_t subpos = x & 0x07;
  if (color.is_on()) {
    this->buffer_[pos] &= ~(0x80 >> subpos);
  } else {
    this->buffer_[pos] |= 0x80 >> subpos;
  }
}

void CrowPanelEPaper::update() {
  this->do_update_();
  this->display();
}

void CrowPanelEPaper::display() {
  if (this->is_failed())
    return;

  const bool full_update = this->previous_buffer_ == nullptr || this->at_update_ == 0;
  this->at_update_ = (this->at_update_ + 1) % std::max<uint32_t>(this->full_update_every_, 1);

  this->reset_();
  this->init_panel_();

  this->command_(CMD_VCOM_AND_DATA_INTERVAL);
  this->data_(0xD7);

  if (!full_update) {
    this->write_frame_(CMD_DATA_START_TRANSMISSION_1, this->previous_buffer_);
  }
  this->write_frame_(CMD_DATA_START_TRANSMISSION_2, this->buffer_);

  if (full_update) {
    this->write_lut_gc_();
  } else {
    this->write_lut_du_();
  }

  this->refresh_();
  this->deep_sleep_();

  if (this->previous_buffer_ != nullptr)
    memcpy(this->previous_buffer_, this->buffer_, BUFFER_LENGTH);
}

void CrowPanelEPaper::clear_screen() {
  if (this->is_failed())
    return;

  this->reset_();
  this->init_panel_();

  this->command_(CMD_VCOM_AND_DATA_INTERVAL);
  this->data_(0xD7);
  this->write_constant_frame_(CMD_DATA_START_TRANSMISSION_1, 0xFF);
  this->write_constant_frame_(CMD_DATA_START_TRANSMISSION_2, 0xFF);
  this->write_lut_gc_();

  this->refresh_();
  this->deep_sleep_();

  if (this->previous_buffer_ != nullptr)
    memset(this->previous_buffer_, 0xFF, BUFFER_LENGTH);
  this->at_update_ = 0;
}

void CrowPanelEPaper::power_on_() {
  if (this->power_pin_ == nullptr)
    return;

  this->power_pin_->setup();
  this->power_pin_->digital_write(true);
  delay(50);
}

void CrowPanelEPaper::reset_() {
  if (this->reset_pin_ == nullptr)
    return;

  this->reset_pin_->digital_write(true);
  delay(10);
  this->reset_pin_->digital_write(false);
  delay(100);
  this->reset_pin_->digital_write(true);
  delay(100);
}

void CrowPanelEPaper::init_panel_() {
  static const uint8_t POWER_SETTING[] = {0x03, 0x00, 0x3F, 0x3F, 0x03};
  static const uint8_t BOOSTER_SOFT_START[] = {0x27, 0x27, 0x2F};
  static const uint8_t RESOLUTION[] = {WIDTH, 0x00, HEIGHT};  // 128 x 250

  this->command_(CMD_PANEL_SETTING);
  this->data_(0xF7);
  this->data_(0x8A);

  this->command_with_data_(CMD_POWER_SETTING, POWER_SETTING, sizeof(POWER_SETTING));

  this->command_(0x03);
  this->data_(0x00);

  this->command_with_data_(0x06, BOOSTER_SOFT_START, sizeof(BOOSTER_SOFT_START));

  this->command_(0x30);
  this->data_(0x0D);

  this->command_(0x60);
  this->data_(0x22);

  this->command_(0x82);
  this->data_(0x07);

  this->command_(0xE3);
  this->data_(0x88);

  this->command_(0x41);
  this->data_(0x00);

  this->command_with_data_(CMD_RESOLUTION_SETTING, RESOLUTION, sizeof(RESOLUTION));

  this->command_(0x65);
  this->data_(0x00);
  this->data_(0x00);
  this->data_(0x00);

  this->command_(CMD_VCOM_AND_DATA_INTERVAL);
  this->data_(0xB7);
}

void CrowPanelEPaper::refresh_() {
  this->command_(CMD_DISPLAY_REFRESH);
  this->data_(0xA5);
  this->wait_until_idle_();
}

void CrowPanelEPaper::deep_sleep_() {
  this->command_(CMD_DEEP_SLEEP);
  this->data_(0xA5);
  delay(20);
}

bool CrowPanelEPaper::wait_until_idle_() {
  if (this->busy_pin_ == nullptr) {
    delay(3000);  // NOLINT
    return true;
  }

  const uint32_t start = millis();
  // BUSY is low while the panel is working.
  while (!this->busy_pin_->digital_read()) {
    if (millis() - start > 15000) {
      ESP_LOGE(TAG, "Timeout while waiting for the panel to finish refreshing");
      return false;
    }
    App.feed_wdt();
    delay(1);
  }
  return true;
}

void CrowPanelEPaper::write_lut_gc_() {
  this->write_lut_(LUT_R20_GC, LUT_R21_GC, LUT_R22_GC, LUT_R23_GC, LUT_R24_GC);
}

void CrowPanelEPaper::write_lut_du_() {
  this->write_lut_(LUT_R20_DU, LUT_R21_DU, LUT_R22_DU, LUT_R23_DU, LUT_R24_DU);
}

void CrowPanelEPaper::write_lut_(const uint8_t *r20, const uint8_t *r21, const uint8_t *r22, const uint8_t *r23,
                                 const uint8_t *r24) {
  this->command_with_data_(0x20, r20, LUT_LENGTH);
  this->command_with_data_(0x21, r21, LUT_LENGTH);
  this->command_with_data_(0x24, r24, LUT_LENGTH);

  // Channels 0x22 and 0x23 alternate between refreshes to keep the panel DC balanced.
  this->command_with_data_(this->lut_swapped_ ? 0x23 : 0x22, r22, LUT_LENGTH);
  this->command_with_data_(this->lut_swapped_ ? 0x22 : 0x23, r23, LUT_LENGTH);
  this->lut_swapped_ = !this->lut_swapped_;
}

void CrowPanelEPaper::command_(uint8_t value) {
  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(value);
  this->disable();
  this->dc_pin_->digital_write(true);
}

void CrowPanelEPaper::data_(uint8_t value) {
  this->dc_pin_->digital_write(true);
  this->enable();
  this->write_byte(value);
  this->disable();
}

void CrowPanelEPaper::command_with_data_(uint8_t command, const uint8_t *data, size_t length) {
  this->command_(command);
  this->dc_pin_->digital_write(true);
  this->enable();
  this->write_array(data, length);
  this->disable();
}

void CrowPanelEPaper::write_frame_(uint8_t command, const uint8_t *data) {
  this->command_with_data_(command, data, BUFFER_LENGTH);
}

void CrowPanelEPaper::write_constant_frame_(uint8_t command, uint8_t value) {
  this->command_(command);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (size_t i = 0; i < BUFFER_LENGTH; i++) {
    this->write_byte(value);
  }
  this->disable();
}

}  // namespace crowpanel_epaper
}  // namespace esphome
