# 站六十二 devlog:版本 pin+SBOM 台账(批次 D 第三站)

> 承接站 61(镜像聚合提交)。门禁 **178→179**。镜像机 未同步(用户令)。

## 0. 机制

- 3rd/manifest.yaml:机器可读事实源(八组件:musl/cjson/zlib in-repo
  (工件名含钉死版本)+busybox/linux-image(GPL 仓外)+gcc/binutils/
  qemu(宿主 prebuilt 台账));站 59 LEDGER.md=人读面同表
- NOTICE 聚合对照:八组件名+许可全列;LICENSE=Apache-2.0 本体
- tools/sbom-check.py:完整性脚本(门 [179] 消费者)——五面:字段齐/
  in-repo 工件在且名含 pin 版本/GPL 恒仓外/NOTICE 列全/许可本体;
  极简 YAML 子集解析器(零依赖)

## 1. 战雷

- 无新雷(本站=台账面;版本事实全部实采:gcc 13.3.0/binutils 2.42/
  qemu 8.2.2 现场打印,非记忆)。

## 2. 判据与验收

sbom-check 五面全绿(0 退):8 组件/3 in-repo pin/5 out-repo GPL
隔离/NOTICE 聚合/许可本体。门禁 [179] 写而不跑(批验收纪律;宿主
侧脚本门)。help 179 步;ARCHITECTURE 同步。
