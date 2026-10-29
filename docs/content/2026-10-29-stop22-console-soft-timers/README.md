<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第二十二站内容包:[54] 定谳闭案 / 软定时器面

口径(README 索引):
- **A 块([54] 四站悬案闭案)**:R1b dump 取证——needle 不是缺席
  是**字节级互撕**("spinni[vm2] [   n 0.1339g17]");根因两层=
  console_putc 原语裸奔(vuart fg 逐字符透传是漏网裸写者)+ fg
  字符缝隙可嵌整行;修=原语持锁+fg 行缓冲化+泊位冲刷(爆发感知
  +全 VM 静默判)。**第二雷**:自己的泊位冲刷切了同 VM 次核的
  在途行("[vm0] rtos> c[vm0] pu1: online"被门禁 A 轮当场抓住)
  ——全 VM 静默判收治。失败测试先行(60 行风暴 4+ 撕→输出行
  0/60 碎)。
- **B 块(软定时器)**:rt_timer.c 静态池+核 0 tick-ISR 回调语境
  (自旋锁契约:持锁即屏本核,ISR 不可能再入持锁任务),零漂移
  周期重臂;swtdemo 三面(单发/周期×3 自停/取消缺席)。
- **门禁 75→76**,A+B 终构建连续两轮全绿;[54] 观察哨闭案撤销。
- 残余:chardev 分块边界的回显碎裂(判据面零暴露,根治需 100Hz
  宿主时钟=远期 console 服务化);回调内 start 禁止。

文件:
- zhihu-gongzhonghao.md:知乎/公众号长文(主线:四站悬案的真凶
  是一行裸 putc——以及我的修复差点成为下一个真凶)
- xiaohongshu.md:小红书短文(1/10 闪断追了四个站,真凶一行代码)
- x-thread.md:X 8 推(取证→两层根因→第二雷→闭案)
- youtube.md:视频脚本(按图施工:如何从一份 TimeoutError dump
  里读出字节级撕裂)
- 同批:docs/devlog/22-console-atomicity-soft-timers.md
