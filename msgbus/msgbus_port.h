#ifndef __MSGBUS_PORT_H__
#define __MSGBUS_PORT_H__

#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define MBUS_PRINTF printf
#define MBUS_MALLOC malloc
#define MBUS_FREE free
#define MBUS_ASSERT(_cond)                                            \
    do                                                                \
    {                                                                 \
        if (!(_cond))                                                 \
        {                                                             \
            MBUS_PRINTF("MBUS_ASSERT:%s %d\r\n", __func__, __LINE__); \
        }                                                             \
    } while (0)

#endif