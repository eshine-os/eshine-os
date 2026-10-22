# 小红书:给 hypervisor 装 VM 管理器的一晚

## 正文(文案)

一晚给自研 Type-1 hypervisor 加了 VM 管理器,双 VM 同跑👇

现在我的 hypervisor 里,VM 是被"管"起来的:
📋 vmls 看两槽状态
🏗 vmcreate 一键拉起
▶️ vmrun 进 guest 跑
🗑 vmkill 未跑先销毁

最得意的设计:两个 VM 的物理内存窗口完全不重叠——
同一个虚拟地址 0x40000000,VM0 落在物理 0x50000000,VM1 落在 0x51000000。
VM0 就算地址写穿也摸不到 VM1 的镜像🛡
隔离不靠自觉,靠页表✅

一晚还抓了三个真 bug,全都 gdb 实锤:
1️⃣ 20KB 结构体放 16KB 栈上 → 栈溢出砸了页表
2️⃣ 页表没 4KB 对齐 → 描述符掩码把指针砍回自己
3️⃣ ERET 后 callee-saved 全是 guest 的 → 宿主打印出异常号

每天 1-2 小时,和 AI 结对从零写,代码 Apache-2.0 全开源💚
已坚持到 27.04 第二站,门禁 14 步连过 4 次

## 配图清单(发帖前截图)

1. 封面:vmls 双 VM 状态输出(深色终端,对比图更好:[0] RUNNING [1] INIT)
2. vmrun 0 → vm0 banner → vmrun 1 → vm1 banner(两身份同源镜像)
3. vm1: CREATED (ram 0000000051000000) 特写(同入口不同物理)
4. make verify 14/14 RESULT PASS
5. gdb 现场:pgd[0] 指向 pgd 自己的 dump(坑二证据,极客感拉满)

## 标签

#hypervisor #虚拟化 #嵌入式 #ARM #操作系统 #开源项目 #程序员 #AI结对编程 #系统编程 #从零写操作系统
