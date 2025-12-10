#include "msgbus.h"
#include "rbtree.h"
#include "sslist.h"
#include "bitmap.h"
#include "msgbus_port.h"

enum
{
    TOPIC_BUS_SUB = MSG_TOPIC_SYSTEM_TOPIC_MAX,
    TOPIC_BUS_SYNC,
    TOPIC_BUS_EXT_SYNC,
};

/* 获取用户主题，通过最大值限制来实现 */
#define GET_USER_TOPIC(__topic) ((__topic) & MSG_TOPIC_MAX)

/* 检查topic是否为为本地 */
#define MSG_TOPIC_IS_LOCAL(__topic) ((__topic) & (0x80u << 24))

/* 检查topic是否为强制分发 */
#define MSG_TOPIC_IS_DISPATCHED(__topic) ((__topic) & (0x40u << 24))

typedef struct
{
    struct slist_head node;      // 订阅列表节点
    msgbus_user_t user_id;       // 用户标识符
    msgbus_channel_t channel;    // 接收数据队列
    uint32_t user_local_sub : 1; // 用户本地订阅
} sub_user_node_t;

typedef struct
{
    struct rb_node node;             // 树节点
    uint32_t topic_key;              // 主题键
    struct slist_head sub_user_list; // 订阅的用户列表
    bitmap_t sub_bus_map;            // 外部总线订阅表
} topic_node_t;

typedef struct
{
    struct rb_root topic_tree;                         // 主题红黑树
    msgbus_channel_t sys_channel;                      // 内部事件队列
    msgbus_channel_t ext_bus_channel;                  // 外部总线队列
    channel_msg_write_handler_t channel_write_handler; // 发送数据回调
    uint16_t bus_id;                                   // 当前总线编号
    uint16_t topic_total;                              // 当前主题总数量（本地+外部总线）
    uint16_t topic_local_num;                          // 本地已订阅的主题数量
    uint16_t init_flag : 1;                            // 已初始化标记
    uint16_t selfness_flag : 1;                        // 自私模式，不同步外部总线的主题
    uint16_t sync_start_flag : 1;                      // 开始同步主题标记
    uint16_t sync_over_flag : 1;                       // 已完成同步主题标记
    bitmap_t ext_bus_map;                              // 外部总线表
    bitmap_t ext_bus_map_sync;                         // 已同步外部总线表
} msgbus_context_t;

typedef struct
{
    msgbus_channel_t channel;     // 收到订阅数据的发送队列
    msgbus_user_t user_id;        // 订阅的用户
    uint32_t topic_num;           // 主题数量
    msgbus_topic_t topic_list[0]; // 订阅的主题列表
} topic_sub_data_t;

// 总线同步主题数据体
typedef struct
{
    uint32_t topic_num;           // 主题数量
    msgbus_topic_t topic_list[0]; // 订阅的主题列表
} topic_sync_data_t;

static msgbus_context_t msgbus_ctx;
/* 本地总线编号 */
#define LOCAL_BUS_ID (msgbus_ctx.bus_id)

#define SIZEOF_MSGBUS_MSG(_pmsg) ((_pmsg)->len + sizeof(msgbus_msg_t))

static topic_node_t *msg_topic_search(struct rb_root *root, uint32_t topic)
{
    struct rb_node *node = root->rb_node;

    while (node)
    {
        topic_node_t *data = rb_entry(node, topic_node_t, node);
        int32_t result;

        result = topic - data->topic_key;

        if (result < 0)
            node = node->rb_left;
        else if (result > 0)
            node = node->rb_right;
        else
            return data;
    }
    return NULL;
}

static int32_t msg_topic_insert(struct rb_root *root, topic_node_t *data)
{
    struct rb_node **new = &(root->rb_node), *parent = NULL;

    /* Figure out where to put new node */
    while (*new)
    {
        topic_node_t *this = rb_entry(*new, topic_node_t, node);
        int32_t result = data->topic_key - this->topic_key;

        parent = *new;
        if (result < 0)
            new = &((*new)->rb_left);
        else if (result > 0)
            new = &((*new)->rb_right);
        else
            return -1;
    }

    /* Add new node and rebalance tree. */
    rb_link_node(&data->node, parent, new);
    rb_insert_color(&data->node, root);

    return 0;
}

