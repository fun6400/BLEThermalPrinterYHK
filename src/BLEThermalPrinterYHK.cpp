/*
 * BLEThermalPrinterYHK
 * by Hans Schou <hans@schou.dk> © 2026
 * SPDX-License-Identifier: MIT
 *
 * Minimal BLE driver for YHK-compatible thermal printers (graphics-only
 * models). Handles scanning, connecting, and sending ESC/POS raster
 * graphics. Pairs naturally with u8g2 as an off-screen drawing canvas:
 * draw whatever you like with u8g2's normal API, then hand the finished
 * buffer to printCanvas().
 *
 * Protocol / UUIDs reverse-engineered from vendor scan + graphics demos.
 * Based on the same BLE service family as tiny_print_library
 * (https://github.com/ramo828/tiny_print_library/).
 *
 * IMPORTANT: printCanvas() only works with u8g2's *full frame buffer*
 * constructors (the ones with "_F_" in the class name), since it reads
 * the whole buffer at once. Paged ("_1_" or "_2_") constructors won't
 * work because only one page is resident in RAM at a time.
 */

#include "BLEThermalPrinterYHK.h"
#include <algorithm>
#include <cstring>

namespace {

const NimBLEUUID SERVICE_UUID("49535343-fe7d-4ae5-8fa9-9fafd205e455");  // Microchip Transparent UART (or clone)
const NimBLEUUID WRITE_UUID("49535343-8841-43f4-a8d4-ecbe34729bb3");    // Write, no response
const NimBLEUUID NOTIFY_UUID("49535343-1e4d-4bd9-ba61-23c647249616");   // Notify

const std::vector<NimBLEUUID> KNOWN_SERVICES = {
  NimBLEUUID("49535343-fe7d-4ae5-8fa9-9fafd205e455"),  // 128-bit: Microchip Transparent UART
  NimBLEUUID((uint16_t)0xAF30),                        // 16-bit: Thermal printers like Peripage/Phomemo
};

bool isKnownService(const NimBLEUUID &uuid) {
  for (const auto &known : KNOWN_SERVICES) {
    if (uuid.equals(known)) return true;
  }
  return false;
}

// Local scan callback: remembers the first advertised device that
// exposes a known printer service, then stops caring about the rest.
class ScanCB : public NimBLEScanCallbacks {
public:
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    if (_found || !dev->haveServiceUUID()) return;
    for (size_t i = 0; i < dev->getServiceUUIDCount(); i++) {
      if (isKnownService(dev->getServiceUUID(i))) {
        if (dev->haveName()) _name = dev->getName();
        _address = dev->getAddress();
        _found = true;
        return;
      }
    }
  }
  bool found() const { return _found; }
  const NimBLEAddress &address() const { return _address; }
  const std::string &name() const { return _name; }

private:
  bool _found = false;
  NimBLEAddress _address;
  std::string _name;
  bool _debug = false;
  
};

constexpr size_t CHUNK_SIZE = 48;  // BLE write-safe chunk

}  // namespace

BLEThermalPrinterYHK *BLEThermalPrinterYHK::_activeInstance = nullptr;

BLEThermalPrinterYHK::BLEThermalPrinterYHK() {}

BLEThermalPrinterYHK::~BLEThermalPrinterYHK() {
  end();
}

bool BLEThermalPrinterYHK::scanForPrinter(uint32_t scanTimeoutMs, uint32_t settleMs) {
  static bool bleInited = false;
  if (!bleInited) {
    NimBLEDevice::init("");
    bleInited = true;
  }

  ScanCB cb;
  NimBLEScan *pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&cb, false);
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);
  pScan->start(scanTimeoutMs, false);

  int32_t remaining = (int32_t)scanTimeoutMs;
  const int32_t step = 10;
  while (remaining > 0 && !cb.found()) {
    delay(step);
    remaining -= step;
  }
  delay(settleMs);

  if (!cb.found()) return false;
  _address = cb.address();
  _name = cb.name();
  return true;
}

bool BLEThermalPrinterYHK::begin(uint32_t scanTimeoutMs, uint32_t settleMs) {
  end();  // clean up any previous session first

  if (!scanForPrinter(scanTimeoutMs, settleMs)) {
    return false;
  }

  _client = NimBLEDevice::createClient();
  if (!_client->connect(_address)) {
    NimBLEDevice::deleteClient(_client);
    _client = nullptr;
    return false;
  }

  NimBLERemoteService *pService = _client->getService(SERVICE_UUID);
  if (pService == nullptr) {
    // Some clones hide the service until after a security handshake.
    _client->secureConnection();
    pService = _client->getService(SERVICE_UUID);
  }
  if (pService == nullptr) {
    end();
    return false;
  }

  _writeChar = pService->getCharacteristic(WRITE_UUID);
  _notifyChar = pService->getCharacteristic(NOTIFY_UUID);

  if (_writeChar == nullptr) {
    end();
    return false;
  }

  if (_notifyChar != nullptr && _notifyChar->canNotify()) {
    _activeInstance = this;
    _notifyChar->subscribe(true, BLEThermalPrinterYHK::onNotify);
  }

  _connected = true;
  return true;
}

