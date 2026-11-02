# 站五十一 devlog:driver_manager 治理面 v0(服务清单治理域)

> 承接站 50(ac58414)。批次 A 第三站;门禁 **145→148**,冒烟
> 18/18 全 PASS(boot 链+svcdemo 交互复跑双验)。镜像机 未同步(用户令)。

## 0. 形态(参考实现 driver_manager 白描)

- 治理域=inittab 之外的服务域(inittab 判据行=[103-105]门锚,动不得);
- /etc/services.json 清单+最小 JSON 行走器(schema 约束:平对象/无
  嵌套/仓内配方生成——自研最小子集路线,零依赖)
- 治理链:有序拉起+就绪门检查点(探针 open,100 拍上界)+once
  wait4 收尸/respawn 监控重启+上限;boot 尾与 svcdemo 命令同体
- RPC 注册面/服务 chid 门/异步拉起监控池=后弧记档

## 1. 战雷

- **js_key 漏跳开头 '{'**(doc 顶层与服务对象两用):manifest
  parse failed 一发定谳——行走器的入口契约(可带 '{')与调用侧
  (doc/obj 双形态)必须成对想清。

## 2. 判据与验收

- 冒烟 18/18:boot 链(marker start→gated gate/gate open/exited→
  beater restart/giving up→governed)+svcdemo 复跑同链
- 门禁 [146-148] 写而不跑;help 148 步;三处同步同批
