# X 8 推(站 54 跨域 fail-closed 门)

1/8
eshine-os station 54: the fail-closed gate for the cross-VM shared window. The audit's requirement (reference-audit §C7-A3): BEFORE any cross-domain operation exists, a permission gate must. #hypervisor #buildinpublic

2/8
Design insight: the strongest permission gate IS the page-table entry. The shared window is one 2MB block descriptor in l2_ram[9]. Authorized = entry present. Revoked = entry zeroed. No new checkpoint — the mapping's existence IS the policy.

3/8
After revocation, any guest access to the window is a translation fault → the existing default-deny path (ESR/FAR print, VM isolation death). Fail-closed without adding a single check to the hot path.

4/8
Runtime stage-2 modification discipline (§C2, now our in-house form): dsb sy (entry write lands) → TLBI IPAS2E1IS (IPA | VMID<<48, inner-shareable broadcast — all cores) → dsb sy + isb. Three lines, cross-core consistent.

5/8
That discipline is the admission ticket for everything dynamic later: demand paging, memory ballooning, dirty tracking. Today it guards one permission bit; tomorrow every dynamic memory mechanism walks through the same door.

6/8
Semantics: revoke → vm1 touches the window → "fault:" + isolation death (vmls: VM_DEAD). Re-grant → vmcreate/vmrun → vm1 boots WITH the window back. The ACL is durable across slot reuse: DEAD→create rebuilds per the flag.

7/8
Small landmines worth naming: the dispatch's `line` is a single token (prefix+word-boundary is the house idiom — a trailing space in the pattern never matches); parse_id eats only single chars, so the command parses its own args. Both cost a compile+smoke round each.

8/8
Smoke 21/21: revoke → fault → DEAD → re-grant → revive chain → full ACL state machine. Gates 154→157. Next: PSCI completion (CPU_OFF/AFFINITY_INFO/SYSTEM_OFF/RESET — vm2 Linux poweroff) + per-VM quota accounting. #ARM #virtualization
