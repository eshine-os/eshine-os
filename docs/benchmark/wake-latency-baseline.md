# 唤醒-运行延迟基线(站六十,cyclictest 形)

> 周期唤起任务实测唤醒-运行延迟(超额=实测-名义);数值=基线记录
> **非硬门**(趋势对照门:门禁 [176] 只断言直方图形状+max 上界)。
> 环境:QEMU virt 4xCortex-A57 TCG,-m 512;vm0 eshine-rtos
> 100Hz tick;demo=/bin/cyclictest(200 环,10ms=1 拍周期)。

## 基线(2026-11-03,站 60 收口样)

```
cyclictest: 200 loops period=10ms
cyclictest: 0-1ms: 0        ← 亚拍唤醒不存在(无 hrtimer,后弧)
cyclictest: 1-5ms: 0
cyclictest: 5-6ms: 1        ← 相位游走事件(拍内相位穿越)
cyclictest: 6-9ms: 0
cyclictest: 9-10ms: 149     ← 主分布:恰一拍超睡
cyclictest: >10ms: 50       ← 相位穿越后双拍超睡(10-20ms)
cyclictest: max=10054us avg≈9972us(9-10ms 主簇)
```

## 调度延迟分解

- **超额=一拍(10ms)减拍内相位**:nanosleep POSIX 至少语义=ceil+1 拍界
  (站 60),超睡下界=拍内相位、上界=+1 拍;200 环实测 9.97-10.05ms
  主簇=相位锁定(每环醒后即睡,相位≈30µs)。
- **>10ms 尾部(25%)**:相位游走——环路周期与拍周期差拍累积,偶越
  拍界=+1 拍(≤20ms);5-6ms 单发=相位回卷事件。
- **亚拍面(0-1ms)恒零**:tick 量子调度器无亚拍唤醒——hrtimer/oneshot
  亚拍精度=后弧(参考实现 hrtimer 栈)。

## 趋势对照说明

- 后续站(真板/中断接管/优先级面)重跑本表,对照主簇位移;
- 主簇右移=调度退化红旗(硬门不在本站,记档观察)。
