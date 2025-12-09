#ifndef __MSGBUS_H__
#define __MSGBUS_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

    typedef uint32_t msgbus_topic_t;
    typedef uint32_t msgbus_user_t;
    typedef void *msgbus_channel_t;

    typedef struct
    {
        msgbus_topic_t topic;  /* 主题 值不能为0*/
        uint32_t len;          /* msg_data数据长度 */
        msgbus_user_t user_id; /* 用户ID */
        char msg_data[0];      /* 可能的数据 */
    } msgbus_msg_t;

/* 设置本次发布，主题属性为本地 */
#define MSG_TOPIC_SET_LOCAL(__topic) ((__topic) | (msgbus_topic_t)(0x80u << 24))

/* 设置本次发布，主题属性为强制派发 */
#define MSG_TOPIC_SET_DISPATCHED(__topic) ((__topic) | (msgbus_topic_t)(0x40u << 24))

    /* 主题的限定以及提供一些专用主题 */
    typedef enum
    {
        MSG_TOPIC_NULL = 0,                           /* 主题不能为0 */
        MSG_TOPIC_USER_MAX = 0xFFFFFF - 0x100,        // 供用户使用的最大topic编号
        MSG_TOPIC_SYNC_OVER,                          // 通知总线同步结束专用主题，在下发同步指令且总线与相邻总线同步结束时，向本地发布，由用户订阅
        MSG_TOPIC_RESYNC,                             // 通知总线出现过重新同步的专用主题，比如对端总线出现过复位。
        MSG_TOPIC_SYSTEM_TOPIC_MAX = 0xFFFFFF - 0x20, // 系统主题最大值
        MSG_TOPIC_MAX = 0xFFFFFF,                     // 主题最大值
    } msgbus_topic_internal_t;

    /**
     * @brief 消息总线初始化。
     *
     * @return int32_t
     */
    int32_t msgbus_init(void);

    /**
     * @brief 使用指定消息通道，向消息总线订阅主题。
     *
     * @param channel 消息通道，当订阅的主题有消息发布时，会通过port层的接口向该通道发送。
     * @param topic_list 主题列表
     * @param topic_num 主题数量
     * @return int32_t =0：成功，其他：错误
     */
    int32_t msgbus_subscribe(msgbus_channel_t channel, msgbus_user_t user_id, const msgbus_topic_t *topic_list, uint32_t topic_num);

    /**
     * @brief 在当前总线所有订阅完成后，与其他总线同步主题列表。
     *
     * @return int32_t
     */
    int32_t msgbus_sync(void);


    /**
     * @brief 向指定主题发布消息。
     *
     * @param topic 主题
     * @param data 消息数据
     * @param data_len 数据长度
     * @return int32_t
     */
    int32_t msgbus_publish(msgbus_topic_t topic, const void *data, uint32_t data_len);

    /**
     * @brief 消息总线处理内部的系统消息，供外部线程等调用。
     * 
     * @param bus_msg 消息内容
     */
    void msgbus_system_msg_handler(msgbus_msg_t *bus_msg);

#ifdef __cplusplus
} /* __cplusplus */
#endif
#endif