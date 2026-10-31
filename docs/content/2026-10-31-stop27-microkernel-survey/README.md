<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第二十七站内容包:微内核机制普查+路线 spec

口径(README 索引):
- **站性**:纯普查站(零代码,80 门不动)——用户四指令
  (参考实现=机制参考基准/类 QNX 微内核终态/yocto 编译/docker
  发布)落地为路线图;红线定格:机制参考、零代码拷贝、
  参考实现权属私藏。
- **方法**:两份一手档案(参考实现 syscall 全 305 号表+08-07
  机制审计报告)+四路普查代理(2.9M+ tokens,白描零转录)。
- **五处直觉纠偏**:架构真身=userspace-server(fd=通道
  三元组 QNX resmgr 会合点)/xspace=全局共享窗非每进程
  /exec=用户态驱动内核只帧改写/CFS=仅背景类/VM_SET=单拷
  窗非零拷贝。
- **溯源三档表**(红线执行依据):开源RTOS底座→Apache-2.0
  公开上游可直对照;Linux 派生层→公开机制文档参照;
  参考实现自有服务层→机制形状白描。
- **六差距修复清单**(站 30):coid/优先级传播/channel
  flags/命名层级/事件绑定族——机制照参考实现+审计修复向
  QNX 补齐。
- **产出**:spec §0-10 定稿+偏差记录 #24-30+站点序列
  (28 EL0+syscall→29 进程+地址空间→30 消息 IPC→31
  server+rootfs→32 signal+futex→33 musl/基础库→yocto→
  docker)。
- **诚实账**:站十六"参考RTOS+不翻树"当时入册为用户指令,
  今日基准切换同为用户令,沿革如实记;参考实现沿革
  (参考实现工作区→开源重构→干净副本)入档案。

文件:
- zhihu-gongzhonghao.md:知乎/公众号长文(主线:给一座
  38 万行的参考架构画图纸——一次四路普查的定谳与纠偏)
- xiaohongshu.md:小红书短文(38 万行参考实现普查记)
- x-thread.md:X 8 推(普查方法论+五处直觉纠偏)
- youtube.md:视频脚本(普查课:一手档案优先+代理分治
  +红线执行)
- 同批:docs/devlog/27-microkernel-survey.md
