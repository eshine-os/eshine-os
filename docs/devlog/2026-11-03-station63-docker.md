# 站六十三 devlog:docker 弧(批次 E 首站)

> 承接站 62(SBOM 台账提交)。门禁 **179→180**。镜像机 未同步(用户令)。

## 0. 机制

- docker/Dockerfile.build:工具链运输形态——基座 ubuntu:24.04(noble)
  =宿主同源,apt 交付版本与 manifest.yaml prebuilt pin(gcc 13.3.0/
  binutils 2.42/qemu 8.2.2)同源即同 pin;件面=cross 工具链+qemu
  +dtc(make -B 重建 dtb 要)+python3+make
- scripts/docker-build.sh:CI 容器化判据——容器内重建全链(3rd 三件
  +make demos+make -B+sbom-check 全绿);**版本对账=容器实采 vs pin
  逐字相等**(manifest 提取 pin→环境变量入容器→-dumpversion/--version
  实采比对);--user 降权跑=产物落宿主树 owner 即宿主用户,镜像层零
  仓字节;构建输出静默化+失败 tail(站 34 同律)
- scripts/make-release.sh:发布镜像 tar——站 61 部署四件+run 入口
  (机器定义镜像面:socket 串口同形)+manifest.json 工件账+LICENSE/
  NOTICE 合规面 → release/eshine-os-<ver>.tar.gz;**发布=可解包即靴**
  (脚本尾自靴判据:解包临时目录现场重演 musl hello 全链)
- .gitignore:release/ 入构建产物面

## 1. 战雷

- awk pin 提取首版 `-v n="  - name: gcc\$"`:`\$` 是字面 $ 非正则锚,
  index() 子串匹配恒失——写前先过一遍转义语义;修=去 $ 用纯子串。
- **gcc 版本对账假雷**:`-dumpversion` 在 gcc 7+ 只出主版本号("13"),
  `-dumpfullversion` 才出全 pin("13.3.0");qemu 版本行尾带发行版括注
  (非数字结尾)——$ 锚恒失,锚 "version " 前缀提取。对账首跑即红×2=
  对账机器自己被对账。
- **docker -e 环境变量假传**:`-e GCC_PIN`(无值形式)读的是**环境
  变量**而脚本变量未 export——pin 恒空串;修=-e VAR="$VAR" 显式赋值。
- **musl configure wrapper 自动探测的环境假死(容器才现形)**:探测=
  host 编译一个 `#include <stdlib.h>`+`#if ! __GLIBC__` 探针——纯净
  容器(无 libc6-dev)交叉编译器报错即探成 "none",TOOL_LIBS 清空,
  musl-gcc.specs 从不生成而链接面 -specs 硬依赖;宿主 libc-dev 在场
  探测恒过=增量环境遮罩。修=configure 显式 --enable-wrapper=gcc(免
  探测,环境不敏感)。
- **readelf 中文 locale 假死(镜像遮罩的镜像)**:宿主 LANG=zh_CN,
  readelf 标签翻译为"机器:"/"类别:"——`grep -E "Machine|Type"` 空匹配
  退 1,set -e 假死且零诊断输出;容器 POSIX locale 英文=容器全绿遮住
  宿主死。修=四处 readelf 管道全钉 LC_ALL=C(构建脚本输出解析=环境
  不敏感)。战雷链形态:容器绿→宿主死→手动同命令绿→bash -x 定谳
  (grep 退出码,非 gcc)。
- 容器以 root 跑会产出 root-owned 工件污染宿主树——修=--user
  "$(id -u):$(id -g)" 降权(产物 owner 即宿主用户)。
- release 首版 install -m 0755 带 0644 fallback 的双行怪形——站 61
  部署件全 0644(可执行位只属于 run.sh),对齐单一事实源删 fallback。

## 2. 判据与验收

docker-build.sh 实跑全绿(站级冒烟):镜像 build+版本对账 pin ok(gcc
13.3.0/binutils 2.42/qemu 8.2.2 容器实采逐字等)+全链重建零警告+sbom
ALL GREEN。make-release.sh 实跑全绿:tar 打包+解包自靴 musl hello 全链
(判据行 "release: unpacked tar boots end-to-end (musl hello) — RELEASE
OK")。门禁 [180]=文件面+语法面+指令面
(Dockerfile 钉链指令+bash -n+docker-build.sh 对账挂点),写而不跑
(批验收纪律);docker build 实跑不嵌门禁(时间预算律:门禁跑 docker
 build 会爆 30 分钟预算)。help 180 步;ARCHITECTURE 同步。
