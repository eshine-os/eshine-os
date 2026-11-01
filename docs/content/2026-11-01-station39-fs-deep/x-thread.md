# X 8 推(站39 FS deep)

1/8
eshine-os 站39:文件系统深面。ramfs 写时复制(首写拷入页池页,归档字节零改动——rootfs 永远是只读事实源)+CREATE/UNLINK/READDIR 全生命周期+SEEK whence 三档+blkdev 第四 resmgr 服务器(真 virtio-blk 裸盘,后端=宿主静态盘)。 #embedded #kernel

2/8
UNLINK 的设计细节:死节点标记而非数组挪移——挪移会把所有还拿着旧索引的打开句柄(ocb)撕开。参考实现 POCB 引用计数的教训在文件面上的回响:句柄持有的是位置,不是名字。 #design

3/8
lseek 按契约返新位置(POSIX 面)——站33 写的旧判据(返回值!=0 即失败)当场撞破,loader 的 seek 检查同步改只拒负。教训:改契约的第一动作是 grep 全部判据面,不是改完实现就走。 #lessons

4/8
本站最大战果是一颗重犯的雷:通道/连接/名字三张表 4/8/8 是站30-31 定的账,本站加第四服务器没人重算——开机恰满通道表,sigq 后继每个 ch_create 都 -ENOMEM 整脸溺毙,三轮误当雷追。 #build

5/8
七个站前(站31)在 ocb 池上交过一模一样的学费,教训原话"冒烟前算账"。容量不是配置项,是账——每加一个占用者都要重新合一次。修=按账放宽(8/16/16),bss 只多一 KB。 #engineering

6/8
另一颗:READDIR 命中分支的 continue 只续 for 环——游标永不增+连环回执+终末回执(status=0)最后到把之前的全覆写,客户端恒收 0(LSDBG 探针一发定谳)。修=单答形。多路 reply 的协议,任何一路后到都能覆盖前功;回执必须单点。 #debugging

7/8
fblkdemo 一回过的原因值得记:宿主 virtio 设备模型(站9-10)厚实——desc/avail/used 全链宿主侧服务+静态盘后端,guest 侧只管提交与等完成(WFI 由 tick 兜醒)。基础设施的红利,在第一个真消费者上门时兑现。 #virtio

8/8
eshine-os:clean-room ARM64 Type-1 hypervisor+RTOS,Apache-2.0。站39/41:103 门 ×2 双绿(新 [96-98],旧顺移 [99-103])。下一站服务收编+busybox,直开不停。 #opensource #hypervisor
