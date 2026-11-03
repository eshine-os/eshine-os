# X 8 推(站 62 SBOM 台账,完整性可执行)

1/8
eshine-os station 62: SBOM closure. Eight components, one ledger, machine-readable and script-verified — the integrity of a bill of materials is an executable property. #buildinpublic #SBOM #opensource

2/8
Two faces of one table: 3rd/LEDGER.md (station 59, for humans) gets a machine-readable twin — 3rd/manifest.yaml. Eight components: musl 1.2.5, cjson 1.7.18, zlib 1.3.1 (in-repo, pinned in artifact filenames), busybox 1.36.1, linux-image 6.6 (GPL, always out-of-repo), gcc 13.3.0, binutils 2.42, qemu 8.2.2.

3/8
Version pinning, mechanically: an in-repo artifact must exist AND its filename must contain the pinned version. musl-1.2.5.tar.gz isn't a filename, it's a promise. GPL components must be out-of-repo — license policy as machine-enforced invariant, not folklore.

4/8
sbom-check.py: five faces — fields complete, in-repo artifacts present + pinned, GPL always outside, NOTICE aggregates every component name, LICENSE stays Apache-2.0. All green or exit non-zero; gate [179] consumes exactly that exit code.

5/8
The parser is worth a note: a minimal YAML subset, zero dependencies. An SBOM — where simpler structure means more trust — should not drag in a YAML parser's supply chain to describe its own.

6/8
NOTICE grew from four lines to a ledger: eight component names + licenses, cross-checked line by line against manifest.yaml. Deliverable side (station 61's image manifest.json sha256s) + component side = both SBOM layers: "what I ship" and "what I built on."

7/8
Prebuilts are ledger entries too: gcc 13.3.0, binutils 2.42, qemu 8.2.2 — versions sampled from live --version output, not memory. The toolchain is part of the reproducibility story.

8/8
Gates 178→179, five faces green. Next: the docker arc — Dockerfile.build pinning the toolchain, CI containerization, release image. #buildinpublic
