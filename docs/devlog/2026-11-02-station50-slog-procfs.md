# 站五十 devlog:slog 持久化+klog 双源+logctrl+procfs 扩面

> 承接站 49(c91dd38)。批次 A 第二站;门禁 **139→145**(append-only),
> 冒烟 19/19 ×4 连续全 PASS。镜像机 未同步(用户令明早统一)。

## 0. 形态(参考实现 slog/log_file+seq_file 白描)

- **消息面持久化**:slogsrv 以 resmgr 客户端身份写 /var/log(服务器
  互为客户端=微内核正道):open/create+SEEK END+WRITE 44B 分块+STAT
  尺寸影子;**双世代交替命名轮转**(rotate=unlink 对侧+create 对侧,
  零拷贝——8MB 页池下 10MB×10 轮转不可为的诚实形)。
- **klog 双源**:核心新增 `eshine_log_ring_delta`(绝对索引游标模型
  ——**head=total mod SIZE 恒等式,phys(K)=K%SIZE,首版 off 换算是
  错位雷**)+`log_ring_lock/unlock` 锁对(收割读与存环写同锁);
  slogsrv 1s recv 超时醒=周期 flush+用户写 piggyback。
- **深径前缀路由**:`rt_msg_name_prefix_open`(最长前缀逐级截短)
  ——**深径 openat 从未真正路由过**(站 40 v0 全失配回退 ramfs),
  procfs 文件面的第一块基石;procfs OPEN 按路径分型,meminfo/cpuinfo/
  interrupts 三文件行游标生成,irq 表新 count 域。

## 1. 战雷(纪律进账,本站最大战果=战雷四枚)

1. **boot 期往返与 pm 绑定窗竞态**(本站旗舰雷):
   slogsrv boot 期的 ramfs 往返(open/create/首收割)与 pm stdio
   绑定窗(20 拍重试)同拍打散 FIFO 序→**pm 间歇 bind 失败**,且
   busybox 全链挂死(stdio 缺位=cat 写进自己的文件 fd,静默无输出)。
   基线对照法二分(worktree 挂 d9e172b/c91dd38 对照,gitignore 的
   busybox 仓外产物需拷入+删 cpio 强制重打包——cp -a 保时间戳=
   "已是最新"假绿)定谳雷在站 50。修=**惰性开卷**(boot 往返撤除,
   首个客户端写/周期收割时按需起);场景加 pm bind 绊线(fail_if)
   后 4/4 稳。
2. **前向引用编译雷**:`slog_gen_size` 在定义前被
   slog_store_ready 调用(implicit declaration+类型冲突=硬错误)→
   **rt_slog.o 编译失败而 make 尾行只显 objcopy"成功"**——三轮
   冒烟全跑旧二进制,旧雷间歇复现。教训:**make 输出 grep 计数≠
   构建绿;错误行必过眼,尤其含 -j4 并行时**。修=函数块前移。
3. **臆造 needle**:[141] 门首版 needle "slog: 3 lines roundtrip"
   是臆造(cmd_slogdemo 真输出 "slog: dumped 3 lines")——**新 needle
   落笔前先 grep 产出真身**(站 41 第二判据教训重犯)。
4. **存量警告三处清账**:sys_pipe 未用 frame/gic.h 唯一非 inline
   static(搬移雷:vm1 裸机客也消费 gic_dist_init,搬进 rt_main=
   guest 链接炸;一字修=static inline)/rt_blk.h include 序——
   "警告在,没人读"(站 41 教训)再犯即抓,-B 全量重建 0 警告收口。

## 2. 判据与验收

- 冒烟 19/19 ×4 连续(scene:pm bind 绊线+双源读回+轮转+三 procfs
  文件+深径路由);门禁 [140-145] 写而不跑(批验收纪律);help
  走查 145 步;三处同步同批。
