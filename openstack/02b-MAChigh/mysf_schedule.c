// schedule_manager.c
#include "mysf_schedule.h"


void mysf_init(void) {

    #if SCHEDULE_ROOT
        initRootSchedule();
    #elif SCHEDULE_LEADER
        initLeaderSchedule();
    #elif SCHEDULE_MEMBER
        initMemberSchedule();
    #elif SCHEDULE_MOBILE
        initMobileSchedule();
    #endif

}

// 数据接收回调（被TSCH层调用，上报数据包）
void schedule_receive_data_callback(OpenQueueEntry_t* msg) {
    return;
    // if (g_dataHandler != NULL) {
    //     // 提取源地址
    //     uint16_t srcAddr = msg->l2_nextORpreviousHop;
        
    //     // 调用处理函数
    //     g_dataHandler(srcAddr, msg->payload, msg->length);
    // }
    
    // // 释放消息缓冲
    // openqueue_freePacketBuffer(msg);
}

// PC命令接收回调（被串口中断调用�
void schedule_receive_pc_callback(uint8_t* data, uint8_t len) {
    return;
    // if (g_pcCmdHandler != NULL) {
    //     g_pcCmdHandler(data, len);
    // }
}
