
#include "mysf_schedule.h"

static uint8_t memberData[MAX_GROUP_MEMBERS][MAX_DATA_LEN];  // 数据存储数组
static uint32_t memberBitmap = 0;  // 传输状态位图
static uint8_t numGroupMembers = 0;  // 组内节点数量
static bool okToSend = FALSE;

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

void mysf_setOkToSend(bool status){
    okToSend = status;
}

bool mysf_getOkToSend(void){
    if(schedule_getType() == CELLTYPE_TX_CMD)
        return TRUE;
    return okToSend;
}

// 处理组员数据流
// return: 是否所有组员在本周期内都完成传输
bool storeMemberData(uint8_t memberId, uint8_t* data, uint8_t len) {
    INTERRUPT_DECLARATION();
    DISABLE_INTERRUPTS();
    
    // 获取组员在组内的索引
    uint8_t memberIndex = GET_NODE_INDEX(memberId);
    
    // 检查参数有效性
    if (memberIndex >= MAX_GROUP_MEMBERS || len > MAX_DATA_LEN) {
        ENABLE_INTERRUPTS();
        return FALSE;
    }
    
    // 存储数据
    memcpy(memberData[memberIndex], data, len);
    
    // 更新bitmap
    memberBitmap |= (1 << memberIndex);
    
    // 检查是否所有组员都完成传输
    uint32_t expectedBitmap = (1 << numGroupMembers) - 1;
    bool allComplete = (memberBitmap == expectedBitmap);
    
    // 如果全部完成，清零bitmap
    if (allComplete) {
        memberBitmap = 0;
        //后续需考虑减少memcpy的次数，测试阶段暂略。
        sendToRoot(memberData, numGroupMembers*MAX_DATA_LEN, PRIORITY_HIGH);
    }else{
        //如果组员数据始终未完成，应做何处理？
        //1. 是否需要立即发送？
        //2. 是否需要等待所有组员完成，再发送？
    }
    
    
    ENABLE_INTERRUPTS();
    return allComplete;
}

