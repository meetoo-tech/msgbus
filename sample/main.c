#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <mqueue.h>

#include "msgbus.h"

enum msg_topic
{
    MSG_TOPIC_TEST1 = 1,
};

int msgbus_port_channel_send(msgbus_channel_t channel, const void *msg, int msg_size)
{
    return mq_send((mqd_t)channel, msg, msg_size, 0);
}

mqd_t create_msgbus_channel(const char *mq_name)
{
    int rc;
    struct mq_attr mqAttr;
    mqd_t _mq;

    rc = mq_unlink(mq_name);
    if (rc < 0)
    {
        printf("Warning mq_unlink.\n");
    }
    mqAttr.mq_maxmsg = 10;
    mqAttr.mq_msgsize = 1024;
    // 创建消息队列
    _mq = mq_open(mq_name, O_CREAT | O_RDWR, S_IWUSR | S_IRUSR, &mqAttr);
    if (_mq == -1)
    {
        perror("mq_open");
        return NULL;
    }

    return _mq;
}
static void *msgbus_sys_thread_handler(void *arg)
{
    mqd_t sys_mq = (mqd_t)arg;
    char msg_buff[2048];
    while (1)
    {
        ssize_t res = mq_receive(sys_mq, msg_buff, sizeof msg_buff, NULL);
        if (res > 0)
        {
            msgbus_system_msg_handler((msgbus_msg_t *)msg_buff);
        }
    }
}

void msgbus_sys_init(mqd_t sys_mq, mqd_t ext_mq)
{
    int rc;
    pthread_t sys_thread;

    msgbus_config_t msgbus_config =
        {
            .local_bus_id = 1,
            .system_channel = sys_mq,
            .port_channel = ext_mq,
            .channel_msg_write_handler = msgbus_port_channel_send,
        };
    msgbus_init(&msgbus_config);
    pthread_create(&sys_thread, NULL, (void *)msgbus_sys_thread_handler, (void *)sys_mq);
}

static void *msgbus_test_thread_handler(void *arg)
{
    char msg_buff[2048];
    mqd_t msg_mq = (mqd_t)arg;
    msgbus_msg_t *msg = (msgbus_msg_t *)msg_buff;
    while (1)
    {
        ssize_t res = mq_receive(msg_mq, msg_buff, sizeof msg_buff, NULL);
        printf("msgbus_test_thread_handler: topic:%d,msg len:%d\n", msg->topic, msg->len);
        if (res > 0)
        {
            switch (msg->topic)
            {
            case MSG_TOPIC_SYNC_OVER:
                break;

            case MSG_TOPIC_TEST1:
                break;

            default:
                break;
            }
        }
    }
}

static const msgbus_topic_t test_topic_list[] = {
    MSG_TOPIC_SET_LOCAL(MSG_TOPIC_SYNC_OVER),
    MSG_TOPIC_TEST1,
};

static mqd_t test_mq, sys_mq, port_mq;
static pthread_t test_thread;

#define SYS_QUEUE_NAME "/mbus_mq_sys"
#define PORT_QUEUE_NAME "/mbus_mq_port"
#define TASK_QUEUE_NAME "/mbus_mq_task"
int main(int argc, char **argv)
{
    sys_mq = create_msgbus_channel(SYS_QUEUE_NAME);
    port_mq = create_msgbus_channel(PORT_QUEUE_NAME);
    test_mq = create_msgbus_channel(TASK_QUEUE_NAME);

    msgbus_sys_init(sys_mq, port_mq);
    /* 订阅消息 */
    msgbus_subscribe((msgbus_channel_t)test_mq, 0, test_topic_list, sizeof test_topic_list / sizeof(msgbus_topic_t));
    /* 启动消息处理线程 */
    pthread_create(&test_thread, NULL, (void *)msgbus_test_thread_handler, test_mq);
    /* 开启消息总线同步 */
    msgbus_sync();

    char test_buf[1024] = "hello world";
    while (1)
    {
        sleep(1);
        msgbus_publish(MSG_TOPIC_TEST1, test_buf, rand() % 512);
    }

    return 0;
}