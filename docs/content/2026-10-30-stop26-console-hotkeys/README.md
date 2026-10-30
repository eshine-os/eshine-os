<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第二十六站内容包:控制台快捷键(Ctrl-^ 前缀直切三 VM)

口径(README 索引):
- **选型即排雷**(键面是稀缺资源):0x1d(Ctrl-])逃生契约三站
  门禁锚定不可动;Ctrl-A..Z 大多有宿主/guest 语义不可偷
  (0x03=SIGINT、0x1c=SIGQUIT、0x04=EOF……);F 键/方向键=
  多字节转义序列与 vi/bash 的 ESC 无法区分。**定案
  Ctrl-^(0x1e) 前缀**:与 0x1d 字节相邻成一对心智模型
  ("Ctrl-] 夺回宿主,Ctrl-^ 跨 VM 直切")、无 TTY 信号绑定
  (三客壳零成本)、QEMU Ctrl-A/tmux/screen 前缀键先例。
- **语义契约(QEMU 同律)**:Ctrl-^ 0|1|2 切 vm / h 回宿主 /
  双击=字面 0x1e 直递 owner;**未绑定键=前缀取消、本键照常
  处理(不吞字节)**;0x1d 逃生优先级恒高。
- **实现最小面**:vmconsole 命令体原样抽为公共函数——快捷键与
  命令字零行为差异;单字节前缀态机+fall-through,双击字面
  复用站 24 推环唤醒不变量全路径,零 goto 零复制。
- **门禁 79→80**:新门 [77] 三台在场窗一门全验(切换+hb 回复
  证输入链完整);顺移一折(旧 [78] 的 `\x1d` 逃生步骤前提被
  新门收台改写,冗余移除——**插新门改世界状态时,下游旧门
  前提要重验**);终树双绿 80/80 ×2。
- 冒烟 11 项含**取消路径字节直通实证**(`\x1eXhb` 的 X 未被吞,
  guest 把 "Xhb" 当未知命令回 usage)。

文件:
- zhihu-gongzhonghao.md:知乎/公众号长文(主线:键面是稀缺
  资源——一次快捷键选型的排雷推演)
- xiaohongshu.md:小红书短文(Ctrl-^ 诞生记)
- x-thread.md:X 8 推(选型推演+零行为差异+一折教训)
- youtube.md:视频脚本(选型课:每个控制键都有主)
- 同批:docs/devlog/26-console-hotkeys.md
