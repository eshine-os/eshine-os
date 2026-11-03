# YouTube 分镜(站 63 docker 弧)

标题:pin 是对账出来的——工具链的运输形态

## 分镜

1. **冷开场(0:00-0:45)**:docker-build.sh 运行输出——"pin ok: gcc
   13.3.0 / binutils 2.42 / qemu 8.2.2"。"这不是 Dockerfile 注释,
   是容器实采对账"。
2. **同源即同 pin(0:45-2:00)**:manifest.yaml 台账与 Dockerfile
   并排;ubuntu:24.04 基座=宿主同源的论证链。
3. **对账红一次(2:00-3:30)**:-dumpversion 只出 "13" 的战雷现场
   (对账第一次跑就红)→ -dumpfullversion 修绿。"对账本身也被对账过"。
4. **镜像层零仓字节(3:30-4:40)**:--user 降权+挂载;容器内全链重建
   实录(3rd 三件+make demos+make -B+sbom ALL GREEN)。
5. **发布自靴(4:40-6:00)**:make-release.sh 打 tar→解包临时目录→
   run.sh 起 QEMU→musl hello 判据行——"发布件的最后一道工序是证明
   它自己能靴"。
6. **收尾(6:00-6:30)**:门禁 179→180;下一站 ELF core 深水区预告。

## 素材清单

- docker-build.sh 全绿输出(对账行+构建+sbom)
- -dumpversion vs -dumpfullversion 对比演示
- make-release.sh 自靴判据输出
