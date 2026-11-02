# 权限门的最物理形态:一个页表项 🧱

站 54:共享窗补 fail-closed 门。
授权 = stage-2 表项在
撤销 = 表项清零 + 一发 TLBI 广播
越权访问 = 翻译 fault → 隔离死(默认拒绝面不动)✅

改表三纪律:dsb→TLBI(带 VMID,跨核广播)→dsb+isb——
这套是将来 demand paging/内存气球的入场券 🎫

xvmacl off→fault 隔离死→on→复活链,冒烟 21/21,门禁 154→157 ✅

#hypervisor #ARM #虚拟化 #cleanroom #buildinpublic
