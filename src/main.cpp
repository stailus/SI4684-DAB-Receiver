// PlatformIO wrapper - includes the main .ino file from project root
// Arduino IDE compiles the .ino directly and ignores this file (empty without PLATFORMIO flag)
#ifdef PLATFORMIO
#include "../SI4684-DAB-Receiver.ino"
#endif
