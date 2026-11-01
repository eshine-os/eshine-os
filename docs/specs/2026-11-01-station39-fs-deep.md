# 站 39 spec:FS 深面——ramfs 可写化+VFS 路径层+blk-backed FS

日期:2026-11-01 · 作者:Julian Chu + Claude Code
参考实现机制白描(零拷贝红线):VFS=用户态 server 统一层;
rootfs=initramfs+server;blk-backed=真盘真分区表(参考实现 VM 楼
盘的 F5 块设备面);readdir/whence 语义 POSIX。

## 1. 我们世界里的三件事

现状盘点:ramfs=cpio 只读(站 31,文件内容只经消息送达);
fd 面=write 内联 44B/read 64B 窗(whence 只有 SEEK_SET);
virtio-blk=probe 观察哨(站 10 起,RTOS 侧未接)。

### A. ramfs 可写化(rt_fsrv.c 扩)
- WRITE op:ramfs 服务器按 ocb{node,off} 位置推进写——
  节点数据区从 cpio rodata 只读改为**写时复制到页池**:
  首写把节点内容拷入一个页池页(≤4KB v0),后续写直落
  该页;EOF 后追加=length 推进,≤4KB 边界诚实拒绝。
- 新 op CREATE(fOPEN 创文件,空节点)/UNLINK(摘 PNODE,
  页还池)/READDIR(枚举路径名,64B 窗分次)。
- **参考实现 "文件内容只经消息送达"红线不破**:blk 页不映射
  用户窗,一切仍走 resmgr 消息。

### B. VFS 路径层(rt_vfs.c 新)
- 统一查找:名字注册表(站 30)+ramfs PNODE+pipe 端的
  **路径解析单入口**(层级分段比较已有);mount 面 v0
  =静态表(路径前缀→服务器),挂名即挂载(参考实现单挂
  语义)。
- fd 面 read/write 加 whence:SEEK_CUR/SEEK_END 语义
  (服务器持 ocb.off 自推进;fd wire 的 SEEK op 扩 mode)。

### C. blk-backed FS(观察哨转真,virtio-blk 接上)
- RTOS 侧 virtio-blk 队列真读写(站 10 的 vm1 侧
  dwrite/dread 机制移植):blkdev 服务器(第四个 resmgr
  服务器)以裸块设备为后备——**v0 = blkdev 直读直写
  (无文件系统解析,参考实现 F5 裸盘面);盘内 FAT/ext 解析
  =后弧**。
- demo:fblkdemo——fdwrite 到 /dev/blk 块设备(扇区写)、
  fdread 回读、字节比对;QEMU virtio-blk 真盘驱动
  (-drive if=virtio,真 I/O 落宿主镜像)。

## 2. 判据(100→103)

- [96] wrdemo:ramfs 写+回读+EOF 追加+UNLINK 生命周期。
- [97] dirlist:READDIR 枚举+/etc/motd 存在性+新文件入册。
- [98] fblkdemo:块设备写读比对(真 virtio 通路)。
- 插 [95] sigqdemo 后,旧 [96-100] 顺移 [99-103];三处同步。

## 3. 边界(v0)

无 FAT/ext 解析(裸盘面);无并发写仲裁(单写者 v0);
ramfs 写上限 4KB/文件(页池一页);UNLINK 打开中=关闭计数
归零才真摘(POSIX 近亲,v0 即摘,记档);无 mount 系统调用
(静态表);blk 无分区表。

## 4. DoD

spec→实现→103 门×2→devlog→内容包 5→commit→镜像机→记忆→
直开站 40(服务收编+busybox),不停。
