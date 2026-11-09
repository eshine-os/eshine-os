# X 8 推(站 45 APS+MPRT)

1/8
eshine-os 站 45:混合关键性的本职技能——CPU 预算分区(APS,QNX 白描)+内存预算分区(MPRT)。参考实现是 1108 行的 k_aps.c,我们落地 200 行:白描的是机制不是行数。 #RTOS #mixedcriticality

2/8
APS 两个语义:guarantee——20% 分区在争跑时实得 20%(窗 20 tick 记账,tick 粒度与参考同款);steal——没人要的 CPU 白给你,记账回冲。保证是下限不是上限,偷闲是权利不是漏洞。 #scheduling

3/8
实现账:超标节流复用睡眠机器(state=RT_SLEEP+窗界唤醒),偷闲判断扫描任务池——等待原语零新增,调度器净增 30 行。体量来自机制,不来自特例。 #architecture

4/8
MPRT 记账点选在 VMA 声明——mmap 和 fork 恰好同走一个 declare 函数,一钩双面。孙进程 fork 全拷入组,32 页>16 上限直接拒:内存隔离连 fork 一起管。 #memory

5/8
本站最硬的雷:新字段 mpart 没进进程 create 清单。BSS 零初值=0,而 0 恰是合法数组下标——每个 fork 儿都"入组"了不存在的分区 0(上限也是 0),所有 VMA 声明被拒。 #bug

6/8
症状四张脸:信号 demo 子无输出、另一 demo fork failed、管道 demo 读失败、内存 demo 退账脸 wrong。四个测试连炸,一张脸各报一种错——冒烟的价值不是绿灯,是把互为镜像的失败抓成全家福。 #testing

7/8
复盘:一个根因三个修复(字段入清单/identity VMA 走专口/fork 承继一行),10/10 复绿。"新域入槽清单"第六案,新教训:零初值恰好合法时,残值不是崩,是幻觉。 #discipline

8/8
判据:apsdemo 对齐窗界两满窗确定性 share=20%(带宽判)+偷闲窗 45 拍独跑满账;mprtdemo mmap/fork 双拒面+退场全退;parts 两表=k_aps_show 镜像。门禁 129。批令推进,第 45 站。 #eshineos #buildinpublic
