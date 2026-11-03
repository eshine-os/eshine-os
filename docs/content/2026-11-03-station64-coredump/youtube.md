# YouTube 分镜(站 64 ELF core 文件)

标题:崩溃取证落盘——"不可为"的翻案(ELF core 文件)

## 分镜

1. **冷开场(0:00-0:45)**:站 46 的注释特写("ELF 核文件不可为:
   ramfs=只读")→ crashdemo 运行→"crash: core dumped /var/core/core.2"
   ——"十八站前写下的不可为,今天翻案"。
2. **前提链(0:45-2:00)**:ramfs 只读→站 39 COW 页→前提拆除;
   死者最短路径论证(消息面=活任务的活路)。
3. **格式解剖(2:00-3:40)**:600B core 的十六进制+结构图(Ehdr/Phdr/
   NT_PRSTATUS/NT_ESHINE);gregset 272B 全寄存器;NT_PRSTATUS 与
   elf_prstatus 布局对照。
4. **双解析器(3:40-5:20)**:EL0 coreparse 输出(pid=2 cursig=11
   pc/far 帧)+宿主 python 重组断言——"两个互不相识的解析器读出
   同一语义"。
5. **取证叙事(5:20-6:10)**:far=0x4f000000 与野址对齐;esr/pc 的
   法医读法——core 文件比控制台倾倒诚实。
6. **收尾(6:10-6:40)**:门禁 180→182;批次 E(59-64)六站连收
   ——深水区收官。

## 素材清单

- crashdemo→core dumped 控制台输出
- core 文件 hex(600B)+结构标注图
- coreparse 输出与宿主解析断言对照
