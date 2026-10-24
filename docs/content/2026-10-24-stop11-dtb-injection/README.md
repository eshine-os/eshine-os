<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十一站内容包:DTB 注入站(设备树自描述,Linux 前传第一幕)

`docs/content/2026-10-24-stop11-dtb-injection/` —— 27.04 第十一站(master
fe5ee6f,2026-10-24 完成)的公开内容底稿,按平台一文件。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文 |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 |
| `youtube.md` | YouTube(B 站) | 分镜脚本 |

## 统一素材

1. 主视觉:`[vm0] fdt ok: cpu arm,cortex-a57 / uart arm,pl011 / blk
   virtio,mmio` 三行自查(guest 从注入的树查出设备身份)
2. 诚实裁剪原则:树里**没有 GIC 节点**——GICD/GICR 未仿真,登记即
   说谎;psci 同理
3. dumpdtb 底稿(1048576 字节)→ 裁剪树(1057 字节)对比
4. 十六进制位错战况:0x4F000000 vs 0x40F00000,fault 机器完整现场打
   印(FAR/HPFAR/pc/x0),平滑隔离死
5. make verify 34 步 PASS ×2(31→34)+ 嵌合体事故根因与三处同步
   纪律的落地

## 统一口径

- 事实:QEMU dumpdtb 裁剪补丁成最小诚实树(保留节点保真 QEMU
  原形;中断三元组 SPI1/3、PPI11 恰与前站通道 33/35/27 一一对上);
  blob incbin 入宿主 .rodata;vm_create 整拷 guest RAM 固定偏移
  0x40F00000;双 VM 共树;guest ~120 行 mini-FDT 只读走查器三节点
  自查;门禁 31→34
- 主打故事:**硬件开始自描述**——guest 对世界的认知从链接脚本
  常量搬进了宿主下发的一棵树;裁剪的第一原则是"诚实":没有的
  设备不进树,树不替未来撒谎
- 副线一:十六进制位错被隔离面关门内(宿主/他 VM 纹丝不动,低级错
  只烧掉一个租户)——十站基建的隐性验收
- 副线二:嵌合体事故(user 抓出的"标题 31 正文 25"help)根治:
  三处同步纪律首次执行——门禁降序重编号防级联+断言自校/help
  整块重写/ARCHITECTURE 验证节
- 方法论招牌句:**裁剪的第一原则是诚实:没有的设备不进树**;
  放对地方的东西不需要维护(承九站)
- 战况:一个,我自己的十六进制;隔离面替我兜了底
- AI 署名:Claude / GLM-5.3 · 系列位:27.04 连载第 11 篇
