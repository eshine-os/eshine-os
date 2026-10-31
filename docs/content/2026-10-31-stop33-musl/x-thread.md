# 站33 · X 8 连推

## 推 1(总览)
musl 1.2.5 上游原版,零 patch,静态 hello 跑在 eshine-rtos 上。
这是站 28-32 真 asm-generic 号面纪律的兑现日——当年选真 Linux
号看似多余,今天 musl 一行不改直连。92 门 ×2。

## 推 2(双 musl 世界)
判据=同一 48KB ELF 字节跑两个 OS:vm0 经我们内核+用户态
loader,vm2 经真 Linux busybox。同 libc,两个世界,一台
hypervisor——项目的宣言级截图,现在是门禁自动跑的两个步骤。

## 推 3(对比参考实现)
参考实现(仅机制参考)的 musl=67 文件补丁+__syscallN 改道+
ar 手工并库。我们零 patch——因为号面本来就是 Linux 的。
五站纪律,一站兑现。

## 推 4(用户态 loader)
内核零镜像字节红线保全:fork→子进程用 fd=通道三元组读
ramfs→hint-mmap 段→64B 窗拷贝→栈块排 argc/argv/envp/auxv
(AT_PHDR/PHENT/PHNUM/PAGESZ/ENTRY/RANDOM/SECURE)→
exec_setup 帧改写。ELF 装载是用户态的活,内核只给原语。

## 推 5(雷一:EL1 uaccess)
首冒烟 panic:esr=0x96 FSC=0x06 far=段基。定谳:内核拷贝
(fdread 写用户 VA)先于缺页建立——站 29 缺页机器只挂 EL0
面。修=段页用户态预触。教训:uaccess 方向决定谁消化缺页。

## 推 6(雷二:EL0 FP 陷阱)
修完雷一 musl 已在跑(elr 在镜像内),然后 EC=0x07 FP 陷阱:
站 19 per-task FP lazy 只挂 EL1 向量,musl memcpy 的 NEON
首触在 EL0→终态面 panic。修=陷阱体抽出两向量共用。站 19
的债,musl 上门讨清。

## 推 7(雷三:AT_PHDR)
vm2 门实抓段错误,而 vm0 活(qemu-user 活)≠证据:首版链接
脚本头不在段内,真内核 AT_PHDR=base+e_phoff 落垃圾字节,
musl 按幻影 phdr 找 PT_TLS=SEGV。修=PHDRS FILEHDR(头入段,
vmlinux 同律)。真内核 auxv 才是甲方。

## 推 8(musl 事实)
静态 hello 文本段 33KB(musl 小体积名不虚传);零 PT_TLS
合态(builtin_tls 后备);writev 是 printf 的真通路;
exit_group 是 exit 的真号;ioctl ENOTTY=全缓冲+exit 冲刷
的确定性。冷门号一个不落,全是实测驱动。

## 推 9(下一站)
musl 不是测试用例——它比一切测试用例更不知疲倦的用户。
下一站按序列走 yocto:把"能跑真 libc 的 RTOS"装进正经
构建系统。Apache-2.0,结对从零造,结对中国造。
