<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 3rd 台账(第三方组件清单)

> 站五十九起步(批次 C 第三站)。纪律:每入一组件先记台账(名/版/许可/
> 形态/源/用途),append-only;许可政策=本仓 Apache-2.0,**GPL 仓外隔离**
> (源与产物皆不入仓);台账=站 62 SBOM 的前置事实源(§62 前置)。

| 组件 | 版本 | 许可 | 形态 | 源 | 用途 |
|------|------|------|------|-----|------|
| musl | 1.2.5 | MIT | tarball 入仓(3rd/musl-1.2.5.tar.gz) | Debian 重打包(上游 musl.libc.org) | EL0 libc(vm0 ramfs+vm2 initramfs 双 musl 世界,tools/build-musl.sh) |
| busybox | 1.36.1 | GPL-2.0 | **仓外**(legacy 外部工件区,GPL 隔离,站 40 先例;现经 `bootstrap --from` 迁入 `ESHINE_WORK`) | busybox.net | vm0/vm2 用户态工具(tools/build-busybox.sh 精选集) |
| cJSON | 1.7.18 | MIT | tarball 入仓(3rd/cjson-1.7.18.tar.gz) | github.com/DaveGamble/cJSON | JSON 解析(jsontest demo;RTOS 内核自研最小 JSON 不动,站 51 决策) |
| zlib | 1.3.1 | Zlib | tarball 入仓(3rd/zlib-1.3.1.tar.gz) | github.com/madler/zlib codeload 归档(非 zlib.net 官方发布) | 压缩(ztest demo:slog 压缩落盘) |

## 台账纪律

1. 组件入仓(3rd/*.tar.gz)必须先有本表行;版本钉死在文件名与行内。
2. GPL 组件不入仓(busybox 先例):台账记"仓外"+外部路径+构建配方入仓。
3. 每行许可以组件自带 LICENSE 为准;in-repo 工件以 3rd/manifest.yaml
   的 sha256 钉死,由 tools/sbom-check.py(门 [179])逐项实算核验。
4. 台账只增不减(append-only);升级=新行+旧行标 superseded。
5. tools/dtb/*.dts 由 QEMU 8.2.2 `-M virt,dumpdtb` 产物反编译裁剪派生
   (QEMU 为 GPL-2.0+),属硬件描述数据;派生依据已在 dts 头注标明,
   文件本体以 Apache-2.0 头发布。
