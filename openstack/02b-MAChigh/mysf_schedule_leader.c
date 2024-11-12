// schedule_manager.c
#include "mysf_schedule.h"

// schedule_manager.c

// IMU数据包类型定义
typedef enum {
    IMU_DATA_NORMAL,    // 正常采样数据
    IMU_DATA_URGENT,    // 高优先级数据
    IMU_DATA_ERROR,      // 错误状态数据
    ROOT_MSG_SCHEDULE_UPDATE,    // 新的调度表
    ROOT_MSG_CONFIG_UPDATE,      // 配置更新
    ROOT_MSG_TIME_SYNC,         // 时间同步信息
    ROOT_MSG_COMMAND            // 控制命令
} leader_data_type_t;



// 组长节点的数据处理函数
static void leaderDataHandler(uint16_t srcAddr, uint8_t* data, uint8_t len) {
    uint8_t nodeType = GET_NODE_TYPE(srcAddr);  // 从源地址判断是固定节点还是移动节点
    leader_data_type_t dataType = (leader_data_type_t)data[0];

    // if (nodeType == NODE_TYPE_MOBILE) {
    //     // 移动节点直接转发给根节点
    //     sendToRoot(data, len, PRIORITY_HIGH);
    //     return;
    // }

    // 准备转发包
    uint8_t fwdData[MAX_PACKET_LEN];
    uint8_t fwdLen = 0;
    
    // 添加组长头部信息
    fwdData[fwdLen++] = LEADER_PACKET_HEADER;
    fwdData[fwdLen++] = myId >> 8;        // 组长ID高字节
    fwdData[fwdLen++] = myId & 0xFF;      // 组长ID低字节
    fwdData[fwdLen++] = srcAddr >> 8;     // 源节点ID高字节
    fwdData[fwdLen++] = srcAddr & 0xFF;   // 源节点ID低字节
    
    switch(dataType) {
        case IMU_DATA_NORMAL:
            // 正常数据，添加时间戳后转发
            addTimeStamp(&fwdData[fwdLen]);
            fwdLen += TIMESTAMP_LEN;
            memcpy(&fwdData[fwdLen], &data[1], len-1);
            fwdLen += len-1;
            
            // 使用正常优先级发送给根节点
            sendToRoot(fwdData, fwdLen, PRIORITY_NORMAL);
            break;
            
        case IMU_DATA_URGENT:
            // 紧急数据，立即转发
            memcpy(&fwdData[fwdLen], &data[1], len-1);
            fwdLen += len-1;
            
            // 使用高优先级发送给根节点
            sendToRoot(fwdData, fwdLen, PRIORITY_HIGH);
            break;
            
        case IMU_DATA_ERROR:
            // 错误状态数据，记录并转发
            logNodeError(srcAddr, &data[1]);
            memcpy(&fwdData[fwdLen], &data[1], len-1);
            fwdLen += len-1;
            
            // 使用中等优先级发送给根节点
            sendToRoot(fwdData, fwdLen, PRIORITY_MEDIUM);
            break;
        case ROOT_MSG_SCHEDULE_UPDATE:
            // 处理新的调度表
            handleScheduleUpdate(&data[1], len-1);
            break;
            
        case ROOT_MSG_CONFIG_UPDATE:
            // 处理配置更新
            handleConfigUpdate(&data[1], len-1);
            break;
            
        case ROOT_MSG_TIME_SYNC:
            // 处理时间同步信息
            handleTimeSync(&data[1], len-1);
            break;
            
        case ROOT_MSG_COMMAND:
            // 处理控制命令
            handleRootCommand(&data[1], len-1);
            break;
    }
    
    // 更新节点状态记录
    updateNodeStatus(srcAddr, dataType);
}
// 处理新的调度表
static void handleScheduleUpdate(uint8_t* data, uint8_t len) {
    // 1. 更新本地调度表
    updateLocalSchedule(data, len);
    
    // 2. 更新移动节点的时隙分配
    updateMobileSlots();
    
    // 3. 广播更新给组内节点（如有必要）
    broadcastScheduleUpdate();
}

