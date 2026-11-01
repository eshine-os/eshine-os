# 站三十九 devlog:FS 深面——ramfs 可写化+枚举+裸盘块设备

日期:2026-11-01 · 作者:Julian Chu + Claude Code
状态:**已收口**——103 门(新 [96] wrdemo/[97] lsdemo/[98]
fblkdemo,旧 [96-100] 顺移 [99-103])两连 RESULT PASS(首轮即双)

## 0. 本站做了什么

参考实现 FS 白描的站内形状,三件套:

- **ramfs 写时复制**:首写把节点内容拷入一个页池页(≤4KB 上限
  v0 诚实拒绝),追加随位长,读优先 cow 面——**rodata 红线保全:
  归档字节零改动,一切写落页池**。CREATE=建即开+挂名;UNLINK
  =死节点标记(免索引挪移撕开在册 ocb)+页还池;READDIR=活
  节点序枚举(/dev/ramfs 元名+游标协议)。
- **SEEK whence 三档**:SET/CUR/END 透传 wire rsv 槽(站 33
  的 rsv 闲置槽转正,同站 38 shm_id 手法);**lseek 返新位=
  POSIX 面**(站 33 旧返 0 契约退役,loader 判据同步只拒负)。
- **blkdev=第四 resmgr 服务器**:真 virtio-blk 裸盘面——数据
  后端=**宿主 EMULATED 静态盘**(站 9-10 机器:desc/avail/used
  全链宿主侧服务,免 QEMU -drive);写=读改写整扇回写;
  WFI 等完成由 tick 兜醒(单核上等待期无他跑者,客户端阻塞
  在回执 sem)。

## 1. 战果一:容量账(站 31 ocb 池同课,再犯)

CH/CONN/NAME 表 4/8/8:开机 4 服务器恰满通道表——sigq 后继
(msgseg/longmsg)的 ch_create 满表 -12=整脸溺毙,**连续三轮
"尾段 demo 集体失败"误当雷追了半轮才定谳**。修=按账放宽
(8/16/16),bss +1KB。**站 31 教训原文"冒烟前算账"——本站
加盘(第四服务器+元名+demo 瞬时)时没重算容量账,先犯后补。
容量是账,不是配置。**

## 2. 战果二:READDIR 单答形(自雷,双探针定谳)

初版 matched 分支内 `continue` 只续 for 环——live 永不增+
命中后连环 reply+**终末 reply 最后到覆写 status=0**,客户端
恒收 0(LSDBG r=0 实抓)。修=单答形(一次走查恰一次回执)。
**判据教训:多路 reply 的协议,任何一路后到都能覆盖前功;
回执必须是单点的。**另:批量编辑两度 assert 拦截未落盘的
修正(嵌合体防线连续第四站兑现),但也暴露"批编辑挂一半"
的核对盲区——修完必须 grep 现役文本再跑。

## 3. 自雷小账

- fblkdemo 判据差一字节("BLK39-SECTOR0"=13 字符写成 14,
  b0=66 探针一发定谳);loader 判 `!=0` 撞 lseek 新契约
  (POSIX 返新位)→改只拒负——**契约改动要 grep 全部判据面**。
- wrdemo 首跑即全绿(COW 一次成形);fblk 真盘读写一回过
  (宿主设备模型的厚实红利)。

## 4. 门禁与收口

[96] wrdemo 五锚(写/追加/读回/EOF/ENOENT);[97] lsdemo
枚举+/etc/motd 在册;[98] fblkdemo 扇区读写回。三处同步
103 步;内容包 5;commit→镜像机→记忆;**直开站 40(服务收编
+busybox),不停**。
