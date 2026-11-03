# X 8 推(站 64 ELF core,翻案日)

1/8
eshine-os station 64: ELF core files. Eighteen stations ago we wrote "core files impossible: ramfs is read-only rodata." Station 39's copy-on-write pages quietly demolished that premise; today we go back and finish the sentence. #buildinpublic #binfmt #postmortem

2/8
The write path: rt_ramfs_core_store, a kernel-side direct write. The crash context — the dying task itself, in EL1, page tables still live, exit imminent — writes the ELF core file in one shot. Zero message round-trips. The message path is a living task's livelihood; the forensics path is the dying task's shortest path home.

3/8
The file is a real aarch64 ELF64 core: ET_CORE, EM_AARCH64, one PT_NOTE segment holding two notes. NT_PRSTATUS follows the elf_prstatus shape (siginfo + cursig + pid + gregset) — gdb/readelf-readable semantics. The gregset carries the full 272 bytes: x0-x30, sp, pc — the whole dying frame.

4/8
The second note, NT_ESHINE, is our extension: code, tick, the hardware triple (esr/elr/far), and the user frame chain. Wild read at 0x4F000000 → core says far=0x4f000000, esr=0x92000006 (EL0 data abort), pc = the faulting instruction. The core file is more honest than any console dump.

5/8
600 bytes per core — inside the one-page COW budget. Version 0 keeps PT_LOAD out (window-sized segments need multi-page COW chains — noted for a later arc). The register face alone is enough to reconstruct the death.

6/8
The verdict is two-faced on purpose. EL0 side: coreparse, a musl tool, reads the file back and prints pid/cursig/pc/sp/esr/far/frames. Host side: a gcore command hex-dumps the file over the serial line; the gate reassembles the bytes and parses the ELF independently.

7/8
Two parsers that have never met, reading the same file, deriving the same semantics — Ehdr, Phdr, NT_PRSTATUS header, sig, pid. That's the bar for "standard format": not one implementation, but two independent ones agreeing.

8/8
Gates 180→182, smoke 15/15 green (wild VA → core dumped → gcore → coreparse → prompt). Batch E closes: stations 59-64, six in a row. #buildinpublic
