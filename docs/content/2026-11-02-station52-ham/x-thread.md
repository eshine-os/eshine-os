# X 8 推(站 52 ham 健康面)

1/8
eshine-os station 52: the health face. Governance (st.51) answers "how services start"; ham answers "are they still alive". Heartbeat entities, missed condition, log action. Batch A finale. #buildinpublic #rtos

2/8
The mechanism in one line: each entity declares its expected beat distance (ham add hb1 100 = 1s at 100Hz tick). The scanner kernel task sweeps every second: 2× period silent => MISSED, warn fires. A beat => HEALTHY again. Thresholds are data, not magic numbers.

3/8
参考实现's ham has five action types (command/signal/bufqueue/waitfor/loginfo) + raise-type conditions + a self-healing daemon. We took exactly one: log. Because before deciding what to DO about a dead service, the fact of its death must be reliably RECORDED. #minimalism

4/8
Consumer-gated discipline (station 33): restart-actions arrive when a real consumer needs them — not one line earlier. The discipline has shipped 24 stations of features without shipping a single line of speculative machinery.

5/8
The gate I'm most proud of: [151] — ham's warn line flows through the elog contract ring (station 49), persists into /var/log/slog (station 50), and gets read back by busybox cat. Three stations close one loop: SPEAK, RECORD, WATCH. #observability

6/8
War story from this station: my first draft of k_ham_add had placeholder garbage code from mid-thought editing — caught in self-review, whole-file rewritten. The discipline that catches landmines BEFORE the smoke run is cheaper than the one that catches them after.

7/8
Lifecycle proof, live: healthy → sleep 4s → "ham: hb1 MISSED" → ls shows MISSED → beat → "recovered" → HEALTHY → cat /var/log/slog shows "ham hb1 missed" persisted. Smoke 16/16.

8/8
Batch A (49-52) complete: log contract → persistence → governance → health. Next batch: back to the hypervisor — debug/PMU register isolation across VMs (the "both trees diseased" finding from the reference-audit audit). #hypervisor
