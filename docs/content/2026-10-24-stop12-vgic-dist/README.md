<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第十二站内容包:vgic GICD/GICR 仿真站(distributor 成为权威)

`docs/content/2026-10-24-stop12-vgic-dist/` —— 27.04 第十二站(master
站内,2026-10-24 完成)的公开内容底稿,按平台一文件。

| 文件 | 平台 | 形态 |
|---|---|---|
| `zhihu-gongzhonghao.md` | 知乎 / 微信公众号 | 深度长文 |
| `xiaohongshu.md` | 小红书 | 短内容 + 配图 + 标签 |
| `x-thread.md` | X / Twitter | 英文 8 推 |
| `youtube.md` | YouTube(B 站) | 分镜脚本 |

## 统一素材

1. 主视觉:`[vm0] gicd v3 ok` + `[vm0] gic on: 27 33 35`(真启动式
   bring-up,PIDR2 探测→WAKER→ISENABLER→回读验证)
2. "distributor 成为权威"哲学:泵注入按 guest 使能位门控——没使能
   就不注入、不代完成 WFI
3. 战况一:stage-2 单页铁律拦下真布局(vGICD 64KB/vGICR 128KB)
   →EMULATED 窗口放行多页
4. 战况二:GICD SPI 寄存器族解码 bug——(off-0x080)>>2 把
   ISENABLER0 算成 n=32 越界,写静默 WI,**guest 使能回读当场抓获**
5. 战况三:SAS 编码值 vs 字节数——2(=4 字节)当 2 字节传,半字掩码
   把 0x08000000 吃成 0,探针两行实锤
6. intc 节点请回树(第十一站"没有的设备不进树"的兑现);门禁 36 步
   PASS ×2

## 统一口径

- 事实:vGICD@0x08000000(0x10000)+vGICR@0x080A0000(RD+SGI 双帧)
  镜像 QEMU virt 真布局;offset 全经手册复核;寄存器分层(探测/使能/
  唤醒真语义、状态存储回读、未知 RAZ/WI 设备级默认拒绝);写即完成
  (RWP 恒 0,Linux 轮询零等待);状态随 VM 生命周期(vm_create 复位)
- 主打故事:**distributor 成为权威**——中断注入从"宿主独断"改为
  "guest 使能了才注入";guest init 长成真 OS 形态,树里缺席的 intc
  节点兑现归位
- 副线:两处 bug 都被"回读验证"与"探针打点"抓获——**判据即测试,
  自证无误≠实测无误**(自证三遍全对的代码被探针两行实锤)
- 方法论招牌句:**判据即测试:把验证放进系统,bug 会自己走进来**;
  位号以手册为准(站八纪律再次执行,GICR_TYPER Last=bit4)
- AI 署名:Claude / GLM-5.3 · 系列位:27.04 连载第 12 篇
