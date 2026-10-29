<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2026 Julian Chu -->
# 第二十一站内容包:三块合成站(同步超时/panic 观测/栈防护)

口径(README 索引):
- **主体(已批 spec docs/specs/2026-10-29-station21-timeout-panic-
  stack-design.md v2)**:A 同步超时面(参考RTOS 公开语义 0=no-wait/
  FOREVER,单窗挂等,扫描摘除+owner PI 重算,tmodemo 全弧判据)/
  B panic 观测面(未知 EC 静默 wfi 退役,双面 panic,裸打不取锁)/
  C 栈防护(栈行=垫+哨兵+水位,tick 扫描,ps 水位列)。
- **对抗评审翻案三案**:PI boost 滞留(v1"零改动"被证伪——owner
  永不放锁恰是超时面要防的病,重算不变量与 give 同律)/两窗 gap=
  丢醒窗(复核+登记+挂链+摘 rdy 同一 DAIF 窗)/magic 首拍假 panic
  (扫描只查已创建槽)。
- **实测三雷**:`__builtin_frame_address` -O2 无帧指针不可用(递归
  零下潜假象,asm 直读 SP)/递归帧稀疏足迹(潜穿哨兵而 magic 落
  缝,全宽帧写)/overflower 深 sp 停泊必穿透垫区(解栈回浅再 wfi)。
- **判据**:72→75(+[40] tmodemo/[41] stkdemo/[74] overflow→panic
  复活链);R2/R3 连续双绿;R1 verify 锚在途提示符雷、R1b [54]
  观察哨家族第 2 案([61] spin 超时)如实记档。
- 门禁工程课:read_until 下界纪律第三现(verify [41] 首版锚 +
  探针陈旧匹配假阴性)。

文件:
- zhihu-gongzhonghao.md:知乎/公众号长文(主线:我写的 spec 被我的
  评审舰队证伪——PI 翻案与三雷实录)
- xiaohongshu.md:小红书短文(自己的 spec 被自己养的 20 个 agent 揪出
  16 条真伤)
- x-thread.md:X 8 推(串位:翻案→三雷→双绿)
- youtube.md:视频脚本(对抗评审实录+深度判据三雷)
- 同批:docs/devlog/21-timeout-panic-stack.md(devlog 收口)
