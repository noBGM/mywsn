// schedule_manager.c


// schedule_manager.c

// 函数指针类型定义
typedef void (*data_handler_t)(uint16_t srcAddr, uint8_t* data, uint8_t len);
typedef void (*pc_cmd_handler_t)(uint8_t* data, uint8_t len);

// 全局处理函数指针
static data_handler_t g_dataHandler = NULL;
static pc_cmd_handler_t g_pcCmdHandler = NULL;
// -------- 根节点相关功能 --------
// schedule_manager.c

// 根节点与组长的固定链路配置
static const root_leader_link_t ROOT_LEADER_LINKS[] = {
    {.leaderId = 0x0001, .slotOffset = 1, .channelOffset = 0},  // 头颈组长
    {.leaderId = 0x0002, .slotOffset = 2, .channelOffset = 0},  // 躯干组长
    {.leaderId = 0x0003, .slotOffset = 3, .channelOffset = 0},  // 上肢组长
    {.leaderId = 0x0004, .slotOffset = 4, .channelOffset = 0}   // 下肢组长
};
// 注册数据处理函数
static void registerDataHandler(data_handler_t handler) {
    INTERRUPT_DECLARATION();
    DISABLE_INTERRUPTS();
    
    g_dataHandler = handler;
    
    ENABLE_INTERRUPTS();
}

// 注册PC命令处理函数
static void registerPCCommandHandler(pc_cmd_handler_t handler) {
    INTERRUPT_DECLARATION();
    DISABLE_INTERRUPTS();
    
    g_pcCmdHandler = handler;
    
    ENABLE_INTERRUPTS();
}

// 根节点的数据处理函数
static void rootDataHandler(uint16_t srcAddr, uint8_t* data, uint8_t len) {
    // 添加根节点头部信息（用于PC识别数据来源）
    uint8_t pcData[MAX_PC_PACKET_LEN];
    uint8_t pcDataLen = 0;
    
    // 填充头部
    pcData[pcDataLen++] = ROOT_PACKET_HEADER;
    pcData[pcDataLen++] = srcAddr >> 8;    // 源地址高字节
    pcData[pcDataLen++] = srcAddr & 0xFF;  // 源地址低字节
    
    // 复制原始数据
    memcpy(&pcData[pcDataLen], data, len);
    pcDataLen += len;
    
    // 转发给PC
    sendToPC(pcData, pcDataLen);
}

// 根节点的状态报告处理函数
static void rootStatusHandler(uint16_t srcAddr, uint8_t* data, uint8_t len) {
    // 直接转发给PC，格式同上
    uint8_t pcData[MAX_PC_PACKET_LEN];
    uint8_t pcDataLen = 0;
    
    pcData[pcDataLen++] = ROOT_STATUS_HEADER;
    pcData[pcDataLen++] = srcAddr >> 8;
    pcData[pcDataLen++] = srcAddr & 0xFF;
    
    memcpy(&pcData[pcDataLen], data, len);
    pcDataLen += len;
    
    sendToPC(pcData, pcDataLen);
}

// 处理来自PC的命令（如新的调度表）
static void handlePCCommand(uint8_t* data, uint8_t len) {
    // 解析命令类型
    uint8_t cmdType = data[0];
    
    switch(cmdType) {
        case PC_CMD_NEW_SCHEDULE:
            // 转发新调度表给相关组长
            forwardScheduleToLeaders(&data[1], len-1);
            break;
            
        case PC_CMD_CONTROL:
            // 转发控制命令给指定节点
            forwardControlCommand(&data[1], len-1);
            break;
            
        default:
            // 未知命令类型，记录错误
            logError(ERR_UNKNOWN_PC_CMD);
            break;
    }
}

void initRootSchedule(void) {
    // 1. 初始化与PC的串口通信
    initPCConnection();
    
    // 2. 初始化与各组长的固定通信时隙
    for(uint8_t i = 0; i < NUM_GROUPS; i++) {
        // 为每个组长添加接收时隙（接收来自组长的数据）
        schedule_addActiveSlot(
            ROOT_LEADER_LINKS[i].slotOffset,
            ROOT_LEADER_LINKS[i].channelOffset,
            CELLTYPE_RX,                    // 接收时隙
            ROOT_LEADER_LINKS[i].leaderId,  // 对应组长ID
            FALSE                           // 非共享时隙
        );
        
        // 为每个组长添加发送时隙（发送PC命令给组长）
        schedule_addActiveSlot(
            ROOT_LEADER_LINKS[i].slotOffset + NUM_GROUPS,  // 错开接收时隙
            ROOT_LEADER_LINKS[i].channelOffset,
            CELLTYPE_TX,                    // 发送时隙
            ROOT_LEADER_LINKS[i].leaderId,  // 对应组长ID
            FALSE                           // 非共享时隙
        );
    }
    
    // 3. 注册数据处理函数（处理来自组长的数据）
    registerDataHandler(rootDataHandler);
    
    // 4. 注册PC命令处理函数（处理来自PC的命令）
    registerPCCommandHandler(handlePCCommand);
    
    // 5. 启动定时器，定期检查PC连接状态
    startPCConnectionTimer();
}