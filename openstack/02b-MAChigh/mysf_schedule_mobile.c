// 移动节点状态
typedef enum {
    MOBILE_STATE_INIT,        // 初始状态
    MOBILE_STATE_SCANNING,    // 扫描阶段
    MOBILE_STATE_JOINING,     // 加入过程
    MOBILE_STATE_CONNECTED,   // 已连接
    MOBILE_STATE_HANDOVER,    // 切换过程
    MOBILE_STATE_SLEEP       // 休眠状态
} mobile_state_t;

// 组长信息结构
typedef struct {
    uint16_t leaderId;
    int8_t   rssi;           // 信号强度
    uint8_t  freeSlots;      // 可用时隙数
    uint8_t  channelOffset;
    uint8_t  joinPriority;   // 加入优先级
} leader_info_t;
// RSSI监控相关参数
typedef struct {
    int8_t  currentRssi;
    int8_t  avgRssi;
    uint8_t sampleCount;
    int8_t  rssiHistory[RSSI_HISTORY_SIZE];
    int8_t  handoverThreshold;
    int8_t  emergencyThreshold;
} rssi_monitor_t;

// 切换相关参数
typedef struct {
    uint16_t currentLeaderId;
    uint8_t  handoverState;
    bool     emergencyHandover;
    uint16_t targetLeaderId;
    uint8_t  handoverAttempts;
} handover_vars_t;
void initMobileSchedule(void) {
    // 初始化移动节点参数
    initMobileParams();
    
    // 1. 扫描组长beacon
    if (scanLeaderBeacons() != E_SUCCESS) {
        // 扫描失败处理
        handleScanFailure();
        return;
    }
    
    // 2. 选择最佳组长
    if (selectBestLeader() != E_SUCCESS) {
        // 选择失败处理
        handleSelectionFailure();
        return;
    }
    
    // 3. 请求时隙资源
    if (requestSlots() != E_SUCCESS) {
        // 请求失败处理
        handleRequestFailure();
        return;
    }
    
    // 4. 启动移动性管理
    startMobilityManagement();
}

// 初始化移动节点参数
static void initMobileParams(void) {
    // 设置移动节点特有参数
    mobile_vars.state = MOBILE_STATE_INIT;
    mobile_vars.scanTimeout = MOBILE_SCAN_TIMEOUT;
    mobile_vars.handoverThreshold = RSSI_HANDOVER_THRESHOLD;
    mobile_vars.minSlotReq = MIN_SLOT_REQUEST;
    mobile_vars.batteryLevel = getBatteryLevel();
}

// 启动移动性管理
static void startMobilityManagement(void) {
    // 1. 启动RSSI监控
    startRssiMonitoring();
    
    // 2. 启动切换评估定时器
    startHandoverTimer();
    
    // // 3. 注册紧急切换处理函数
    // registerEmergencyHandler();
    
    // // 4. 启动电池监控
    // startBatteryMonitoring();
}


static rssi_monitor_t rssi_vars;
static handover_vars_t handover_vars;

static void startBatteryMonitoring(void){
    return;
}


// 启动RSSI监控
static void startRssiMonitoring(void) {
    // 初始化RSSI监控变量
    rssi_vars.sampleCount = 0;
    rssi_vars.handoverThreshold = RSSI_HANDOVER_THRESHOLD;
    rssi_vars.emergencyThreshold = RSSI_EMERGENCY_THRESHOLD;
    
    // 注册RSSI采样定时器
    opentimers_scheduleIn(
        RSSI_MONITOR_TIMER_ID,
        RSSI_SAMPLE_PERIOD,
        TIME_MS,
        TIMER_PERIODIC,
        rssiSampleCallback
    );
}

// RSSI采样回调
static void rssiSampleCallback(opentimer_id_t timerId) {
    // 获取当前RSSI值
    rssi_vars.currentRssi = radio_getReceivedRSSI();
    
    // 更新RSSI历史
    rssi_vars.rssiHistory[rssi_vars.sampleCount % RSSI_HISTORY_SIZE] = rssi_vars.currentRssi;
    rssi_vars.sampleCount++;
    
    // 计算平均RSSI
    rssi_vars.avgRssi = calculateAvgRssi();
    
    // RSSI检查
    checkRssiStatus();
}

// 启动切换评估定时器
static void startHandoverTimer(void) {
    // 初始化切换变量
    handover_vars.currentLeaderId = getCurrentLeaderId();
    handover_vars.handoverState = HANDOVER_STATE_NORMAL;
    handover_vars.emergencyHandover = FALSE;
    handover_vars.handoverAttempts = 0;
    
    // 注册切换评估定时器
    opentimers_scheduleIn(
        HANDOVER_TIMER_ID,
        HANDOVER_CHECK_PERIOD,
        TIME_MS,
        TIMER_PERIODIC,
        handoverCheckCallback
    );
}

