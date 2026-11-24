# 站三十四 devlog:yocto 构建面——零补丁层与产品可靴判据

日期:2026-11-01 · 作者:Julian Chu + Claude Code
状态:**已收口**——93 门(新增 [93] yocto 产品可靴)连续 2 轮全绿

## 0. 本站做了什么

把"能跑真 libc 的 RTOS"装进正经构建系统:**meta-eshine 层**入仓
(层=4 文件),一条 `bitbake eshine-rtos` 从 git archive 提交树
构建到部署四件(eshine.elf/rtos.bin/guest2.bin/ramfs.cpio)。
判据=**部署产物真实可靴**:[93] 门以第二台 QEMU 吃部署物起 vm0
跑出 musl run——**验证的是产品,非构建系统自嗨**(parse-only
不算数)。

## 1. 机制落点

- **源归档**=git archive HEAD 出 tar(scripts/archive-source.sh)
  ——参考实现 "git archive 单仓进 WORKDIR"机制镜像;构建吃**提交树
  非工作树**=可复现纪律。tar 落层 files/ 供 bitbake file:// 搜索
  面(gitignore 工件,v0 代价记档)。
- **零补丁工具链**:参考实现与 yocto 打了 **~60 枚工具链切换补丁**
  的战争;我们的 Makefile 是 `CROSS ?=` 环境驱动,配方一行
  `EXTRA_OEMAKE` 注入+layer.conf HOSTTOOLS 白名单四件
  (gcc/ld/objcopy/ar)——战争没有发生。INHIBIT_DEFAULT_DEPS
  不拉 gcc-cross(内建工具链=后弧,v0 政策记档)。
- **环境装配可复现**:scripts/setup-yocto.sh(poky scarthgap
  浅克隆,直连不通退 代理 代理=busybox 取源同法;仓外 build
  目录生成 bblayers/local conf=eshine-linux 同位的机器外部工件
  纪律);scripts/yocto-build.sh=判据脚手架(bitbake sstate 快
  路径+`bitbake -e` 问 DEPLOYDIR 真值——TMPDIR 随 TCLIBC 带
  后缀,不猜路径)。
- **部署配方**:inherit deploy+nopackages,do_compile=make,
  do_deploy 四件落 DEPLOYDIR;MACHINE=qemuarm64 位面占位。
- **仓根 LICENSE**(Apache-2.0 全文)入史——LIC_FILES_CHKSUM
  的前置,亦是权属卫生(license 自定调日起就在册,文本今天
  落地)。

## 2. 集成皱褶(构建器自曝,两枚皆低烈度)

- **LICENSE 不在 HEAD**:git archive 纪律反咬——构建吃提交树,
  而 LICENSE 是本站新文件还没进历史;populate_lic QA 指着 tar 内
  不存在的文件炸。修=LICENSE 先立独立小提交再归档
  (09febba)。这正是"构建吃提交树"纪律的**自我证明**:构建
  系统拒绝构建未入库的源。
- **tar 前缀+subdir 双层嵌套**:SRC_URI 的 `subdir=${S}` 叠加
  tar 自带 `--prefix` 出双层目录,LICENSE 落一层深 QA 又炸。
  修=去 subdir,tarball 自前缀即 S。教训:**tar 自带前缀与
  bitbake subdir 二选一,勿叠**。
- 两枚皱褶皆 bitbake QA 面如实拦下——构建系统的 QA 诚意
  (license 校验和/目录结构)本站白嫖了两道。

## 3. 判据面(门 92→93)

- **[93/93] yocto**:scripts/yocto-build.sh(bitbake+DEPLOYDIR
  问真值)→第二台 QEMU 以部署物(eshine.elf/rtos.bin/
  guest2.bin)起宿主→vmcreate/vmrun/vmconsole→`run
  /bin/hello` 出 "musl: hello from /bin/hello"+收尸 0
  ——站 33 的双世界左翼在**构建系统产品**上复验。
- append-only 顺移免:旧终门 [92]→[92/93],新门踞 [93/93]。

## 4. 边界与不做(v0 诚实)

无 gcc-cross 内建(宿主工具链 v0 政策);无 runqemu 集成
(裸工件由脚本驱动 QEMU,机器面外置);无 eshine-boot.bb
平台/型别组装、无 uImage 包装、无镜像生成(后弧);tar 走
层 files/ 非 SRC_URI 远端校验和纪律(层内工件不入 git);
bitbake 首建外置(约 2-3 分钟+下载),sstate 后秒级。

## 5. 站利

- 参考实现的 76GB yocto 树 vs 我们的:poky 净克隆+4 文件层+2
  脚本。**构建系统的体量应该来自上游,不是来自补丁。**
- "构建吃提交树"纪律自此有机器背书:未入库的源=构建系统
  不认。
- [93] 让每一轮门禁都证明一次"yocto 产品能跑 musl"——
  构建面从此不是文档,是判据。
