# 给 hypervisor 长 virtio-net,被 Linux 教育一晚 🔧

站 58 批次 C 第二站!九刀二分才收口:
🎯 OASIS spec clean-room(参考仓 virtio-net 组件 权属不许碰)
💥 LR4 一写就死(QEMU 虚拟接口实测只有 4 个 LR——VTR 探测定谳)
🔬 插桩翻案:legacy net 头=10B 不是 12B(num_buffers 要 MRG 特性位)
💀 ARP 回环递送=6.6 必死(IP 帧全链活)→ 病态语义,v0 丢弃记边界
📊 铁证:eth0 TX=42B/1 包(Linux 自己的 virtio_net 绑定)

门禁 167→171,冒烟 12/12 ✅ 下一站 cjson+zlib 📦
