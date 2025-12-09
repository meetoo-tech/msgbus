#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <mqueue.h>

#include "msgbus.h"

static mqd_t test_mq;
static pthread_t test_thread;

enum msg_topic
{
    MSG_TOPIC_TEST1 = 1,
};

static void *msgbus_test_thread_handler(void *arg)
{
    char msg_buff[2048];

    msgbus_msg_t *msg = (msgbus_msg_t *)msg_buff;
    while (1)
    {
        ssize_t res = mq_receive(test_mq, msg_buff, sizeof msg_buff, NULL);
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

static int msgbus_test_init(void)
{
    // 创建消息队列
    struct mq_attr mqAttr;
    int rc;
    const char *mq_name = "/msgbus_test";

    rc = mq_unlink(mq_name);
    if (rc < 0)
    {
        printf("Warning mq_unlink.\n");
    }
    mqAttr.mq_maxmsg = 10;
    mqAttr.mq_msgsize = 1024;

    test_mq = mq_open(mq_name, O_CREAT | O_RDWR, S_IWUSR | S_IRUSR, &mqAttr);
    if (test_mq == -1)
    {
        perror("mq_open");
        return -1;
    }

    /* 订阅消息 */
    msgbus_subscribe((msgbus_channel_t)test_mq, 0, test_topic_list, sizeof test_topic_list / sizeof(msgbus_topic_t));

    /* 启动消息处理线程 */
    pthread_create(&test_thread, NULL, (void *)msgbus_test_thread_handler, NULL);

    return 0;
}

int main(int argc, char **argv)
{
    msgbus_init();
    msgbus_test_init();
    msgbus_sync();
    char test_buf[1024] = "hello world";
    while (1)
    {
        sleep(1);
        msgbus_publish(MSG_TOPIC_TEST1, test_buf, rand() % 512);
    }

    return 0;
}