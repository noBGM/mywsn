#include "mysf_schedule.h"
#include "openqueue.h"

// 函数指针类型定义
typedef void (*data_handler_t)(uint16_t srcAddr, uint8_t* data, uint8_t len);
typedef void (*pc_cmd_handler_t)(uint8_t* data, uint8_t len);

// 全局处理函数指针
data_handler_t g_dataHandler = NULL;
pc_cmd_handler_t g_pcCmdHandler = NULL;
// -------- 根节点相关功�-------

// 根节点与组长的固定链路配�
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

    openserial_printf("rootDataHandler\n");
    return;
    // // 添加根节点头部信息（用于PC识别数据来源�
    // uint8_t pcData[MAX_PC_PACKET_LEN];
    // uint8_t pcDataLen = 0;
    
    // // 填充头部
    // pcData[pcDataLen++] = ROOT_PACKET_HEADER;
    // pcData[pcDataLen++] = srcAddr >> 8;    // 源地址高字�
    // pcData[pcDataLen++] = srcAddr & 0xFF;  // 源地址低字�
    
    // // 复制原始数据
    // memcpy(&pcData[pcDataLen], data, len);
    // pcDataLen += len;
    
    // // 转发给PC
    // sendToPC(pcData, pcDataLen);
}

// 根节点的状态报告处理函�
static void rootStatusHandler(uint16_t srcAddr, uint8_t* data, uint8_t len) {
    return;
    // // 直接转发给PC，格式同�
    // uint8_t pcData[MAX_PC_PACKET_LEN];
    // uint8_t pcDataLen = 0;
    
    // //pcData[pcDataLen++] = ROOT_STATUS_HEADER;
    // pcData[pcDataLen++] = srcAddr >> 8;
    // pcData[pcDataLen++] = srcAddr & 0xFF;
    
    // memcpy(&pcData[pcDataLen], data, len);
    // pcDataLen += len;
    
    // //sendToPC(pcData, pcDataLen);
}

static void forwardScheduleToLeaders(uint8_t* data, cmdType_t cmdType){
    openserial_mysf_printf("forwardScheduleToLeaders\n");
    OpenQueueEntry_t *pkt[NUM_GROUPS];
    uint8_t len;

    len = 0;
    for(uint8_t i = 0; i < NUM_GROUPS; i++){
        pkt[i] = openqueue_getFreePacketBuffer(COMPONENT_MSF);
        if(pkt[i] == NULL){
            openserial_mysf_printf("No free packet for group %d\r\n", i);
            return;
        }
        uint8_t payloadLen = data[len]; // 获取当前组调度表长度，包含长度字段�
        memcpy(pkt[i]->payload, data+len, payloadLen);
        len += payloadLen;
        if (packetfunctions_reserveHeader(&pkt[i], sizeof(uint8_t)) == E_FAIL) {
            openserial_mysf_printf("Failed to reserve header for group %d\r\n", i);
            return;
        }
        pkt[i]->payload[0] = cmdType;
    }

    for(uint8_t i = 0; i < NUM_GROUPS; i++){
        openserial_mysf_printf("Group %d payload: \r\n", i);
        for(uint8_t j = 0; j < pkt[i]->payload[0]; j++) {
            openserial_mysf_printf("%02x ", pkt[i]->payload[j]);
        }
        openserial_mysf_printf("\r\n");
    }
}
static void forwardControlCommand(uint8_t* data, uint8_t len) {
    openserial_mysf_printf("forwardControlCommand\r\n");
    return;
}
// 处理来自PC的命令（如新的调度表�
static void handlePCCommand(uint8_t* data, uint8_t len) {
    // 解析命令类型
    cmdType_t cmdType = (cmdType_t)data[0];
    
    switch(cmdType) {
        case PC_CMD_NEW_SCHEDULE:
            // 转发新调度表给相关组�
            forwardScheduleToLeaders(&data[1], cmdType);
            break;
            
        case PC_CMD_CONTROL:
            // 转发控制命令给指定节�
            forwardControlCommand(&data[1], len-1);
            break;
            
        default:
            // // 未知命令类型，记录错�
            // LOG_ERROR(COMPONENT_MSF, ERR_WRONG_STATE_IN_STARTSLOT, //TBD
            //       (errorparameter_t) ieee154e_vars.state, //TBD
            //       (errorparameter_t) ieee154e_vars.slotOffset); //TBD
            break;
    }
}

void initRootSchedule(void) {
    // 1. 初始化与PC的串口通信�
    //initPCConnection();

    // 2. 初始化与各组长的固定通信时隙
    for(uint8_t i = 0; i < NUM_GROUPS; i++) {
        // 为每个组长添加接收时隙（接收来自组长的数据）
        schedule_addActiveSlot(
            ROOT_LEADER_LINKS[i].slotOffset,
            CELLTYPE_RX,                    // 接收时隙
            FALSE,                           // 非共享时�
            FALSE,                           // 自主调度
            ROOT_LEADER_LINKS[i].channelOffset,
            ROOT_LEADER_LINKS[i].leaderId  // 对应组长ID
        );
        
        // 为每个组长添加发送时隙（发送PC命令给组长）
        schedule_addActiveSlot(
            ROOT_LEADER_LINKS[i].slotOffset + NUM_GROUPS,// 错开接收时隙
            CELLTYPE_TX,                    // 发送时�
            FALSE,                           // 非共享时�
            FALSE,                           // 自主调度
            ROOT_LEADER_LINKS[i].channelOffset,
            ROOT_LEADER_LINKS[i].leaderId  // 对应组长ID
        );

    }
    
    // 3. 注册数据处理函数（处理来自组长的数据�
    registerDataHandler(rootDataHandler);
    
    // 4. 注册PC命令处理函数（处理来自PC的命令）
    registerPCCommandHandler(handlePCCommand);
    
    // 5. 启动定时器，定期检查PC连接状�
    //startPCConnectionTimer();
}

// 处理调度命令
// void process_schedule_command(uint8_t* payload, uint16_t len) {
//     uint8_t cmd;
//     schedule_entry_t entry;
//     uint16_t slotOffset;
    
//     // 检查参�
//     if(payload == NULL || len < 1) {
//         return;
//     }
    
//     cmd = payload[0];
//     switch(cmd) {
//         case CMD_ADD_SCHEDULE:
//             if(len < 7 + sizeof(open_addr_t)) {
//                 return;
//             }
//             // 解析调度条目
//             entry.slotOffset = (payload[1] << 8) | payload[2];
//             entry.channelOffset = (payload[3] << 8) | payload[4];
//             entry.type = payload[5];
//             memcpy(&entry.neighbor, &payload[6], sizeof(open_addr_t));
            
//             // 添加到调度表
//             add_schedule_entry(&entry);
//             break;
            
//         case CMD_REMOVE_SCHEDULE:
//             if(len < 3) {
//                 return;
//             }
//             // 解析时隙偏移
//             slotOffset = (payload[1] << 8) | payload[2];
//             remove_schedule_entry(slotOffset);
//             break;
            
//         case CMD_CLEAR_SCHEDULE:
//             // 清空调度�
//             init_schedule_table();
//             break;
            
//         default:
//             // 未知命令
//             break;
//     }
// }