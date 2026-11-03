# docker 弧:pin 是对账出来的,不是注释出来的

站 63,批次 E 首站:构建环境容器+发布镜像 tar。

## 运输形态

前一站把"我用了什么"记成了机器可读的台账(manifest.yaml)。这一站
把台账兑现成可执行承诺:docker/Dockerfile.build——基座 ubuntu:24.04,
与宿主同源,apt 交付 gcc 13.3.0/binutils 2.42/qemu 8.2.2,恰好就是
台账里的 prebuilt pin。

"恰好"不能是注释里的自说自话。判据脚本的第一个动作就是版本对账:
从 manifest.yaml 提取 pin,传进容器,容器里 `-dumpfullversion` 实采,
逐字相等才放行。pin 是对账出来的,不是注释出来的。

(这站还真抓到一个:`gcc -dumpversion` 在 gcc 7 之后只打印主版本号
"13",要 `-dumpfullversion` 才出 "13.3.0"——版本对账第一次跑就红了,
改对了才绿。对账本身也被对账过。)

## 镜像层零仓字节

容器以宿主 uid 降权跑,源树挂 /src:容器内重建全链(3rd 三件+make
demos+make -B+sbom-check),产物落宿主树,owner 就是宿主用户。镜像
只运输工具链,不运输代码——复现的是"环境",不是"快照"。

## 发布=可解包即靴

make-release.sh 把站 61 的部署四件+run 入口+manifest.json 工件账
+LICENSE/NOTICE 合规面打成 release/eshine-os-27.04-dev.tar.gz。脚本
尾部不是"打完了",是自靴判据:解包到干净的临时目录,run.sh 直起
QEMU,现场重演 musl hello 全链——发布件的最后一道工序是证明它自己
能靴。

门禁 179→180。下一站:ELF core 文件,批次 E 深水区。
