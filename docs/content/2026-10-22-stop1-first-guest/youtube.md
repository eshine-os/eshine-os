# YouTube / B 站分镜脚本 — 第一站:stage-2 + 第一个 guest

## 标题(YouTube 英文)
Three bugs, one uninitialized register: the night my hypervisor met its first guest
## 标题(B 站中文)
被一个未初始化的寄存器耍了一晚:stage-2 与第一个 guest 的三连坑

## 描述
Stop 1 of the from-scratch ARM64 Type-1 hypervisor: a stage-2 module
with non-identity mapping (guest at IPA 0x40000000 → PA 0x50000000),
the vCPU entry/exit path, and a bare-metal guest with two IO routes
to the same UART — hypercalls and direct MMIO through the stage-2
device page. Three debug stories: the mandatory AF bit, the HVC
register contract, and a night-long hunt where SP_EL0 (never
initialized, value zero) impersonated three unrelated bugs until a
gdb single-step nailed it. github.com/eshine-os/eshine-os ·
Apache-2.0 · built 1-2h/day, human + AI.

## 分镜脚本

| # | 时长 | 画面 | 口播要点 |
|---|---|---|---|
| 1 | 0:00-0:30 | 黑屏淡入:run_guest → banner 滚屏 → 崩 | 开场:goal 一句话 —— 第二级地址翻译 + 第一个 guest。以及一晚三坑的预告 |
| 2 | 0:30-1:30 | 代码讲解:VTCR/描述符/非恒等映射示意 | stage-2 模块:guest 以为自己独占 0x40000000,真身在 0x50000000 —— 翻译在工作才证明虚拟化在工作 |
| 3 | 1:30-2:10 | 双 IO 路径录屏(HVC banner + 直访交互) | 一个 UART 两条路:HVC 超调用 + stage-2 设备页直访,两条都绿才认账 |
| 4 | 2:10-3:00 | 坑一:ESR 0x8200000A + ARM 手册表 | AF bit 铁律:手册里有答案的 bug 都是好 bug |
| 5 | 3:00-3:40 | 坑二:反汇编里指针在 x1 | HVC 寄存器契约:ABI 是双方契约,编译器只读你声明的 |
| 6 | 3:40-5:30 | 主菜:gdb 四连单步(A/B/C/H) | SP_EL0=0 连环案:banner 活/退出死的一个根因;"修复"救活一半杀死一半;boot 归一三行修 |
| 7 | 5:30-6:10 | 番外:GOOK18822187 现场 | FR 偏移 0x14 vs 0x18:永不等待的轮询=竞态制造机;trace 一开就躲的 Heisenbug |
| 8 | 6:10-6:40 | make verify 8 连过 + 仓库页 | 门禁;方法论三句(换 gdb/查错误码/归一不补偿);下集:VM 抽象 |

## 录屏清单
1. run_guest 全流程(banner→交互→exit→回宿主)
2. gdb 会话复刻:四断点四截图(核心素材,单独存高清)
3. s2dump 页表三级输出(非恒等特写)
4. ESR 对照:0x8200000A(AF)/0x02000000(SP bank 崩)
5. make verify 连续 8 次 RESULT PASS(剪成快进)
6. GitHub 仓库 + commit 历史(AI 署名特写)

## 标签
YouTube: hypervisor, ARM64, QEMU, virtualization, stage-2 translation, ERET, HVC, gdb debugging, operating systems, from scratch
B 站: 虚拟化 hypervisor ARM 页表 gdb 调试 操作系统 底层开发 从零开始 开源