// 处理配置更新
static void handleConfigUpdate(uint8_t* data, uint8_t len) {
    // 1. 更新本地配置
    updateLocalConfig(data, len);
    
    // 2. 如果需要，转发给特定节点
    forwardConfigIfNeeded(data, len);
}

// 处理时间同步信息
static void handleTimeSync(uint8_t* data, uint8_t len) {
    // 1. 更新本地时间
    updateLocalTime(data, len);
    
    // 2. 向组内节点广播同步信息
    broadcastTimeSync();
}

// 处理控制命令
static void handleRootCommand(uint8_t* data, uint8_t len) {
    uint8_t cmdType = data[0];
    uint16_t targetNode = (data[1] << 8) | data[2];
    
    if (isNodeInGroup(targetNode)) {
        // 命令针对本组节点
        forwardCommandToNode(targetNode, &data[3], len-3);
    } else if (targetNode == myId) {
        // 命令针对组长本身
        executeLocalCommand(cmdType, &data[3], len-3);
    }
}


// 发送数据到根节点
static void sendToRoot(uint8_t* data, uint8_t len, uint8_t priority) {
    OpenQueueEntry_t* pkt;
    
    // 获取发送缓冲
    pkt = openqueue_getFreePacketBuffer(COMPONENT_SCHEDULE);
    if (pkt == NULL) {
        // 缓冲区满，记录错误
        logError(ERR_BUFFER_FULL);
        return;
    }
    
    // 填充数据
    memcpy(pkt->payload, data, len);
    pkt->length = len;
    
    // 设置目标地址为根节点
    pkt->l2_nextORpreviousHop = ROOT_ID;
    
    // 设置优先级
    pkt->priority = priority;
    
    // 发送数据
    if (schedule_sendPacket(pkt) != E_SUCCESS) {
        // 发送失败，释放缓冲
        openqueue_freePacketBuffer(pkt);
        logError(ERR_SEND_FAILED);
    }
}

static void updateLocalSchedule(uint8_t* data, uint8_t len) {
    schedule_update_t* update = (schedule_update_t*)data;
    
    // 1. 版本检查
    if (update->version <= currentScheduleVersion) {
        return;
    }
    
    INTERRUPT_DECLARATION();
    DISABLE_INTERRUPTS();
    
    // 2. 清除旧的调度条目（保留与根节点的固定链路）
    clearOldSchedule();
    
    // 3. 解析分配给本组的资源块
    group_resource_t myGroupResource;
    if (!parseGroupResource(update, &myGroupResource)) {
        ENABLE_INTERRUPTS();
        return;
    }
    
    // 4. 为组内每个固定节点计算具体时隙分配
    for (uint8_t i = 0; i < myGroupResource.numNodes; i++) {
        uint16_t nodeId = getGroupMemberId(i);  // 获取组内第i个节点的ID
        
        // 根据节点ID计算其时隙偏移
        uint8_t slotOffset = calculateNodeSlot(
            nodeId, 
            myGroupResource.startSlot,
            myGroupResource.slotsPerNode
        );
        
        // 在组长节点注册接收时隙
        schedule_addActiveSlot(
            slotOffset,
            myGroupResource.channelOffset,
            CELLTYPE_RX,           // 组长接收组员数据
            nodeId,                // 对应组员节点ID
            FALSE                  // 非共享时隙
        );
    }
    
    // 5. 处理移动节点的资源分配（如果有）
    if (myGroupResource.mobileSlots > 0) {
        registerMobileSlots(
            myGroupResource.mobileStartSlot,
            myGroupResource.mobileSlots,
            myGroupResource.channelOffset
        );
    }
    
    // 6. 更新版本号
    currentScheduleVersion = update->version;
    
    ENABLE_INTERRUPTS();
    
    // 7. 广播新的资源分配给组内节点
    broadcastResourceUpdate(&myGroupResource);
}

