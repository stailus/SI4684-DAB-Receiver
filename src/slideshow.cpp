#include "slideshow.h"

File pngfile;
File jpgfile;
PNG png;
JPEGDEC jpeg;

bool isJPG;
bool isPNG;

// JPEG dimensions for centering
static int jpegWidth = 0;
static int jpegHeight = 0;

// Progressive JPEG file-based storage
static File progFile;
static int progThumbWidth = 0;
static int progThumbHeight = 0;
static const char* PROG_TEMP_FILE = "/prog_temp.raw";

// JPEG draw callback - direct to screen
static int JPEGDraw(JPEGDRAW *pDraw) {
  int x = pDraw->x + (320 - jpegWidth) / 2;
  int y = pDraw->y + (240 - jpegHeight) / 2;
  tft.pushImage(x, y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
  return 1;
}

// JPEG draw callback for progressive - write pixels to file
static int JPEGDrawProgressiveToFile(JPEGDRAW *pDraw) {
  if (!progFile) return 1;

  // Write each row of pixels to the correct position in the file
  for (int row = 0; row < pDraw->iHeight; row++) {
    int srcY = pDraw->y + row;
    if (srcY >= progThumbHeight) continue;

    // Calculate file position for this row
    int srcX = pDraw->x;
    int writeWidth = min(pDraw->iWidth, progThumbWidth - srcX);
    if (writeWidth <= 0) continue;

    // Seek to correct position (row * width * 2 bytes per pixel + column offset)
    size_t filePos = (srcY * progThumbWidth + srcX) * sizeof(uint16_t);
    progFile.seek(filePos);

    // Write the pixels for this row
    progFile.write((uint8_t*)&pDraw->pPixels[row * pDraw->iWidth], writeWidth * sizeof(uint16_t));
  }
  return 1;
}

// Extract RGB components from RGB565
static inline void rgb565ToRgb(uint16_t color, int &r, int &g, int &b) {
  r = (color >> 11) & 0x1F;
  g = (color >> 5) & 0x3F;
  b = color & 0x1F;
}

// Convert RGB to RGB565
static inline uint16_t rgbToRgb565(int r, int g, int b) {
  return ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F);
}

// Read a pixel from the progressive temp file
static inline uint16_t readPixelFromFile(File &file, int x, int y) {
  size_t pos = (y * progThumbWidth + x) * sizeof(uint16_t);
  file.seek(pos);
  uint16_t pixel = 0;
  file.read((uint8_t*)&pixel, sizeof(uint16_t));
  return pixel;
}

