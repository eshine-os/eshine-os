<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 27.04 第七站:per-vCPU vtimer_ctx + 多等待者设计 spec

- 日期:2026-10-23/24 · 版本:27.04-dev · 前置:cd1ef63(第六站设备仿真,25 步门禁双绿)
- 用户批准:min-deadline 宿主仲裁(推荐);两站连续执行(vtimer_ctx → UART IRQ)

## 0. 目标与真绿定义

还偏差 #10 的债:双 VM 各自武装定时器、各自入睡、各自按点醒——先睡先醒
不丢(单等待者假设退役)。真绿=门禁 25 → 27 步连续 2 过。

## 1. 形态(用户批准:min-deadline 宿主仲裁)

- **ctx 切换对(参考实现 vtimer_ctx 同构)**:per-VM `vtimer_ctl/vtimer_cval`
  内嵌 vcpu 帧尾(@400/@408,asm 不触及);泵切出(每次 vcpu_run 返回后)
  读物理 CNTV → 存该 VM ctx;切入(vcpu_run 前)恢复该 VM ctx。
  cortex-a57(v8.0)无 CNTV 陷阱位 → guest 直写物理,宿主以切换对齐
  (参考实现同形,KVM 才走陷阱)。
- **物理 CNTV 归属不变量**:运行中 guest 的物理 CNTV 恒为其本人 ctx——
  切出读回即其最新编程,无歧义。
- **多等待者(min-deadline 表)**:宿主侧到期判定不依赖"27 只属于一家"——
  tick(宿主 CNTHP 节拍)扫 armed VM 的 ctx 表,cval 已过者一律
  WOKE(reason=TIMER),物理照旧。**拍频=1 tick 与泵粒度同阶:WOKE 的
  生效时刻本就只能等下一次泵轮到它,即时判定与 tick 判定等效**——
  这不是妥协,是单核调度的本质粒度。
- **27 三路归属**:①guest 运行中到期(物理=其本人)→ 第四站原链
  (HW LR 注入,guest handler 消费);②宿主态到期(host 段)→ 表扫
  WOKE 到期者(多家皆醒);③表扫(每 tick)兜住"在别家量子内错过"。
- **醒后 ctx 熄火**:vgic_wake_waiter 关物理线时同步置该 VM ctx
  ctl=0——防止下次切入装回已过期 cval 即刻重挂 27 风暴。
- WAIT 原因判定改用**保存后的 ctx**(v->vtimer_ctl 使能位),不再读
  物理(物理此刻可能是切出后的他人视角)。
- sched_waiter() 单等待者扫描退役,代之以"到期表扫"。

## 2. guest 侧

- sleep 命令带参:`sleep`(默认 2s)/`sleep 4`(u32 秒,>9 拒绝)——
  双 VM 不同时长才是多等待者判据的形状。

## 3. 门禁 25 → 27(只增不减)

- 新增:双 VM 同时入睡(vm0 2s,vm1 4s)→ vm0 先醒 → vm1 后醒,
  顺序判据(单等待者时代 vm0 醒后 vm1 永睡的回归线);
- 新增:两 VM 醒后各自 shell 完整(命令往返)。

## 4. 偏差登记

#10 收口(CNTVOFF 仍共享 0:双 guest 同墙钟,正确简化,非缺省)。
