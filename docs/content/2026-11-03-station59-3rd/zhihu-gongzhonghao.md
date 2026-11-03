# SBOM 的第一页:给第三方组件立台账

站 59,批次 C 第三站:生态件入仓。这站不大,但它是站 62(SBOM)
的地基。

## 一行台账,先于一个 tarball

3rd/ 目录今天起有一张表:组件、版本、许可、形态、源、用途。四行:
musl 1.2.5(MIT,入仓)、busybox 1.36.1(GPL,**仓外**——站 40 的
隔离先例,源和产物都住在仓外,GPL 不污染 Apache-2.0 仓)、cJSON
1.7.18(MIT,入仓)、zlib 1.3.1(Zlib 许可,入仓)。

纪律写死在表尾:入仓先记行、版本钉死文件名、GPL 永远仓外、只增不减。
升级=新行+旧行标 superseded。这不是文档洁癖——站 62 要做 SBOM 时,
这张表就是事实源,而事实源从来不是补出来的,是攒出来的。

## 生态件住 EL0

cJSON 和 zlib 都是 musl 世界的 demo 消费者:jsontest 用 cJSON 解析
治理面的真清单 /etc/services.json(marker/gated/beater 三行,输出
确定性);ztest 做的是 slog 压缩落盘——deflate /var/log/slog 到
/tmp/slog.z,读回来 inflate,逐字节比对。

值得说的是边界:RTOS 内核站 51 自研的最小 JSON 解析器不动。生态件
住 EL0,内核的自研面越稳,生态的引入越安全——cJSON 进 EL0 的 demo,
不进内核。

## 配方直编

cJSON 是单文件库,直编一发;c zlib 的核心源文件表直编 libz.a。
为什么不用 configure?CHOST configure 对 musl specs 的交叉语境
不友好,而直编清单就是 make libz.a 的内部清单——交叉语境下,
configure 是负债不是资产。

门禁 171→174,冒烟 10/10。下一站:cyclictest 延迟基线。
