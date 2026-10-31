# 站三十三 spec:musl 用户态——零 patch 直连与用户态 loader

日期:2026-10-31 · 作者:Julian Chu + Claude Code
基线:参考实现普查 spec §10("双 musl 世界";参考实现形=eshine-musl-host
67 文件补丁+改道分发→自有编号;EXEC_SETUP 七参帧改写=内核不读
ELF,loader 在用户态)。红线:机制参考、零代码拷贝、参考实现权属
私藏(musl 上游 MIT 干净取,不取其补丁叉)。
前置:站 32 收口 b801481(90 门双绿;真 Linux 号面=本站地基)。

## 0. 目标

**真 musl(1.2.5,零 patch)静态 hello 在 vm0 eshine-rtos 上
跑起来**——站 28-32 的真 asm-generic 号面兑现日。判据="双 musl
世界":同一 48KB 静态 ELF 字节,vm0(经我们内核)与 vm2 busybox
Linux(标准 Yocto 式客)两边都跑出同一输出——同一 libc,两个
世界,一台 hypervisor。参考实现的翻译补丁层(67 文件)不抄:我们
的号面就是 Linux 号,零 patch 直连(普查裁决行)。

## 1. 机制映射(参考实现 → 新仓站内尺度)

| 参考实现 | 站 33 新仓形态 |
|---|---|
| eshine-musl-host=67 文件补丁+__syscallN 改道+ar 并库 | **零 patch**:真 musl-1.2.5 上游(MIT)交叉构建 libc.a;号面=站 28-32 真 asm-generic;后置薄面(见 §2)补 musl 启动期实测需求 |
| loader 在用户态(内核不读 ELF) | 壳=loader:fork→子读 ELF(fd=通道三元组经 ramfs 服务器)→hint-mmap 段+拷贝→栈块(argc/argv/envp/auxv)→exec_setup 七参帧改写。**内核零镜像字节红线保全** |
| rootfs=cpio | hello 入 tools/ramfs/bin(hello)→gen_ramfs 走子目录→挂名 /bin/hello |
| ELF 链接 | 自带链接脚本:窗口 0x40C00000(=AS_MMAP_BASE)单 PT_LOAD RWE(FileSiz/memsz);入口=段基 |
| 客 Linux=标准 | vm2 busybox initramfs 塞同一 hello(build-initramfs.sh cp)→双世界右翼 |

## 2. 内核薄面(musl 启动期实测需求;全真 Linux 号)

- **writev(66)**:musl stdio __stdout_write 走 writev(iov 链)——
  printf 的真通路;uaccess iovec 逐项,fd 1/2 走原始行打印面。
- **exit_group(94)**:musl exit 的真退场号→与 exit(93) 同路。
- **set_tid_address(96)**:musl __init_tp 写线程号位→返 pid
  (tid 语义=站 33+ threads 弧,v0 地址存档不落)。
- **ioctl(29)**:isatty 探测→恒 -ENOTTY(25)=非 tty 判据→
  musl 全缓冲+exit 冲刷,输出确定性。
- **rt_sigprocmask(135)**:SIG_BLOCK/UNBLOCK/SETMASK 三 how 落
  位图(站 32 的 sig_blocked 兑现面);oldset 拷出。
- **lseek(62)**:loader 的段定位(SEEK_SET 绝对位;ramfs wire
  加 SEEK op=6 直写 ocb->off;whence 其余 v0 拒)。
- **fd READ 窗 56→64**:status 走 reply 面,rbuf 可满 64B(站 31
  留的 8B 白舍);loader 往返数 -12.5%。
- **mmap hint 尊重**:x0 非 0 且窗内页对齐不撞→落 hint(loader
  的段定位契约);否则原游标分配。

## 3. 用户态 loader(壳;参考实现 "loader 在用户态"机制)

`run <path>`:fork→子(MAGIC 支)→fdopen+fdread 64B 窗读
Ehdr(64B)+Phdr(56B/phdr)→逐 PT_LOAD:fdseek(p_offset)+读
filesz 逐 64B→hint-mmap(vaddr,pagesz(memsz))→拷→段尾 memsz-
filesz 由 mmap 零填→fdseek 0 读 phdr 拷贝留栈上(AT_PHDR 用)
→mmap 栈(0x40D00000,128KB)→栈顶向下排块:argc=1/argv[0]/
envp NULL/auxv{AT_PHDR=栈上 phdr 副本,AT_PHENT=56,AT_PHNUM,
AT_PAGESZ=4096,AT_ENTRY,AT_RANDOM→16B 栈上,AT_SECURE=0,
AT_NULL}/argv 串→exec_setup(entry,sp,0,0,0)→父 wait4 收
code。musl crt1 读栈块→__libc_start_main→init_libc(auxv 消费)
→__init_tp(builtin_tls 后备=零 PT_TLS 合态)+set_tid_address
→main→printf(writev)→exit_group。

## 4. 判据面(门禁只增不减:90→92)

- **[88] musldemo(run /bin/hello)**:vm0 全链——fork/用户态
  loader/fd=通道三元组/hint-mmap/exec_setup/musl 启动/
  writev/exit_group/wait4。锚:"musl: hello from /bin/hello"
  +"musl: fmt proof 6*7=42"(pid 行打印不入锚=抗位漂)。
- **[77] vm2 hello**:插入 [76] cpuinfo 后——vmconsole@vm2 的
  busybox sh 跑 `/bin/hello`,同一 ELF 字节于真 Linux 出同
  输出="双 musl 世界"右翼。旧 [77-90] 顺移。
- 三处同步 92 步;ARCHITECTURE 双世界节;help 补 run。

## 5. 构建面(从零可复现)

- 3rd/musl-1.2.5.tar.gz 入仓(MIT,上游;1.0MB)。
- tools/build-musl.sh:抽源→configure --target=aarch64(AR/
  RANLIB 前缀坑:musl 派 CROSS_COMPILE=aarch64- 而工具链真名
  aarch64-linux-gnu-,configure 后改 config.mak 续建)→
  libc.a→hello.c(自带链接脚本 0x40C00000)→strip 前 ELF 落
  tools/ramfs/bin/hello(**产物提交=站 15 dtb 先例**:dtc 缺席
  时代随仓走;重编=跑配方)。
- Makefile:ramfs cpio 依赖 wildcard→find(子目录 bin/)。
- vm2:build-initramfs.sh 组装处 cp hello→initramfs 重打包
  (8MB 定长槽不变;hello ≤48KB 窗余量足)。

## 6. 边界与不做(v0 诚实)

无动态链接(ld-musl.so 面=后议;静态自足);无 stdio 按行
缓冲(ENOTTY→全缓冲,exit 冲刷);无 unistd 开发面大集(实测
需求驱动,缺口 ENOSYS 诚实露出);loader 不查段重叠/权限位
(单段 RWE 自带态);auxv 无 AT_SYSINFO_EHDR(vdso=后议);
hello 的 pid 打印入输出不入锚;vm2 侧 pid 不定(锚 pid 无关)。

## 7. DoD

spec→实现→92 门×2 双绿→devlog→内容包 5→commit→镜像机→记忆。
