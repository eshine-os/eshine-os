# docker 弧:pin 是对账出来的 🐳

站 63 批次 E 首站!工具链的运输形态:
🐳 Dockerfile.build:ubuntu:24.04 同源=apt 版本即 pin(gcc 13.3.0)
🔍 版本对账:容器实采 vs manifest.yaml pin 逐字相等(真抓到 dumpversion 只出"13"的雷)
📦 发布 tar:四件+run.sh+工件账+合规面=可解包即靴
🥾 自靴判据:打包脚本尾部现场重演 musl hello 全链
🧼 镜像层零仓字节:只运工具链不运代码,--user 降权跑

门禁 179→180 ✅ 下一站 ELF core 文件 🌊
