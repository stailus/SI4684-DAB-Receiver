#pragma once
#include <LittleFS.h>
#include <TFT_eSPI.h>

// Decode a progressive JPEG from LittleFS and render to TFT display.
// Uses multi-pass row-by-row decoding to minimize RAM usage (~30KB).
// Supports up to 320x240 pixels, YCbCr 4:4:4, 4:2:2, and 4:2:0.
// Returns true on success.
bool decodeProgressiveJPEG(const char* filename, TFT_eSPI& tft, int displayWidth = 320, int displayHeight = 240);
