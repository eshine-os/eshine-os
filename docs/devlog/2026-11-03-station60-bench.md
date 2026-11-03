# 站六十 devlog:benchmark 延迟基线(cyclictest 形,批次 D 首站)

> 承接站 59(生态件提交)。门禁 **174→176**,冒烟 11/11 全 PASS。
> 镜像机 未同步(用户令)。

## 0. 机制

- nanosleep 真号 101(asm-generic/musl aarch64 头定号);POSIX 至少
  语义=ceil+1 拍界(最多超睡一拍 10ms,tick 量子诚实超睡记档)
- 消费者=/bin/cyclictest(200 环,10ms=1 拍周期,超额 1ms 桶直方图)
- 基线落盘 docs/benchmark/wake-latency-baseline.md(数值非硬门)

## 1. 战雷

1. **号面臆测重犯(站 42 纪律)**:ES_NR_nanosleep 首版臆 35(=x86_64
   号)——冒烟直方图全 0-1ms 桶+rc=-1/errno=38(ENOSYS)三连信号;
   插桩(dispatcher 印 35 号路径)零输出=svc 根本没到;**反汇编自证法
   翻车**(名字计数 sendto=0 而它明明活着=objdump 注记不可靠);
   终局=直读 musl 安装头 bits/syscall.h:`__NR_nanosleep 101`(asm-generic
   真号;35 是 x86_64 的)。教训:号面必读 musl 头,反汇编注记不可作判据。
2. **POSIX 至少语义违** :首版 ceil 取整=拍界醒最短可 <ns(10ms 睡眠
   实睡可 0.1ms);修=ceil+1 拍界(保证 ≥ns,代价≤+1 拍超睡)。
3. 桶距适配:首版 8 桶(>7ms 收尾)全挤尾桶;11 桶(0-10+>10)分布
   可读(9-10ms 主簇现形)。

## 2. 基线定谳(数字=记录非硬门)

主簇 **9-10ms=恰一拍超睡**(相位锁定 ≈30µs,200 环 149/50/1 分布);
>10ms 尾 25%=相位游走偶越拍界(≤20ms 双拍);亚拍面恒零(tick 量子
调度器无亚拍唤醒——hrtimer/oneshot=后弧)。

## 3. 判据与验收

冒烟 11/11(200 环直方图+max 在界);门禁 [175-176] 写而不跑(批验收
纪律):[175]直方图形状+max≤3 拍界(30000µs 理智界);[176]宿主侧
基线文档在位。help 176 步;ARCHITECTURE 同步。