static int32_t msgbus_proc_event_publish(msgbus_msg_t *bus_msg)
{
    int32_t err = -1;
    topic_node_t *topic_node;
    uint32_t user_topic;
    uint32_t sender_bus_id = bus_msg->user_id;

    user_topic = GET_USER_TOPIC(bus_msg->topic);
    MBUS_PRINTF("[MBUS] proc event pub, topic: %" PRIu32 " bus id:%" PRIu32 "\r\n",
                bus_msg->topic, bus_msg->user_id);
    topic_node = msg_topic_search(&msgbus_ctx.topic_tree, user_topic);
    // 分发给本地订阅该主题的用户
    if (topic_node)
    {
        sub_user_node_t *sub_user;
        struct slist_head *pos;

        slist_for_each(pos, &topic_node->sub_user_list)
        {
            // 遍历订阅用户列表，依次发送消息
            sub_user = slist_entry(pos, sub_user_node_t, node);
            bus_msg->user_id = sub_user->user_id;
            bus_msg->topic = user_topic;
            err = msgbus_ctx.channel_write_handler(sub_user->channel, (const void *)bus_msg,
                                                   SIZEOF_MSGBUS_MSG(bus_msg));
            // MBUS_ASSERT(err == 0);
            if (err != 0)
            {
                MBUS_PRINTF("[MBUS] Publish channel:%p,Topic:%" PRIu32 " failed\n",
                            sub_user->channel, bus_msg->topic);
            }
        }
    }
    else
    {
        if (!msgbus_ctx.selfness_flag)
        { // 不存在的主题，发布错误
            MBUS_PRINTF("[MBUS] publish error unsubscribe topic, topic: %" PRIu32 "\r\n",
                        bus_msg->topic);
            return -1;
        }
    }
    // 分发给外部总线
    if (!MSG_TOPIC_IS_LOCAL(bus_msg->topic) &&
        bitmap_cnt(&msgbus_ctx.ext_bus_map))
    {
        uint32_t sub_bus_id = msgbus_ctx.selfness_flag
                                  ? bitmap_next(&msgbus_ctx.ext_bus_map, 0)
                                  : bitmap_next(&topic_node->sub_bus_map, 0);

        while (sub_bus_id)
        {
            if (sub_bus_id != sender_bus_id)
            { // 转发时，排除原始发送者
                bus_msg->user_id = sub_bus_id;
                err = msgbus_ctx.channel_write_handler(msgbus_ctx.ext_bus_channel,
                                                       bus_msg, SIZEOF_MSGBUS_MSG(bus_msg));
                if (err != 0)
                {
                    MBUS_PRINTF("[MBUS] Publish Ext bus id:%" PRIu32 " channel:%p,Topic:%" PRIu32 " failed\n",
                                bus_msg->user_id, msgbus_ctx.ext_bus_channel, bus_msg->topic);
                }
            }
            sub_bus_id = msgbus_ctx.selfness_flag
                             ? bitmap_next(&msgbus_ctx.ext_bus_map, sub_bus_id)
                             : bitmap_next(&topic_node->sub_bus_map, sub_bus_id);
        }
    }

    return err;
}

static topic_node_t *msgbus_create_topic_node(uint32_t topic)
{
    topic_node_t *topic_node;

    topic_node = MBUS_MALLOC(sizeof(topic_node_t));
    MBUS_ASSERT(topic_node);
    memset(topic_node, 0, sizeof(topic_node_t));
    topic_node->topic_key = topic;
    msg_topic_insert(&msgbus_ctx.topic_tree, topic_node);
    bitmap_set(&topic_node->sub_bus_map, 0);
    SINIT_LIST_HEAD(&topic_node->sub_user_list);
    msgbus_ctx.topic_total++;

    return topic_node;
}

