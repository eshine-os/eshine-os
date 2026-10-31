# musl 一行没改,跑在我自己的 OS 上 🎉

造 hypervisor 第 33 站:真 musl 落地,92 门 ×2 双绿✅

五站号面纪律的兑现日:
站 28 起每个 syscall 都用真 Linux 号
(read 63/write 64/futex 98/kill 129…)
今天:musl 1.2.5 上游原版,**零 patch**
静态 hello 直接跑在 eshine-rtos 上🔥

判据=双 musl 世界:
同一 48KB ELF 字节
vm0(我们内核+用户态 loader)✓
vm2(真 Linux busybox)✓
同 libc,两个 OS,一台 hypervisor🏠

装载全程用户态,内核零镜像字节:
fork→fd=通道三元组读 ramfs(站31 机器成真工具!)
→hint-mmap 段(站29)→栈块 auxv→exec_setup 帧改写

三颗真雷,musl 上门讨的:
①EL1 uaccess 先于映射打穿(缺页机器只挂 EL0 面)
→修=段页用户态预触📖
②EL0 FP 陷阱落终态(musl memcpy 用 NEON,
站19 FP lazy 机器只挂 EL1 向量)
→修=陷阱体抽出两向量共用

③AT_PHDR 幻影 phdr(链接脚本头不在段内,
真内核算 AT_PHDR 落垃圾字节=SEGV)
vm0 活/qemu-user 活≠证据——
真内核的 auxv 才是甲方📝

musl 不是测试用例——
它比一切测试用例更不知疲倦的用户💪
你的机器缺哪面,它第一个跑到哪踩

冷知识:静态 musl hello 文本段只有 33KB
musl 小体积真不是传说💡

#eshineOS #hypervisor #musl #操作系统 #从零造轮子 #开源
