
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "msgbus_port.h"

#define QUEUE_NAME "/mbus_queue_port"

static mqd_t port_mq;
static pthread_t port_thread;

static void *msgbus_thread_handler(void *arg)
{
    char msg_buff[2048];
    while (1)
    {
        ssize_t res = mq_receive(port_mq, msg_buff, sizeof msg_buff, NULL);
        if (res > 0)
        {
            msgbus_system_msg_handler((msgbus_msg_t *)msg_buff);
        }
    }
}

static void msgbus_thread_init(void)
{
    pthread_create(&port_thread, NULL, (void *)msgbus_thread_handler, NULL);
}

msgbus_channel_t msgbus_port_init(void)
{
    int rc;
    struct mq_attr mqAttr;

    printf("msgbus_port_init.\n");
    rc = mq_unlink(QUEUE_NAME);
    if (rc < 0)
    {
        printf("Warning mq_unlink.\n");
    }
    mqAttr.mq_maxmsg = 10;
    mqAttr.mq_msgsize = 1024;
    // 创建消息队列
    port_mq = mq_open(QUEUE_NAME, O_CREAT | O_RDWR, S_IWUSR | S_IRUSR, &mqAttr);
    if (port_mq == -1)
    {
        perror("mq_open");
        return NULL;
    }
    msgbus_thread_init();

    return (msgbus_channel_t)port_mq;
}

int msgbus_port_send(msgbus_msg_t *msg_port)
{
    return 0;
}
uint32_t msgbus_port_get_local_bus_id(void)
{
    return 1;
}
void msgbus_port_get_ext_bus_map(bitmap_t *pbus_map)
{
    bitmap_set(pbus_map, 0);
}

int32_t msgbus_recv_ext_msg(msgbus_msg_t *msg_port)
{
    return msgbus_port_channel_send((msgbus_channel_t)port_mq, msg_port, sizeof(msgbus_msg_t) + msg_port->len);
}

int32_t msgbus_port_channel_send(msgbus_channel_t channel, const void *msg, uint32_t msg_size)
{
    mq_send((mqd_t)channel, msg, msg_size, 0);
    return 0;
}

int32_t msgbus_port_channel_send_timed(msgbus_channel_t channel, const void *msg, uint32_t msg_size, uint32_t timeout)
{
    return 0;
}