static msgbus_msg_t *msgbus_create_topic_sync_data(uint32_t except_bus_id)
{
    topic_sync_data_t *topic_sync_data = NULL;
    msgbus_msg_t *msg_port = NULL;

    MBUS_PRINTF("[MBUS] msgbus_create_topic_sync_data except bus id:%" PRIu32 "\r\n", except_bus_id);

    msg_port = MBUS_MALLOC(sizeof(msgbus_msg_t) + sizeof(topic_sync_data_t) +
                           sizeof(msgbus_topic_t) * msgbus_ctx.topic_total);
    MBUS_ASSERT(msg_port);
    topic_sync_data = (topic_sync_data_t *)msg_port->msg_data;
    // 遍历红黑树中的主题节点，创建订阅主题列表
    topic_node_t *topic_node;
    struct rb_node *tree_node = rb_first(&msgbus_ctx.topic_tree);
    uint32_t count = 0;
    while (tree_node)
    {
        topic_node = container_of(tree_node, topic_node_t, node);
        // 打包除指定消息总线外的其他主题列表
        struct slist_head *pos;
        int sub_user_cnt = 0;
        slist_for_each(pos, &topic_node->sub_user_list)
        {
            sub_user_node_t *sub_user = slist_entry(pos, sub_user_node_t, node);
            if (!sub_user->user_local_sub)
            { /* 存在用户的非本地订阅 */
                topic_sync_data->topic_list[count++] = topic_node->topic_key;
                sub_user_cnt++;
                break;
            }
        }
        if (!sub_user_cnt)
        { /*没有内部用户订阅，检查外部总线订阅 */
            int sub_bus_cnt = bitmap_cnt(&topic_node->sub_bus_map);
            if (sub_bus_cnt &&
                (!except_bus_id || sub_bus_cnt > 1 || !bitmap_is_set(&topic_node->sub_bus_map, except_bus_id)))
            {
                topic_sync_data->topic_list[count++] = topic_node->topic_key;
            }
        }
        /* 取下条主题节点 */
        tree_node = rb_next(tree_node);
        if (count >= msgbus_ctx.topic_total)
        {
            break;
        }
    }
    topic_sync_data->topic_num = count;
    msg_port->len = sizeof(topic_sync_data_t) + sizeof(msgbus_topic_t) * count;
    msg_port->topic = TOPIC_BUS_EXT_SYNC;

    return msg_port;
}

static void msgbus_ext_bus_map_sync(void)
{
    uint32_t bus_total = 0, bus_sync_num = 0;
    msgbus_msg_t *msg_port = NULL;

    if (msgbus_ctx.sync_start_flag &&
        !msgbus_ctx.sync_over_flag)
    {
        bus_total = bitmap_cnt(&msgbus_ctx.ext_bus_map);
        bus_sync_num = bitmap_cnt(&msgbus_ctx.ext_bus_map_sync);

        if (!bus_total || bus_total == bus_sync_num)
        { /* 没有外部总线，或者首次同步时就已经全部收到其他总线的同步（只有一个外部总线的情况下） */
            uint32_t except_bus_id = bitmap_next(&msgbus_ctx.ext_bus_map, 0);
            while (except_bus_id)
            {
                msg_port = msgbus_create_topic_sync_data(except_bus_id);
                msg_port->user_id = except_bus_id;
                msgbus_ctx.channel_write_handler(msgbus_ctx.ext_bus_channel, msg_port,
                                                 SIZEOF_MSGBUS_MSG(msg_port));
                MBUS_FREE(msg_port);
                except_bus_id = bitmap_next(&msgbus_ctx.ext_bus_map, except_bus_id);
            }
            msg_port = MBUS_MALLOC(sizeof(msgbus_msg_t));
            MBUS_ASSERT(msg_port);
            memset(msg_port, 0, sizeof(msgbus_msg_t));
            msg_port->user_id = LOCAL_BUS_ID;
            msg_port->topic = MSG_TOPIC_SET_LOCAL(MSG_TOPIC_SYNC_OVER);
            msg_port->len = 0;
            msgbus_proc_event_publish(msg_port);
            MBUS_FREE(msg_port);
            msgbus_ctx.sync_over_flag = 1;
        }
        else if (bus_total && ((bus_total - 1) == bus_sync_num))
        { /* 只剩下一个未同步外部总线，向该总线发送当前主题列表 */
            uint32_t except_bus_id = bitmap_cmp(&msgbus_ctx.ext_bus_map,
                                                &msgbus_ctx.ext_bus_map_sync);
            msg_port = msgbus_create_topic_sync_data(except_bus_id);
            msg_port->user_id = except_bus_id;
            msgbus_ctx.channel_write_handler(msgbus_ctx.ext_bus_channel, msg_port,
                                             SIZEOF_MSGBUS_MSG(msg_port));
            MBUS_FREE(msg_port);
        }
    }
}

static int32_t msgbus_proc_event_sync(void)
{
    MBUS_PRINTF("[MBUS] proc event topic sync\r\n");
    msgbus_ctx.sync_start_flag = 1;

    msgbus_ext_bus_map_sync();
    return 0;
}

