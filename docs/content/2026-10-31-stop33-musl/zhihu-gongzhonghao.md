# musl 一行没改,跑在我自己的操作系统上

(eshine-os 从零造 ARM64 Type-1 hypervisor+RTOS,站 33 记)

从站 28 到站 32,每一站给 syscall 分配号码的时候,我都选了
真 Linux 的号码:read 是 63,write 是 64,futex 是 98,kill 是
129。当时看是"多此一举"——我们自己的内核,想用几号用几号。
今天这一站把这份纪律兑现了:**musl 1.2.5,上游原版,一行
补丁不打,静态链接的 hello 直接跑在我的 eshine-rtos 上。**

## 先看判据:双 musl 世界

同一个 48KB 的静态 ELF 字节,放进 vm0 的 cpio 根文件系统,
在我的内核上跑出 "musl: hello from /bin/hello";再把同一个
文件塞进 vm2 的 busybox Linux initramfs,在真 Linux 上跑出
同样的两行。**同 libc,两个 OS,一台 hypervisor**——这是
这个项目到今天为止最像"宣言"的一张判据截图,而它现在是
门禁里两个自动跑的步骤。

对比一下参考实现(参考实现,我们只做机制参考):他们的 musl
需要 67 个文件的补丁,把 __syscallN 改道进自家的分发层,
再用 ar 手工把库目标文件并进 libc.a。我们一行没改——因为
号面本来就是 Linux 的。五站纪律,一站兑现。

## 装载:内核一个字节都不读

参考实现普查里最漂亮的一条机制裁决:EXEC_SETUP 是七参帧改写,
**内核不读任何镜像字节**——ELF 装载是用户态 loader 的活。
这一站把"用户态 loader"真的写了出来,就在 shell 里:

fork 出子进程,子进程用 fd=通道三元组(站 31 的 resmgr 机器)
打开 ramfs 里的 /bin/hello,64 字节一窗把 ELF 头和段头读出来;
每个 PT_LOAD 用带 hint 的 mmap 落到链接地址(站 29 的地址
空间机器);把段字节从文件拷进映射;再 mmap 一页栈,在栈顶
排好 argc、argv、envp 和一串 auxv(AT_PHDR/AT_PAGESZ/
AT_RANDOM…);最后 exec_setup 一发帧改写,eret 落进 musl 的
_start。内核全程零镜像字节——红线没破。

## 三颗真雷,都是 musl 上门讨的

第一颗:首冒烟直接 panic,esr=0x96、FSC=0x06、far=段基。
定谳——**内核的 uaccess 拷贝先于缺页建立**。站 29 的缺页
机器只挂在 EL0 异常向量上,而 loader 的拷贝路径是"内核把
字节写进用户 VA",那是 EL1 的写,直接打穿。修法:段页先在
用户态逐页写零预触,缺页在 EL0 消化,内核拷贝后行即安全。
这颗雷的教训值得入档:uaccess 的方向决定谁来消化缺页。

第二颗更有味道:修完第一颗,musl 已经在跑了(elr 落在镜像
内!),然后 esr 的 EC=0x07——FP 陷阱。我们站 19 做过 per-task
FP lazy 化:非 FP 任务零帧税,首条 FP 指令陷进来轮转持有权。
但那套机器只挂在 EL1 向量上;musl 的 memcpy 用 NEON,首触
在 EL0,EL0 向量一看到它只会落"终态面"panic。修法:把陷阱
体从 EL1 向量里抽出来,两个向量共用。站 19 的 FP 机器欠的
那一面,由 musl 上门讨清了。

第三颗雷是门禁抓的,味道最好。前两颗修完,vm0 上一切
全绿——但把同一个 hello 塞进 vm2 的真 Linux 一跑:段错误。
而我在宿主机上用 qemu-user 模式跑同一个文件,活着;vm0 上
也活着。两个"活着"都是假阴性:vm0 活,是因为我的用户态
loader 自己带了一份 phdr 副本当 AT_PHDR;qemu-user 活,是
因为它自己合成 phdr。真正的判据只有真内核的 auxv:我的
链接脚本把 PT_LOAD 放在文件偏移 0x10000,ELF 头不在段内;
真内核算 AT_PHDR=加载基址+64,落进段内 .text 的垃圾字节,
musl 的 init_libc 拿幻影 phdr 找 PT_TLS,当场段错误。修法
一句话:链接脚本加 PHDRS FILEHDR 子句,让 ELF 头进首段
——和 Linux 内核自己的链接脚本同一条规矩。

三颗雷的共同点:musl 不是测试用例,**它是比一切测试用例
更不知疲倦的用户**。你的机器缺哪一面,它第一个跑到哪里
去踩;而"能跑"的两个证据都可能在骗你,真内核才是甲方。

## 构建面的小账

musl 源码(1.0MB tarball)入仓,附一个从零复现的构建脚本。
坑:musl 的 configure 用 --target=aarch64 会派生出
CROSS_COMPILE=aarch64-,而 Ubuntu 工具链的真名是
aarch64-linux-gnu-——构建到最后一步 ar 找不到,改 config.mak
两行续建。最后:静态 musl hello 的文本段只有 33KB,musl 的
小体积真不是传说。

九十二门,两轮全绿。下一站按既定序列走 yocto——把今天这个
"能跑真 libc 的 RTOS"装进正经构建系统。代码 Apache-2.0,
欢迎来看。