void BLEThermalPrinterYHK::end() {
  if (_client != nullptr) {
    if (_client->isConnected()) _client->disconnect();
    NimBLEDevice::deleteClient(_client);
    _client = nullptr;
  }
  _writeChar = nullptr;
  _notifyChar = nullptr;
  _connected = false;
  if (_activeInstance == this) _activeInstance = nullptr;
}

bool BLEThermalPrinterYHK::isConnected() const {
  return _connected;
}

void BLEThermalPrinterYHK::onNotify(NimBLERemoteCharacteristic *pChar, uint8_t *pData, size_t length, bool isNotify) {
  if (_activeInstance == nullptr) return;
  _activeInstance->_lastNotify.assign(pData, pData + length);
  _activeInstance->_notifyCount++;
}

void BLEThermalPrinterYHK::writeChunked(const uint8_t *data, size_t length) {
  size_t offset = 0;
  while (offset < length) {
    size_t chunk = std::min(CHUNK_SIZE, length - offset);

    bool ok = false;
    for (int attempt = 0; attempt < 10 && !ok; attempt++) {
      ok = _writeChar->writeValue(data + offset, chunk, false);
      if (!ok) delay(10);
    }
    // If it still failed after retries we just move on; a single dropped
    // chunk usually shows up as a blank stripe rather than a hard error.

    offset += chunk;
    delay(15);
  }
}

bool BLEThermalPrinterYHK::printBitmap(const uint8_t *buf, uint16_t width, uint16_t height) {
  if (!_connected || _writeChar == nullptr) return false;

  const uint8_t init[] = { 0x1B, 0x40 };  // ESC @ : initialize printer
  writeChunked(init, sizeof(init));

  uint16_t widthBytes = (width + 7) / 8;

  // ESC/POS raster graphics header: 0x1D 0x76 0x30 0x00 xL xH yL yH
  // Sent one row at a time (yL/yH = 1), matching the reference demo.
  for (uint16_t row = 0; row < height; row++) {
    uint8_t header[8] = {
      0x1D, 0x76, 0x30, 0x00,
      (uint8_t)(widthBytes & 0xFF), (uint8_t)(widthBytes >> 8),
      0x01, 0x00
    };
    writeChunked(header, sizeof(header));
    writeChunked(buf + (size_t)row * widthBytes, widthBytes);
  }

  feed(3);  // clear the tear bar
  return true;
}

bool BLEThermalPrinterYHK::printBitmapFromU8g2(const uint8_t *buf, uint16_t width, uint16_t height) {
  if (buf == nullptr || width == 0 || height == 0) return false;

  // u8g2's full-frame ("_F_") buffer is organized like most OLED
  // controllers: one byte per pixel-column per 8-row "page", bit 0 =
  // top row of that page. buf[page * width + x], page = y / 8.
  //
  // The printer wants row-major, MSB-first, 1 bit per pixel (bit 7 =
  // leftmost pixel of the byte) -- matching the standard Netpbm P4 /
  // Epson ESC/POS raster convention. Verified directly against a real
  // P4 test file (frame + diagonal cross + text): decoding it MSB-first
  // gives a clean image, LSB-first mirrors each 8-pixel group and turns
  // the text to garbage.
  uint16_t widthBytes = (width + 7) / 8;
  std::vector<uint8_t> out((size_t)widthBytes * height, 0);

  for (uint16_t y = 0; y < height; y++) {
    uint16_t page = y >> 3;
    uint8_t bit = y & 7;
    for (uint16_t x = 0; x < width; x++) {
      uint8_t srcByte = buf[(size_t)page * width + x];
      if (srcByte & (1 << bit)) {
        out[(size_t)y * widthBytes + (x >> 3)] |= (0x80 >> (x & 7));
      }
    }
  }

  return printBitmap(out.data(), width, height);
}

void BLEThermalPrinterYHK::feed(uint8_t lines) {
  if (!_connected || _writeChar == nullptr || lines == 0) return;
  std::vector<uint8_t> buf(lines, 0x0A);
  writeChunked(buf.data(), buf.size());
}

bool BLEThermalPrinterYHK::sendCommandAndWait(const std::vector<uint8_t> &cmd, std::vector<uint8_t> &reply, uint32_t timeoutMs) {
  if (!_connected || _writeChar == nullptr) return false;

  uint32_t before = _notifyCount;
  _writeChar->writeValue(cmd.data(), cmd.size(), false);

  int32_t remaining = (int32_t)timeoutMs;
  const int32_t step = 10;
  while (remaining > 0 && _notifyCount == before) {
    delay(step);
    remaining -= step;
  }

  if (_notifyCount == before) return false;
  reply = _lastNotify;
  return true;
}

bool BLEThermalPrinterYHK::queryStatus(std::vector<uint8_t> &reply, uint32_t timeoutMs) {
  return sendCommandAndWait({ 0x1E, 0x47, 0x03 }, reply, timeoutMs);
}

bool BLEThermalPrinterYHK::querySerialNumber(std::vector<uint8_t> &reply, uint32_t timeoutMs) {
  return sendCommandAndWait({ 0x1D, 0x67, 0x39 }, reply, timeoutMs);
}

bool BLEThermalPrinterYHK::queryProductName(std::vector<uint8_t> &reply, uint32_t timeoutMs) {
  return sendCommandAndWait({ 0x1D, 0x67, 0x69 }, reply, timeoutMs);
}
