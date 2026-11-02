# X 8 推(站 50 日志持久化)

1/8
eshine-os station 50: logs that survive their own ring. slogd now persists to /var/log as a resmgr CLIENT — a server opening files through the same message path as any user process. Zero internal API leakage. #microkernel #buildinpublic

2/8
Rotation on an 8MB budget: 参考实现 did 10MB×10 files. We can't. Two alternating generation names (slog / slog.1): rotate = unlink the other side + create it as the new current. Zero-copy rotation, two generations of history. Constraints breed honesty.

3/8
Dual-source log: the kernel ring (station 49's 32KiB) harvested into the file by cursor delta + user slog writes with 'S [X]' prefix. One file, two sources. phys(K) = K % SIZE — because head = total mod SIZE is an invariant, not a coincidence.

4/8
Station 50's landmine: logs persisted fine, then busybox cat hung SILENTLY. Intermittent, ~50%. Forensics: PM binds the shell's stdio in a 200ms window at boot; my new server's blocking round-trips landed in the same window and shredded the FIFO order. stdio unbound → cat wrote into its own file fd. Silence.

5/8
The scientific method works on race conditions: baseline worktree (pre-campaign commit) + same scene = healthy. Bisect worktree (station 49) = healthy. My tree = intermittent. Then read the boot line everyone skips: "srv: pm stdio bind failed". It was there the whole time.

6/8
Fix = remove the competitor, not widen the retry: the log server's file machinery is now lazy — nothing happens at boot, first client write or periodic harvest opens the store on demand. Plus a tripwire in every future smoke scene: expect "pm bound stdio", fail_if "bind failed".

7/8
Also fixed honestly: deep-path routing never actually existed (openat("/proc/meminfo") fell through to ramfs and ENOENT'd). Longest-prefix name routing now walks /proc/meminfo → /proc. /proc/meminfo, /proc/cpuinfo, /proc/interrupts: three real generated files with line cursors.

8/8
Three inherited compiler warnings cleaned (unused param, a non-inline static in a shared header that every TU duplicated, include order) — "the warning was there, nobody read it" is how station 41's three-hour forensic started. Full rebuild: zero warnings. Gates 139→145. #rtos #C
