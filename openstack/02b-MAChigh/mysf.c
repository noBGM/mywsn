// 根节点分配资源给组长节点
bool assign_resource_to_leader(uint8_t groupId, uint16_t startSlot, uint16_t endSlot) {
    schedule_entry_t entry;
    uint16_t i;
    
    // 检查参数
    if(startSlot >= g_schedule_table.frameLength || 
       endSlot >= g_schedule_table.frameLength ||
       startSlot > endSlot) {
        return FALSE;
    }
    
    // 只有根节点可以分配资源给组长
    if(g_schedule_table.nodeRole != SCHEDULE_TYPE_ROOT) {
        return FALSE;
    }
    
    // 检查时隙范围是否已被占用
    for(i = startSlot; i <= endSlot; i++) {
        if(get_schedule_entry(i) != NULL) {
            return FALSE;
        }
    }
    
    // 为组长分配资源
    entry.type = SCHEDULE_TYPE_LEADER;
    entry.groupId = groupId;
    entry.isUsed = TRUE;
    
    // 添加调度条目
    for(i = startSlot; i <= endSlot; i++) {
        entry.slotOffset = i;
        entry.channelOffset = i % NUM_CHANNELS; // 简单的信道分配策略
        if(!add_schedule_entry(&entry)) {
            // 添加失败,回滚已添加的条目
            while(i > startSlot) {
                i--;
                remove_schedule_entry(i);
            }
            return FALSE;
        }
    }
    
    return TRUE;
}

// 组长分配资源给组员节点
bool alloc_resource_to_member(open_addr_t* memberAddr, uint16_t slotOffset) {
    schedule_entry_t entry;
    schedule_entry_t* existingEntry;
    
    // 检查参数
    if(memberAddr == NULL || 
       slotOffset >= g_schedule_table.frameLength) {
        return FALSE;
    }
    
    // 只有组长可以分配资源给组员
    if(g_schedule_table.nodeRole != SCHEDULE_TYPE_LEADER) {
        return FALSE;
    }
    
    // 检查时隙是否在组长的资源范围内
    existingEntry = get_schedule_entry(slotOffset);
    if(existingEntry == NULL || 
       existingEntry->type != SCHEDULE_TYPE_LEADER ||
       existingEntry->groupId != g_schedule_table.groupId) {
        return FALSE;
    }
    
    // 创建组员的调度条目
    entry.slotOffset = slotOffset;
    entry.channelOffset = existingEntry->channelOffset;
    entry.type = SCHEDULE_TYPE_MEMBER;
    entry.groupId = g_schedule_table.groupId;
    entry.isUsed = TRUE;
    memcpy(&entry.neighbor, memberAddr, sizeof(open_addr_t));
    
    // 移除原有的组长条目
    remove_schedule_entry(slotOffset);
    
    // 添加组员的调度条目
    if(!add_schedule_entry(&entry)) {
        // 添加失败,恢复组长的条目
        add_schedule_entry(existingEntry);
        return FALSE;
    }
    
    return TRUE;
}
// 初始化调度表
void init_schedule_table(void) {
    memset(&g_schedule_table, 0, sizeof(schedule_table_t));
    g_schedule_table.frameLength = SLOTFRAME_LENGTH;
}

// 添加调度条目
bool add_schedule_entry(schedule_entry_t* entry) {
    uint16_t i;
    
    // 检查参数
    if(entry == NULL || entry->slotOffset >= g_schedule_table.frameLength) {
        return FALSE;
    }
    
    // 查找空闲位置
    for(i=0; i<MAX_SCHEDULE_ENTRIES; i++) {
        if(!g_schedule_table.entries[i].isUsed) {
            // 复制条目
            memcpy(&g_schedule_table.entries[i], entry, sizeof(schedule_entry_t));
            g_schedule_table.entries[i].isUsed = TRUE;
            g_schedule_table.numActiveEntries++;
            return TRUE;
        }
    }
    return FALSE;
}

// 移除调度条目
bool remove_schedule_entry(uint16_t slotOffset) {
    uint16_t i;
    
    // 检查参数
    if(slotOffset >= g_schedule_table.frameLength) {
        return FALSE;
    }
    
    // 查找并移除条目
    for(i=0; i<MAX_SCHEDULE_ENTRIES; i++) {
        if(g_schedule_table.entries[i].isUsed && 
           g_schedule_table.entries[i].slotOffset == slotOffset) {
            g_schedule_table.entries[i].isUsed = FALSE;
            g_schedule_table.numActiveEntries--;
            return TRUE;
        }
    }
    return FALSE;
}

// 获取调度条目
schedule_entry_t* get_schedule_entry(uint16_t slotOffset) {
    uint16_t i;
    
    // 检查参数
    if(slotOffset >= g_schedule_table.frameLength) {
        return NULL;
    }
    
    // 查找条目
    for(i=0; i<MAX_SCHEDULE_ENTRIES; i++) {
        if(g_schedule_table.entries[i].isUsed && 
           g_schedule_table.entries[i].slotOffset == slotOffset) {
            return &g_schedule_table.entries[i];
        }
    }
    return NULL;
}

#define ENTRIES_PER_GROUP    25    // 每组调度条目数
#define BYTES_PER_ENTRY     2     // 每个调度条目占用字节数
#define GROUP_DATA_SIZE     (ENTRIES_PER_GROUP * BYTES_PER_ENTRY)  // 每组数据大小

// 存储各组调度数据的结构
typedef struct {
    uint8_t schedData[GROUP_DATA_SIZE];  // 50字节调度数据
    bool isValid;                        // 数据是否有效
} group_sched_data_t;

// 处理调度命令
void process_schedule_command(uint8_t* payload, uint16_t len) {
    uint8_t groupId;
    OpenQueueEntry_t* pkt;
    
    // 检查参数
    if(payload == NULL || len < NUM_GROUPS * GROUP_DATA_SIZE) {
        return;
    }
    
    // 解析并存储每组的调度数据
    for(groupId = 0; groupId < NUM_GROUPS; groupId++) {
        // 复制该组的调度数据
        memcpy(groupSchedData[groupId].schedData, 
               payload + groupId * GROUP_DATA_SIZE, 
               GROUP_DATA_SIZE);
        groupSchedData[groupId].isValid = TRUE;
        
        // 为该组创建发送包
        pkt = openqueue_getFreePacketBuffer(COMPONENT_MYSF);
        if(pkt == NULL) {
            log_error(ERR_NO_FREE_PACKET_BUFFER, "mysf");
            return;
        }
        
        // 设置包属性
        pkt->creator = COMPONENT_MYSF;
        pkt->owner = COMPONENT_MYSF;
        pkt->l2_frameType = IEEE154_TYPE_DATA;
        pkt->l2_nextORpreviousHop.type = ADDR_64B;
        // 设置目标组长地址
        memcpy(&pkt->l2_nextORpreviousHop.addr_64b, 
               &groupLeaderAddrs[groupId],
               sizeof(open_addr_t));
        
        // 放入发送队列
        if(packetfunctions_reserveHeader(&pkt, GROUP_DATA_SIZE) != E_SUCCESS) {
            openqueue_freePacketBuffer(pkt);
            log_error(ERR_QUEUE_ADD_FAILED, "mysf");
            return;
        }
        ((uint8_t *) pkt->payload)[0] = GROUP_SCHED_CMD;
        // 复制调度数据到包
        memcpy(pkt->payload + 1, 
               groupSchedData[groupId].schedData, 
               GROUP_DATA_SIZE);
        
    }    

}
