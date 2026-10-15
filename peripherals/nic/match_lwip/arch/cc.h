#ifndef MATCH_LWIP_CC_H
#define MATCH_LWIP_CC_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#define LWIP_PLATFORM_DIAG(x) do { printf x; } while(0)
#define LWIP_PLATFORM_ASSERT(x) do { fprintf(stderr,"lwIP assertion: %s\n",x); abort(); } while(0)
#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif
#endif