// Bilinear interpolation for upscaling from file
static void drawBilinearScaledFromFile(int dstWidth, int dstHeight, int offsetX, int offsetY) {
  File readFile = LittleFS.open(PROG_TEMP_FILE, "r");
  if (!readFile) {
    Serial.println("Progressive: failed to open temp file for reading");
    return;
  }

  // Scale factors (fixed point 8.8)
  int scaleX = (progThumbWidth << 8) / dstWidth;
  int scaleY = (progThumbHeight << 8) / dstHeight;

  uint16_t lineBuffer[320];

  // Cache for two source rows to minimize file reads
  uint16_t* srcRow0 = (uint16_t*)malloc(progThumbWidth * sizeof(uint16_t));
  uint16_t* srcRow1 = (uint16_t*)malloc(progThumbWidth * sizeof(uint16_t));

  if (!srcRow0 || !srcRow1) {
    // Free any partially allocated memory
    if (srcRow0) free(srcRow0);
    if (srcRow1) free(srcRow1);

    // Fallback: read pixel by pixel (slower but works with minimal RAM)
    Serial.println("Progressive: low memory, using pixel-by-pixel read");

    for (int dstY = 0; dstY < dstHeight; dstY++) {
      int srcYFixed = dstY * scaleY;
      int srcY0 = srcYFixed >> 8;
      int srcY1 = min(srcY0 + 1, progThumbHeight - 1);
      int fracY = srcYFixed & 0xFF;

      for (int dstX = 0; dstX < dstWidth; dstX++) {
        int srcXFixed = dstX * scaleX;
        int srcX0 = srcXFixed >> 8;
        int srcX1 = min(srcX0 + 1, progThumbWidth - 1);
        int fracX = srcXFixed & 0xFF;

        // Read 4 neighboring pixels from file
        uint16_t p00 = readPixelFromFile(readFile, srcX0, srcY0);
        uint16_t p10 = readPixelFromFile(readFile, srcX1, srcY0);
        uint16_t p01 = readPixelFromFile(readFile, srcX0, srcY1);
        uint16_t p11 = readPixelFromFile(readFile, srcX1, srcY1);

        int r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11;
        rgb565ToRgb(p00, r00, g00, b00);
        rgb565ToRgb(p10, r10, g10, b10);
        rgb565ToRgb(p01, r01, g01, b01);
        rgb565ToRgb(p11, r11, g11, b11);

        int invFracX = 256 - fracX;
        int invFracY = 256 - fracY;

        int r = (r00 * invFracX * invFracY + r10 * fracX * invFracY +
                 r01 * invFracX * fracY + r11 * fracX * fracY) >> 16;
        int g = (g00 * invFracX * invFracY + g10 * fracX * invFracY +
                 g01 * invFracX * fracY + g11 * fracX * fracY) >> 16;
        int b = (b00 * invFracX * invFracY + b10 * fracX * invFracY +
                 b01 * invFracX * fracY + b11 * fracX * fracY) >> 16;

        lineBuffer[dstX] = rgbToRgb565(r, g, b);
      }
      tft.pushImage(offsetX, offsetY + dstY, dstWidth, 1, lineBuffer);
    }

    readFile.close();
    return;
  }

  // Optimized: cache two rows at a time
  int cachedY0 = -1;
  int cachedY1 = -1;

  for (int dstY = 0; dstY < dstHeight; dstY++) {
    int srcYFixed = dstY * scaleY;
    int srcY0 = srcYFixed >> 8;
    int srcY1 = min(srcY0 + 1, progThumbHeight - 1);
    int fracY = srcYFixed & 0xFF;

    // Load source rows if not cached
    if (srcY0 != cachedY0) {
      if (srcY0 == cachedY1) {
        // Row 0 is the old row 1, just swap pointers
        uint16_t* temp = srcRow0;
        srcRow0 = srcRow1;
        srcRow1 = temp;
        cachedY0 = srcY0;
      } else {
        // Load row 0 from file
        readFile.seek(srcY0 * progThumbWidth * sizeof(uint16_t));
        readFile.read((uint8_t*)srcRow0, progThumbWidth * sizeof(uint16_t));
        cachedY0 = srcY0;
      }
    }
    if (srcY1 != cachedY1) {
      // Load row 1 from file
      readFile.seek(srcY1 * progThumbWidth * sizeof(uint16_t));
      readFile.read((uint8_t*)srcRow1, progThumbWidth * sizeof(uint16_t));
      cachedY1 = srcY1;
    }

    for (int dstX = 0; dstX < dstWidth; dstX++) {
      int srcXFixed = dstX * scaleX;
      int srcX0 = srcXFixed >> 8;
      int srcX1 = min(srcX0 + 1, progThumbWidth - 1);
      int fracX = srcXFixed & 0xFF;

      // Get 4 neighboring pixels from cached rows
      uint16_t p00 = srcRow0[srcX0];
      uint16_t p10 = srcRow0[srcX1];
      uint16_t p01 = srcRow1[srcX0];
      uint16_t p11 = srcRow1[srcX1];

      int r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11;
      rgb565ToRgb(p00, r00, g00, b00);
      rgb565ToRgb(p10, r10, g10, b10);
      rgb565ToRgb(p01, r01, g01, b01);
      rgb565ToRgb(p11, r11, g11, b11);

      int invFracX = 256 - fracX;
      int invFracY = 256 - fracY;

      int r = (r00 * invFracX * invFracY + r10 * fracX * invFracY +
               r01 * invFracX * fracY + r11 * fracX * fracY) >> 16;
      int g = (g00 * invFracX * invFracY + g10 * fracX * invFracY +
               g01 * invFracX * fracY + g11 * fracX * fracY) >> 16;
      int b = (b00 * invFracX * invFracY + b10 * fracX * invFracY +
               b01 * invFracX * fracY + b11 * fracX * fracY) >> 16;

      lineBuffer[dstX] = rgbToRgb565(r, g, b);
    }

    tft.pushImage(offsetX, offsetY + dstY, dstWidth, 1, lineBuffer);
  }

  free(srcRow0);
  free(srcRow1);
  readFile.close();
}


