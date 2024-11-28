// schedule_manager.h
#include "schedule.h"

#define NUM_GROUPS 4
#define MAX_PC_PACKET_LEN 255
// 组内资源分配规则
typedef struct {
    uint8_t startSlot;      // 本组起始时隙
    uint8_t totalSlots;     // 本组总时隙数
    uint8_t nodesPerSlot;   // 每个时隙分配给几个节点
    uint8_t channelOffset;  // 本组信道偏移
} group_resource_t;

typedef struct {
    uint16_t leaderId;      // 组长节点ID
    uint16_t slotOffset;    // 时隙偏移量
    uint8_t  channelOffset; // 信道偏移量
    uint8_t  reserved;      // 保留字节(用于字节对齐)
} root_leader_link_t;

typedef enum {
    PC_CMD_NEW_SCHEDULE = 0,  
    PC_CMD_CONTROL = 1
} cmdType_t;

typedef enum {
    NODE_UNKNOWN = 0,  // 未初始化状态
    NODE_ROOT    = 1,  // 根节点
    NODE_LEADER  = 2,  // 组长节点
    NODE_MEMBER  = 3,  // 组成员节点
    NODE_MOBILE  = 4   // 移动节点
} node_role_t;

// 预设的组资源配置表
static const group_resource_t GROUP_RESOURCES[] = {
    // 头颈组 (4个节点)
    {.startSlot = 10, .totalSlots = 4, .nodesPerSlot = 1, .channelOffset = 0},
    // 躯干组 (7个节点)
    {.startSlot = 14, .totalSlots = 7, .nodesPerSlot = 1, .channelOffset = 1},
    // 上肢组 (16个节点)
    {.startSlot = 21, .totalSlots = 8, .nodesPerSlot = 2, .channelOffset = 2},
    // 下肢组 (16个节点)
    {.startSlot = 29, .totalSlots = 8, .nodesPerSlot = 2, .channelOffset = 3}
};

void mysf_init(node_role_t role);