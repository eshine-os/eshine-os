# 站五十四 devlog:跨域 fail-closed 门(safeshm 窗 ACL)

> 承接站 53(0a04dd8)。批次 B 第二站;门禁 **154→157**,冒烟 21/21
> 全 PASS(revoke→fault 隔离死→re-grant→复活链)。镜像机 未同步(用户令)。

## 0. 机制

- xvmacl 授权表+条目级运行时 stage-2 改表(l2_ram 单块清/置+TLBI
  IPAS2E1IS 内广播)=reference-audit §C2 改表守则的站内形——也是后续
  demand paging/balloon 的改表入场券。
- revoke 后客访问=翻译 fault→隔离死:门=映射的存废,默认拒绝面
  (guest_fault)不动——跨域 fail-closed 的最小诚实形。

## 1. 战雷(小雷二枚,解析层)

- xvmacl 前缀模式带尾随空格(dispach 的 line=分词后的单词)——
  前缀+词界守卫是 house 定式;parse_id 只吃单字符(args="1 off"
  非法)——自解析;python 嵌入残渣+链式 else-if 借位双收尾(两轮
  编译错)——嵌 heredoc 改码后必编必跑,残渣自雷。

## 2. 判据与验收

冒烟 21/21;门禁 [155-157] 写而不跑;help 157 步;三处同步同批。