// Draw full resolution image from temp file to screen (line by line)
static void drawFromFile(int width, int height, int offsetX, int offsetY) {
  File readFile = LittleFS.open(PROG_TEMP_FILE, "r");
  if (!readFile) {
    Serial.println("Progressive: failed to open temp file for reading");
    return;
  }

  uint16_t lineBuffer[320];

  for (int y = 0; y < height; y++) {
    // Read one line from file
    size_t bytesToRead = width * sizeof(uint16_t);
    readFile.read((uint8_t*)lineBuffer, bytesToRead);

    // Push to display
    tft.pushImage(offsetX, offsetY + y, width, 1, lineBuffer);
  }

  readFile.close();
}

// JPEG file callbacks for JPEGDEC library
static void *jpegOpen(const char *filename, int32_t *size) {
  jpgfile = LittleFS.open(filename, "rb");
  if (!jpgfile) return nullptr;
  *size = jpgfile.size();
  return &jpgfile;
}

static void jpegClose(void *handle) {
  if (jpgfile) jpgfile.close();
}

static int32_t jpegRead(JPEGFILE *pFile, uint8_t *buffer, int32_t length) {
  if (!jpgfile) return 0;
  return jpgfile.read(buffer, length);
}

static int32_t jpegSeek(JPEGFILE *pFile, int32_t position) {
  if (!jpgfile) return 0;
  return jpgfile.seek(position);
}

