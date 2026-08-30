/*
  PrintText

  Copyright (c) 2026 Hans Schou

  Uses u8g2 purely as an in-RAM drawing canvas (no real display attached)
  -- draw with the normal u8g2 API (text, shapes, bitmaps...), then hand
  the finished buffer straight to the printer.

*/

#include <U8g2lib.h>
#include <BLEThermalPrinterYHK.h>

// ---- canvas size: must both be multiples of 8 ----
#define CANVAS_WIDTH_PX 384  // 58mm printers are commonly 384px wide
#define CANVAS_HEIGHT_PX 88  // however many print rows you want in RAM at once

#define CANVAS_TILE_W (CANVAS_WIDTH_PX / 8)
#define CANVAS_TILE_H (CANVAS_HEIGHT_PX / 8)

static const u8x8_display_info_t printer_specification = {
  .chip_enable_level = 0,
  .chip_disable_level = 1,
  .post_chip_enable_wait_ns = 0,
  .pre_chip_disable_wait_ns = 0,
  .reset_pulse_width_ms = 0,
  .post_reset_wait_ms = 0,
  .sda_setup_time_ns = 0,
  .sck_pulse_width_ns = 0,
  .sck_clock_hz = 0,
  .spi_mode = 0,
  .i2c_bus_clock_100kHz = 0,
  .data_setup_time_ns = 0,
  .write_pulse_width_ns = 0,
  .tile_width = CANVAS_TILE_W,
  .tile_height = CANVAS_TILE_H,
  .default_x_offset = 0,
  .flipmode_x_offset = 0,
  .pixel_width = CANVAS_WIDTH_PX,
  .pixel_height = CANVAS_HEIGHT_PX
};

uint8_t u8x8_d_virtual_canvas(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  switch (msg) {
    case U8X8_MSG_DISPLAY_SETUP_MEMORY:
      u8x8_d_helper_display_setup_memory(u8x8, &printer_specification);
      return 1;
    default:
      // no real hardware messages to handle -- we only care about the RAM buffer
      return 1;
  }
}

// Formats a status reply into a printable line, ready for drawStr().
// Falls back to a placeholder message if querying failed.
void formatStatusReply(BLEThermalPrinterYHK &printer, char *out, size_t outSize) {
  std::vector<uint8_t> reply;
  if (!printer.queryStatus(reply)) {
    snprintf(out, outSize, "<no status received>");
    return;
  }

  size_t n = 0;
  while (n < reply.size() && n < outSize - 1 && reply[n] >= 0x20) {
    out[n] = (char)reply[n];
    n++;
  }
  out[n] = '\0';
}

// Plain U8G2 base object -- no built-in comms, since we supply our own
// display driver + dummy callbacks below instead of a stock constructor.
U8G2 u8g2;
static uint8_t canvas_buf[CANVAS_TILE_W * 8 * CANVAS_TILE_H];

BLEThermalPrinterYHK printer;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  // u8x8_cad_empty / u8x8_byte_empty / u8x8_dummy_cb are built into u8g2 itself
  // (see csrc/u8x8_setup.c) -- no need to write our own no-op callback.
  u8g2_SetupDisplay(u8g2.getU8g2(), u8x8_d_virtual_canvas, u8x8_cad_empty, u8x8_byte_empty, u8x8_dummy_cb);
  u8g2_SetupBuffer(u8g2.getU8g2(), canvas_buf, CANVAS_TILE_H, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);

  Serial.println("Searching for a printer...");
  if (!printer.begin()) {
    Serial.println("No printer found / failed to connect.");
    return;
  }
  Serial.printf("Connected to MAC address: %s\n", printer.getAddress().toString().c_str());

  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, u8g2.getWidth(), u8g2.getHeight());
  u8g2.drawLine(0, u8g2.getHeight() - 1, u8g2.getWidth(), 0);
  u8g2.setFont(u8g2_font_helvB12_tf);  // Can do UTF8
  u8g2.drawUTF8(4, 20, "BLEThermalPrinterYHK © 2026 Hans Schou");
  u8g2.drawUTF8(4, 40, "¡Hælløv, YHK printer!");
  u8g2.drawUTF8(4, 60, printer.getName().empty() ? "<has-no-name>" : printer.getName().c_str());

  u8g2.setFont(u8g2_font_fub14_tf);  // Font is big
  u8g2.drawStr(210, u8g2.getHeight() - 24, printer.getAddress().toString().c_str());

  u8g2.setFont(u8g2_font_6x13_mf);  // Is NOT transparant (writing over crossed line)
  char line[64];
  formatStatusReply(printer, line, sizeof(line));
  u8g2.drawStr(160, u8g2.getHeight() - 8, line);

  bool ok = printer.printCanvas(u8g2);
  Serial.println(ok ? "Canvas sent to printer." : "Print failed (not connected?)");

  //u8g2.writeBufferPBM(Serial); // Dump NetPBM P1 format image to Serial console

  printer.end();
}

void loop() {
  delay(1000);
}
