// A Wire-shaped view of the HOST's I2C bus.
//
// On this board the ES8311 codec, the FT6336U touch panel and the DS3231 all sit on one bus
// (SDA 16, SCL 15). Whoever calls i2c_new_master_bus() owns it; a second call on the same port
// fails outright with "I2C bus id(0) has already been acquired". The host brings the bus up for
// the codec during board init, long before bunbun starts, so bunbun cannot also be an owner.
//
// Rewriting bunbun's 36 Wire call sites against the raw i2c_master API would work and would also
// be 36 chances to introduce a subtle transfer bug in code that currently works. Presenting the
// host's bus behind the Wire interface instead keeps every one of those call sites untouched.
//
// Only the subset bunbun actually uses is implemented — beginTransmission/write/endTransmission
// and requestFrom/read/available. Anything else is deliberately absent rather than silently
// wrong: a missing method is a compile error, which is the failure mode you want.
#pragma once
#include <Arduino.h>
#include "driver/i2c_master.h"

extern "C" i2c_master_bus_handle_t board_get_i2c_bus(void);

class HostWire {
public:
  // The bus is already running; bunbun's begin() call becomes an acknowledgement, not an action.
  void begin(int, int, uint32_t = 400000) {}

  void beginTransmission(uint8_t addr) { m_addr = addr; m_txN = 0; }
  void beginTransmission(int addr)     { beginTransmission((uint8_t)addr); }

  size_t write(uint8_t b) {
    if (m_txN >= sizeof(m_tx)) return 0;
    m_tx[m_txN++] = b;
    return 1;
  }

  // Returns 0 on success, matching Wire. `false` means "no STOP" — a repeated start, which the
  // FT6336U needs between the register write and the read. i2c_master_transmit_receive() does
  // exactly that in one call, so a no-stop transmit is deferred until the following requestFrom.
  uint8_t endTransmission(bool stop = true) {
    if (!stop) { m_pending = true; return 0; }
    m_pending = false;
    // A zero-length transmit is how Wire probes for a device — it is the whole basis of the
    // classic I2C scanner, and of bunbun's DS3231 detection. The IDF driver rejects it outright
    // ("transmit buffer or size invalid"), so it has to become an explicit probe instead.
    if (m_txN == 0) {
      i2c_master_bus_handle_t bus = board_get_i2c_bus();
      if (!bus) return 4;
      return i2c_master_probe(bus, m_addr, 50) == ESP_OK ? 0 : 2;
    }
    i2c_master_dev_handle_t dev = device(m_addr);
    if (!dev) return 4;
    return i2c_master_transmit(dev, m_tx, m_txN, 20) == ESP_OK ? 0 : 4;
  }

  uint8_t requestFrom(uint8_t addr, size_t n) {
    m_rxN = m_rxI = 0;
    if (n > sizeof(m_rx)) n = sizeof(m_rx);
    i2c_master_dev_handle_t dev = device(addr);
    if (!dev) return 0;
    esp_err_t e;
    if (m_pending && addr == m_addr) {
      // repeated start: write the register we buffered, then read without releasing the bus
      e = i2c_master_transmit_receive(dev, m_tx, m_txN, m_rx, n, 20);
      m_pending = false;
    } else {
      e = i2c_master_receive(dev, m_rx, n, 20);
    }
    if (e != ESP_OK) return 0;
    m_rxN = n;
    return (uint8_t)n;
  }
  uint8_t requestFrom(int addr, int n) { return requestFrom((uint8_t)addr, (size_t)n); }

  int available() { return (int)(m_rxN - m_rxI); }
  int read()      { return (m_rxI < m_rxN) ? m_rx[m_rxI++] : -1; }

private:
  // Device handles are cached per address: adding one is not free, and the touch panel is polled
  // every frame.
  i2c_master_dev_handle_t device(uint8_t addr) {
    for (int i = 0; i < m_devN; i++)
      if (m_devAddr[i] == addr) return m_dev[i];
    i2c_master_bus_handle_t bus = board_get_i2c_bus();
    if (!bus || m_devN >= (int)(sizeof(m_dev) / sizeof(m_dev[0]))) return nullptr;
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address  = addr;
    cfg.scl_speed_hz    = 400000;
    i2c_master_dev_handle_t d = nullptr;
    if (i2c_master_bus_add_device(bus, &cfg, &d) != ESP_OK) return nullptr;
    m_devAddr[m_devN] = addr;
    m_dev[m_devN++]   = d;
    return d;
  }

  uint8_t m_addr = 0;
  uint8_t m_tx[8]  = {0};  size_t m_txN = 0;
  uint8_t m_rx[16] = {0};  size_t m_rxN = 0, m_rxI = 0;
  bool    m_pending = false;

  uint8_t m_devAddr[6] = {0};
  i2c_master_dev_handle_t m_dev[6] = {nullptr};
  int m_devN = 0;
};

static HostWire hostWire;
#define Wire hostWire