void ShowSlideShow(void) {
  for (int x = ContrastSet; x > 0; x--) {
    analogWrite(CONTRASTPIN, x * 2);
    delay(5);
  }

  auto restoreContrast = [&]() {
    for (int x = 0; x <= ContrastSet; x++) {
      analogWrite(CONTRASTPIN, x * 2 + 27);
      delay(5);
    }
  };

  auto closeFiles = [&]() {
    if (jpgfile) {
      jpgfile.close();
    }
    if (pngfile) {
      pngfile.close();
    }
  };

  File file = LittleFS.open("/slideshow.img", "r");
  byte header[8];
  size_t bytesRead = file.read(header, sizeof(header));
  file.close();
  if (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47 && header[4] == 0x0D && header[5] == 0x0A && header[6] == 0x1A && header[7] == 0x0A) {
    isJPG = false;
    isPNG = true;
  } else if (header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) {
    isJPG = true;
    isPNG = false;
  } else {
    isJPG = false;
    isPNG = false;
  }

  if (isJPG) {
    tft.fillScreen(TFT_BLACK);

    int rc = jpeg.open("/slideshow.img", jpegOpen, jpegClose, jpegRead, jpegSeek, JPEGDraw);
    if (!rc) {
      restoreContrast();
      return;
    }

    jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
    jpegWidth = jpeg.getWidth();
    jpegHeight = jpeg.getHeight();
    bool isProgressive = (jpeg.getJPEGType() == 1);

    if (isProgressive) {
      jpeg.close();

      // Progressive JPEG: JPEGDEC only decodes DC coefficients (first scan)
      // This gives a lower resolution image that we upscale
      // Try HALF scale first (better quality), fall back to QUARTER if needed

      size_t freeHeap = ESP.getFreeHeap();
      int scaleOption;
      int scaleDivisor;
      const char* scaleName;

      // Calculate buffer sizes for different scales
      size_t halfSize = (jpegWidth / 2) * (jpegHeight / 2) * sizeof(uint16_t);
      size_t quarterSize = (jpegWidth / 4) * (jpegHeight / 4) * sizeof(uint16_t);

      // Choose best scale that fits in memory with safety margin
      if (freeHeap > halfSize + 30000) {
        scaleOption = JPEG_SCALE_HALF;
        scaleDivisor = 2;
        scaleName = "half";
      } else if (freeHeap > quarterSize + 20000) {
        scaleOption = JPEG_SCALE_QUARTER;
        scaleDivisor = 4;
        scaleName = "quarter";
      } else {
        scaleOption = JPEG_SCALE_EIGHTH;
        scaleDivisor = 8;
        scaleName = "eighth";
      }

      progThumbWidth = jpegWidth / scaleDivisor;
      progThumbHeight = jpegHeight / scaleDivisor;

      // Create temp file for pixel storage
      LittleFS.remove(PROG_TEMP_FILE);
      progFile = LittleFS.open(PROG_TEMP_FILE, "w");
      if (!progFile) {
        Serial.println("Progressive: failed to create temp file");
        restoreContrast();
        return;
      }

      // Pre-allocate file size
      size_t fileSize = progThumbWidth * progThumbHeight * sizeof(uint16_t);
      uint8_t zeroBuf[256] = {0};
      size_t written = 0;
      while (written < fileSize) {
        size_t toWrite = min((size_t)256, fileSize - written);
        progFile.write(zeroBuf, toWrite);
        written += toWrite;
      }
      progFile.flush();

      Serial.printf("Progressive: %dx%d -> %dx%d (%s scale, %d bytes, free: %d)\n",
                    jpegWidth, jpegHeight, progThumbWidth, progThumbHeight,
                    scaleName, fileSize, freeHeap);

      // Re-open with callback to write to file
      int rc = jpeg.open("/slideshow.img", jpegOpen, jpegClose, jpegRead, jpegSeek, JPEGDrawProgressiveToFile);
      if (!rc) {
        Serial.println("Progressive: reopen failed");
        progFile.close();
        LittleFS.remove(PROG_TEMP_FILE);
        restoreContrast();
        return;
      }

      jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
      rc = jpeg.decode(0, 0, scaleOption);
      jpeg.close();
      progFile.close();

      if (rc) {
        // Calculate destination size and position
        int dstWidth = min(jpegWidth, 320);
        int dstHeight = min(jpegHeight, 240);
        int offsetX = (320 - dstWidth) / 2;
        int offsetY = (240 - dstHeight) / 2;

        Serial.printf("Progressive: bilinear scaling %dx%d -> %dx%d from file\n",
                      progThumbWidth, progThumbHeight, dstWidth, dstHeight);

        tft.startWrite();
        drawBilinearScaledFromFile(dstWidth, dstHeight, offsetX, offsetY);
        tft.endWrite();
      } else {
        Serial.printf("Progressive: decode failed, error=%d\n", jpeg.getLastError());
      }

      // Clean up temp file
      LittleFS.remove(PROG_TEMP_FILE);
    } else {
      // Baseline JPEG - direct decode to screen
      tft.startWrite();
      rc = jpeg.decode(0, 0, 0);
      tft.endWrite();
      jpeg.close();

      if (!rc) {
        restoreContrast();
        return;
      }
    }
  } else if (isPNG) {
    tft.fillScreen(TFT_BLACK);
    pngfile = LittleFS.open("/slideshow.img", "rb");
    if (!pngfile) {
      closeFiles();
      restoreContrast();
      return;
    }
    int16_t rc = png.open("/slideshow.img",
      +[](const char *filename, int32_t *size) -> void * {
        *size = pngfile.size();
        return &pngfile;
      },
      +[](void *handle) {
      },
      +[](PNGFILE *page, uint8_t *buffer, int32_t length) -> int32_t {
        if (!pngfile) return 0;
        return pngfile.read(buffer, length);
      },
      +[](PNGFILE *page, int32_t position) -> int32_t {
        if (!pngfile) return 0;
        return pngfile.seek(position);
      },
      +[](PNGDRAW *pDraw) {
        uint16_t lineBuffer[320];
        png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);
        tft.pushImage((320 - png.getWidth()) / 2, ((240 - png.getHeight()) / 2) + pDraw->y, pDraw->iWidth, 1, lineBuffer);
        return 1;
      });

    if (rc != PNG_SUCCESS) {
      closeFiles();
      restoreContrast();
      return;
    }

    tft.startWrite();
    rc = png.decode(nullptr, 0);
    png.close();
    tft.endWrite();
    closeFiles();
  }

  for (int x = 0; x <= ContrastSet; x++) {
    analogWrite(CONTRASTPIN, x * 2 + 27);
    delay(5);
  }
}
