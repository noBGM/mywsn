#include "mysf_schedule.h"


static void waitForLeaderBeacon(void) {

    while(){
        __WFE();
    }

}

// schedule_manager.c

// 组资源配置结构体（从组长广播获取）
typedef struct {
    uint8_t startSlot;        // 本组起始时隙
    uint8_t numSlots;         // 总时隙数
    uint8_t channelOffset;    // 信道偏移
    uint8_t slotsPerNode;     // 每节点时隙数
    uint8_t numNodes;         // 固定节点数量
    uint8_t membersPerTeam;   // 每队成员数
} group_resource_t;

// 本地时隙配置结构体
typedef struct {
    uint8_t slotOffset;       // 时隙偏移
    uint8_t channelOffset;    // 信道偏移
    uint8_t type;            // 时隙类型
    uint16_t neighborId;     // 邻居节点ID（这里是组长ID）
} slot_config_t;

// 全局变量
static group_resource_t myGroupResource;
static uint16_t myLeaderId;

// 接收组长广播时隙配置，应该来自于EB配置。
#define LEADER_BROADCAST_SLOT_OFFSET    0x00    // 共享时隙偏移量
#define LEADER_BROADCAST_CHANNEL_OFFSET 0x00    // 共享信道偏移量

static uint16_t currentLeaderId;  // 当前组长ID
static bool receivedSchedule;     // 是否收到调度表标志

static void initMemberSchedule(void) {
    open_addr_t leaderAddr;
    
    // 获取预设的组长ID
    currentLeaderId = getPresetLeader();
    receivedSchedule = FALSE;
    
    // 配置组长地址
    leaderAddr.type = ADDR_64B;
    getLeaderAddr(currentLeaderId, &leaderAddr);
    
    // 添加接收时隙
    schedule_addActiveSlot(
        LEADER_BROADCAST_SLOT_OFFSET,        // 时隙偏移
        CELLTYPE_RX,              // 接收类型
        FALSE,                     // 共享时隙
        FALSE,                     // 自动时隙
        LEADER_BROADCAST_CHANNEL_OFFSET,    // 信道偏移
        &leaderAddr              // 组长地址
    );
    
    // 等待组长广播调度表
    while(!receivedSchedule) {
        // 可以添加超时机制
        if(waitForSchedule(SCHEDULE_TIMEOUT_MS) == FALSE) {
            // 超时处理
            LOG_ERROR(COMPONENT_SCHEDULE, ERR_SCHEDULE_TIMEOUT, 
                     (errorparameter_t)currentLeaderId, 
                     (errorparameter_t)0);
            // 可以选择重试或其他处理
            continue;
        }
    }
}

// 等待并处理组长beacon
static owerror_t waitForLeaderBeacon(void) {
    uint8_t attempts = 0;
    while (attempts < MAX_SYNC_ATTEMPTS) {
        // 等待接收beacon
        if (waitForBeacon(BEACON_TIMEOUT) == E_SUCCESS) {
            // 解析beacon中的资源配置
            if (parseResourceConfig()) {
                return E_SUCCESS;
            }
        }
        attempts++;
    }
    return E_FAIL;
}

// 根据节点ID获取时隙配置
static slot_config_t* getMySlots(uint16_t nodeId) {
    static slot_config_t slots[MAX_SLOTS_PER_NODE];
    uint8_t numSlots = 0;
    
    // 计算节点在组内的索引
    uint8_t nodeIndex = GET_NODE_INDEX(nodeId);
    
    // 计算基础时隙偏移
    uint8_t baseSlot = myGroupResource.startSlot + nodeIndex;

    uint8_t interval = myGroupResource.membersPerTeam;
    
    // 配置发送时隙（发送给组长）
    for (uint8_t i = 0; numSlots < myGroupResource.slotsPerNode; i=i+interval) {
        slots[numSlots].slotOffset = baseSlot + i;
        //组内成员的信道偏移是不相同的，避免残余多径干扰
        slots[numSlots].channelOffset = myGroupResource.channelOffset[nodeIndex];
        slots[numSlots].type = CELLTYPE_TX;
        slots[numSlots].neighborId = myLeaderId;
        numSlots++;
    }
    
    return (numSlots > 0) ? slots : NULL;
}

// 添加调度表条目
static void addScheduleEntries(slot_config_t* slots) {
    INTERRUPT_DECLARATION();
    DISABLE_INTERRUPTS();
    
    // 清除旧的调度条目
    schedule_removeAllCells();
    
    // 添加新的调度条目
    for (uint8_t i = 0; i < myGroupResource.slotsPerNode; i++) {
        schedule_addActiveSlot(
            slots[i].slotOffset,
            slots[i].channelOffset,
            slots[i].type,
            slots[i].neighborId,
            FALSE  // 固定节点使用专用时隙
        );
    }
    
    ENABLE_INTERRUPTS();
}

// 启动状态检查
static void startStatusCheck(void) {
    // 注册定期检查定时器
    opentimers_scheduleIn(
        STATUS_CHECK_TIMER_ID,
        STATUS_CHECK_PERIOD,
        TIME_MS,
        TIMER_PERIODIC,
        checkNodeStatus
    );
}

// 状态检查回调
static void checkNodeStatus(opentimer_id_t timerId) {
    // 检查与组长的连接状态
    if (!isLeaderConnected()) {
        // 连接丢失，尝试重新同步
        reSync();
    }
    
    // 检查时隙使用情况
    checkSlotUsage();
    
    // 更新本地统计信息
    updateStatistics();
}

// 组员接收调度更新的处理函数
void handleScheduleUpdate(uint8_t* payload, uint8_t len) {
    uint8_t slotsPerMember;
    uint8_t myIndex;
    uint8_t i;
    
    if(len < 2) {  // 至少需要N和一个时隙
        return;
    }
    
    // 获取每个组员的时隙数
    slotsPerMember = payload[0];
    
    // 获取自己的组内索引
    myIndex = getMyGroupIndex();
    
    // 计算自己的时隙起始位置
    uint8_t mySlotStartIndex = 1 + (myIndex * slotsPerMember);
    
    // 确保不越界
    if(mySlotStartIndex + slotsPerMember > len) {
        return;
    }
    
    // 更新自己的时隙
    for(i = 0; i < slotsPerMember; i++) {
        uint8_t slotoffset = payload[mySlotStartIndex + i];
        i++;
        uint8_t channelOffset = payload[mySlotStartIndex + i] >> 2;
        uint8_t type = payload[mySlotStartIndex + i] & 0x03;

        schedule_addActiveSlot(
            slotOffset,          // 时隙偏移
            type,                // 接收类型
            FALSE,               // 非共享
            FALSE,               // 非自动
            channelOffset,       // 信道偏移
            &leaderAddr          // 组长地址
        );
    }
}