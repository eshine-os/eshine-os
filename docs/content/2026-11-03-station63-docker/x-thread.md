# X 8 推(站 63 docker 弧,pin 是对账出来的)

1/8
eshine-os station 63: the docker arc. Dockerfile.build ships the toolchain; version pins are audited, not asserted; the release tar boots itself. Same tree, any machine, same binaries. #buildinpublic #docker #reproducibility

2/8
The container: FROM ubuntu:24.04 — same distro as the host, so apt's cross-toolchain (gcc 13.3.0, binutils 2.42, qemu 8.2.2) matches the SBOM manifest's prebuilt pins by construction. Plus dtc, python3, make — everything `make -B` needs from a cold start.

3/8
"By construction" is not a claim, it's a gate. docker-build.sh extracts the pins from manifest.yaml, passes them into the container, and the container must sample its own gcc/binutils/qemu versions live — string-equal to the ledger, or red. A pin is audited, not commented.

4/8
First run caught a real one: `gcc -dumpversion` prints only the major ("13") since gcc 7 — you need `-dumpfullversion` for "13.3.0". The version audit failed its own first audit. Fixed; green.

5/8
The image ships the toolchain, not the code. The tree is bind-mounted at /src, and the container runs with --user host-uid:gid — so a full rebuild (3rd x3 + make demos + make -B + sbom-check) drops artifacts onto the host tree owned by the host user. Zero repo bytes in image layers.

6/8
Release = unpack-and-boot. make-release.sh packs the four deploy artifacts + a run.sh entry + manifest.json (sha256 ledger) + LICENSE/NOTICE into release/eshine-os-<ver>.tar.gz.

7/8
The last step of the packing script is not "done" — it's a boot verdict: unpack into a fresh temp dir, run run.sh, and replay the full musl-hello chain live. A release artifact's final manufacturing step is proving it boots.

8/8
Gates 179→180. Next: ELF core files — batch E deep water. #buildinpublic
