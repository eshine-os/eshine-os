<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十七站内容包:同步原语站(信号量 + 互斥锁)

口径(README 索引):
- **里程碑**:eshine-rtos 补上内核人格最后一块——每对象 FIFO 等待
  队列的计数信号量 + owner/递归互斥;等待不再只有"壳等串口"一条路。
  门禁 55→58 双绿。
- **契约**:"唤醒=允许重试,非授予"(take 重检环),唤醒即抢占;
  阻塞在 ps 表里是活状态(ps mid-hold 判据),不是打出来的日志。
- **首跑炸出的真凶不是新原语**:站 16 潜伏 bug——任务栈下标被名字
  拷贝循环复用成了"名字长度",shell 住 stack[5]、心跳住越界区、
  全靠名字长度不撞车;第一对同长并发任务(producer/consumer 双双
  8 字)当场对撞,零帧 eret 去 EL0 PC0。
- **诚实记录**:[53] initramfs 步 4 轮 1 挂闪断(wake_waiter 无条件
  关线×在途拍蒸发,站 16 残余物理),归因入册,修法立为站 18 首选
  候选;三处同步纪律第二起嵌合体(e87a3bb 宣称同步但 help 未落盘)
  本站根治。

文件:
- zhihu-gongzhonghao.md:知乎/公众号长文(主线:"按名字长度发栈的
  内核活了两天"+ 等待的契约设计)
- xiaohongshu.md:小红书短文
- x-thread.md:X 8 推
- youtube.md:YouTube/B 站分镜
