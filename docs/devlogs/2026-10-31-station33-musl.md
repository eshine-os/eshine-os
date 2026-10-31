# 站三十三 devlog:musl 用户态——零 patch 直连与双 musl 世界

日期:2026-10-31 · 作者:Julian Chu + Claude Code
状态:**已收口**——92 门(新增 [77] vm2 右翼/[88] vm0 左翼)
连续 2 轮全绿

## 0. 本站做了什么

**真 musl(1.2.5,上游 MIT,零 patch)静态 hello 在 vm0
eshine-rtos 上跑起来**——站 28-32 真 asm-generic 号面的兑现日
(参考实现的 67 文件翻译补丁层不抄:我们的号面就是 Linux 号)。
判据=**双 musl 世界**:同一 48KB 静态 ELF 字节,vm0(经我们
内核+用户态 loader)与 vm2 busybox Linux(标准客)两边跑出
同一输出——同 libc,两个 OS,一台 hypervisor。

## 1. 机器落点

- **内核薄面**(musl 启动期实测需求,全真 Linux 号):writev
  (66,printf 真通路=stdio iov 链)/exit_group(94,与 exit 同
  路)/set_tid_address(96,返 pid)/ioctl(29,恒 -ENOTTY=
  musl 判非 tty→全缓冲+exit 冲刷,输出确定性)/
  rt_sigprocmask(135,站 32 位图掩码兑现面)/lseek(62,SEEK_SET
  绝对位,fd wire 加 SEEK op)。fd READ 窗 56→64(status 走
  reply 面,rbuf 满格——站 31 留的 8B 白舍)。
- **mmap hint 尊重**:窗内+页对齐+不撞既有 VMA 落 hint(loader
  段定位契约),游标恒越段尾防回头;musl mmap(NULL) 与老面
  零感。
- **用户态 loader**(参考实现 "loader 在用户态"机制,红线保全):
  `run <path>`=fork→子:fdopen/fdread 64B 窗读 Ehdr+Phdrs→
  逐 PT_LOAD:hint-mmap(p_vaddr)+**EL0 预触缺页**+seek+拷
  (段尾 memsz-filesz=mmap 零填)→mmap 栈(0x40D00000,128KB)
  →栈顶向下排块(argc/argv/envp/auxv:AT_PHDR=驻 .user 的
  phdr 副本、AT_PHENT/PHNUM/PAGESZ/ENTRY/RANDOM/SECURE/
  NULL)→exec_setup 七参帧改写→父 wait4 收 code。**内核零
  镜像字节 by construction**。
- **musl 自证面**:静态 hello 33KB 文本(musl 小体积名不虚传);
  零 PT_TLS 合态(builtin_tls 后备);init_libc 消费 auxv;
  __init_tp(set_tid_address)+printf(writev)+exit_group。
- **构建面**:3rd/musl-1.2.5.tar.gz 入仓(MIT 上游,1.0MB);
  tools/build-musl.sh 从零配方(坑记档:configure --target
  派生 CROSS_COMPILE=aarch64- 而工具链真名 aarch64-linux-gnu-,
  AR/RANLIB 找不到须改 config.mak 续建;musl-gcc specs 无
  -lc,须显式);链接脚本=窗口 0x40C00000 单 RWE PT_LOAD;
  hello ELF 产物入仓=站 15 dtb 先例。vm2 右翼=
  build-initramfs.sh cp 同一 hello(48KB vs 8MB 槽,余量足)。

## 2. 雷与定谳(三颗真雷+一枚自家嵌合体复发,冒烟/门禁连抓)

- **雷一:EL1 uaccess 先于映射打穿**。首冒烟 panic:
  esr=0x96 FSC=0x06 L2 翻译错+elr=内核址+far=段基=
  **内核 uaccess(fdread 拷贝落段)先于缺页建立**。站 29
  缺页机器只挂 EL0 面(rt_pfault 在 guest_el0_sync);loader
  的拷贝路径是"内核把字节写进用户 VA"=EL1 写。修=**段页
  用户态预触**:mmap 后逐页写零在 EL0 触缺页(机器在位的面
  消化),内核拷贝后行即安全。教训:**uaccess 方向决定谁消化
  缺页;内核写用户 VA 的路径,必须先让用户态把页立起来。**
- **雷二:EL0 FP 陷阱落终态面**。二冒烟 panic:esr EC 0x07
  (CPACR FPEN 陷阱)+elr=**镜像内**(musl 已在跑!)。
  per-task FP lazy 机器(站 19)只挂 EL1 guest_sync 向量;
  musl 的 NEON(memcpy)首触在 EL0→rt_el0_fault 只认
  FSC→终态面。修=**陷阱体抽出共用**:rt_fp_trap_cur 自
  rt_sync_fault 抽出(EL1/EL0 两向量同律)。musl 的第一只
  NEON 指令即此雷的探针——站 19 的 FP 机器差一面的债,由
  musl 上门讨清。
