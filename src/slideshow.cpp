#include "slideshow.h"

File pngfile;
PNG png;

void ShowSlideShow(void) {
  for (int x = ContrastSet; x > 0; x--) {
    analogWrite(CONTRASTPIN, x * 2);
    delay(5);
  }

  File file = LittleFS.open("/slideshow.img", "r");
  byte header[8];
  file.read(header, sizeof(header));
  file.close();

  bool isJPG = false;
  bool isPNG = false;

  if (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47 && header[4] == 0x0D && header[5] == 0x0A && header[6] == 0x1A && header[7] == 0x0A) {
    isPNG = true;
  } else if (header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) {
    isJPG = true;
  }

  if (isJPG) {
    tft.fillScreen(TFT_BLACK);
    analogWrite(CONTRASTPIN, ContrastSet * 2 + 27);
    tft.startWrite();
    JPEGdecoder("/slideshow.img", tft);
    tft.endWrite();
  } else if (isPNG) {
    pngfile = LittleFS.open("/slideshow.img", "rb");
    if (!pngfile) {
      for (int x = 0; x <= ContrastSet; x++) {
        analogWrite(CONTRASTPIN, x * 2 + 27);
        delay(5);
      }
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
        static uint32_t pngBkgd;
        pngBkgd = png.hasAlpha() ? 0x00FFFFFF : 0xFFFFFFFF;
        uint16_t lineBuffer[320];
        png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_LITTLE_ENDIAN, pngBkgd);
        tft.pushImage((320 - png.getWidth()) / 2, ((240 - png.getHeight()) / 2) + pDraw->y, pDraw->iWidth, 1, lineBuffer);
        return 1;
      });

    if (rc != PNG_SUCCESS) {
      pngfile.close();
      for (int x = 0; x <= ContrastSet; x++) {
        analogWrite(CONTRASTPIN, x * 2 + 27);
        delay(5);
      }
      return;
    }

    tft.fillScreen(png.hasAlpha() ? TFT_WHITE : TFT_BLACK);
    tft.startWrite();
    rc = png.decode(nullptr, 0);
    png.close();
    tft.endWrite();
    pngfile.close();
  }

  for (int x = 0; x <= ContrastSet; x++) {
    analogWrite(CONTRASTPIN, x * 2 + 27);
    delay(5);
  }
}
