# 最硬的权限门,是页表项本身

站 54:给站 47 建的跨 VM 共享窗补上"谁能用"的答案。

## 问题

safeshm 窗建好之后,vm0 和 vm1 都能访问——这是"配置即授权"。
审计报告(reference-audit §C7-A3)的要求是 fail-closed:任何跨域操作
出现之前,先有权限门。

## 方案:门就是墙

共享窗在 stage-2 里就是一个 2MB 块描述符(l2_ram[9])。授权=表项
在;撤销=表项清零。撤销之后,guest 对窗的任何访问都是翻译故障,
走既有的默认拒绝路径(打印 ESR/FAR,VM 隔离死)。权限不再是一个
新的检查点,而是映射本身的存废——这是"fail-closed"最物理的实现。

## 改表三纪律

运行中的 VM 改 stage-2,最怕的是别的核上还有旧翻译缓存。三条:
dsb sy(新表项写落地)→ TLBI IPAS2E1IS(IPA 带 VMID,内共享域广播,
所有核的该 VMID 该 IPA 翻译一并作废)→ dsb sy + isb(收口)。
这套纪律是将来 demand paging、内存气球、脏页跟踪的入场券——
今天为一个权限门立规矩,明天所有动态内存机制都踩着它进来。

## 验收

xvmacl 1 off → vm1 访问窗 → fault 隔离死;vmls 显示 DEAD;
xvmacl 1 on → vmcreate/vmrun → vm1 带着窗复活。授权还跨槽位
复用持久:DEAD 之后重建,新旗生效。门禁 154→157,冒烟 21/21。
