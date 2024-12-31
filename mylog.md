

## machigh

### schedule.c

1. schedule_addActiveSlot()
   1. 取消backup机制。√

## maclow

### ieee802154e.c

1. notif_sendDone()

   1. 核心为向6top上报收发成败，6top根据帧类型来进行处理。应该重新设计，因为6top不应作为默认接口。

2. notif_Receive()

   1. 也是交予6top处理。

3. ri5

   1. 接收到帧后，接收方会判断其被分配了通信资源的父节点是否是发送方，若是则同步至发送方节点。schedule_hasNegotiatedCellToNeighbor()，

      ```c
      // synchronize to the received packet if I'm not a DAGroot and this is my preferred parent or in case I'm
      // in the middle of the join process when parent is not yet selected or in case I don't have an autonomous
      // Tx cell cell to my parent yet
      ```

4. msf_updateCellsElapsed()，msf_updateCellsUsed()

   1. 这是msf.c的函数，但是在ieee802154e.c中(activity_ti1ORri1()和endSlot())直接调用了，应该有一个通用接口层。

      > 但是实际中在没启用ADAPTIVE_MSF宏的时候，两个函数都没有实际作用。

### 同步bug

#### 问题描述

> 仅针对synchronizePacket，因为synchronizeAck不会出现跨时隙的frameReceived，在跨时隙时本身是同步状态的情况会直接调用endSlot()清楚所有活动，包括正处于接收ACK的过程，radio直接关闭，然后重置定时器。

