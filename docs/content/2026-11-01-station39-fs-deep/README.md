# 站39 口径:FS 深面——可写 ramfs+枚举+真盘块设备

**eshine-os** · 站 39(FS deep)· 2026-11-01

## 本站做了什么

- **ramfs 写时复制**:首写拷入页池页(≤4KB v0),追加随位长
  ——**rodata 红线保全:归档字节零改动**。CREATE 建即开挂名;
  UNLINK 死节点标记(免索引挪移撕开在册 ocb);READDIR 活
  节点序枚举(/dev/ramfs 元名+游标)。
- **SEEK whence 三档**:SET/CUR/END 透传 wire rsv 槽;lseek
  返新位=POSIX 面(站 33 旧契约退役,loader 判据同步)。
- **blkdev=第四 resmgr 服务器**:真 virtio-blk 裸盘面,数据
  后端=**宿主 EMULATED 静态盘**(站 9-10 机器,免 -drive);
  写=读改写整扇回写;WFI 等完成由 tick 兜醒。

## 战果两枚

- **容量账再犯**(站 31 同课):CH/CONN/NAME 4/8/8,开机
  四服务器恰满通道表——sigq 后继整脸溺毙,三轮误当雷。修=
  按账放宽(8/16/16)。**容量是账,不是配置。**
- **READDIR 单答形**(自雷):matched 分支内 continue 只续
  for 环——连环 reply+终末 reply 覆写 status=0,客户端恒收 0。
  **多路 reply 的协议,任何一路后到都能覆盖前功;回执必须
  单点。**

## 门禁

103 门 ×2 双绿(新 [96-98],旧 [96-100] 顺移 [99-103])。

## 诚实边界(v0)

4KB/文件上限(一页);无 FAT/ext(裸盘面);无分区表;
UNLOCK 即摘(POSIX 打开中语义后弧);枚举一事一连接往返。

## 关键词

ARM64 hypervisor · clean-room · RTOS · resmgr · ramfs ·
virtio-blk · 写时复制 · VFS · 容量账
