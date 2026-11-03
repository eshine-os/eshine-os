# 构建系统的终点:一张会自己启动的镜像

站 61,批次 D 第二站:yocto 镜像聚合,终装。

## 什么算"终装"

站 34 我们让 bitbake 出四个部署件(eshine.elf/rtos.bin/guest2.bin/
ramfs.cpio)——那是"出了产品"。这一站的问题是:用户拿到的应该是什么?

答案是一个目录:

```
eshine-image/
  boot/eshine.elf  boot/rtos.bin
  boot/guest2.bin   boot/eshine-ramfs.cpio
  run.sh           manifest.json
```

run.sh 就是产品的入口:./run.sh 起第二台 QEMU,串口 socket 路径
可以入参。manifest.json 是工件账:版本、目标机、逐件 sha256。

## 机器定义是参数的唯一出处

run.sh 里的 QEMU 参数(Machine/CPU/内存/装载地址)不是散落在
脚本里的常量,而是从 machine conf——meta-eshine/conf/machine/
eshine-qemu-a57-type1.conf——的六个变量生成的。改机器定义,
run.sh 跟着变;参数散落之日,就是漂移开始之时。

## 两枚构建系统的地形雷

第一枚:deploy 类的 DEPLOYDIR 是每个配方独立的(WORKDIR/deploy-PN),
真正的共享面在 sstate 输出(DEPLOY_DIR_IMAGE)。第一版镜像配方从
自己家的 DEPLOYDIR 里找邻居家的件,当然 install cannot stat——
读对院子,sstate 镜像面才是组装的正确入口。

第二枚更隐蔽:local.conf 里一条硬设的 MACHINE = "qemuarm64",
把我环境变量里传的目标机静默盖掉了。铁证是 manifest.json 里的
"machine": "qemuarm64"——产物不会说谎。修法:setup 脚本的生成面
直接写目标机,存量 build 一并打补丁。

## 判据

站 34 同型:门禁自己跑 bitbake。[177] 聚合完整性:boot/ 四件+
run.sh+manifest 在位,sha256 逐件重算核验,机器名必须是目标机。
[178] 终装入口自靴:run.sh 起第二台 QEMU,vmcreate/vmrun/console,
musl hello 从镜像里跑出来——组装的可信,来自入口自己会启动。

门禁 176→178。下一站:版本 pin+SBOM 台账。
