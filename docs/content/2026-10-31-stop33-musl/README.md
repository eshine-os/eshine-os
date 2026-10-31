# 站33 口径:musl 零 patch 直连与双 musl 世界

**eshine-os** · 站 33(musl 用户态)· 2026-10-31

## 本站做了什么

**真 musl(1.2.5,上游 MIT,零 patch)静态 hello 在 vm0
eshine-rtos 上跑起来**——站 28-32 五站真 asm-generic 号面的
兑现日。判据=**双 musl 世界**:同一 48KB 静态 ELF 字节,vm0
(经我们内核+用户态 loader)与 vm2 busybox Linux 两边跑出同
一输出——同 libc,两个 OS,一台 hypervisor。

- **用户态 loader**:内核零镜像字节红线保全——fork→子用
  fd=通道三元组读 ramfs→hint-mmap 段→栈块 auxv→exec_setup
  七参帧改写。ELF 装载全程在用户态,内核只提供原语。
- **内核薄面**(musl 启动期实测):writev(printf 真通路)/
  exit_group/set_tid_address/ioctl(恒 ENOTTY=确定性缓冲)/
  rt_sigprocmask/lseek;fd READ 窗 56→64;mmap hint 尊重。
- **musl 事实**:静态 hello 33KB 文本(小体积名不虚传);零
  PT_TLS 合态(builtin_tls 后备);writev 是 printf 的真路径。
- **构建面**:3rd/musl-1.2.5.tar.gz 入仓+tools/build-musl.sh
  从零配方(AR 前缀坑记档)+hello ELF 产物入仓(dtb 先例)。

## 三颗真雷(冒烟+门禁连抓)

1. **EL1 uaccess 先于映射打穿**:loader 的内核拷贝(fdread
   写用户 VA)先于缺页建立——站 29 缺页机器只挂 EL0 面。
   修=段页用户态预触。教训:uaccess 方向决定谁消化缺页。
2. **EL0 FP 陷阱落终态面**:musl memcpy 的 NEON 首触在 EL0,
   per-task FP lazy 机器只挂 EL1 向量。修=陷阱体抽出共用。
   站 19 的 FP 机器差一面的债,由 musl 上门讨清。
3. **AT_PHDR 幻影 phdr(vm2 门实抓)**:首版链接脚本让 ELF
   头不在 PT_LOAD 内,真内核算 AT_PHDR 落进段内垃圾字节,
   musl 按幻影 phdr 找 PT_TLS=SEGV。vm0 活(loader 自带 phdr
   副本)、qemu-user 活(自合成 phdr)——**两个活的都不是
   证据,真内核的 auxv 才是甲方**。修=PHDRS FILEHDR(头入段,
   vmlinux 同律)。

musl 不是测试用例——**是比一切测试用例更不知疲倦的用户**。

## 门禁

92 门 ×2 双绿([77] vm2 右翼/[88] vm0 左翼)。
