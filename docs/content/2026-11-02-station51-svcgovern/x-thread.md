# X 8 推(站 51 服务治理面)

1/8
eshine-os station 51: service governance v0. How services start, in what order, whether they're ready, what happens when they die — moved from hardcoded boot logic into a JSON manifest. #buildinpublic #rtos

2/8
The 参考实现 reference has driver_manager: JSON config / order / pre_wait / post_wait / recover / PID registration / waitpid monitoring. We took the core faces (order + ready gate + respawn cap) and skipped the RPC registry — consumer-gated discipline.

3/8
Writing a JSON parser for one machine-generated manifest: schema constraints do the work. Flat objects, strings, ints, bools. No nesting, no escapes. ~90 lines, zero dependencies. The parser only speaks its own dialect — and that's a feature, not a gap.

4/8
Governance chain demo: marker starts first and produces a file. gated waits at the ready gate until that file exists (probe-open, 100-tick bound). beater is respawn-class: exit → restart → restart → max_restarts → "giving up". Everything on the manifest, nothing in code.

5/8
Design tension worth naming: sequential governance makes the ready gate a CHECKPOINT, not a synchronization barrier (the file already exists by the time we check). True async bring-up with a monitor pool is the later arc — the spec records the boundary honestly.

6/8
Landmine: the walker's entry contract. js_key must accept both the whole document AND a single service object — the former starts with '{'. First version didn't skip it: "manifest parse failed", one shot, one fix. Entry shapes and call shapes must be designed in pairs.

7/8
The governance domain sits BESIDE the inittab flow, not replacing it — inittab's output lines are load-bearing gate needles ([103-105]). Gates only grow (append-only discipline): new mechanism, new domain, old assertions untouched.

8/8
Smoke 18/18: boot chain runs the full governance + svcdemo re-runs it interactively (gate anchors on the re-run, boot window audited in full buffer). Gates 145→148. Next: ham health face — governance starts services, health watches them live. #systems
