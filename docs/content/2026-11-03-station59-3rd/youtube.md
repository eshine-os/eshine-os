# YouTube 分镜(站 59 生态件入仓)

标题:SBOM 的第一页——给第三方组件立台账

## 分镜

1. **冷开场(0:00-0:40)**:3rd/ 目录从 musl 一个 tarball 到今天
   三个——台账表滚动展示(六列四行)。
2. **台账纪律(0:40-2:00)**:入仓先记行/版本钉死/GPL 仓外/
   append-only;升级=新行+superseded——为什么事实源要攒不要补。
3. **生态件住 EL0(2:00-3:30)**:jsontest(cJSON 解析治理真清单)+
   ztest(slog 压缩落盘回读)实机;内核自研 JSON 不动的边界论。
4. **配方直编(3:30-4:30)**:单文件库直编+zlib 核心源直编;
   交叉语境 configure=负债。
5. **收尾(4:30-5:30)**:门禁 [172-174](含宿主侧台账完整性门)+
   下一站 cyclictest 预告。

## 素材清单

- 3rd/LEDGER.md 全表
- QEMU 实机:jsontest+ztest 输出
- build-cjson.sh/build-zlib.sh 配方
