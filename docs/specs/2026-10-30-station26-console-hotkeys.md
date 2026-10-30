# 站二十六设计:控制台快捷键(Ctrl-^ 前缀,vm0/vm1/vm2 直切)

> 承接站二十五(e7b8ab3,79 门双绿)。用户令:"通过快捷键来切换
> vm0,vm1,vm2 的 shell 吧。你看看用哪个快捷键比较好,然后在
> 代码中实现它。" 单块小站。门禁 **79→80**(新门 [77] 插入,
> 旧 [77]-[79] 顺移)。

## 1. 快捷键选型(定案与理由)

**定案:Ctrl-^(0x1e) 前缀键 + 第二键 `0|1|2`(切 vm)/`h`(回宿主);
Ctrl-^ Ctrl-^ 双击=字面 0x1e 递给当前 owner。**

选型推演:

- **已被占的键面**:0x1d(Ctrl-])=站十五定的逃生夺回(Xen/ssh
  惯例),三站门禁与全部探针锚定它——**不可复用不可改语义**
  (否则 [48]/[62]/[79] 逃生契约全破)。
- **不可偷的键面**:Ctrl-A..Z 大多有宿主/guest 语义(0x03=SIGINT
  归 Linux tty,0x04=EOF,0x1a=SIGTSTP……偷任何一个都破 guest
  shell 真实性);F1-F3/方向键=多字节转义序列,裸串口下与 vi/bash
  的 ESC 用法无法无歧义区分。
- **0x1e(Ctrl-^) 的三重优势**:①**与 0x1d 字节相邻**——"Ctrl-]
  夺回宿主,Ctrl-^ 跨 VM 直切"构成一对心智模型;②**无 TTY 信号
  绑定**(0x1c=Ctrl-\ 是 SIGQUIT,偷它破 busybox;0x1e 是 RS 记录
  分隔符,Linux tty/sh/readline/vi 全不用),对 vm0 RTOS 壳
  (0x20-0x7e 过滤器)零成本;③**前缀键有业界先例**(QEMU Ctrl-A
  c 切 monitor、tmux Ctrl-B、screen Ctrl-A),双键前缀不与任何
  单键输入争用。
- **语义契约(QEMU Ctrl-A 同律)**:前缀后未绑定键=前缀取消、
  本键照常处理(**不吞字节**——误触 Ctrl-^ 后续输入零损坏);
  双击=字面直通(escape hatch);0x1d 逃生优先级恒高于前缀态
  (前缀态随夺回清零)。

## 2. 实现(shell.c,最小面)

- 抽 `console_to_host(void)` / `console_to_vm(u32 id)`——
  vmconsole 命令体原样提出,**快捷键与命令字共用同一函数=零行为
  差异**(状态门拒绝/owner 落位/唤醒/切换消息/冲刷次序全同)。
- shell_run 输入层插前缀状态机(`static char vmkey_pending`):
  1. `owner && c==0x1d` → 逃生(原样,前缀态清零);
  2. pending → 清 pending;`0|1|2`(界内按 ESHINE_MAX_VMS)→
     console_to_vm;`h` → console_to_host;`0x1e && owner` →
     落入下方常态推环(c 即字面 0x1e,**复用站 24 推环唤醒不变量
     全路径**:push+dmb+ws 唤醒+ev9+poke,零复制);
     其余 → 前缀取消,本键照常处理;
  3. `c==0x1e` → pending=1(不回显不入环);
  4. 原有 owner 推环/host 行编辑不变。
- **两条路径全覆盖**:owner=guest 时(用户在 VM 会话中直切他 VM)
  与 owner=host 时(宿主快捷进入)统一。
- 拒绝路径:目标 VM 非 RUNNING → console_to_vm 的既有 denied 行
  (**被拒不夺台**,当前 owner 原位)。

## 3. 判据与门禁(79→80)

- **新门 [77]**(插 [76] 后——彼时 vm0/vm1/vm2 三台全在场、
  owner=vm2 busybox,一窗全验):`\x1e0`→console->vm + `hb` 回
  (热键切换后输入链完整:switch→push→醒→回,同站 24 不变量);
  `\x1e1`→vm;`\x1e2`→vm;`\x1eh`→host。
- 旧 [77]-[79] 顺移 +1=[78]-[80];全部 `[N/79]`→`[N/80]`;头注
  79 步→80 步。
- 字面直通(双击)不设门:三客壳过滤器均吞 0x1e,判据不可见
  (诚实记档:功能在、见证缺,靠 spec/help 文档)。
- 三处同步:verify+shell help(80 步表+快捷键说明行)+
  ARCHITECTURE(RX 仲裁节补站 26 面)。

## 4. 风险与诚实边界

- 0x1e 偷键面:三客现状零成本(记档);未来 guest 若需收 0x1e,
  双击字面路径已备。
- 前缀态是宿主壳输入层单字节状态,跨切换残留即下一键自然
  解决(设计上无超时窗=裸串口下无时序歧义)。
- vmconsole 命令面不变(命令与快捷键双轨,命令面全部探针/
  门禁契约原样)。
