#ifndef YI_ROM_PROFILE_H
#define YI_ROM_PROFILE_H
#include <stddef.h>
#include <stdint.h>

typedef enum YiRomProfile {
  YI_ROM_UNSUPPORTED = -1,
  YI_ROM_US_10 = 0,
  YI_ROM_EU_11 = 1
} YiRomProfile;

#define YI_ROM_SIZE 2097152u
extern const uint8_t yi_known_sha256[2][32];
/* Hashes refer to the payload, without an optional 512-byte copier header. */
YiRomProfile YiIdentifyRom(const uint8_t *data, size_t size);
int YiRomIsPal(YiRomProfile profile);
double YiRomFrameRate(YiRomProfile profile);
#endif
