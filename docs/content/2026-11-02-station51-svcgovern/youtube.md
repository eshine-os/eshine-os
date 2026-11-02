# YouTube 分镜(站 51 服务治理面)

标题:90 行 JSON 解析器,换来一套服务治理面

## 分镜

1. **冷开场(0:00-0:40)**:对比演示——inittab 时代(写死序列)
   vs 站 51(清单驱动:改 JSON 不改代码)。
2. **清单解剖(0:40-2:30)**:/etc/services.json 三个服务
   (marker/gated/beater)逐字段讲;schema 约束设计观(方言即防线)。
3. **行走器走读(2:30-4:00)**:js_key/jsv_str/jsv_int 90 行;
   入口契约雷(漏跳 '{')实拍。
4. **治理链实机(4:00-6:00)**:boot 自动跑+svcdemo 交互复跑:
   start→gate→gate open→exited→restart→giving up→governed。
5. **边界诚实(6:00-7:00)**:检查点 vs 同步栅;RPC 注册面后弧;
   inittab 门锚与 append-only 纪律。
6. **收尾(7:00-7:30)**:门禁 145→148;下一站 ham 健康面预告。

## 备注

- 演示:make run + svcdemo 命令。
