# SBOM 收口:八组件一台账,完整性可执行

站 62,批次 D 第三站:版本 pin+SBOM 台账。

## 两副面孔

站 59 起了 3rd/LEDGER.md——人读的表。这一站给它配机器可读的孪生:
3rd/manifest.yaml。八组件全量:musl 1.2.5、cJSON 1.7.18、zlib 1.3.1
(仓内,工件名含钉死版本)、busybox 1.36.1、linux-image 6.6(GPL,
恒仓外)、gcc 13.3.0、binutils 2.42、qemu 8.2.2(宿主 prebuilt
台账——版本全部现场实采,不是记忆)。

## 可执行的完整性

台账不是文档,是被门禁消费的代码:tools/sbom-check.py 五面校验——
字段齐(in-repo 工件必须存在且文件名含 pin 版本)/GPL 恒仓外
(许可政策的机器执法)/NOTICE 聚合列全(八组件名逐一对账)/
LICENSE 本体=Apache-2.0。全绿才退 0,门 [179] 吃的就是这个退出码。

解析器值得一提:极简 YAML 子集,零依赖——SBOM 这种"结构越简单
越可信"的账,不需要也不应该引入一个 YAML 解析器的供应链。

## NOTICE 聚合

NOTICE 从四行变成了账本:八组件名+许可逐行,与 manifest.yaml
一一对照。交付物(站 61 的镜像 manifest.json 的 sha256)加上这张
组件账,SBOM 的两层——"我交付了什么"和"我用了什么"——都齐了。

门禁 178→179。下一站:docker 弧。