- **雷三:AT_PHDR 幻影 phdr(vm2 右翼门实抓)**。修完两雷
  vm0 全绿,门禁却在 vm2 真内核上实抓 `/bin/hello` 段错误
  ——而 qemu-user 下同一字节**活着**(假阴性!)。定谳:
  首版链接脚本让 PT_LOAD 落文件偏移 0x10000,**ELF 头不在
  段内**;真内核算 AT_PHDR=base+e_phoff=0x40C00040 落进段内
  .text 的垃圾字节,musl init_libc 按幻影 phdr 找 PT_TLS=
  SEGV。vm0 不炸是因我们 loader 自带 phdr 副本(AT_PHDR 指向
  .user 静态区),qemu-user 不炸是因它自合成 phdr——**两个
  活的都不是证据,只有真内核的 AT_PHDR 是判据**。修=链接
  脚本加 PHDRS FILEHDR PHDRS 子句(ELF 头入首段,vmlinux
  同律)+基址上提一页(段首 0x40C00000=头页,须≥窗基)。
  教训:**自定义链接脚本必须过"头在段内"检;真内核 auxv
  才是 AT_PHDR 的甲方。**
- **自家嵌合体复发**:走查表编辑用 sed 式拼接拆破了
  console_puts 串(shell.c 编译炸 2 错)——站 24/27 两犯的
  **help 嵌合体同案第三犯**。纪律原句:"编辑用整块重写或
  assert 替换+肉眼过 help",本轮 sed 式替换再违反;已整块
  重写修复,并加验成品串(strings 过 [88]/[77] 行)。
- **闪断档(终判:宿主争抢型残余,站 25 族,未复现于受控窗)**:
  verify 早期八跑五败,死点散布在负载敏感窗([21]/[22] ps/spawn
  ×3+[62] Linux SMP×1+[21] 静窗×1),签名一致:console 静默+
  QEMU 活(TimeoutError 非 ConnectionError)+gdb accept 超时=
  QEMU 主线程饿(chardev 投递停摆);失败时窗宿主负载 2-3,
  三人共用机的外部负载。**收口取证战**:自建复现器 13 连跑
  (补齐 verify 全前序+Linux Image loader+T+90 垫)全活,挂死
  无法在受控环境复现;最终门禁 w 轮+监视轮**两连 RESULT PASS**
  (门禁判据面达成)。行动纪律:**verify 期间不并发跑重构建+
  安静窗跑门禁**;残余与站 25 第③案("QEMU chardev 投递滞后,
  失败在上游")同档;若成常态拦路,再议 ps 门装 ev9 式推入
  见证位取证。取证自雷记档:监视器首版 pgrep 误长名(须 -f)
  白丢一次现场。

## 2b. 站利补记

三颗雷共同点:musl 不是测试用例,**是比一切测试用例更不知
疲倦的用户**;且两颗(缺页/FP)都是"新用户揭老机器缺面"型
——机器没坏,只是面没挂全。

## 3. 判据面(门 90→92)

- **[77] vm2 右翼**:busybox sh 跑 `/bin/hello`,真 Linux 上
  同字节出同输出。
- **[88] vm0 左翼**:`run /bin/hello` 全链(锚:musl: hello
  from /bin/hello / musl: fmt proof 6*7=42 / run: exited
  code=0;pid 行打印不入锚=抗位漂)。
- 旧门顺移:vm2 段 [77-86]→[78-87],热键段 [87-90]→[89-92]。

## 4. 边界与不做(v0 诚实)

无动态链接(ld-musl.so=后议);无 stdio 行缓冲(ENOTTY→全
缓冲);EL1 缺页接管不做(uaccess 先立页的界);exec 不撤旧像
(段头驻 .user=AT_PHDR 依托,记档);RT_AS_VMA_MAX=6 恰容
fork 继承 2+loader 段 1+栈 1+musl mallocng 2,更大 musl 程序
须扩;auxv 无 vdso(AT_SYSINFO_EHDR);run 失败路径打印+exit 1。

## 5. 站利

- 站 28-32 五站的号面纪律(read/write 63/64 起全真 Linux 号)
  在本日兑现:musl **零 patch** 直连——参考实现需 67 文件补丁+
  改道分发层,我们一行不改。
- 用户态 loader 让 fd=通道三元组(站 31)成了真工具:ramfs
  服务器在为 musl 装载服务。三段消息的"让服务器服务世界"
  落到 loader 脸上。
- 双 musl 世界=这个项目的一句话宣言,从此有机器判据背书。
