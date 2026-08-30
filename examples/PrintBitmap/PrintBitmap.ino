/*
  PrintBitmap

  Scans for a YHK-compatible printer, connects, and prints a raw
  1bpp bitmap (same format/content as the original demo's bitmap.h).
  Put your own bitmap.h alongside this sketch, generated the same
  way as the reference project's.
*/

#include <BLEThermalPrinterYHK.h>
#include "bitmap.h"  // provides: bitmap[], BITMAP_WIDTH_BYTES, BITMAP_HEIGHT

BLEThermalPrinterYHK printer;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("Searching for a printer...");
  if (!printer.begin()) {
    Serial.println("No printer found / failed to connect.");
    return;
  }
  Serial.printf("Connected: %s\n", printer.getAddress().toString().c_str());

  bool ok = printer.printBitmap(bitmap, 8 * BITMAP_WIDTH_BYTES, BITMAP_HEIGHT);
  Serial.println(ok ? "Graphics sent." : "Print failed (not connected?)");

  printer.end();
}

void loop() {
  delay(1000);
}
