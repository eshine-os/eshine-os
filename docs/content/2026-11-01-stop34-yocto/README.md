# 站34 口径:yocto 构建面——零补丁层与产品可靴判据

**eshine-os** · 站 34(yocto)· 2026-11-01

## 本站做了什么

把"能跑真 libc 的 RTOS"装进正经构建系统:仓内即层
(**meta-eshine**,4 文件),一条 `bitbake eshine-rtos` 从
**git archive 提交树**构建到部署四件(eshine.elf/rtos.bin/
guest2.bin/ramfs.cpio)。判据=**部署产物真实可靴**:门禁
[93] 以第二台 QEMU 吃部署物起 vm0 跑出 musl hello——验证的
是产品,非构建系统自嗨。

- **零补丁工具链**:参考实现与 yocto 打了 ~60 枚工具链切换补丁
  的战争;我们 Makefile 是 `CROSS ?=` 环境驱动+HOSTTOOLS
  白名单四件——战争没有发生。
- **构建吃提交树**:git archive HEAD 出源 tar(参考实现机制
  镜像)——未入库的源,构建系统不认(本站被自己的纪律咬了
  一口:LICENSE 还没进历史,populate_lic QA 当场拦下)。
- **可复现装配**:setup-yocto.sh(poky scarthgap 净克隆+
  仓外 build 目录)+yocto-build.sh(bitbake sstate 快路径+
  DEPLOYDIR 问真值)。

## 门禁

93 门 ×2 双绿([93] yocto 产品可靴;append-only 顺移免)。

## 诚实边界(v0)

无 gcc-cross 内建(宿主工具链一行声明);无 runqemu 集成;
无 eshine-boot.bb 平台组装、无 uImage(后弧);tar 走层
files/(非远端校验和纪律,代价记档)。

## 站利

参考实现的 76GB yocto 树 vs 我们的:poky 净克隆+4 文件层+2
脚本。**构建系统的体量应该来自上游,不是来自补丁。**
