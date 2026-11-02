# socket 上微内核,数据面零新 wire 🔌

站 57 批次 C 首站!AF_UNIX socket 服务上线:
🎯 八门真号直连(198/200/201/202/203/206/207/210)——musl 零
   patch,号面一个不臆造
🏗️ socksrv=第九内核服务器,/dev/sock 元名
🛤️ sendto/recvfrom 骑 fd 读写路——数据面 0 条新 wire!
🔁 connect 即建对(backlog 语义);-110 有界窗;DUP refs 计数
   (fork 雷先拆)

冒烟 10/10:"sockdemo: echo ok <ESHINE57>" ✅
门禁 164→167。下一站 virtio-net 🚀