// 切换评估回调
static void handoverCheckCallback(opentimer_id_t timerId) {
    // 检查是否需要切换
    if (needHandover()) {
        if (rssi_vars.avgRssi < rssi_vars.emergencyThreshold) {
            // 触发紧急切换
            triggerEmergencyHandover();
        } else {
            // 正常切换流程
            startNormalHandover();
        }
    }
}

// 注册紧急切换处理函数
static void registerEmergencyHandler(void) {
    // 注册紧急情况处理函数
    emergency_vars.handlerFunction = emergencyHandlerCallback;
    
    // 设置紧急情况检测阈值
    emergency_vars.rssiThreshold = RSSI_EMERGENCY_THRESHOLD;
    emergency_vars.maxAttempts = MAX_EMERGENCY_ATTEMPTS;
    
    // 注册紧急情况检测定时器
    opentimers_scheduleIn(
        EMERGENCY_TIMER_ID,
        EMERGENCY_CHECK_PERIOD,
        TIME_MS,
        TIMER_PERIODIC,
        emergencyCheckCallback
    );
}

// 紧急情况检测回调
static void emergencyCheckCallback(opentimer_id_t timerId) {
    if (isEmergencyCondition()) {
        emergency_vars.handlerFunction();
    }
}

// 紧急处理回调
static void emergencyHandlerCallback(void) {
    // 1. 立即开始扫描新组长
    startEmergencyScan();
    
    // 2. 使用备用信道通知当前组长
    notifyCurrentLeader(EMERGENCY_HANDOVER_MSG);
    
    // 3. 尝试快速切换
    performEmergencyHandover();
}

// 检查RSSI状态
static void checkRssiStatus(void) {
    if (rssi_vars.avgRssi < rssi_vars.emergencyThreshold) {
        // 触发紧急处理
        emergency_vars.handlerFunction();
    } else if (rssi_vars.avgRssi < rssi_vars.handoverThreshold) {
        // 开始预切换扫描
        startPreHandoverScan();
    }
}

// 执行紧急切换
static void performEmergencyHandover(void) {
    // 1. 快速扫描可用组长
    leader_info_t* newLeader = quickScanLeaders();
    
    if (newLeader != NULL) {
        // 2. 立即请求加入新组长
        requestEmergencyJoin(newLeader->leaderId);
        
        // 3. 等待响应，超时后重试
        waitForJoinResponse(EMERGENCY_JOIN_TIMEOUT);
    } else {
        // 没有找到合适的组长，进入应急模式
        enterEmergencyMode();
    }
}
// RSSI监控回调
static void rssiMonitorCallback(opentimer_id_t timerId) {
    int8_t currentRssi = measureRssi();
    
    // 检查是否需要触发切换
    if (currentRssi < mobile_vars.handoverThreshold) {
        // 开始预切换扫描
        startPreHandoverScan();
    }
}

// 电池监控回调
static void batteryMonitorCallback(opentimer_id_t timerId) {
    uint8_t batteryLevel = getBatteryLevel();
    
    // 根据电池电量调整行为
    if (batteryLevel < BATTERY_CRITICAL_LEVEL) {
        // 进入低功耗模式
        enterLowPowerMode();
    } else {
        // 更新请求的时隙数量
        adjustSlotRequest(batteryLevel);
    }
}

// 处理扫描失败
static void handleScanFailure(void) {
    // 记录错误
    LOG_CRITICAL(COMPONENT_SCHEDULE, ERR_SCAN_LEADER_BEACON, 0);
    
    // 设置重试定时器
    opentimers_scheduleIn(
        SCAN_RETRY_TIMER_ID,
        SCAN_RETRY_DELAY,
        TIME_MS,
        TIMER_ONESHOT,
        retryInitialization
    );
    
    // 进入休眠状态
    mobile_vars.state = MOBILE_STATE_SLEEP;
}

// 预切换扫描
static void startPreHandoverScan(void) {
    // 在当前连接保持的同时扫描其他组长
    mobile_vars.state = MOBILE_STATE_HANDOVER;
    
    // 启动后台扫描
    startBackgroundScan();
    
    // 设置切换超时
    setHandoverTimeout();
}

// 进入低功耗模式
static void enterLowPowerMode(void) {
    // 减少时隙请求
    reduceSlotUsage();
    
    // 增加休眠时隙
    increaseSleepSlots();
    
    // 调整扫描间隔
    adjustScanInterval();
}