1. synchronizePacket(timeReceived)

   传入的参数timeReceived是相对于时隙开始时刻的帧开始接收时间，用于计算时间修正。

   修正分两种：同步前和同步后。

   同步后的理解稍易，见官方文档，核心思想就是将当前时隙周期加上多等待的时间或减去少等待的时间。[Synchronization - OpenWSN - Confluence (atlassian.net)](https://openwsn.atlassian.net/wiki/spaces/OW/pages/688253/Synchronization)

   同步前的修正核心代码

   ```c
   if (currentValue < timeReceived) 
           newPeriod = (PORT_TIMER_WIDTH) timeCorrection;
   ```

   currentValue即完成接收帧之后所捕捉的时刻，也是相对于时隙起始时刻的值。timeReceived见上。newPeriod即将要设置的本次时隙新周期，用来替代设置前的周期。timeCorrection即时间修正，通过计算timeReceived-TsTxOffset得到，TsTxOffset可以理解为发送方节点进入一个新时隙周期在其开始发送信号之前必须等待的时长。

   currentValue < timeReceived条件满足时意味着接收方在正在接收的过程中已经开始新的时隙，但是通过以下代码将处理逻辑重新回到了上个时隙，

   ```c
   //I'm in the middle of receiving a packet
   if (ieee154e_vars.state == S_SYNCRX) {
   	return;
   }
   ```

   但是其进入synchronizePacket(timeReceived)时，所取得的currentValue是相对于其新时隙的，而timeReceived却是上一时隙的，所以currentValue < timeReceived可能会成立。

   但是若currentValue >= timeReceived时，会产生一个影响性能的BUG，具体分析如下：

   <img src="C:\Users\Administrator\AppData\Roaming\Typora\typora-user-images\image-20241128194304300.png" alt="image-20241128194304300" style="zoom: 25%;" />

   要满足currentValue < timeReceived，则需满足
   $$
   S-0>E-T
   $$
   即$E-T==\text{wdDataDuration}<S$

   而$S==\text{TsTxOffset}+\text{timeCorrection}$

   所以当$\text{wdDataDuration}<\text{TsTxOffset}+\text{timeCorrection}$时才不会出现$\text{currentValue} \ge \text{timeReceived}$，而timeCorrection实际是随机的，以下情况：

   - wdDataDuration < TsTxOffset，timeCorrection >= 0时恒成立，timeCorrection < 0时可能会有异常。
   - wdDataDuration >= TsTxOffset，timeCorrection <= 0时必出现异常，timeCorrection > 0时可能会出现异常。

   出现异常后，会执行else分支：

   ```c
   if (currentValue < timeReceived) {
   	newPeriod = (PORT_TIMER_WIDTH) timeCorrection;
   } else {
   	newPeriod = (PORT_TIMER_WIDTH)((PORT_SIGNED_INT_WIDTH) currentPeriod + timeCorrection);
   }
   ```

   导致当前节点的时隙周期相比较于它期望同步的节点，延长了一个时隙周期：timeCorrection已经使其能够在时隙结束边界互相对齐，而额外的currentPeriod会使得当前节点的时间感知延后。

   后果就是：节点会恰好错过与其他节点所约定好的收发时隙。称之为伪同步，与之相反的概念则为真同步。

#### 可能影响

严重性由高到低

1. 节点宕机。若为中继节点则导致某支路直接瘫痪。（组长节点负责的一个组的数据收集会中断）

2. 对于组员节点。一般不会在中途出现伪同步问题，因为它们始终在向组长节点发送数据，并持续通过组长的ACK进行同步。组员节点的伪同步更大可能发生在初始化阶段。

   由于每次发送之后因为调度表错位所以都不会收到父节点的ACK，且desyncTimeout的设计是需要经过固定个时隙数量之后才会失去同步，所以会因为PDR始终过低而频繁触发housekeeping中的realloc（对于msf而言），与此同时以更高频率发送数据的组员节点会比低频发送的组员节点消耗更多电量。

   若组员节点数据发送频率高，。则很快可以因为desyncTimeout的到期而重新进行同步。但若发送频率低，比如一个超帧周期发送一次。

3. 无效维护。对于组长节点，原定于用于维护小组成员的时隙资源由于错开而无效。

4. 移动支持。像移动节点这种在随机时间频繁加入和退出网络的节点类型受到的影响更大。

5. 资源浪费。



#### 解决方案

##### 补救措施

1. Housekeeping函数

   调用schedule_getCellsToBeRelocated(&parentNeighbor, celllist_delete) 来遍历每个cell并计算其PDR，如果PDR小于30%，则触发6topReallocate删除当前cell，同时添加一个可用的其他cell。

   但是这并不能改变节点属于伪同步的状态。尤其是当添加的cell恰好和邻居用于接收的cell在真实时间上重合的时候，虽然能够进行通信，但是整个调度表的策略安排仍然是错位的，会导致资源的浪费、甚至带来额外的干扰从而降低系统的PDR。

2. deSyncTimeout变量

   该值在每次调用syncpacket函数或syncack函数时重置，是一个随进入每一个活动时隙而减小的量，每次会减去当前活动时隙和上一个活动时隙的差值，并在sync处理中重置。

   若节点始终无法收到任何数据包或者ack，就会调用changeSync(false)来宣布自己为非同步状态。然后后面就会继续去侦听EB包进行新一轮的同步活动。然后大概率会达到真同步的状态。

##### 隐患消除

上述补救措施更多的是属于一个通用的范畴，所解决的问题不局限于我们所分析的伪同步情况，比如信道条件差，节点硬件问题（损坏或者电量耗尽）这些无可避免的外在客观因素。因为无可避免，意味着它总会发生，所以这种基于自检的补救措施是非常合理的。

但是对于伪同步这种本质上属于程序逻辑和参数配置完备性的问题，在合理的约束范围内是可以完全消除的。但最简单有效的方法还是借助本地时隙编号直接判断当前接收是否跨时隙，从逻辑上消除此隐患：

```c
typedef struct {
	...
	PORT_TIMER_WIDTH numOfnewSlot;                  // used in syncpacket and syncack
	...
}ieee154e_vars_t;
```

```c
void isr_ieee154e_newSlot(opentimers_id_t id) {
    ...
	ieee154e_dbg.num_newSlot++;
}
port_INLINE void activity_synchronize_newSlot(void) {
    // I'm in the middle of receiving a packet
    if (ieee154e_vars.state == S_SYNCRX) {
        return;
    }
	ieee154e_vars.numOfnewSlot = ieee154e_dbg.num_newSlot;
    ...
}
```

```c
void synchronizePacket(PORT_TIMER_WIDTH timeReceived) {
    ...
	if (ieee154e_vars.numOfnewSlot != ieee154e_dbg.num_newSlot) {
        newPeriod = (PORT_TIMER_WIDTH) timeCorrection;
    } else {
        newPeriod = (PORT_TIMER_WIDTH)((PORT_SIGNED_INT_WIDTH) currentPeriod + timeCorrection);
    }
    ...
}
```



### 防止时隙中断嵌套

在TSCH协议的实现中，对于时隙切换时可能出现的状态混乱问题已经做了很好的处理。当新时隙的中断触发时，首先会检查当前状态是否为SLEEP状态。**如果不是SLEEP状态，说明上一个时隙的操作还未完成就发生了超时**，这时会记录一个错误日志，包含当前的异常状态和时隙偏移量信息。通过立即return的方式放弃新时隙的执行，而不是嵌套执行新的中断处理函数，这有效防止了中断的无限嵌套导致栈溢出。同时通过endSlot()强制结束当前时隙，清理所有资源并将状态重置为SLEEP。这种设计既能及时发现和记录异常，又能优雅地处理超时情况，避免了状态机的混乱和资源竞争，保证了系统的稳定性。

```c
 //activity_ti1ORri1()
 if (ieee154e_vars.state != S_SLEEP) {
     // log the error
     LOG_ERROR(COMPONENT_IEEE802154E, ERR_WRONG_STATE_IN_STARTSLOT,
     (errorparameter_t) ieee154e_vars.state,
     (errorparameter_t) ieee154e_vars.slotOffset);
     // abort
     endSlot();
     return;
 }
```

### 保证时隙顺利跳转

```c
//IEEE802154e.c
#define DURATION_si  ieee154e_vars.slotDuration-SERIALINHIBITGUARD

void activity_synchronize_newSlot(...){
    ...
    opentimers_scheduleAbsolute(
        ieee154e_vars.serialInhibitTimerId,     // timerId
        DURATION_si,                            // duration
        ieee154e_vars.startOfSlotReference,     // reference
        TIME_TICS,                              // timetype
        isr_ieee154e_inhibitStart               // callback
    );
	openserial_inhibitStop();
}
void endslot(){
    ...
    if (ieee154e_vars.isSync == TRUE) {
        opentimers_scheduleAbsolute(
            ieee154e_vars.serialInhibitTimerId,
            DURATION_si,
            ieee154e_vars.startOfSlotReference,
            TIME_TICS,
            isr_ieee154e_inhibitStart
        );
    }

    if (opentimers_getValue() - ieee154e_vars.startOfSlotReference < DURATION_si) {
        openserial_inhibitStop();
    }
}
```

在时隙跳转前设置约1ms的保护间隔，用来保证时隙跳转时串口不活动。

注意Duration_si = ieee154e_vars.slotDuration-SERIALINHIBITGUARD，而ieee154e_vars.slotDuration会被修改为时隙长度乘以睡眠时隙数，是为了实现让串口禁用定时器始终是在活动时隙跳转前一小段时间（1ms）才触发。同时也保证了在睡眠中的串口通信是始终允许的。

### 安全加密机制

先贴代码

```c
        // 如果安全级别不是无安全级别，则需要解析IEs和获取ASN，
        // 因为nonce是基于ASN创建的，
        // 所以，在认证beacon之前，需要先解析IEs和获取ASN。
        if (ieee154e_vars.dataReceived->l2_securityLevel != IEEE154_ASH_SLF_TYPE_NOSEC) {
            // If we are not synced, we need to parse IEs and retrieve the ASN
            // before authenticating the beacon, because nonce is created from the ASN
            if (!ieee154e_vars.isSync && ieee802514_header.frameType == IEEE154_TYPE_BEACON) {
                if (!isValidJoin(ieee154e_vars.dataReceived, &ieee802514_header)) {
                    break;
                }
            } else { // discard other frames as we cannot decrypt without being synced
                break;
            }
        }
```

在IEEE 802.15.4e安全机制中，nonce(随机数)是用于加密的一个重要组成部分。基于ASN创建nonce的原因如下：

1. ASN(Absolute Slot Number)的特点：
```c
- ASN是一个5字节的计数器
- 从网络启动开始单调递增
- 每个时隙ASN加1
- 网络中所有节点同步ASN
```

2. Nonce的构成：
```c
[源地址(8字节)] + [ASN(5字节)] + [安全级别(1字节)]
```

3. 使用ASN创建nonce的优势：
- **唯一性**：因为ASN持续递增，确保每个时隙的nonce都不同
- **同步性**：网络中所有节点都知道当前ASN，便于解密
- **防重放**：旧的ASN对应的包会被丢弃，防止重放攻击
- **无需额外开销**：ASN本身就需要同步，不需要专门同步nonce

4. 实际应用：
```c
// 创建nonce示例
nonce[0-7]  = 源地址(8字节)
nonce[8-12] = ASN(5字节)      // 保证每个时隙都不同
nonce[13]   = 安全级别        // 用于区分不同的安全模式
```

这种基于ASN的nonce设计既保证了安全性，又充分利用了TSCH网络已有的同步机制。

### IE分组

在IEEE 802.15.4e中，信息元素(IE)被分为不同的组，每个组有一个唯一的组ID。

IE组主要分为以下几类：

```
1. Header IE Groups (头部IE组):
+------------------------+------------------+------------------------+
| Group ID              | 值               | 用途                    |
+------------------------+------------------+------------------------+
| HEADER_TERMINATION_1  | 0x7E             | 表示后面有Payload IE    |
| HEADER_TERMINATION_2  | 0x7F             | 表示后面是Payload       |
| ACK/NACK TIME CORR    | 0x1A             | ACK中的时间校正信息     |
| ... 其他Header IE     |                  |                        |


2. Payload IE Groups (负载IE组):
+------------------------+------------------+------------------------+
| Group ID              | 值               | 用途                    |
+------------------------+------------------+------------------------+
| MLME_IE_GROUPID       | 0x01             | MAC层管理实体信息       |
| IETF_IE_GROUP_ID      | 0x05             | IETF定义的IE(如6top)   |
| PAYLOAD_TERMINATION   | 0xF                | IE结束标记             |
| ... 其他Payload IE    |                  |                        |
```

每种IE组的具体用途：

1. **MLME IE组**：
```c
- 同步信息 (ASN, Join Priority)
- 时隙模板: 定义时隙结构
- 跳频模板：定义跳频序列。
- 时隙帧结构：定义时隙帧的结构和链接。
```

2. **IETF IE组**：
```c
- 6top协议消息
- RPL路由信息
- 其他IETF标准定义的控制信息
```

3. **Header IE组**：
```c
- 时间同步信息
- ACK/NACK信息
- 其他帧头控制信息
```

这种分组结构使得：
1. 不同类型的控制信息可以有序组织
2. 便于快速定位和处理特定类型的IE
3. 支持标准扩展(如IETF新增控制信息)





MLME IE组包含了与MAC层管理相关的信息，比如：

1. 同步信息(Sync IE)：用于网络时间同步，包含ASN和加入优先级。
2. 跳频模板(Channel Hopping IE)：定义跳频序列。
3. 时隙模板(Timeslot IE)：定义时隙结构。
4. 时隙帧链接(Slotframe Link IE)：定义时隙帧的结构和链接。

### why分组

#### 收益

1. retry

   原本代码的retry应该改为0，禁止所有retry。组长在收到缺了某个组员的坐标数据时，不会往根节点传而是直接丢弃。所以，如果不分组，将所有的数据都往根节点传，就会将剔除不完整数据的压力全都放到了根节点处，是组长处理消耗的4倍（假设分4组）。

2. 预测

   > 每个\*代表一个坐标，T代表节点的一次发送。每一行代表一个组员节点。横轴是时间，纵轴代表不同的节点。
   >
   >                     ****************T****************T****************T
   >                 ****************T****************T****************T
   >             ****************T****************T****************T
   >         ****************T****************T****************T
   >     ****************T****************T****************T
   > 上图为节点的IMU是持续工作的
   >
   >                     ****************T               ****************T
   >                 ****************T               ****************T
   >             ****************T               ****************T
   >         ****************T               ****************T
   >     ****************T               ****************T
   > 上图为节点的IMU是间隔一个超帧周期再工作。

3. 管理

   1. 根节点直接管理所有节点，难以及时响应需求：

      - 突发事件处理不及时，根节点可能在处理其他的事情或者距离较远：组长则可以第一时间响应，再由根节点支援，PC做出决策。

   2. 设备故障、错误处理

      ```
      根节点管理：
      设备损坏 → 等待上报 → 根节点统一处理
      
      分组管理：
      设备损坏 → 组长记录 → 根节点汇总
               → 分级处理（小问题就近解决，大问题上报）
      ```

      传输失败的处理，重传开销。

### 随机数生成算法

```c
/*
openstack\cross-layers\openrandom.c: openrandom_get16b()
*/
uint16_t openrandom_get16b(void) {
    uint8_t i;
    uint16_t random_value;
    random_value = 0;
    for (i = 0; i < 16; i++) {
        // Galois shift register
        // taps: 16 14 13 11
        // characteristic polynomial: x^16 + x^14 + x^13 + x^11 + 1
        random_value |= (random_vars.shift_reg & 0x01) << i;
        random_vars.shift_reg = (random_vars.shift_reg >> 1) ^ (-(int16_t)(random_vars.shift_reg & 1) & 0xb400);
    }
    return random_value;
}
```

这段代码实现了一个基于LFSR(线性反馈移位寄存器)的16位随机数生成器，其工作原理如下：

1. 基本结构：
- 使用一个16位的移位寄存器(shift_reg)
- 采用Galois LFSR配置
- 特征多项式为x^16 + x^14 + x^13 + x^11 + 1，对应的tap位置为16、14、13、11位

2. 生成过程：
- 每次循环取移位寄存器的最低位(LSB)
- 将这个位放到结果的对应位置
- 对移位寄存器进行右移操作
- 如果移出的位为1，则与反馈多项式(0xb400)进行异或运算

3. 关键实现：
```c
random_value |= (random_vars.shift_reg & 0x01) << i;  // 取最低位并放到结果中
random_vars.shift_reg = (random_vars.shift_reg >> 1)   // 右移
    ^ (-(int16_t)(random_vars.shift_reg & 1) & 0xb400); // 条件异或
```

4. 数学原理：
- 在GF(2)伽罗瓦域上进行运算
- 选择的特征多项式是本原多项式，可以产生最大周期序列
- 周期长度为2^16-1，几乎遍历了所有16位非零值

这种实现方式的优点是：
- 硬件实现简单
- 计算速度快
- 周期性好
- 资源占用少

缺点是：
- 随机性相对较弱
- 序列可预测
- 不适合密码学应用

### EB格式

<img src="C:\Users\Administrator\AppData\Roaming\Typora\typora-user-images\image-20241216172221495.png" alt="image-20241216172221495" style="zoom:50%;" />

[RFC 8180: Minimal IPv6 over the TSCH Mode of IEEE 802.15.4e (6TiSCH) Configuration (rfc-editor.org)](https://www.rfc-editor.org/rfc/rfc8180.html#appendix-A.1)

### 实现链路重传支持？

```c
port_INLINE void activity_tie5(void) {
    // indicate transmit failed to schedule to keep stats
    schedule_indicateTx(&ieee154e_vars.asn, FALSE);

    // decrement transmits left counter
    ieee154e_vars.dataToSend->l2_retriesLeft--;

    if (ieee154e_vars.dataToSend->l2_retriesLeft == 0) {
        // indicate tx fail if no more retries left
        notif_sendDone(ieee154e_vars.dataToSend, E_FAIL);
    } else {
        // return packet to the virtual COMPONENT_SIXTOP_TO_IEEE802154E component
        ieee154e_vars.dataToSend->owner = COMPONENT_SIXTOP_TO_IEEE802154E;
    }

    // reset local variable
    ieee154e_vars.dataToSend = NULL;

    // abort
    endSlot();
}
```

核心是：如果当前发送失败（触发异常），且重传次数还有剩余，则 ieee154e_vars.dataToSend->owner = COMPONENT_SIXTOP_TO_IEEE802154E;

因为mac的取包逻辑就是在每个活动时隙搜索owner为COMPONENT_SIXTOP_TO_IEEE802154E的pkt。

### 位图

用于组长节点快速判断哪些小组成员提交了数据。





## scheduler

### why scheduler？

单核时基切换、多优先级任务支持。

> 推入调度器队列的任务是在中断栈清空时开始执行。
>
> 在一个时隙中中断栈可能有多次被清空的时机，因为维持状态机某一状态的时长会大于该状态完成相应的指令所需要的时长，在一切正常的情况下，会留出多余的时间让调度器运行。与此同时，由于活动时隙的数量在整个超帧中的占比较低，所以一个超帧中其实会有大量的时间留与调度器。

中断引入新时隙的活动，通过状态变量，确保每个活动开始时之前的堆栈是清空的。



## 03macpong逻辑

去掉网络层、传输层、应用层协议，只保留了mac层。

### 通信流程

1. 节点默认都不是根节点，根节点的设置是openvisualizer通过串口设置的。

   > 这就意味着大家一开始都在监听来自根节点的数据包，而只有当openvisualizer设置之后，才会有根节点来发送数据包。

2. 非根节点分支同步过程

   - 进入一个新时隙，isr_ieee154e_newSlot

   - 若未同步则进行监听，activity_synchronize_newSlot

     - 随机选择一个信道
     - asn++

   - 监听到帧开始，activity_synchronize_startOfFrame

     - 记录当前时刻timeReceived

     - ieee154e_processIEs()处理EB帧的IE

       - 在isValidEbFormat()中调用asnStoreFromEB()存储EB帧携带的asn。

       - ieee154e_syncSlotOffset()获取slotOffset和asnOffset

         > slotOffset，时隙在时隙帧中偏移，不多说。
         >
         > asnOffset，用于计算信道跳变，channel = 11 + chTemplate[(asnOffset + channelOffset) % NUM_CHANNELS]，信道模板chTemplate存储的是相对于协议规定的信道11的偏移。asnOffset是每时隙+1，channelOffset是用户指定。

   - 帧接收末尾，activity_synchronize_endOfFrame

     - synchronizePacket(timeReceived);
     - changeIsSync(TRUE);

3. 根节点发EB包：

   - 首先，网络伊始，根节点、非根节点未建立任何联系。

   - 根节点会调用schedule_startDAGroot为自己添加一个minimal cell（0，0）。非根节点则会在ieee154e_init中配置定时器以直接开始时隙活动，进而进入activity_synchronize_newSlot()等待同步。

   - 当进入minimal cell后，根节点由于自身就是时间基准，所以配置为isSync(true)，然后调用openqueue_macGetEBPacket获取EB包，进一步根节点将自己的asn放入EB包的l2_ASNpayload字段......

     > 注意，EB包是由sixtop_init()初始化函数注册的定时器回调sixtop_sendingEb_timer_cb以每超帧周期1/10的概率触发sixtop_sendEB，该函数sixtop_sendEB会创建EB包并推入包队列供openqueue_macGetEBPacket获取。

   - 根节点启动发送EB包的流程

   - 非根节点则一开始就走inSync(false)且非root分支进入了activity_synchronize_newSlot()，持续监听，直到定时器到期（TsSlotDuration）触发下一个时隙到来

     - 若新时隙到来时，上个时隙已经处于接收状态（即接收中途时隙就结束了），新时隙会结束中断并返回，恢复到上个时隙的堆栈，继续中断前的指令进行处理。这里会有一个问题（详见上面的同步bug笔记）
     - 一直没有进行接收，则除了一些特殊处理外，即便时隙更迭，也会一直处于接收状态，直到接收到信号才会更换状态。

   - 非根节点接收到EB之后，会调用ieee154e_processIEs()

     - 进一步调用isValidEBFormat，将EB信息中的时隙偏移模板、信道偏移模板（也是0，0）加入本地调度表，用于支持本地的EB发送。
       - 调用asnStoreFromEB取出EB的同步内容（主要是asn，slotOffset，asnOffset），并通过synchronizePacket(caputureTime_startOfFrame)将自己当前的slot边界同步到根节点的边界。

     > 非根节点有两种添加邻居的方式：
     >
     > 1. 在每次完成整个接收操作后，会调用notif_receive()，将task_sixtopNotifReceive()推入调度器任务队列，进而调用neighbors_indicateRx()，进而调用registerNewNeighbor()为自己添加新邻居（如果是的话）。
     > 2. 收到EB后，在activity_synchronize_endOfFrame()中的synchronizePacket()之后直接调用notif_receive()，后续过程与1相同。

4. 当根节点切换为非根节点

   - 非根节点都会设置slotSkip = TRUE：
     - 在activity_ti1ORri1中配置需要跳过一些非活动时隙。

   - 原因：
     - 当节点从根节点切换为非根节点时失去了时间基准的地位需要重新通过EB获取网络同步
     - 减少能耗。

5. 非根节点发普通包

   - 非根节点在registerNewNeighbor()中将根节点注册为自己的邻居后，03macpong的macpong_send会调用sixtop_send_internal向此邻居发送单播包，在sixtop_send_internal中通过自主调度（即根据根节点的地址计算其slotoffset和channeloffset）注册活动时隙。

     > 依据是在msf_init()中，每个节点都会根据自主调度规则，根据自己的地址来计算并注册自己的接收活动时隙。

6. 非根节点收普通包

   - task_sixtopNotifReceive中正常会调用网络层接口iphc_receive将数据包向网络层进行传递，而在03macpong中修改处理流程为如下，使得数据包直接在6p（仍为mac）层处理。

     ```c
     void iphc_receive(OpenQueueEntry_t *msg) {
         msg->owner = COMPONENT_IPHC;
         macpong_send(++msg->payload[0]);
         openqueue_freePacketBuffer(msg);
     }
     ```

   - 在iphc_receive的前几行进行一次判断，若是EB包，则不会往上传。

7. 非根节点发EB包

   - 核心逻辑仍在sixtop_sendEB中，若以下条件为真，则说明该非根节点不满足发送EB要求。

     ```c
     (ieee154e_isSynch() == FALSE) ||
     (IEEE802154_security_isConfigured() == FALSE) ||
     (icmpv6rpl_getMyDAGrank() == DEFAULTDAGRANK) ||
     icmpv6rpl_daoSent() == FALSE)
     /*
     在开始广播网络之前，我需要确保自己满足以下条件：
     已经同步到网络
     已经加入网络
     已经获得了DAG等级(在RPL路由中的层级)
     已经发送过DAO消息(下行路由通告)
     这些条件都是为了确保其他节点能够通过下行路由找到我，所以必须先发送DAO消息。
     简单来说就是：在节点开始向外广播之前，需要先确保自己是网络中的"合法公民"，并且其他节点能够可靠地找到自己。这就像是你要先有门牌号，别人才能找到你家一样。
     */
     ```

   - 若满足条件，则发生过程和根节点一致。

8. 除了IE外的payload在什么地方处理的？

   - IE在MAC层进行处理。
   - notif_receive往上层传，直到应用层。

## 修改

### 网络拓扑结构

1. 问题：如何在最初让组长节点确定与根节点的活动时隙？
   1. 硬编码。
      - 组号：节点硬编码组号。
        - 根节点首先在其本地为每个组号分配发送和接收时隙、以及一个额外的广播时隙。
        - 根节点在广播包携带所有组的调度表信息。
        - 组长在其本地会配置一个和根节点同样的广播时隙。
        - 组长节点接收到广播包后取出调度表，并根据自己的组号计算或取出属于自己的调度表。

2. 问题：如何在最初让组员节点确定与组长节点的活动时隙？
   1. 组长节点广播调度表。组员节点收到调度表后取出属于自己的资源。

3. 问题：某次传输失败怎么处理？
   1. 整帧丢弃？（帧：即用以描述某一时刻的动作的所有节点的坐标集合）
      - 丢弃逻辑在组长节点处处理，将丢弃之后的数据整理发送到根节点。	

4. 问题：组员节点如何区分根节点和组长节点的EB？
   1. 根节点和组长节点使用不同的信道偏移。
   2. 取消了信道偏移随时隙变化的随机性，即修改calculateFrequency()，删除了ieee154e_vars.asnOffset
   3. 取消了同步过程中监听时频率选择的随机性，即将activity_synchronize_newSlot中的ieee154e_vars.freq = (openrandom_get16b() & 0x0F) + 11修改为ieee154e_vars.freq=calculateFrequency(SCHEDULE_MINIMAL_6TISCH_CHANNELOFFSET);

5. 问题：组员节点如何区分不同的组长节点EB？

> 信道区分？EB内容区分？
>
> 如果信道区分，那么对于移动节点，遍历信道就可以得到所有的组长信息。
>
> 如果EB内容区分，则需要取出EB才能得到所有的组长信息，从而判断是否是期望的组长，且可能会重复获取同一个组长的信息，这些会带来额外的计算开销从而拖缓系统的响应。
>
> 但即便是信道区分，EB也需要携带不同组长所特有的信息，比如该组组长所使用的广播时隙的时隙偏移和信道偏移，组员节点将用于注册相应的接收时隙。

​		1. 暂时选择通过信道来划分。

<img src="C:\Users\Administrator\AppData\Roaming\Typora\typora-user-images\image-20241216142705520.png" alt="image-20241216142705520" style="zoom:33%;" />

若1、2、3、4在同一信道偏移上，则组员节点只能通过EB内容来区分组长。

若在同一时隙上，则移动节点无法在一个超帧周期内获取所有组长节点的信息。

而这种布局可以同时满足两种要求。

6. 问题：非根节点收到根节点的EB后如何确定自己的EB发送计划？

   原本方案是：调用isValidEBFormat，将EB信息中的时隙偏移模板、信道偏移模板（0，0，可修改）加入本地调度表，用于支持本地的EB发送。

   由于我们的方案是不同组长的EB发送应该在不同的信道偏移及时隙偏移，而EB本身作为广播包也不应该有特异性，即每次发送的EB包都不应该不相同，所以取消了在isValidEBFormat中根据EB所携带的模板信息加入本地调度表的部分。

7. 问题：组员节点到组长节点的单播如何确定？

   答：由组长节点广播小组全局调度表实现。各组员节点根据自身标号（eg. 0,1,2,3,...,15，共16个组员节点）从组长节点广播的调度表中取出属于自己的资源分配。在此之前，组长节点应已经注册好了对于每个组员的接收时隙，（eg. 16个组员，每个组员注册X个发送时隙，那么组长节点就要提前注册好16X个接收时隙）。

   7.1 问题：若有组员注册时隙失败，组长节点如何知晓并处理？

   ​	答：组员节点注册时隙失败有以下迹象：组长节点在位于该组员节点的接收状态时会触发超时。	组长节点将始终在其广播时隙尝试恢复出问题的组员节点，在其广播包中用1个字节表示该广播  	包的类型，此情形可表示为针对某几个可能出问题的特定组员的组播，其他收到广播包的组员节	点将忽略此包。此策略的依据是：组长节点没有能力去修复出问题的组员节点，只能期待其问题	被人为解决，组长节点只需要保证其组员节点再次成功上线的时候，依然能够顺利加入小组。

8. 问题：组长节点如何知道移动节点的加入？

   答：移动节点通过接收并分析所有组长节点的EB，选择最合适的组长节点进行加入。

   组长对移动时隙池提前注册好本地的接收部分，当接收部分收到响应后注册发送部分（减小功耗）。举个例子，组长A的移动时隙池包含元索引10、11、12、13，移动节点从EB中取得时隙池之后，会从可选TX和RX部分选取期望的发送和接收时隙并注册，然后通过该时隙向组长A发送自己入组的信息（包括自身ID和所选取的时隙索引集）和想上报的应用数据信息，组长A收到后，根据入组信息中的接收时隙索引在其本地注册相应的发送和接收时隙。

   类比于linux支持热插拔的机制。在内核的内存布局中分出来一块zone_device专门留给热插拔的设备，这块区域不能被用作普通的系统内存，可能还有特殊的一致性要求。

9. 问题：如果多个移动节点选取了同一个组长节点的同一块资源，如何知晓，如何解决？

   答：同步机制问题，要么使这种情况能够避免，要么存在锁机制，要么使移动节点最终加入的决定权留在组长节点手里（而非移动节点检查到EB就能加入）。我们采取的机制是元索引+资源池，为每个移动节点分配其专属的元时隙，但后续其真正使用的资源池是共享的。在元索引的交互中组长会最终确定资源池的最终分配。

   - 分配机制对于移动节点而言可以是：what i need OR what i choose，

10. 每个组长节点的移动时隙池都是一样的吗？

    答：为了支持不同功能类型的移动节点，组长节点的移动时隙池在一个超帧中其时隙偏移的分布是不同的，数据收集型的时隙池的时隙资源是连续的，用于快速低延迟的数据和指令信息交换。而交互型的时隙池的时隙资源是均匀分布的，用于频繁入组出组的握手信息交换。

11. 组长的EB包含什么额外内容吗？

    1. 表示该组的冷热程度，即最近有多少个移动节点加入了该组。
    2. 表示该组的资源数量和分布类型
       1. 但是有信息滞后性，会影响移动节点的决策优劣。
       2. .

12. 根节点的角色？

    1. speaker of server, 作为server的无线通信话筒，传递server的指令
    2. what else? root节点为了能够完整接收所有队长节点传输来的数据，通常会被分配大于预期的时隙资源，也就是说root节点的资源总是会有余量的。以及在通过串口传输完成一个组长节点的所有数据之后，在下一个组长节点的传输到来之前，也是能够完成部分工作的。否则，就需要server的额外的指令，再者如果传输的数据是有问题的则浪费了一次串口传输的资源。

### 数据收集策略

特征：

1. 非关节节点距离关节的距离不变，只是方向变化。（球面运动）
2. 关节节点与相邻关节节点的距离不变，只是方向变化。（根节点可以据此优化）

问题：

1. 组长节点可以累计多少接收的数据再一起发送给根节点？
   1. 非实时处理系统的时延容忍度较高

## 06mysf逻辑

去掉网络层、传输层、应用层协议，只保留了mac层。

### 通信流程

1. 根节点在编译时确定，且提前已分配和各组长节点的后续通信资源，相应的，组长节点也分配好了和根节点的通信资源。

   > 根节点会周期性的广播EB，组长节点需要通过成功接收EB来和根节点同步。

2. 组长节点同步过程

   - 进入一个新时隙，isr_ieee154e_newSlot

   - 若未同步则进行监听，activity_synchronize_newSlot

     - 随机选择一个信道
     - asn++

   - 监听到帧开始，activity_synchronize_startOfFrame

     - 记录当前时刻timeReceived

     - ieee154e_processIEs()处理EB帧的IE

       - 在isValidEbFormat()中调用asnStoreFromEB()存储EB帧携带的asn。

       - ieee154e_syncSlotOffset()获取slotOffset和asnOffset

         > slotOffset，时隙在时隙帧中偏移，不多说。
         >
         > asnOffset，用于计算信道跳变，channel = 11 + chTemplate[(asnOffset + channelOffset) % NUM_CHANNELS]，信道模板chTemplate存储的是相对于协议规定的信道11的偏移。asnOffset是每时隙+1，channelOffset是用户指定。

   - 帧接收末尾，activity_synchronize_endOfFrame

     - synchronizePacket(timeReceived);
     - changeIsSync(TRUE);

3. 根节点发EB包：

   - 首先，网络伊始，根节点、非根节点未建立任何联系。

   - 根节点会调用schedule_startDAGroot为自己添加一个minimal cell（0，0）。非根节点则会在ieee154e_init中配置定时器以直接开始时隙活动，进而进入activity_synchronize_newSlot()等待同步。

   - 当进入minimal cell后，根节点由于自身就是时间基准，所以配置为isSync(true)，然后调用openqueue_macGetEBPacket获取EB包，进一步根节点将自己的asn放入EB包的l2_ASNpayload字段......

     > 注意，EB包是由sixtop_init()初始化函数注册的定时器回调sixtop_sendingEb_timer_cb以每超帧周期1/10的概率触发sixtop_sendEB，该函数sixtop_sendEB会创建EB包并推入包队列供openqueue_macGetEBPacket获取。

   - 根节点启动发送EB包的流程

   - 非根节点则一开始就走inSync(false)且非root分支进入了activity_synchronize_newSlot()，持续监听，直到定时器到期（TsSlotDuration）触发下一个时隙到来

     - 若新时隙到来时，上个时隙已经处于接收状态（即接收中途时隙就结束了），新时隙会结束中断并返回，恢复到上个时隙的堆栈，继续中断前的指令进行处理。这里会有一个问题（详见上面的同步bug笔记）
     - 一直没有进行接收，则除了一些特殊处理外，即便时隙更迭，也会一直处于接收状态，直到接收到信号才会更换状态。

   - 非根节点接收到EB之后，会通过ieee154e_processIEs()取出EB的同步内容（主要是asn，slotOffset，asnOffset），并通过synchronizePacket(caputureTime_startOfFrame)将自己当前的slot边界同步到根节点的边界。

     > 非根节点有两种添加邻居的方式：
     >
     > 1. 在每次完成整个接收操作后，会调用notif_receive()，将task_sixtopNotifReceive()推入调度器任务队列，进而调用neighbors_indicateRx()，进而调用registerNewNeighbor()为自己添加新邻居（如果是的话）。
     > 2. 收到EB后，在activity_synchronize_endOfFrame()中的synchronizePacket()之后直接调用notif_receive()，后续过程与1相同。

4. 当根节点切换为非根节点

   - 非根节点都会设置slotSkip = TRUE：
     - 在activity_ti1ORri1中配置需要跳过一些非活动时隙。

   - 原因：
     - 当节点从根节点切换为非根节点时失去了时间基准的地位需要重新通过EB获取网络同步
     - 减少能耗。

5. 非根节点发普通包

   - 非根节点在registerNewNeighbor()中将根节点注册为自己的邻居后，03macpong的macpong_send会调用sixtop_send_internal向此邻居发送单播包，在sixtop_send_internal中通过自主调度（即根据根节点的地址计算其slotoffset和channeloffset）注册活动时隙。

     > 依据是在msf_init()中，每个节点都会根据自主调度规则，根据自己的地址来计算并注册自己的接收活动时隙。

6. 非根节点收普通包

   - task_sixtopNotifReceive中正常会调用网络层接口iphc_receive将数据包向网络层进行传递，而在03macpong中修改处理流程为如下，使得数据包直接在6p（仍为mac）层处理。

     ```c
     void iphc_receive(OpenQueueEntry_t *msg) {
         msg->owner = COMPONENT_IPHC;
         macpong_send(++msg->payload[0]);
         openqueue_freePacketBuffer(msg);
     }
     ```

   - 在iphc_receive的前几行进行一次判断，若是EB包，则不会往上传。

7. 非根节点发EB包

   - 核心逻辑仍在sixtop_sendEB中，若以下条件为真，则说明该非根节点不满足发送EB要求。

     ```c
     (ieee154e_isSynch() == FALSE) ||
     (IEEE802154_security_isConfigured() == FALSE) ||
     (icmpv6rpl_getMyDAGrank() == DEFAULTDAGRANK) ||
     icmpv6rpl_daoSent() == FALSE)
     /*
     在开始广播网络之前，我需要确保自己满足以下条件：
     已经同步到网络
     已经加入网络
     已经获得了DAG等级(在RPL路由中的层级)
     已经发送过DAO消息(下行路由通告)
     这些条件都是为了确保其他节点能够通过下行路由找到我，所以必须先发送DAO消息。
     简单来说就是：在节点开始向外广播之前，需要先确保自己是网络中的"合法公民"，并且其他节点能够可靠地找到自己。这就像是你要先有门牌号，别人才能找到你家一样。
     */
     ```

   - 若满足条件，则发生过程和根节点一致。

8. 除了IE外的payload在什么地方处理的？

   - IE在MAC层进行处理。
   - notif_receive往上层传，直到应用层。



## 性能比较

1. 移动节点：通过组长入网 vs 通过根节点入网

   虽然本质上都是操纵同样的时隙单元，但在组长这是作为被分配的额外的移动池，组长自己有权力去支配。

   根节点则掌管所有的时隙单元。移动节点固然可以直接向根节点去申请，但如果在移动节点远离根节点的时候，移动节点是否只能被动等待合适的加入时机呢？即便隔得近，又如果多个移动节点都想和根节点申请呢，冲突带来的影响？

# 新增

## process_schedule_command

1. 在openserial.c中,当通过串口接收到调度命令时:

   ```c
   // 在openserial.c中添加命令类型定义
   #define SERFRAME_MOTE2PC_SCHEDULE    0x20   // 调度命令标识
   
   void openserial_handleRxFrame() {
       uint8_t cmdByte;
       
       cmdByte = openserial_vars.inputBuf[0];
       
       switch(cmdByte) {
           case SERFRAME_MOTE2PC_SCHEDULE:
               // 从PC接收到调度命令时处理
               process_schedule_command(&openserial_vars.inputBuf[1],
                                     openserial_vars.inputBufFillLevel-1);
               break;
           // ...其他命令处理
       }
   }
   ```

2. 在IEEE802154E.c中,当接收到无线电数据包中包含调度信息时

   ```c
   // 在IEEE802154E.h中添加命令类型定义 
   #define IEEE154_TYPE_SCHEDULE_CMD    0x15   // 调度命令帧类型
   
   void ieee154e_endOfFrame(PORT_TIMER_WIDTH capturedTime) {
       uint8_t frameType;
       
       // 获取帧类型
       frameType = *((uint8_t*)msg->payload);
       
       if(frameType == IEEE154_TYPE_SCHEDULE_CMD) {
           // 从无线电数据包中接收到调度命令时处理
           process_schedule_command(msg->payload + 1, msg->length - 1);
       }
       
       // ...其他帧类型处理
   }
   ```

   这样实现了两种调度命令的接收途径:

   1. PC通过串口向根节点发送调度命令
   2. 根节点通过无线电向组长发送调度命令,组长向组员发送调度命令

   调度命令的处理流程是:

   1. PC -> 根节点:通过串口发送完整调度表
   2. 根节点 -> 组长:通过无线电发送资源分配命令
   3. 组长 -> 组员:通过无线电发送具体时隙分配

   每个节点根据自己的角色处理相应的调度命令。这样就构成了一个完整的分层调度系统。



## USB传输

背景

为了使**根节点**支持与拓扑中通过BLE可达到分别2Mbps速率的4个**组长节点**进行通信，至少需要让根节点有8Mbps的速率才能使得所有组长节点若以全速发送的信息不丢失。

若使用uart传输，那么110kbps的速率肯定是无法满足的。

而NRF52840支持USB2.0 full-speed协议，最高速率可达12Mbps。

> high-speed模式可达480Mbps，但不支持。

### 工具

1. nRF Connect for Desktop.exe

   > 这是官方提供的一个跨平台开发辅助软件，我们主要使用了其串行终端程序用来调试USB收发。
   >
   > 下载地址：[nRF Connect for Desktop - Downloads - nordicsemi.com](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop/Download#infotabs)

2. nRF5_SDK

   > 官方sdk，我们使用了其example中完整的usbd_cdc_acm工程文件。
   >
   > 下载地址：[nRF5 SDK downloads - nordicsemi.com](https://www.nordicsemi.com/Products/Development-software/nRF5-SDK/Download#infotabs)

**连接方式**

两根USB-typec数据线，一根连接电源端口和PC，一根连接USB端口和PC。

### 性能

经测试最高可达900KB/s的净数据速率（不考虑协议开销）。

### debug

找不到segger某库：版本过高不支持，从7.10恢复到5.70即可。



## 同步bug

见上

## 串口恢复bug

原码	

```c
//ieee802154e.c
if (DURATION_si + ieee154e_vars.startOfSlotReference - opentimers_getValue() < 
    ieee154e_vars.slotDuration) {
    openserial_inhibitStop();
}
```

修改为

```c
if (opentimers_getValue() - ieee154e_vars.startOfSlotReference < DURATION_si) {
    openserial_inhibitStop();
}
```

串口禁用相关说明见第一部分内容。

