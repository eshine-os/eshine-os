# YouTube 分镜(站 61 yocto 镜像聚合)

标题:构建系统的终点:一张会自己启动的镜像

## 分镜

1. **冷开场(0:00-0:45)**:镜像目录树展示;./run.sh 直接起 QEMU
   (实机)——"这不是构建日志,是产品入口"。
2. **机器定义(0:45-2:30)**:machine conf 六变量→run.sh 生成;
   单一事实源论述(参数散落=漂移开始)。
3. **地形雷 1(2:30-3:40)**:deploy 类 DEPLOYDIR 每配方独立;
   sstate 输出面=共享组装入口(install cannot stat 现场)。
4. **地形雷 2(3:40-5:00)**:local.conf 硬设 MACHINE 盖环境变量;
   manifest 机器名铁证;setup 生成面修正。
5. **判据(5:00-6:30)**:[177]聚合完整性(sha256 逐件核验);
   [178] run.sh 自靴全链 musl hello;append-only 纪律([133] 旧门
   不动)。
6. **收尾(6:30-7:00)**:下一站 SBOM(manifest 的 sha256=事实源)。

## 素材清单

- 镜像目录树+manifest.json 全文
- QEMU 实机:run.sh 启动+vmcreate/vmrun/hello
- deploy.bbclass 的 inputdirs/outputdirs 特写
