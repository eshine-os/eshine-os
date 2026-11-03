# 站六十一 devlog:yocto 镜像聚合(终装,批次 D 第二站)

> 承接站 60(基线提交)。门禁 **176→178**。镜像机 未同步(用户令)。

## 0. 机制

- machine conf(meta-eshine/conf/machine/eshine-qemu-a57-type1.conf):
  目标机定义;ESHINE_QEMU_* 六变量=run.sh 启动参数单一事实源
- eshine-image.bb(deploy-only 终装):do_deploy 依赖 eshine-rtos:do_deploy
  →读 deploy 类 sstate 镜像面 ${DEPLOY_DIR_IMAGE}→boot/ 四件+
  manifest.json(版本+机器+sha256 工件账)+run.sh(机器定义生成)
- scripts/yocto-build.sh 扩展:bitbake eshine-image+IMAGE= 报行
- setup-yocto.sh:local.conf 生成面写目标机(站 34 的 qemuarm64
  位面占位退役)

## 1. 战雷

1. **deploy 类每配方独立 DEPLOYDIR**(deploy.bbclass:DEPLOYDIR=
  WORKDIR/deploy-PN;共享面=**sstate 输出** ${DEPLOY_DIR_IMAGE})——
  首版从 ${DEPLOYDIR} 读兄弟配方的件=空(install cannot stat)。
  修:读 ${DEPLOY_DIR_IMAGE}(sstate 镜像面)。
2. **local.conf 硬设 MACHINE 盖环境变量**(MACHINE=xxx bitbake 被
  local.conf 的 qemuarm64 硬设盖过;manifest 机器名=qemuarm64=
  铁证)。修:setup-yocto.sh 生成面写目标机+存量 build 打补丁。

## 2. 判据与验收

站 34 同型(门禁自跑 bitbake,无独立冒烟场景):手动靴证 IMAGE-
BOOT-OK(run.sh 起第二台 QEMU→vmcreate/vmrun 全通)。门禁
[177-178] 写而不跑:[177]聚合完整性(boot/ 四件+run.sh+manifest
在位+sha256 逐件核+机器名=eshine-qemu-a57-type1);[178] run.sh
启动全链 musl hello(终装入口自靴=组装非拼盘)。help 178 步;
ARCHITECTURE 同步。
