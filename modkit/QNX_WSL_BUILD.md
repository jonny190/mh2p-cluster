# Build the disconnect/reconnect port using WSL2

The installed `Ubuntu-22.04` distribution is WSL2 and was inspected on
2026-10-14. It has GCC but no QNX compiler. Ordinary Ubuntu GCC cannot build
programs for the QNX MMI. No SDK, license or WSL packages were installed.

An Ubuntu 12.04 Hyper-V build VM has since been created with the official
installer attached. Use [the VM setup guide](QNX_VM_SETUP.md) for that route.
The VM recipe supports its older Python; Java and final SD assembly stay on
Windows. Ubuntu and the QNX SDK still need installing inside the guest.

WSL2 can host QNX Linux development tools. QNX publishes a
[WSL2 guide for SDP 8.0](https://devblog.qnx.com/using-qnx-with-wsl2/), but that
is not evidence of compatibility with this QNX 6.6 ARM target. The matching
[SDP 6.6 installation guide](https://www.qnx.com/developers/articles/inst_5847_9.html)
documents Ubuntu 12.04 and 32-bit host-library requirements, not Ubuntu 22.04.
Trying its compiler in WSL2 Ubuntu 22.04 is a practical option; compatibility
must be established from the actual binaries and dependencies. If it cannot
run, use a host environment compatible with the old SDK.

## Obtain the target toolchain

Sign in to myQNX and check access to the
[legacy QNX SDP 6.6 downloads](https://www.qnx.com/download/group.html?programid=26071).
The public download link requires login; access/license entitlement for this
account has not been established. Obtain the **Linux-host** SDP 6.6 installer,
its published checksum and the applicable license. QNX lists 6.6 as an
[end-of-life product](https://www.qnx.com/download/group.html?programid=64559).

The build needs ARMv7 little-endian, QNX headers/startup objects/libraries,
Screen, EGL, GLES2 headers and zlib. If these graphics development files are
not in the installed SDP, obtain the matching graphics/Apps and Media
components through QNX's [Apps and Media 1.1 download page](https://get.qnx.com/products/apps-media/download.html).
We do not need to install a new QNX OS or BSP on the car. NvMedia/parser are
resolved dynamically from the existing MMI libraries by the source.

Do not replace this with the free SDP 8.0 toolchain. The scripts require the
6.6 environment and 32-bit ARM outputs, and compatibility of newer binaries
with the car has not been established.

## Once the installer is available

Use `wsl -d Ubuntu-22.04` to open the existing distribution. Verify the
installer checksum, copy it to the Linux filesystem and run the downloaded
installer as documented. Its filename and required 32-bit dependencies must
be taken from the actual download, rather than guessed apt package names.
Use the SDK's own license activation. There is no need to put credentials into
this repository. No installer/credentials are included in our SD package.

After installation, if the default SDK path was selected:

```bash
source ~/qnx660/qnx660-env.sh
qconfig
qcc -V
cd /mnt/d/Beni/Documents/ChatGPT/Audi/ports/mib2p-mh2p-cluster
python3 modkit/port_build.py native
python3 modkit/port_build.py validate
```

If the SDK has a different location, source its actual `qnx660-env.sh`.
The native command builds **both** `gal_cluster.so` and `cluster`, records
compiler commands and a source fingerprint, and rejects non-ARM/QNX outputs.
It links Screen/EGL/zlib; GLES functions use eglGetProcAddress rather than
linking the GLESv2 stub, matching the existing source's build instructions.
This recipe has not yet compiled with QNX: compiler/header/link errors will
need to be resolved once the toolchain is present. Compilation alone does
not establish loader, Screen or decoder behaviour on the car.

## Matched Java build and SD assembly

On Windows, from the repo, with the local Python runtime:

```powershell
& 'F:/AI/roop-unleashed/installer_files/conda/python.exe' modkit/port_build.py java
```

This builds fresh complete Java 1.4 class families against the hash-pinned
saved P2873 stock API. It excludes the unused stale Activator and mirror
placeholder with an unavailable IWidgetContext dependency. It does not mix
ECJ-generated Subsystem inners with upstream javac inners. The generated JAR
is in `build-local/port-artifacts`; do not install it alone with upstream
native binaries. Both the external and embedded config enable the lifecycle
port and initially disable map rendering for diagnostics.

With both build receipts present and matching current sources:

```powershell
& 'F:/AI/roop-unleashed/installer_files/conda/python.exe' modkit/build_audi_sd.py --stock-gal '../../research/evidence/mh2p_2026_10_11_14_08/files/mnt/app/eso/bin/apps/gal' --port-artifacts build-local/port-artifacts --output-directory build-local/sd/mib2p-lifecycle-diagnostic-sd
```

The assembler includes the patched JAR, patched GAL hook, patched player,
source config and runtime GAL_CLUSTER_LIFECYCLE=1 flag with matching receipts.
The first pass retains diagnostic MERGE=0 and map rendering disabled. It
preserves signed ModKit bytes and the verbose stock rollback. Missing native
builds, old upstream binaries, altered artifacts or stale source fingerprints
abort before producing a card folder/ZIP. No patched SD ZIP exists yet.
