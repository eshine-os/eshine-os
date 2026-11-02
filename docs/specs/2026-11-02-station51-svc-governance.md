# 站五十一:driver_manager 治理面 v0(服务清单治理域)

日期:2026-11-02 | 批次 A 第三站
参照:参考实现 driver_manager(JSON 配置/order/pre_wait/post_wait/recover/
PID 登记/waitpid 监控恢复/RPC 注册;agent 2 白描)——RPC 注册面 v0
不立;就绪门三型取文件探针型(服务 chid 探测型=后弧)。

## §1 v0 形状

- **治理域**:inittab 基流之外的服务域(inittab 判据行是门锚,不动);
  清单=/etc/services.json(仓内配方生成,schema 约束:平对象/串/整/
  布尔/无嵌套)。
- **最小 JSON 行走器**(rt_shell.c,EL0 .user 段单 TU):js_key 平对象
  按键取值(值=串/整/布尔;漏跳开头 '{'=首版战雷);jsv_str/jsv_int。
- **治理链**:解析清单→逐服务有序拉起("svc: <name> start")→就绪门
  检查点(wait_file 探针 open,100 拍上界;顺序语义=检查点非同步栅,
  异步拉起=后弧记档)→once=wait4 阻塞收尸(exited)/respawn=监控
  重启+上限(restart N→giving up)。boot 尾与 svcdemo 命令同体
  (门禁交互复跑;boot 窗全 buf 审计)。
- manifest:marker(once)+gated(once,wait /tmp/init40)+beater
  (respawn,max_restarts 2)。

## §2 判据(门禁 145→148,append-only)

- [146] svcdemo 有序拉起+就绪门(marker start→gated gate/gate open;
  boot 链全 buf 审计)
- [147] svcdemo respawn 监控+上限(gated exited→beater restart→
  giving up)
- [148] svcdemo 治理链收口(governed)

## §3 非目标(记档)

- RPC 注册面(rpc_register/probe)/服务 chid 就绪门/异步拉起+监控池
  /PID 登记订阅(exit_notify)/JSON 数组/嵌套/转义(schema 约束=
  仓内配方生成)
