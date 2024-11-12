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


// 处理调度命令
void process_schedule_command(uint8_t* payload, uint16_t len) {
    uint8_t cmd;
    schedule_entry_t entry;
    uint16_t slotOffset;
    
    // 检查参数
    if(payload == NULL || len < 1) {
        return;
    }
    
    cmd = payload[0];
    switch(cmd) {
        case CMD_ADD_SCHEDULE:
            if(len < 7 + sizeof(open_addr_t)) {
                return;
            }
            // 解析调度条目
            entry.slotOffset = (payload[1] << 8) | payload[2];
            entry.channelOffset = (payload[3] << 8) | payload[4];
            entry.type = payload[5];
            memcpy(&entry.neighbor, &payload[6], sizeof(open_addr_t));
            
            // 添加到调度表
            add_schedule_entry(&entry);
            break;
            
        case CMD_REMOVE_SCHEDULE:
            if(len < 3) {
                return;
            }
            // 解析时隙偏移
            slotOffset = (payload[1] << 8) | payload[2];
            remove_schedule_entry(slotOffset);
            break;
            
        case CMD_CLEAR_SCHEDULE:
            // 清空调度表
            init_schedule_table();
            break;
            
        default:
            // 未知命令
            break;
    }
}