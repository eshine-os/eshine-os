# X 8 推(站 59 生态件入仓,SBOM 第一页)

1/8
eshine-os station 59: third-party components get a ledger. 3rd/LEDGER.md: six columns (name/version/license/form/source/usage), append-only — the fact base for the station-62 SBOM. #buildinpublic #SBOM

2/8
Four rows: musl 1.2.5 (MIT, in-repo), busybox 1.36.1 (GPL-2.0, OUT of repo — the station-40 isolation precedent), cJSON 1.7.18 (MIT, in-repo), zlib 1.3.1 (Zlib, in-repo). License policy as a file, not folklore.

3/8
The discipline is written into the ledger itself: a row before a tarball, versions pinned in filenames, GPL always outside, append-only. Upgrades = new row + old row marked superseded. Fact bases are accumulated, not backfilled.

4/8
Ecosystem parts live at EL0. jsontest uses cJSON to parse the governance manifest /etc/services.json — real consumer data, deterministic output. The RTOS kernel's hand-rolled minimal JSON parser (station 51) stays — a stable hand-built core is what makes importing an ecosystem safe.

5/8
ztest does slog compress-to-disk: deflate /var/log/slog into /tmp/slog.z, read it back, inflate, byte-exact compare. Round trip through the ramfs write path while exercising real zlib.

6/8
Build recipes go direct-compile: cJSON is a single file; zlib's core source list compiled straight into libz.a. CHOST configure doesn't play nice with musl specs cross contexts — in cross builds, configure is a liability, not an asset.

7/8
Gates 171→174: [172] jsontest parse, [173] ztest inflate-match, [174] host-side ledger integrity (LEDGER.md lists 4 components + in-repo tarballs present). The ledger gate runs on the host — verify scripts don't have to live inside QEMU.

8/8
Smoke 10/10. Next station: cyclictest latency baseline — the first real-time measurement station. #buildinpublic
