# VM 学会自己关机了 ⏻

站 55:PSCI 补面!
🛑 SYSTEM_OFF:干净死(无 fault 行)——关机≠事故
🔄 SYSTEM_RESET:teardown→重建→重启三段式(泵出口消费复位旗)
📋 AFFINITY_INFO:帧在不在,一查便知
💤 CPU_OFF:次核泊停,主核恒在

外加 per-VM 泵量子账(vmstat 一行)——先记账后执法 ✅

pscireset 全弧实测:关机消息→重建→自报链全绿→控制台重接管,
冒烟 25/25,门禁 157→161 ✅

#hypervisor #PSCI #ARM #cleanroom #buildinpublic
