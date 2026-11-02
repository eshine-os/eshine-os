# 站五十二:ham 健康面 v0(心跳实体+missed 条件-log 动作)

日期:2026-11-02 | 批次 A 末站(49-52 可靠面+治理面批)
参照:参考实现 ham(实体/条件 state+raise 双型/动作五型[command/signal/
bufqueue/waitfor/loginfo]/心跳三态/自愈守护)白描。

## §1 v0 形状

- 心跳实体(name+期望拍距+最近拍+状态);missed 条件=2 周期无拍=>
  MISSED+log 动作(elog 环入卷+控制台);恢复拍=>HEALTHY(recovered
  记录);动作五型取 log 型,其余+raise 型条件+自愈守护=后弧记档。
- hamsrv 内核任务 1s 扫描拍(站 17 睡客机器);ham add/hambeat/
  ham ls 三命令(demo 蹦床;重名=复用重整周期+清拍)。
- 与站 51 治理面互为表里(治理面管怎么起,健康面管活着没);
  warn 动作经站 49 elog 环+站 50 持久化=三站合链([151] 审计门)。

## §2 判据(门禁 148→151,append-only)

- [149] ham add+beat+ls -> HEALTHY
- [150] sleep4 -> MISSED -> beat -> recovered+HEALTHY(全生命周期)
- [151] logctrl flush+cat /var/log -> "ham hb1 missed"(warn 入卷审计)

## §3 非目标(记档)

- 动作 command/signal/bufqueue/waitfor 型/raise 型条件/自愈守护
  (ham 自身被监控)/心跳三态的 MISSEDHIGH 档——消费者上门再立。
