# YouTube 分镜(站 50 日志持久化+开机竞态法医)

标题:cat 一条命令挂死整个系统?——藏在开机 200ms 里的竞态

## 分镜

1. **冷开场(0:00-0:45)**:实机演示症状——busybox cat /etc/motd,
   命令回显,然后死寂。字幕:"系统活着,但它什么都不说"。
2. **三层尸检(0:45-3:30)**:第一层(cat 没退出)→第二层
   (boot 日志里的 "pm stdio bind failed",stdio 缺位=输出写进
   自己的文件 fd)→第三层(新服务的 boot 期阻塞往返 × PM 的
   200ms 绑定窗=同拍打散 FIFO 序)。
3. **基线对照法(3:30-5:00)**:worktree 挂战役前提交跑同一场景;
   gitignore 产物的坑(busybox 拷入+cp -a 时间戳假绿);二分定谳。
4. **修法(5:00-6:15)**:惰性化——boot 窗口撤走一切竞争源;场景
   绊线(expect+fail_if)让竞态无处回归。
5. **功能验收(6:15-7:30)**:logctrl flush/busybox cat /var/log/slog
   读回内核行;轮转双世代;/proc/meminfo/cpuinfo/interrupts 三文件。
6. **收尾(7:30-8:00)**:门禁 139→145;四连绿;下一站预告
   (driver_manager 治理面)。

## 备注

- 战雷段(2-4)是本集爆点,保留完整排查链(症状→对照→二分→定谳)。
- 演示:tools/verify/smoke.py + 场景文件。
