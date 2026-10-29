<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 1/10 闪断追了四个站,真凶是一行代码

从零写 hypervisor+RTOS 的第 22 站:
- 悬案:门禁每十轮闪断一次,等不到 "spinning...",客机健康
- 追了四个站,两个观察哨都没复现
- 这次第一步重读全量错误 dump,当场破案

真凶长这样:
  spinni[vm2] [   n 0.1339g17]
两个 VM 的输出互相嵌进对方字符串中间——vm2 的数字里嵌着
vm1 的 n 和 g。判据找连续字节,当然找不到。

根因一行:console_puts 持锁,console_putc 不持锁——
vuart 前台逐字符透传是全树唯一漏网的裸写者。

修完后自己的门禁又抓住我:
  [vm0] rtos> c[vm0] pu1: online
我新加的停泊冲刷,把同 VM 次核的在途行切断了。
修并发 bug,新路径共享旧状态=下一个雷 ⚠️

失败测试先行:60 行风暴 4+ 撕 → 修后输出行 0/60 碎
门禁 75→76,连续两轮全绿 ✅ 观察哨闭案
Apache-2.0,每天 1-2 小时,从零打造。
#从零打造 #RTOS #hypervisor #并发调试 #ARM64