static void msgbus_add_ext_sync_topic(msgbus_msg_t *bus_msg)
{
    topic_node_t *topic_node;
    topic_sync_data_t *topic_sync_data = (topic_sync_data_t *)bus_msg->msg_data;

    MBUS_PRINTF("[MBUS] add extern topic, topic num: %" PRIu32 "\r\n", topic_sync_data->topic_num);
    // 将主题列表更新到本地总线主题列表
    for (size_t i = 0; i < topic_sync_data->topic_num; i++)
    {
        if (topic_sync_data->topic_list[i] == 0)
        {
            break;
        }

        topic_node = msg_topic_search(&msgbus_ctx.topic_tree,
                                      GET_USER_TOPIC(topic_sync_data->topic_list[i]));
        if (topic_node == NULL)
        { // 当前主题不存在，新建
            topic_node = msgbus_create_topic_node(GET_USER_TOPIC(topic_sync_data->topic_list[i]));
            MBUS_PRINTF("[MBUS] extern topic not exist, create. topic: %" PRIu32 ", topic total:%d\r\n",
                        topic_sync_data->topic_list[i],
                        msgbus_ctx.topic_total);
        }
        if (!bitmap_is_set(&topic_node->sub_bus_map, bus_msg->user_id))
        {
            bitmap_set(&topic_node->sub_bus_map, bus_msg->user_id);
        }
    }
}

static int32_t msgbus_proc_event_ext_sync(msgbus_msg_t *bus_msg)
{
    MBUS_PRINTF("[MBUS] recv_ext_msg, peer id:%" PRIu32 ",topic :%" PRIu32 ", len:%" PRIu32 "\r\n",
                bus_msg->user_id, bus_msg->topic, bus_msg->len);
    if (!bitmap_is_set(&msgbus_ctx.ext_bus_map_sync, bus_msg->user_id))
    { // 该外部总线没有同步过
        bitmap_set(&msgbus_ctx.ext_bus_map_sync, bus_msg->user_id);
        if (!msgbus_ctx.selfness_flag)
        {
            msgbus_add_ext_sync_topic(bus_msg);
        }
        msgbus_ext_bus_map_sync();
    }

    return 0;
}

static int32_t msgbus_proc_event_subscribe(msgbus_msg_t *bus_msg)
{
    topic_node_t *topic_node;
    uint32_t in_list = 0;
    struct slist_head *pos;
    uint32_t user_topic = 0;
    sub_user_node_t *sub_user_node;
    topic_sub_data_t *topic_sub_data = (topic_sub_data_t *)bus_msg->msg_data;

    MBUS_PRINTF("[MBUS] proc event sub, user: %" PRIu32 " topic num: %" PRIu32 ",\r\n",
                (uint32_t)topic_sub_data->user_id,
                topic_sub_data->topic_num);
    for (size_t i = 0; i < topic_sub_data->topic_num; i++)
    {
        if (topic_sub_data->topic_list[i] == 0)
        {
            break;
        }
        in_list = 0;
        user_topic = GET_USER_TOPIC(topic_sub_data->topic_list[i]);
        MBUS_PRINTF("[MBUS] subscribe, topic key: %" PRIu32 "\r\n", user_topic);
        topic_node = msg_topic_search(&msgbus_ctx.topic_tree, user_topic);
        if (topic_node == NULL)
        { // 当前主题不存在，新建
            topic_node = msgbus_create_topic_node(user_topic);
            msgbus_ctx.topic_local_num++;
        }
        else
        {
            // 检查主题下的订阅列表，判断订阅用户是否为重复订阅
            slist_for_each(pos, &topic_node->sub_user_list)
            {
                sub_user_node = slist_entry(pos, sub_user_node_t, node);
                if (sub_user_node->user_id == topic_sub_data->user_id &&
                    sub_user_node->channel == topic_sub_data->channel)
                {
                    in_list = 1;
                    break;
                }
            }
        }

        if (!in_list)
        { // 当前用户没有订阅当前主题，创建一个订阅节点
            sub_user_node = MBUS_MALLOC(sizeof(sub_user_node_t));
            MBUS_ASSERT(sub_user_node);
            memset(sub_user_node, 0, sizeof(sub_user_node_t));
            SINIT_LIST_HEAD(&sub_user_node->node);
            sub_user_node->user_id = topic_sub_data->user_id;
            sub_user_node->channel = topic_sub_data->channel;
            slist_add_tail(&sub_user_node->node, &topic_node->sub_user_list);
        }
        if (MSG_TOPIC_IS_LOCAL(topic_sub_data->topic_list[i]))
        {
            sub_user_node->user_local_sub = 1;
        }
    }

    return 0;
}

