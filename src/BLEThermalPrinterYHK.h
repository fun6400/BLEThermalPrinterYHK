/*
 * BLEThermalPrinterYHK.h
 * by Hans Schou <hans@schou.dk> © 2026
 * SPDX-License-Identifier: MIT
 *
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

#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>

class BLEThermalPrinterYHK {
public:
  BLEThermalPrinterYHK();
  ~BLEThermalPrinterYHK();

  // Scans for a known printer and connects to the first one found.
  // Blocks for up to scanTimeoutMs while scanning, then settleMs more
  // to let the stack settle before returning. Returns true on success.
  bool begin(uint32_t scanTimeoutMs = 5000, uint32_t settleMs = 1000);

  // Disconnects and frees the BLE client. Safe to call even if never
  // connected. begin() calls this internally before a new attempt.
  void end();

  bool isConnected() const;

  // Sends whatever is currently in a u8g2 instance's RAM buffer.
  // Template so this file doesn't need to depend on U8g2lib.h directly;
  // works with any object exposing getBufferPtr()/getWidth()/getHeight()
  // (i.e. any u8g2 "_F_" full-buffer constructor).
  template <typename U8G2_T>
  bool printCanvas(U8G2_T &u8g2) {
    return printBitmapFromU8g2(u8g2.getBufferPtr(), u8g2.getWidth(), u8g2.getHeight());
  }

  // Sends a raw 1-bit-per-pixel bitmap: MSB-first, row-major, with each
  // row padded up to a whole number of bytes (same layout as a PBM P4,
  // header stripped).
  bool printBitmap(const uint8_t *buf, uint16_t width, uint16_t height);

  // Advances the paper without printing anything (e.g. to clear the tear bar).
  void feed(uint8_t lines = 3);

  // Best-effort status/info queries. These write a command and wait for
  // a BLE notification in reply; not all YHK clones answer identically,
  // so treat a false return as "unsupported/no reply" rather than an error.
  bool queryStatus(std::vector<uint8_t> &reply, uint32_t timeoutMs = 1000);
  bool querySerialNumber(std::vector<uint8_t> &reply, uint32_t timeoutMs = 1000);
  bool queryProductName(std::vector<uint8_t> &reply, uint32_t timeoutMs = 1000);

  const NimBLEAddress &getAddress() const { return _address; }
  const std::string &getName() const { return _name; }

private:
  bool printBitmapFromU8g2(const uint8_t *tileBuf, uint16_t width, uint16_t height);
  bool scanForPrinter(uint32_t scanTimeoutMs, uint32_t settleMs);
  void writeChunked(const uint8_t *data, size_t length);
  bool sendCommandAndWait(const std::vector<uint8_t> &cmd, std::vector<uint8_t> &reply, uint32_t timeoutMs);

  static void onNotify(NimBLERemoteCharacteristic *pChar, uint8_t *pData, size_t length, bool isNotify);

  NimBLEClient *_client = nullptr;
  NimBLERemoteCharacteristic *_writeChar = nullptr;
  NimBLERemoteCharacteristic *_notifyChar = nullptr;
  NimBLEAddress _address;
  std::string _name;

  bool _connected = false;

  // NimBLE's subscribe() wants a plain function pointer, so only one
  // BLEThermalPrinterYHK instance can usefully receive notifications at a time.
  // Fine for the common single-printer case; flagged here so it's not
  // a silent surprise if you instantiate more than one.
  static BLEThermalPrinterYHK *_activeInstance;
  volatile uint32_t _notifyCount = 0;
  std::vector<uint8_t> _lastNotify;
};