// 组长节点的数据处理函数
// root：RX时隙接收到根节点的数据;
// member：RX时隙接收到组内节点的数据;
// 同步调用。

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
static void updateMobileSlots(){
    return;
}
// 处理新的调度表 
static void handleScheduleUpdate(uint8_t* data, uint8_t len) {
    // 1. 更新本地调度表（即组长节点与根节点的通信、与组内节点的通信）
    updateLocalSchedule(data, len);
    
    // 2. 更新移动节点的时隙分配
    updateMobileSlots();
    
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
// 应该创建一个类似于sixtop_send的函数，包含了owner修改（不同于sixtop_send）、协议头准备。
static void sendToRoot(uint8_t* data, uint8_t len, uint8_t priority) {
    OpenQueueEntry_t* pkt;
    
    // 获取发送缓冲
    pkt = openqueue_getFreePacketBuffer(COMPONENT_LEADER);

    if (pkt == NULL) {
        LOG_ERROR(COMPONENT_IEEE802154E, ERR_NO_FREE_PACKET_BUFFER, (errorparameter_t) 0, (errorparameter_t) 0);
        // abort
        return;
    }

    if (packetfunctions_reserveHeader(&pkt, len) == E_FAIL){
        return E_FAIL;
    }

    memcpy(pkt->payload, data, len);
    pkt->length = len;
    pkt->l2_nextORpreviousHop = ROOT_ID;
    pkt->priority = priority;//需删除

    pkt->owner = COMPONENT_LEADER;
    pkt->l2_frameType = IEEE154_TYPE_DATA;

    // set l2-security attributes
    pkt->l2_securityLevel = IEEE802154_security_getSecurityLevel(pkt);//影响是否执行isValidJoin
    pkt->l2_keyIdMode = IEEE802154_SECURITY_KEYIDMODE;
    pkt->l2_keyIndex = IEEE802154_security_getDataKeyIndex();    
    pkt->l2_retriesLeft = 1;
    pkt->l2_dsn = 0;
    pkt->l2_numTxAttempts = 0;
    // add a IEEE802.15.4 header
    if (ieee802154_prependHeader(
            pkt,
            pkt->l2_frameType,
            FALSE, //IE present?
            pkt->l2_dsn,
            &(pkt->l2_nextORpreviousHop)
    ) == E_FAIL) {
        return E_FAIL;
    }
    // change owner to IEEE802154E fetches it from queue
    pkt->owner = COMPONENT_LEADER_TO_ROOT;
}

static void updateLocalSchedule(uint8_t* data, uint8_t len) {
    schedule_update_t* update = (schedule_update_t*)data;
    OpenQueueEntry_t* pkt;
    
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

    pkt = openqueue_getFreePacketBuffer(COMPONENT_LEADER_TO_MEMBER);
    // 4. 为组内每个固定节点计算具体时隙分配
    for (uint8_t i = 0; i < myGroupResource.numNodes; i++) {
        uint16_t nodeId = getGroupMemberId(i);  // 获取组内第i个节点的ID
        
        // 根据节点ID计算其时隙偏移
        uint8_t slotOffset = calculateNodeSlot(
            nodeId, 
            myGroupResource.startSlot,
            myGroupResource.slotsPerNode
        );

        uint8_t channelOffset = calculateNodeSlot(
            nodeId, 
            myGroupResource.startSlot,
            myGroupResource.slotsPerNode
        );        
        
        pkt->payload[0+nodeId*2] = slotOffset;
        pkt->payload[1+nodeId*2] = channelOffset;
        //pkt->payload[2] = nodeId;
        pkt->length = myGroupResource.numNodes*2;
        pkt->creator = COMPONENT_LEADER;
        pkt->owner = COMPONENT_LEADER;
        pkt->l2_frameType = IEEE154_TYPE_DATA;
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
    // 异步执行。
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
            CELLTYPE_RX,           // 接收移动节点数据
            MOBILE_BROADCAST_ID,   // 移动节点广播ID
            TRUE                   // 共享时隙
        );
    }
}



static void initLinks(void){
    uint16_t rootID = 0x0000;
    uint8_t thisGroup = 0;//宏决定，还未修改。
    open_addr_t temp_neighbor;
    memset(&temp_neighbor, 0, sizeof(temp_neighbor));
    temp_neighbor.type = ADDR_ANYCAST;
    //用于组内的资源广播。
    schedule_addActiveSlot(
        SCHEDULE_MINIMAL_6TISCH_SLOTOFFSET+1+LEADER_ID,    
        CELLTYPE_TX_CMD, 
        FALSE,          
        FALSE,         
        SCHEDULE_MINIMAL_6TISCH_CHANNELOFFSET, 
        &temp_neighbor
    );

    schedule_addActiveSlot(
        ROOT_LEADER_LINKS[thisGroup].slotOffset,
        CELLTYPE_TX,                     // 接收时隙
        FALSE,                           // 非共享时隙
        FALSE,                           // 自主调度
        ROOT_LEADER_LINKS[thisGroup].channelOffset,
        rootID
    );
    
    schedule_addActiveSlot(
        ROOT_LEADER_LINKS[thisGroup].slotOffset + NUM_GROUPS,// 错开接收时隙
        CELLTYPE_RX,                     // 发送时隙
        FALSE,                           // 非共享时隙
        FALSE,                           // 自主调度
        ROOT_LEADER_LINKS[thisGroup].channelOffset,
        rootID
    );
    
}

static void registerBorrowHandler(void){
    return;
}

static void initMobileSlotPool(void){
    return;
}
static void startStatusReport(void){
    return;
}


// -------- 组长节点相关功能 --------
static void initLeaderSchedule(void) {
    // 初始化与根节点的固定通信时隙
    initLinks();
    
    waitForRootBeacon();

    // 获取本组资源配置
    while(!getGroupResource());

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