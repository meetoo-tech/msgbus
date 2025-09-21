#ifndef __MSGBUS_PORT_H__
#define __MSGBUS_PORT_H__

#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <mqueue.h>

#include "bitmap.h"
#include "msgbus.h"

#ifndef CONFIG_MSGBUS_PUBLISH_TIMEOUT
#define CONFIG_MSGBUS_PUBLISH_TIMEOUT 500
#endif

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

int32_t msgbus_recv_ext_msg(msgbus_msg_t *msg_port);

msgbus_channel_t msgbus_port_init(void);

int32_t msgbus_port_channel_send(msgbus_channel_t channel, const void *msg, uint32_t msg_size);

int32_t msgbus_port_channel_send_timed(msgbus_channel_t channel, const void *msg, uint32_t msg_size, uint32_t timeout);

/* ext bus */
int msgbus_port_send(msgbus_msg_t *msg_port);

uint32_t msgbus_port_get_local_bus_id(void);

void msgbus_port_get_ext_bus_map(bitmap_t *pbus_map);
#endif