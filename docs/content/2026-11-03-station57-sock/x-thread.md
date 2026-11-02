# X 8 推(站 57 AF_UNIX sock,批次 C 首站)

1/8
eshine-os station 57: batch C opens with AF_UNIX sockets. Eight real syscall numbers, zero musl patches, and — the architecture point — zero new data-path wires. #buildinpublic #RTOS #microkernel

2/8
The numbers come from aarch64 asm-generic, not from memory: socket 198, bind 200, listen 201, accept 202, connect 203, sendto 206, recvfrom 207, shutdown 210. Station 42's discipline (pin numbers from musl headers) pays off: musl compiles and connects, no patch.

3/8
Why zero new data wires? fd = channel triple {coid, ocb} with an existing READ/WRITE wire contract to resmgr servers. A socket fd lives in the same table, so sendto routes like write and recvfrom routes like read (fd>=3 rule). The 9th kernel server brings only 6 control ops (12-17).

4/8
connect semantics done honestly: the accepted socket is pre-created and queued at connect time (that's what backlog IS — data can flow before accept). Full queue rejects (-12); empty accept returns -110 bounded-window face, same as pipesrv v0. POSIX infinite blocking = future arc, documented at the boundary.

5/8
shutdown breaks the pair: peer READ returns 0 forever (EOF face). CLOSE respects refs counting — because rt_fd_fork sends DUP per inherited fd, and pipe's code comments the counter-example: ignore DUP and a child's exit CLOSEs the parent's live socket. The mine is defused before anyone steps on it.

6/8
The demo avoids timing entirely: single-process loopback, connect before accept, send before recv — zero retry loops, zero sleeps. Deterministic smoke over fork-race coverage (the fork path is still covered by refs counting).

7/8
Two landmines this station, both in definition regions: a missing `vmconsole 0` step sent `run` into the host shell (one log look: prompt says eshine> not rtos>); an errno-block reshuffle deleted an existing ES_EPERM and, in the dedup pass, deleted two defines my own code used. Grep all consumers before touching shared headers.

8/8
Gates 164→167, smoke 10/10, "sockdemo: echo ok <ESHINE57>". Next station: virtio-net, OASIS spec clean-room. #buildinpublic
