# 站 61:yocto 镜像聚合(内容包口径)

## 一句话

构建系统的终点是一张可启动的镜像目录:boot/ 四件+一个 run.sh+
一份 manifest.json——机器定义是启动参数的唯一事实源。

## 技术叙事

1. **终装不是拼盘**:eshine-image.bb 聚合部署四件,生成 run.sh
   (从 machine conf 读参数,不是散落脚本)和 manifest.json
   (版本+机器+逐件 sha256)。
2. **deploy 类的地形**:每个配方的 DEPLOYDIR 是独立的,共享面
   在 sstate 输出——第一版从邻家院子里拿东西,当然空手。
3. **local.conf 会说谎**:MACHINE 环境变量被 local.conf 的硬设
   盖过,manifest 的机器名就是铁证。

## 金句

- 组装的可信,来自入口自己会启动:run.sh 不是文档,是产品。
- 机器定义是参数的唯一出处;参数散落之日,漂移开始之时。

## 数字

- 门禁 176→178;镜像=4 件+2 生成物;sha256 逐件核验。
