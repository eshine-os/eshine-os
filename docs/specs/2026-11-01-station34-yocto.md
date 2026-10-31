# 站三十四 spec:yocto 构建面——meta-eshine 层与部署产物可靴判据

日期:2026-11-01 · 作者:Julian Chu + Claude Code
基线:参考实现普查 §10 构建面(eshine-rtos.bb do_compile=git archive
单仓子目录进 WORKDIR/rtos+**~60 枚工具链切换补丁**+
其 QEMU 构建脚本 按序;deploy=init.cpio+kernel.dtb+
rtos-uImage;eshine-boot.bb 按平台/型别组装)。红线:机制参考、
零代码拷贝、参考实现权属私藏(poky/oe-core=上游开源,净克隆)。
前置:站 33 收口 0ca7ee8(92 门双绿,musl 双世界在役)。

## 0. 目标

把"能跑真 libc 的 RTOS"装进正经构建系统:**meta-eshine 层**
入仓(仓内即层),一条 `bitbake eshine-rtos` 从源归档构建到
部署三镜像,判据=**部署产物真实可靴**(yocto 构建的
rtos.bin 起动机器并跑出 musl hello)——parse-only 不算数
(判据先于结论)。参考实现与工具链的 60 补丁战争不重演:我们
的 Makefile 是 `CROSS ?=` 环境驱动,配方一行声明宿主交叉
工具链(v0 政策,记档;gcc-cross 内建=后弧)。

## 1. 机制映射(参考实现 → 新仓站内尺度)

| 参考实现(§10) | 站 34 新仓形态 |
|---|---|
| do_compile=git archive 单仓子目录进 WORKDIR | **git archive 单仓整树**进 WORKDIR(scripts/archive-source.sh 出 tar;构建吃提交树非工作树=可复现纪律) |
| ~60 枚工具链切换补丁 | **零补丁**:CROSS ?= 环境注入;v0=宿主 aarch64-linux-gnu- 一行声明(INHIBIT_DEFAULT_DEPS 不拉 gcc-cross;内建工具链=后弧) |
| 其 QEMU 构建脚本 按序 | 单条 `bitbake eshine-rtos` |
| deploy=init.cpio+kernel.dtb+rtos-uImage | deploy=eshine.elf+rtos.bin+guest2.bin+ramfs.cpio(裸工件;uImage/boot.bb 平台组装=后弧) |
| yocto 下载/缓存树 76GB | poky 净克隆(scarthgap LTS,shallow)+仓外 build 目录(eshine-linux 同位的机器外部工件纪律) |

## 2. 结构

- **meta-eshine/**(仓内即层):conf/layer.conf(HOSTTOOLS
  补宿主交叉四件:gcc/ld/objcopy/ar——bitbake 白名单制);
  recipes-eshine/eshine-rtos/eshine-rtos.bb:LICENSE
  Apache-2.0(仓根 LICENSE 文本本站补齐),PV=27.04,
  SRC_URI=file://eshine-os-${PV}.tar.gz(archive-source.sh
  生成进层 files/,gitignore 工件),
  INHIBIT_DEFAULT_DEPS="1",do_compile=make CROSS=…,
  inherit deploy,do_deploy 三镜像+归档落 DEPLOYDIR。
- **scripts/setup-yocto.sh**:poky 路径检测+bblayers.conf/
  local.conf 生成(MACHINE=qemuarm64 位面即占位)+调
  archive-source.sh 出源 tar。
- **scripts/archive-source.sh**:git archive HEAD(参考实现机制
  镜像;pax 提交头随档)。
- **仓根 LICENSE**(Apache-2.0 全文)——LIC_FILES_CHKSUM 的
  前置,亦是权属卫生(记忆:license Apache-2.0)。

## 3. 判据面(门禁只增不减:92→93)

- **[93] yocto**:`bitbake eshime-rtos`(sstate 快路径,首建
  外置)+**部署产物真实靴**:QEMU 以 tmp/deploy 里的
  eshine.elf+rtos.bin 起动(vmcreate/vmrun 全链)→
  `run /bin/hello` 出 musl 双世界左翼输出→收尸码 0。锚=
  yocto-built rtos.bin 跑出 "musl: hello from /bin/hello"。
- 判据哲学:验证的是**产品**(部署物),不是构建系统自嗨;
  bitbake parse/build 的健康由 [93] 的完成隐含。

## 4. 边界与不做(v0 诚实)

无 gcc-cross 内建(宿主工具链 v0 政策,一行声明非补丁战);
无 runqemu 集成(裸工件 QEMU 由脚本驱动,机器面外置=
eshine-linux 同律);无 eshine-boot.bb 平台/型别组装、无
uImage 包装、无镜像生成(后弧);无 sstate 远端发布;
tarball 走层 files/(bitbake file:// 搜索面,非 SRC_URI
远端校验和纪律——代价记档:层内工件不入 git)。

## 5. DoD

spec→实现→93 门×2 双绿→devlog→内容包 5→commit→镜像机→记忆;
验证绿即直开下一站(用户令 10-31:站间不等输入)。
