# 一张会自己启动的镜像 🧱

站 61 批次 D 第二站!yocto 镜像聚合(终装):
📦 eshine-image.bb:boot/ 四件+run.sh+manifest.json
⚙️ machine conf=启动参数唯一事实源(六变量生成 run.sh)
🔐 manifest=版本+机器+逐件 sha256 工件账
💥 两枚地形雷:deploy 类 DEPLOYDIR 每配方独立(共享面在
   sstate 输出)/local.conf 硬设 MACHINE 盖环境变量
   (manifest 机器名=铁证)

门禁 176→178,镜像 run.sh 自靴 musl hello ✅
下一站 SBOM 台账 📋
