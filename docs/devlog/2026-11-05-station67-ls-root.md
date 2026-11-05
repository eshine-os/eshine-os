# 站六十七 devlog:根目录枚举修真(用户报障 ls /)

> 承接站 66(REPL 定稿)。用户实测报障:`ash> ls /` → "ls: /: No such
> file or directory"。门禁 183 步不动([183] 判据扩根枚举面)。

## 0. 定谳

站 40 目录前缀柄的三处"直接子项"判定都以 `b[dl]=='/'` 为深判锚,
根前缀 "/" 的 dl==1 使判定恒假:

1. `rf_under`(前缀匹配):"/bin/x" 的 b[1]='b'≠'/' → 根目录柄判死
   →open("/") 恒 ENOENT(报错吻合)
2. READDIR 过滤+**名起点** `c=b+dl+1`:根越过名首字(修 2 后出
   "/tc"=etc 截 'e',用户第二报障面)
3. 首现去重的同款深判+起点:去重永不中(etc×4/bin×10 全量重复)

## 1. 修真(四处根特判)

- `rf_under`:dl==1 恒真(一切挂名形都在根下)
- READDIR 过滤+去重深判:dl==1 豁免
- 两处名起点:`c = b + (dl==1 ? 1 : dl+1)`(根前缀已含斜杠)
- DT_DIR 深径探测(616 行)以完整路径作前缀,根下语义正确不动

## 2. 判据

冒烟 lsrootprobe:`ls /` → tmp/etc/bin 各一次+code=0;`ls /etc`
回归 PASS;REPL 全链(exit 回 rtos>)。场景 21/21 PASS(加 ls / 判据
两步);门 [183] 判据扩根枚举(bin+etc)。busybox ls 根枚举后 stat
子项("/bin" 目录柄 rf_prefix_any 命中=正常)零报错。
