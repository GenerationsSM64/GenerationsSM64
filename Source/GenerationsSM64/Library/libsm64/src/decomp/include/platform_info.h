#ifndef PLATFORM_INFO_H
#define PLATFORM_INFO_H

#ifdef TARGET_N64
#define IS_64_BIT 0
#define IS_BIG_ENDIAN 1
#else
#include <stdint.h>
#define IS_64_BIT 0
#define IS_BIG_ENDIAN 0
#endif

#define DOUBLE_SIZE_ON_64_BIT(size) ((size) * (sizeof(void *) / 4))

#endif // PLATFORM_INFO_H
