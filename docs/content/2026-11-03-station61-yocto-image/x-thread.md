# X 8 推(站 61 yocto 镜像聚合,终装)

1/8
eshine-os station 61: image aggregation. The build system's deliverable is now a directory that boots itself: boot/ artifacts + a generated run.sh + manifest.json. #buildinpublic #yocto #embedded

2/8
The image dir: boot/{eshine.elf, rtos.bin, guest2.bin, ramfs.cpio} + run.sh + manifest.json. run.sh is the product entry: ./run.sh spawns the second QEMU, serial socket path as an optional arg. The manifest is an artifact ledger: version, machine, per-file sha256.

3/8
Machine definition as single source of truth: run.sh's QEMU parameters (machine/cpu/memory/load addresses) come from meta-eshine/conf/machine/eshine-qemu-a57-type1.conf's six variables — not constants scattered in scripts. Change the machine, the boot entry follows.

4/8
Terrain mine #1: the deploy class gives each recipe its own DEPLOYDIR (WORKDIR/deploy-PN). The shared surface is the sstate OUTPUT (DEPLOY_DIR_IMAGE). First version read the neighbor's artifacts from our own yard — install cannot stat, obviously.

5/8
Terrain mine #2, sneakier: a hardcoded MACHINE = "qemuarm64" in local.conf silently overrode my target machine from the environment. The tell: manifest.json said "machine": "qemuarm64". Artifacts don't lie — the setup script's generated local.conf now writes the real target machine.

6/8
Assembly trust = the entry boots itself. Gate [178] runs the image's own run.sh: second QEMU, vmcreate, vmrun, console, musl hello — from the aggregated image, not the repo tree. If the entry can't boot the product, it's a file dump, not an image.

7/8
Gate [177] is the integrity ledger: all four artifacts + run.sh + manifest present, every sha256 recomputed and matched, machine name must be the target. The sha256s double as the SBOM station's fact source (next station).

8/8
Gates 176→178. Station 34's original deploy gate stays (append-only). Next: version pinning + SBOM ledger. #buildinpublic
