# X 6 推(站 67 ls / 修真)

1/6
A user bug report in the bsh REPL: `ls /` → "No such file or directory". But `ls /etc` works. Three symptoms, one root cause — a good anatomy thread. #buildinpublic

2/6
Layer 1 — ENOENT. Our ramfs has no "/" entity node; directories resolve as prefixes (station 40's dir-prefix handles). The under-test demands b[dl]=='/' after the prefix. For the root prefix "/", dl==1: "/bin/hello" has 'b' at b[1], never '/'. The root dir handle is judged dead on every open.

3/6
Layer 2 — truncated names. Fix the root test and `ls /` starts emitting "/tc", "/mp" — etc lost its 'e'. The enumeration name-start is c=b+dl+1: right for "/etc" (name after the slash), wrong for "/" (the slash is already IN the prefix) — the +1 eats the name's first byte.

4/6
Layer 3 — dedup failure. Now etc×4, bin×10. The first-occurrence dedup loop shares the same two checks (depth test + name start); every candidate compares against truncated history names, never matches, everything counts as first.

5/6
Three symptoms, one root: four code sites share one "direct child" test — and the test has no root special case. Fix: rf_under returns true for dl==1; depth checks exempt the root; name start becomes dl==1 ? 1 : dl+1. DT_DIR's deep-path probe uses full-path prefixes and was already correct.

6/6
After: `ls /` → tmp, etc, bin — each once, exit code 0. Scene 21/21. The lesson is old but it keeps paying: in directory logic, the root is the #1 special case — always used as somebody's prefix, almost never tested as the subject. #buildinpublic
