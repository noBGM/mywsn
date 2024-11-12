// schedule_manager.h

// 组内资源分配规则
typedef struct {
    uint8_t startSlot;      // 本组起始时隙
    uint8_t totalSlots;     // 本组总时隙数
    uint8_t nodesPerSlot;   // 每个时隙分配给几个节点
    uint8_t channelOffset;  // 本组信道偏移
} group_resource_t;

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