

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



