#ifndef SLIDESHOW_H
#define SLIDESHOW_H

#include "Arduino.h"
#include <FS.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <JPEGDEC.h>                // https://github.com/bitbank2/JPEGDEC
#include <PNGdec.h>                 // https://github.com/bitbank2/PNGdec
#include "si4684.h"
#include "constants.h"

extern byte ContrastSet;

extern TFT_eSPI tft;
extern DAB radio;

void ShowSlideShow(void);

#endif