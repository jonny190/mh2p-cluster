# Ubuntu 12.04 QNX build VM

Prepared 2026-10-14. This guest matches the Ubuntu 12.04 host version in
[QNX SDP 6.6's Linux installation notes](https://www.qnx.com/developers/articles/inst_5847_9.html).
Matching the documented host reduces SDK compatibility uncertainty; it does
not prove successful licensing, compilation or mod behaviour on the car.

## Prepared on this PC

- Hyper-V VM `Audi-QNX66-Ubuntu1204`, Generation 1 (BIOS).
- Two virtual CPUs, 4 GB fixed RAM, 32 GB dynamic VHDX, Default Switch network.
- VM files under `D:\Beni\Documents\ChatGPT\Audi\tools\qnx66-vm\`.
- Official Ubuntu 12.04.5 Desktop amd64 ISO attached; VM started and console launched.
- ISO SHA256 `d1f10ea7ca59266567fa8d2522ad800e1aa063f139630f925d2484d8e169b4c2`,
  verified against [Ubuntu's checksums](https://old-releases.ubuntu.com/releases/12.04.5/SHA256SUMS).
- Creation log and result receipt are in that same VM root.

The helper refuses to replace an existing VM/disk. Creation used Windows
administrator consent and succeeded. Automatic VM startup is disabled.
Ubuntu installation and QNX installation are still pending; no account
password was generated or stored.

## Finish Ubuntu installation

In the VM console choose **Install Ubuntu**. Leave download-updates and
third-party software unchecked initially. Choose **Erase disk and install
Ubuntu** only in this VM installer: its disk is the new blank 32 GB virtual
disk. Choose your timezone, keyboard, account name and password. Restart
the guest when it finishes and remove/eject the installer disc if prompted.
If it boots the installer again, eject the DVD in this VM's Hyper-V settings
and restart the guest. Reopen the console via Hyper-V Manager > Connect.

## QNX installation

Obtain the licensed **Linux-host QNX SDP 6.6** installer via the
[legacy download link](https://www.qnx.com/download/group.html?programid=26071),
plus matching Screen/EGL development components. Download/login through
the Windows browser and transfer the installer into the guest. The Ubuntu
image does not include QNX. Follow the QNX guide's 32-bit library requirements
and activation steps. A compiler/Screen smoke build should pass first.

For archived Ubuntu package sources, use Canonical's
[old-releases archive](https://old-releases.ubuntu.com/ubuntu/) if the original
archive/security mirrors no longer provide precise packages. Keep normal
signature verification. Only this guest needs those old dependencies.

## Compile using the exported source bundle

Ubuntu 12.04's Python is older than `port_build.py` requires. Native compilation
uses the exported Bash recipe and Python 2.7-compatible checker; Java and SD
assembly stay on Windows. A source bundle is already under
`build-local/port-artifacts/ubuntu1204-source-*.tar.gz`. Regenerate on Windows
after any source change:

```powershell
& 'F:/AI/roop-unleashed/installer_files/conda/python.exe' modkit/port_build.py stage-vm
```

Transfer the printed archive to the guest, extract it into your home folder,
then source the actual SDK environment, for example:

```bash
source ~/qnx660/qnx660-env.sh
cd ~/audi-qnx-source
bash native_vm_build.sh
```

The recipe checks every source hash and requires the 6.6 environment. It builds
both ARM32 outputs and checks their embedded source fingerprints. Copy
`out/cluster`, `out/gal_cluster.so` and `out/native-build.json` into a new
Windows folder, for example `build-local/vm-native-output`. Import on Windows:

```powershell
& 'F:/AI/roop-unleashed/installer_files/conda/python.exe' modkit/port_build.py import-vm --native-directory build-local/vm-native-output
```

Then use [the matched SD assembly command](QNX_WSL_BUILD.md#matched-java-build-and-sd-assembly).
The QNX compiler has not been installed or run in this VM. No patched SD ZIP
can be produced until the genuine matched native builds exist.
