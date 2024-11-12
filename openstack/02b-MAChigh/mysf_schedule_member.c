static void initMemberSchedule(void) {
    // 获取预设的组长ID
    uint16_t leaderId = getPresetLeader();
    
    // 等待对应组长的beacon
    // 处理组长发送的调度表，赋值给本地全局变量myGroupResource
    waitForLeaderBeacon();
    
    // 获取本组内所有节点的固定时隙配置
    // 在根节点分配给本组组长的时隙资源中，通过自身ID获取对应的份配置
    scheduleEntries = getMySlots(ID);

    // 将调度表条目添加到本地调度表,批量调用schedule_addActiveSlot
    addScheduleEntries(scheduleEntries);
}

static void waitForLeaderBeacon(void) {

    while(){
        wfe();
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

static void initMemberSchedule(void) {
    // 1. 获取预设的组长ID（基于自身ID）
    myLeaderId = getPresetLeader();
    
    // 2. 等待并处理组长的beacon
    // 等待对应组长的beacon
    // 处理组长发送的调度表，赋值给本地全局变量myGroupResource
    if (waitForLeaderBeacon() != E_SUCCESS) {
        // 等待超时，进入错误处理
        handleSyncFailed();
        return;
    }
    
    // 获取本组内所有节点的固定时隙配置
    // 在根节点分配给本组组长的时隙资源中，通过自身ID获取对应的份配置
    // 3. 获取自己的时隙配置
    slot_config_t* mySlots = getMySlots(myId);
    if (mySlots == NULL) {
        // 获取配置失败，进入错误处理
        handleConfigFailed();
        return;
    }
    
    // 4. 添加调度表条目(将调度表条目添加到本地调度表,批量调用schedule_addActiveSlot)
    addScheduleEntries(mySlots);
    
    // 5. 启动定期状态检查
    startStatusCheck();
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