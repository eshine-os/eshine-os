# 站五十四:跨域 hcall fail-closed 门(safeshm 窗 ACL+运行时改表)

日期:2026-11-02 | 批次 B 第二站
参照:reference-audit §C7-A3(跨域 op 前置 fail-closed 权限门)+§C2
(运行时 stage-2 改表守则);站 47 safeshm 几何。

## §1 v0 形状

- xvmacl 授权表(默认 vm0/vm1=站 47 几何保位,vm2 不授);xvmacl
  <id> <on|off> 宿主命令(自解析 id+词,parse_id 单字符局限记档)。
- 运行时开关=条目级 stage-2 改表:stage2_window_toggle(l2_ram 单块
  条目清/置)+运行改表三纪律(dsb sy→TLBI IPAS2E1IS 内广播→dsb sy
  +isb 收口)=§C2 改表守则站内形+改表入场券(demand paging/balloon)。
- revoke→客窗访问=翻译 fault→隔离死(默认拒绝面不动);re-grant→
  DEAD/INIT 下次 vm_create 按新旗建表(授权跨槽位复用持久)。

## §2 判据(门禁 154→157,append-only)

- [155] xvmacl 1 off+vm1 xvm -> fault 隔离死(运行时改表+TLBI 生效)
- [156] xvmacl 1 on+vmcreate/vmrun -> 复活链(boot 自报)
- [157] vmkill+xvmacl 开关收口(ACL 状态机完整)

## §3 非目标(记档)

- per-offset 窗内细粒度 ACL(整窗粒度=站 47 几何)/跨域 hcall 号面
  (现无跨域 hcall;safeshm 访问=纯映射面)/MMIO 仿真窗 ACL