// 解析分配给本组的资源块
static bool parseGroupResource(schedule_update_t* update, group_resource_t* resource) {
    // 在更新包中查找本组的资源配置
    for (uint8_t i = 0; i < update->numGroups; i++) {
        if (update->groups[i].groupId == myGroupId) {
            // 找到本组配置，复制相关信息
            resource->startSlot = update->groups[i].startSlot;
            resource->numSlots = update->groups[i].numSlots;
            resource->channelOffset = update->groups[i].channelOffset;
            resource->slotsPerNode = update->groups[i].slotsPerNode;
            resource->numNodes = update->groups[i].numNodes;
            resource->mobileStartSlot = update->groups[i].mobileStartSlot;
            resource->mobileSlots = update->groups[i].mobileSlots;
            return TRUE;
        }
    }
    return FALSE;
}

// 根据节点ID计算时隙偏移
static uint8_t calculateNodeSlot(uint16_t nodeId, uint8_t startSlot, uint8_t slotsPerNode) {
    uint8_t nodeIndex = GET_NODE_INDEX(nodeId);  // 从节点ID提取组内序号
    return startSlot + (nodeIndex * slotsPerNode);
}

// 注册移动节点时隙池
static void registerMobileSlots(uint8_t startSlot, uint8_t numSlots, uint8_t channelOffset) {
    for (uint8_t i = 0; i < numSlots; i++) {
        schedule_addActiveSlot(
            startSlot + i,
            channelOffset,
            CELLTYPE_RX,          // 接收移动节点数据
            MOBILE_BROADCAST_ID,   // 移动节点广播ID
            TRUE                   // 共享时隙
        );
    }
}

// 广播资源更新给组内节点
static void broadcastResourceUpdate(group_resource_t* resource) {
    // 准备广播包
    uint8_t broadcastData[MAX_PACKET_LEN];
    uint8_t len = 0;
    
    // 填充资源信息
    broadcastData[len++] = RESOURCE_UPDATE_TYPE;
    memcpy(&broadcastData[len], resource, sizeof(group_resource_t));
    len += sizeof(group_resource_t);
    
    // 广播给组内所有节点
    sendGroupBroadcast(broadcastData, len);
}

// -------- 组长节点相关功能 --------
static void initLeaderSchedule(void) {
    // 初始化与根节点的固定通信时隙
    initRootLink();
    
    waitForRootBeacon();

    // 获取本组资源配置
    while(getGroupResource());

    // 初始化组内固定节点的预设时隙（尽量保证均匀分布，避免高密度采样导致相邻动作突变）
    // member0: 0, 3, 6, 9
    // member1: 2, 5, 8, 11
    // ...  
    // 也可以将该过程放在组员节点完成，减轻组长负担。
    initFixedSlots();
    
    // 初始化可分配给移动节点的时隙池
    initMobileSlotPool();
    
    // 注册资源借用请求处理函数
    registerBorrowHandler();
    
    // 启动定期状态报告
    startStatusReport();
    
    //注册数据处理函数（处理来自root和member的数据）
    registerDataHandler(leaderDataHandler);
    
    registerPCCommandHandler(NULL);
}

// -------- 组员节点相关功能 --------
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



// -------- 组长节点的资源管理 --------
bool handleMobileJoin(uint16_t nodeId, uint8_t reqSlots) {
    // 检查本地资源
    if (checkLocalResource()) {
        allocateLocalSlots();
        return TRUE;
    }
    
    // 尝试组间借用
    if (borrowFromOtherGroups()) {
        allocateLocalSlots();
        return TRUE;
    }
    
    return FALSE;
}

// -------- 状态报告机制 --------
void generateStatusReport(void) {
    switch(currentRole) {
        case NODE_LEADER:
            // 组长报告：包含组内固定节点状态、移动节点数量、资源使用情况
            generateLeaderReport();
            break;
            
        case NODE_ROOT:
            // 根节点报告：汇总所有组的状态
            generateRootReport();
            break;
            
        default:
            // 其他节点不需要生成报告
            break;
    }
}