void msgbus_system_msg_handler(msgbus_msg_t *bus_msg)
{
    switch (bus_msg->topic)
    {
    case TOPIC_BUS_SUB:
        msgbus_proc_event_subscribe(bus_msg);
        break;

    case TOPIC_BUS_SYNC:
        msgbus_proc_event_sync();
        break;

    case TOPIC_BUS_EXT_SYNC:
        msgbus_proc_event_ext_sync(bus_msg);
        break;

    default:
        msgbus_proc_event_publish(bus_msg);
        break;
    }
}

int msgbus_init(msgbus_config_t *config)
{
    memset(&msgbus_ctx, 0, sizeof(msgbus_ctx));

    msgbus_ctx.sys_channel = config->system_channel;
    msgbus_ctx.ext_bus_channel = config->port_channel;
    msgbus_ctx.channel_write_handler = config->channel_msg_write_handler;
    msgbus_ctx.selfness_flag = config->is_selfness;
    msgbus_ctx.bus_id = config->local_bus_id;
    bitmap_copy(&msgbus_ctx.ext_bus_map, &config->ext_bus_map);

    msgbus_ctx.topic_tree = RB_ROOT;

    return 0;
}

int msgbus_subscribe(msgbus_channel_t channel, msgbus_user_t user_id,
                     const msgbus_topic_t *topic_list, int topic_num)
{
    msgbus_msg_t *bus_msg;
    int32_t res = 0;

    MBUS_PRINTF("[MBUS] msgbus_subscribe ,user:%" PRIu32 ",topic num:%" PRIu32 "\r\n", user_id, topic_num);
    MBUS_ASSERT(topic_num);

    bus_msg = MBUS_MALLOC(sizeof(msgbus_msg_t) + sizeof(topic_sub_data_t) + sizeof(msgbus_topic_t) * topic_num);
    MBUS_ASSERT(bus_msg);
    bus_msg->topic = TOPIC_BUS_SUB;
    bus_msg->len = sizeof(topic_sub_data_t) + sizeof(msgbus_topic_t) * topic_num;
    topic_sub_data_t *topic_sub_data = (topic_sub_data_t *)bus_msg->msg_data;
    topic_sub_data->channel = channel;
    topic_sub_data->user_id = user_id;
    topic_sub_data->topic_num = topic_num;
    memcpy(topic_sub_data->topic_list, topic_list, sizeof(msgbus_topic_t) * topic_num);
    res = msgbus_ctx.channel_write_handler(msgbus_ctx.sys_channel, bus_msg,
                                           SIZEOF_MSGBUS_MSG(bus_msg));
    MBUS_FREE(bus_msg);
    return res;
}

int msgbus_sync(void)
{
    msgbus_msg_t msg = {0};

    msg.topic = TOPIC_BUS_SYNC;

    return msgbus_ctx.channel_write_handler(msgbus_ctx.sys_channel, &msg, SIZEOF_MSGBUS_MSG(&msg));
}

int msgbus_publish(msgbus_topic_t topic, const void *data, int data_len)
{
    msgbus_msg_t *bus_msg;
    int32_t res = 0;

    if (topic == MSG_TOPIC_NULL || GET_USER_TOPIC(topic) >= MSG_TOPIC_USER_MAX)
    {
        return -1;
    }
    bus_msg = MBUS_MALLOC(sizeof(msgbus_msg_t) + data_len);
    MBUS_ASSERT(bus_msg);
    bus_msg->topic = topic;
    bus_msg->len = data_len;
    bus_msg->user_id = LOCAL_BUS_ID;
    if (data != NULL && data_len)
    {
        memcpy(bus_msg->msg_data, data, data_len);
    }
    res = msgbus_ctx.channel_write_handler(msgbus_ctx.sys_channel, bus_msg, SIZEOF_MSGBUS_MSG(bus_msg));
    MBUS_FREE(bus_msg);

    return res;
}
