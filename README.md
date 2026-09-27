# Android Auto and CarPlay Cluster Integration

Bridge Android Auto navigation from Porsche PCM5 / ~~VW / Audi~~ MH2P to instrument cluster displays.

> **Donate to charity** https://www.justgiving.com/

> **Status:** Beta (beta2_candidate_90d0b76) — Testing compatibility across Porsche Cayenne, Macan, Panamera and 911

> **Download** [latest release](https://github.com/fifthBro/mh2p-cluster/raw/refs/heads/main/builds/ClusterIntegration_v0034_beta2_candidate_90d0b76.zip)

---

## Overview

Translates Android Auto and Carplay navigation events into BAP (Bedien-und Anzeigeprotokoll) messages for real-time turn-by-turn navigation on your instrument cluster. Native navigation monopolizes the cluster — this changes that.

- Intercepts Android Auto navigation events via DSI and Carplay vis RGI (thanks to https://github.com/luka-dev/mib2q-carplay-rgi/)
- Converts them to BAP protocol messages
- Sends to cluster via `CombiBAPServiceNavi`
- Overrides native navigation with configurable heartbeat (default 2s)
- Supports both Google Maps and Waze

---

## Features

- **Full navigation support** — turn arrows, distance, road names, roundabouts, highway exits
- **Real-time updates** — proximity-zone throttling (veryFar→now) with heartbeat to override native nav
- **Unit detection** — automatic km/mi and LHD/RHD detection via platform services with JSON country fallback (60+ countries)
- **JSON configuration** — runtime-configurable without recompilation; override via USB/SD card
- **Comprehensive logging** — dual-timestamp log with optional privacy hashing and external USB/SD output
- **Java 1.4 compatible** — runs on the embedded QNX/OSGI platform without modern Java features

---

## Architecture

`ClusterIntegration` is a single Java class (Java 1.4 compatible) that bridges the Android Auto DSI, Carplay RGI event stream to the Porsche instrument cluster BAP navigation protocol.

### Data Flow

```
Android Auto (Phone)
        |
    DSI Events (Navigation, Media)
        |
AndroidAutoClusterIntegration
    +-- navFocusRequestNotification   -> start/stop heartbeat, set RGType
    +-- updateNavigationNextTurnEvent -> convertToBAPManeuver -> updateManeuverDescriptor
    +-- updateNavigationNextTurnDistance -> rate-limit -> unit convert -> updateDistanceToNextManeuver
    +-- Heartbeat Timer (every 2s)   -> resend cached maneuver+distance
        |
CombiBAPServiceNavi (OSGi)
        |
Instrument Cluster
```

### Service Injection

All platform services are injected via setters after construction:

| Service | Purpose |
|---|---|
| `CombiBAPServiceNavi` | BAP cluster output — maneuver, distance, RGStatus, destination |
| `ISysServices` | Unit system detection, car clock, car type/variant detection |
| `ICarCoreServices` | Drive-side detection via `configuration().airConditionMaster().driverSideLeft()` |
| `StorageMountHandler` | Remounts USB/SD R/W for external log writing |
| `ICarStatisticsService` | Injected but not currently used in active code |

### AA Event → BAP Maneuver Translation

Maps Android Auto `eventCode` (1–19) + `turnSide` + `angle` + `num` to a BAP maneuver descriptor. Direction is a 0–255 byte (0=straight, 64=left/90°, 128=back/180°, 192=right/270°).

| Event(s) | Translation |
|---|---|
| `event=3–5` (turns) | TURN, direction from `angle` or fallback to `turnSide` |
| `event=6` (U-turn) | UTURN |
| `event=7` (on-ramp) | TURN_ON_MAINROAD, direction from `turnSide` |
| `event=8/10` (off-ramp/merge) | EXIT_LEFT or EXIT_RIGHT based on `turnSide`; side=0 → FOLLOW_STREET |
| `event=9` (fork) | FORK_2 |
| `event=14` (STRAIGHT/KEEP) | `num==1` = Waze lane-keep → FOLLOW_STREET; `num==0` or `num>1` = motorway exit → EXIT_LEFT/RIGHT |
| `event=11–13` (roundabout) | ROUNDABOUT_TRS_LEFT/RIGHT. RHD correction: `(540−angle)%360`; LHD: `(angle+180)%360`. T-junction: 180°→0° when exit ≤ 2 |
| `event=16/17` (ferry) | FERRY |
| `event=19` (destination preview) | Sets flag, falls through as FOLLOW_STREET. DESTINATION symbol shown only on `event=0 valid=2` |

### Distance Update Pipeline

1. **Throttle check** — minimum ms between sends per proximity zone (veryFar=2000ms  to now=100ms). In "always" bargraph mode, bargraph-only updates bypass the throttle.
2. **Maneuver change detection** — key = `road|eventCode|turnSide`. New maneuver resets baseline distance and bypasses throttle for first update.
3. **Distance threshold** — minimum change required per zone (100m at veryFar  to 5m at now) before text updates. Bargraph always updates for smooth fill.
4. **Unit conversion** — metric: m below 100m, tenths of km to 19.9km. Imperial: yards below 161m, tenths of miles to 9.9mi.
5. **Bargraph** — `(1 − distance/maneuverInitialDistance) × 100`, clamped 0–100.
6. **Roundabout traversal** — force distance/bargraph to 0 during CONTINUE events to suppress erratic display inside roundabout.

### Heartbeat

A daemon timer fires every `heartbeatInterval` ms (default 2000ms) while navigation is active. It resends the last cached maneuver, distance, bargraph, and destination data to continuously override the cluster's native navigation display.

### Unit & Drive-Side Detection (3-tier each)

| Priority | Units | Drive side |
|---|---|---|
| 1 (highest) | `forceImperial` config flag | `forceRHD` config flag |
| 2 | `ISysServices.units().distance().setting()` (1=km, 2=mi) | `ICarCoreServices.configuration().airConditionMaster().driverSideLeft()` |
| 3 | JSON `countries` table via locale/system properties | JSON `countries` table `rhd` field |
| 4 (default) | Metric | LHD |

### Bargraph Auto Mode — Car Variant Detection

When `bargraphMode="auto"`, the car type is queried at runtime via `sysServices.config().carType()`:

| Car | carClass | generation | Resolved mode |
|---|---|---|---|
| Cayenne E3 | 5 | 3 | `"always"` |
| 911 992 | 6 | 8 | `"always"` |
| Panamera G2 | 6 | 2 | `"always"` |
| Macan | 4 | 1 | `"distance"` |
| Unknown | any | any | `"always"` (safe default) |

Explicit JSON values always override auto-detection.

### Roundabout Latching

Android Auto sends EXIT (event=13) first, then CONTINUE (event=12) × N through the roundabout. Latching caches the exit direction on event=11/13 and reuses it for all subsequent event=12 updates, preventing the arrow from flickering. Cleared on any non-roundabout event. Configurable via `enableRoundaboutLatching`.

### Logging

Two parallel log files: **plain** (full road names) and **hashed** (road names, music metadata replaced with `[HASH_nnn]` for privacy). Each entry has a dual timestamp: QNX boot time + real car clock from `ISysServices.clock().localTime()`. When `enableExternalLogging=true`, logs are written to USB/SD if writable, with automatic fallback to `/tmp`.

---

## Installation

**Based on MH2p Mod Kit** https://lawpaul.github.io/MH2p_SD_ModKit_Site/

> **Warning:** This modifies Java files on your PCM5/MH2P unit. Only proceed if you understand the risks.

**Prerequisites:**
- Car with PCM5/MH2P (Porsche tested; Audi MH2p experimental, see [Audi MH2p](#audi-mh2p-e-tron-and-other-mlbevo-audis--experimental))
- SD card
- Android Auto phone
- Android Auto <a href="https://lawpaul.github.io/MH2p_SD_ModKit_Site/">activated </a>

**Installation:**
- Download latest release
- Format SD card as FAT32
- extract  file to the root of SD card
- start vehicle
- insert the SD card
- within few seconds, MH2p will reboot
- update installs automatically
- when update is done installing, a prompt will say "Please remove update media"
- remove update media from vehicle
- MH2p will reboot into normal mode with mods installed/uninstalled

**Build:**
Edit build script with paths in your environment and run: 
recompile.sh
or
recompile.bat

**Deploy and test:**
The easiest way is to just scp it to the target if you <a href="https://github.com/fifthBro/mh2p-ssh-access">enbaled ssh access</a>

---

## Audi MH2p (e-tron and other MLBevo Audis) — experimental

> **Status:** untested on real hardware. The port changes the install gate and packaging only: the native binaries and the config are byte-identical to the Porsche release, and the Java classes are the release classes unchanged (on Audi the installer uses a copy of the JAR with the CarPlay classes left out). Expect to iterate with logs before the cluster shows anything.

### Why it might just work

The Porsche PCM5 HMI is built on Audi's framework: every platform class this mod touches lives in `de.audi.*` packages (`CombiBAPServiceNavi` for the cluster's BAP navigation protocol, `ISysServices`, `ICarCoreServices`, the navigation bundle's `IMapClusterService` whose methods talk about the "Kombi", Audi's word for the instrument cluster), and Android Auto runs in the same `gal` process. Nothing in the Java layer or the native hooks checks for Porsche; only the ModKit install script did. The turn-by-turn path (arrows, distance, road name over BAP) has the best odds, because it only uses those shared services. The cluster video path additionally assumes the head unit drives the cluster as QNX displayable `33`. That is known for Porsche and looks right for MH2p Audis: Audi's own service training for the MIB2+ A8 (SSP 990293) states the MMI unit renders the large navigation map and the intersection maps and sends them to the virtual cockpit, and a public analysis of an Audi AUG35 P2873 firmware image reports the same `KOMBI_MAP_VIEW` displayable 33 (github.com/Dezoxy/audi-mh2p-p2873-research). The same analysis reports a byte-identical `libautoreceiver.so` and matching `AndroidAuto2Subsystem` / `CarPlayDSIManager` classes on that Audi P2873 image. None of this has been run on a car yet, and upstream has declined Audi support (issues #8 and #16), so treat every step below as the first public attempt.

Prerequisites specific to Audi: the head unit must be MH2p (MMI version string `MH2P_..`; MY2021+ e-trons are MIB3 with 3xxx firmware and out of scope), and Android Auto must already work. LawPaul's ModKit page lists the e-tron as supported with "extra steps for Apple CarPlay or Android Auto"; those steps are not public, so get Android Auto activated first and only then install this mod.

Two different things have to line up in the `gal` hook, and they carry different risk. The C++ symbol names it interposes come from Google's receiver library (`libautoreceiver.so`) and have stayed stable across VW-group generations; the installer now checks them on your unit and a missing one only makes that hook a silent pass-through. The object layouts it pokes (the message router's endpoint table, the `shared_ptr<IoBuffer>` it reads on every incoming message) were taken from the Porsche binary and can only be tested on the car; a mismatch there can crash `gal`, which is why the diagnostic first boot below exists.

### What the Audi package changes

- `install.sh` accepts `OEM=AU` in addition to `PO`. Firmware build numbers are one VAG-wide counter, so the Audi range is 26xx / 27xx / 28xx: public e-tron GE strings are `MH2p_ER_AUG35_P2711`, `P2718`, `K2716_1` and `P2873` (EU) and `MH2P_US_AUG35_P2711` / `P2873` (US). `P2873` sits in the same build window as the Porsche `P2870` / `P2874` the hooks were built against and is the best-case target; the 27xx builds are older and untested, so the installer prints a warning for any Audi build outside 28xx. Units without a TYPE code (`MH2p_ER_AU_P2873`, Q3 and A1) parse fine.
- Other OEMs and out-of-range firmware still abort. `Mods/ClusterIntegration/force_install.txt` on the SD card overrides the OEM and firmware gate for people who know what they are doing.
- A preflight check aborts before changing anything if `/mnt/app/eso/hmi/lsd/jars` or `/mnt/app/eso/bin/apps` is missing.
- Every change is recorded in `Mods/ClusterIntegration/Update/Backup/manifest.txt` with the backup copy it made (see [Backups and rollback](#backups-and-rollback)).
- On Audi, CarPlay support is opt-in via `Mods/ClusterIntegration/enable_carplay.txt`. Without it the installer uses `Update/aa_only/*_aa.jar`, a copy of the release JAR without the `carplay` package (and without the pure-stock `AndroidAuto2EventListener` copy), so the only stock class shadowed is `AndroidAuto2Subsystem` and Audi's own `CarPlayDSIManager` keeps running CarPlay; the `dio_manager` wrapper and hook are not installed either. The CarPlay hook rewrites every iAP2 Identify inside the wireless CarPlay process using Porsche-derived object layouts, and the CarPlay shadow class replaces Audi's whole CarPlay DSI manager; on an untested unit either can break CarPlay outright, and neither does anything for Android Auto. Porsche installs everything as before.
- The installer refuses to run on Audi if no stock HMI jar contains `de.audi.app.car.adi.legacy.sportchrono.StorageMountHandler`, a Porsche-side class the mod's Java code constructs (`force_install.txt` overrides).
- Per-car settings can be baked into the installed config from the card: `Mods/ClusterIntegration/config_overrides.txt` holds `key=value` lines (JSON values) that the installer applies to the top-level `config` block of `cluster_config.json` and records in the manifest; `config_overrides.example.txt` ships next to it with the useful keys commented out. Re-running without the file reinstalls the shipped config.
- Marker files next to `Update/` on the card: `force_install.txt` (skip the gate), `diag.txt` (diagnostic mode), `enable_carplay.txt` (CarPlay hook and full JAR on Audi), `config_overrides.txt` (config values), `uninstall.txt` (ModKit convention: run the uninstaller).
- `uninstall.sh` no longer checks the OEM. It only restores what the mod changed, so it is safe everywhere. `Update/rollback.sh` runs it over SSH without the ModKit.

The scripts are tracked under `modkit/`, and `modkit/build_sd_package.sh` rebuilds a flashable zip from any release zip (deriving the Android-Auto-only JAR as it goes); `modkit/check_hmi_signatures.sh` compares the shadowed classes against a unit's stock jars with `javap`:

```
modkit/build_sd_package.sh builds/ClusterIntegration_v0034_beta2_candidate_90d0b76.zip \
                           builds/ClusterIntegration_v0034_beta2_candidate_90d0b76_audi.zip
```

### Install on an Audi

Do it in two passes. The first pass installs everything in diagnostic mode: the JAR runs turn-by-turn only (`enableMapRender` is set to `false` in the installed config), the Android Auto hook loads and logs but does not inject the cluster service, and Android Auto otherwise behaves as stock. That pass answers the three questions that matter (does the shadowed Java class load on Audi firmware, do the arrows reach the virtual cockpit over BAP, does the hook resolve its symbols) with the smallest possible blast radius. The second pass goes live with the video path.

1. Extract `builds/ClusterIntegration_v0034_beta2_candidate_90d0b76_audi.zip` to a FAT32 SD card and create an empty file `Mods/ClusterIntegration/diag.txt`. For a right-hand-drive car, copy `Mods/ClusterIntegration/config_overrides.example.txt` to `config_overrides.txt` in the same folder and uncomment `forceRHD=true` (it fixes the roundabout arrow correction and Waze keep-lane side regardless of what the car reports). Android Auto must already be activated on the unit.
2. Follow the normal Installation steps above. After the reboot, pull the card and read `Logs/ClusterIntegration.log`:
   - `Head unit: release=MH2p_ER_AUG35_P2873 oem=AU type=G35 region=ER sw=2873 force=0` (your values will differ). `Aborting` means the gate refused, `ERROR` means the preflight or a backup check refused.
   - every `install:` line names a file that was written; `Backups:` names the folder holding the originals; `diag mode: ON` confirms the staged mode.
   - `hmi class check: StorageMountHandler found in: ...` means the Audi HMI ships the Porsche-side class the mod's Java code links against. If it is `NOT found`, the installer aborts on Audi before changing anything, because the shadowed `AndroidAuto2Subsystem` would die with `NoClassDefFoundError` and take Android Auto with it; `force_install.txt` overrides that if you want to try regardless (rollback works).
   - `hook symbols: 14/14 present in ...` means the Android Auto receiver on your unit exports everything the hook interposes. Fewer means the missing hooks will silently do nothing; `check skipped` means the library was not found or `grep -a` is unavailable.
3. Put the card back in, connect the phone, start navigation in Google Maps or Waze and drive a little. In this pass the virtual cockpit should already show Android Auto's turn arrows and distances through BAP (no map video yet). Then read the logs: `cluster.log` on the card must contain `SYS: Android Auto Cluster Integration Initialized`, `SYS: CombiBAPServiceNavi service AVAILABLE` and `DSI_IN:` lines; `/tmp/gal_preload.log` (SSH) must contain `gal diag mode:` and `gal preload: /mnt/app/eso/bin/apps/cluster/gal_cluster.so`; `/tmp/gal_cluster.log` must contain a `symbols: populate_sd=... sd_serialize=...` line with non-zero addresses and no crash. If Android Auto itself no longer starts, run `rollback.sh` (a `gal_exit:` with a non-zero code right after the preload line means the runtime linker refused the hook; no `SYS:` lines at all means the shadowed Java class did not load).
4. Optional but recommended before going live: the diagnostic install also copied the stock HMI jars that contain the shadowed classes to `Mods/ClusterIntegration/Update/Backup/hmi_reference/` on the card. On a PC with a JDK, run `modkit/check_hmi_signatures.sh Mods/ClusterIntegration/Update/ClusterIntegration_v0034_beta2_candidate_90d0b76.jar <that folder>` (the full JAR, so every shadowed class is compared); `MATCH` on `AndroidAuto2Subsystem` means the Audi firmware constructs the class exactly the way the mod's copy expects, `MISMATCH` shows the differing constructor and means the mod JAR will not load on this firmware without a rebuild.
5. Go live: delete `Mods/ClusterIntegration/diag.txt` from the card and run the installation again. It is idempotent: it removes the marker and puts the shipped `cluster_config.json` (video path enabled) back. Reconnect the phone and start navigation.

To get back into diagnostic mode later, put `diag.txt` back and run the installer again; it is the only way that also switches the installed config to turn-by-turn only.

### First-boot checklist

Logs are written to the SD card when `enableExternalLogging` is `true` (the default). With SSH access the same files are under `/tmp`.

| Log | Where | What to look for |
|---|---|---|
| `cluster.log` | SD root, or `/tmp/cluster.log` | `SYS: CombiBAPServiceNavi ...` lines at boot prove the JAR loaded on Audi firmware. `CAR_VARIANT: no entry for X_Y` gives your car's `carClass_generation` key. `DSI_IN:` lines prove Android Auto events reach the bridge. |
| `cluster_daemon.log` | SD root, or `/tmp/cluster_daemon.log` | `detect_disp: disp[n] id=.. size=..` lists every display the head unit drives. `display size: WxH (... auto=ok ...)` proves displayable 33 exists and reports the cluster video resolution; `auto=fail` means it was not found. `[daemon] start:` / `prepare` lines show the Java side driving the mirror. `NvMedia loaded OK` and `BeginSequence:` prove the H.264 decode path works. |
| `gal_preload.log` | `/tmp` (SSH only) | `gal preload: /mnt/app/eso/bin/apps/cluster/gal_cluster.so` proves the wrapper injected the hook into the Android Auto process (`error preload:` means the file was missing); `gal diag mode:` shows the staged mode is active. |
| `gal_cluster.log` | `/tmp` (SSH only; written in diagnostic mode, or with `GAL_CLUSTER_LOG=1` exported in the `gal` wrapper — the shipped v0034 hook reads `GAL_CLUSTER_LOG`, the repo source reads `GAL_CLUSTER_LOG_ALL`) | `symbols: populate_sd=... sd_serialize=...` with non-zero addresses proves the interposed symbols resolved. In live mode `cluster service built`, `sdresp: HYBRID` and `fake endpoint installed` prove the phone was offered the cluster display, and `cluster endpoint: onChannelOpened ch=14` proves it accepted it (the `svc_id=` printed by `handleChannelOpenReq` is layout-derived and may be wrong on Audi). Leave `GAL_CLUSTER_DECODE` unset: the in-hook decoder uses Porsche-only offsets; decoding happens in the `cluster` daemon. |

If the turn-by-turn arrows work but the map video does not, the BAP path is fine and the problem is the video path (`gal_cluster.so`, `cluster` daemon, or displayable 33). If Android Auto itself stops working after install, the shadowed `AndroidAuto2Subsystem` class does not match the Audi firmware; uninstall as described below.

### Tuning for your car

Per-car values live in `cluster_config.json`. A copy at the SD card root (`/fs/sda0/cluster_config.json`) overrides the installed one for every reader while the card is inserted, which is handy for trying zoom and pan values without reflashing. It also silently overrides everything the installer wrote, including `config_overrides.txt` values and diagnostic mode's turn-by-turn-only setting, so use one mechanism or the other (the installer warns if it finds a root copy) and remove the root file when you are done. Take the key from the `CAR_VARIANT` log line and add an entry next to the Porsche ones (v0034 schema shown; older builds used a flat `mirrorCarConfig` block):

```json
"carConfig": {
  "X_Y": {
    "name": "e-tron GE", "bargraphMode": "always", "aaClusterMode": "h264",
    "mirror":   { "mode": "fill",   "zoomX": 1.0, "zoomY": 1.0, "panX": 0.0, "panY": 0.0 },
    "gal_h264": { "codecRes": 720,  "mode": "letter", "zoomX": 1.0, "zoomY": 1.0, "panX": 0.0, "panY": 0.0 }
  }
}
```

For a right-hand-drive car, set `forceRHD=true` through `config_overrides.txt` (or `"forceRHD": true` in the `config` block): drive side is otherwise read from the car's climate-control master position, with the country table as fallback, and it decides the roundabout angle correction and the Waze keep-lane side. Units follow the car's own km/mi setting; `imperialSmallUnit="yards"` only matters if the country lookup fails (GB already maps to yards). Without a car entry the global `config.mirror` and `config.gal_h264` values apply, which is a reasonable first try. The e-tron panel is 1920x720, but the size of the map window the MMI streams to it is unknown, so expect to adjust `gal_h264.mode` / zoom / pan. Note that the native `gal_cluster.so` picks its own per-car settings by Porsche part-number prefix (it queries the head unit's part number and knows 9Y1/9YA/992/971/95B), so on an Audi it uses its global or compiled defaults regardless of the `carConfig` key; the Java side still honours your entry. Fit and framing are tuned with `gal_h264.mode`, `zoomX`/`zoomY`, `panX`/`panY` and `codecRes` (720 or 480); `cluster capture=test verbose=1 zoomX=.. panX=..` over SSH draws a calibration pattern with the same numbers.

If the daemon logs `auto=fail`, the cluster video link is not QNX displayable `33` on your car. That id is compiled into the `cluster` binary and cannot be changed with arguments (`xres=`/`yres=` only size the test pattern and the blit rectangle inside the video-sized window, and values larger than the stream break the blit), so it needs a rebuild of `src/cluster.c` with a `dispid=` argument. Enumerate the displays first with `cluster capture=display verbose=1` (one `disp[n]: id=.. size=..` line per display; Ctrl-C to stop) to learn the real id and size.

### CarPlay on Audi

Turn-by-turn from CarPlay needs the `dio_manager` hook, which is opt-in on Audi (`enable_carplay.txt`). Before enabling it, run a diagnostic-mode pass with it enabled: the wrapper exports `DIO_CLUSTER_LOG=1`, and the shipped hook writes `dio_cluster.log` to the SD card root with `IDENTIFY orig` / `IDENTIFY new` dumps and the phone's `ACCEPTED` / `REJECTED` answer. A rejected Identify on every connection means the Audi head unit already advertises route guidance or the layout differs; disable the hook by deleting `enable_carplay.txt` and running the installer again (it puts the stock `dio_manager` back) rather than leaving CarPlay broken. `uninstall.txt` removes the whole mod instead. The hook also only triggers on wireless CarPlay sessions hosted by `dio_manager` that stream location to the phone; wired sessions live in another process and never reach it.

### Known unknowns

- Whether the Audi firmware's `AndroidAuto2Subsystem` (18-argument constructor) and `CarPlayDSIManager` constructors match the shadowed classes in the JAR. A mismatch raises a linkage error in the caller that nothing catches, and Android Auto or CarPlay stay dead until the JAR is removed. The installer can only check the `StorageMountHandler` class dependency, not constructor signatures; with SSH you can compare them yourself by pulling the stock terminalmode jar from `/mnt/app/eso/hmi/lsd/jars` and running `javap -p` on it.
- Whether the Audi build of `gal` / `libautoreceiver.so` lays out `MessageRouter` and `shared_ptr<IoBuffer>` the way the Porsche one does (the installer's symbol check covers names, not layouts). A layout mismatch can crash `gal` and take Android Auto down until rollback; missing symbols only leave the cluster blank.
- Whether service ids 14 and 21, which the hook adds to the phone's service discovery, are unused in the Audi receiver's own list. A clash makes the phone reject the session (`gal` restarts in a loop). The diagnostic first boot with `GAL_CLUSTER_SD_LOG=1` dumps the stock list as `/tmp/aa_sdresp_*.bin` for checking.
- Whether the Audi cluster video link is displayable `33` and what resolution it has.
- Which Audi firmware versions carry the Android Auto receiver this hook was built against. `P2873` is the closest match to the Porsche builds; the 27xx e-tron builds are accepted by the gate but unverified.
- What LawPaul's e-tron "extra steps" for Android Auto are, and whether they change anything this mod relies on.
- The `carClass`/`generation` values for the e-tron (read them from the log and please report them back).
- The shipped `cluster`, `gal_cluster.so` and JAR in `builds/` are newer than the sources in `src/` and `lsd/` (extra log lines, the `carConfig` schema, config reading in the hook). A rebuild from this repository would regress them, so the Audi port deliberately reuses the release binaries unchanged.

### Backups and rollback

The installer never overwrites a file without first copying it to `Mods/ClusterIntegration/Update/Backup/` on the SD card and verifying the copy (size and checksum); if the backup cannot be verified, that file is skipped. What ends up where:

| Original | Kept on the head unit as | Copy on the SD card |
|---|---|---|
| `/mnt/app/eso/bin/apps/gal` | `gal.real` (the wrapper execs it) | `Backup/gal.real` and `Backup/gal.original.<timestamp>` |
| `/mnt/app/eso/bin/apps/dio_manager` | `dio_manager.real` | `Backup/dio_manager.real` and `Backup/dio_manager.original.<timestamp>` |
| an older `ClusterIntegration_*.jar` or cluster file being replaced | removed | `Backup/<name>.backup.<timestamp>` |

Everything else the mod installs (the JAR, `cluster`, the two `.so` files, `cluster_config.json`) is new on a stock unit and is listed as `add` in `Backup/manifest.txt`; rollback deletes it. The manifest has one line per action (`add`, `replace`, `swap`, `remove`, `restore`) with the backup path, appended on every run.

To roll back:

- **With the ModKit:** put an empty `uninstall.txt` in `Mods/ClusterIntegration/` on the SD card and boot with it inserted. `uninstall.sh` restores `gal` and `dio_manager` from their `.real` originals (falling back to `Backup/*.real` on the card if `.real` is missing), removes the added files (backing each one up as `*.removed.<timestamp>`), and records what it did in the manifest.
- **Over SSH:** `ksh /fs/sda0/Mods/ClusterIntegration/Update/rollback.sh` does the same without a reboot cycle (the card may be on `/fs/sdb0` or `/fs/usb0_0` on your unit).
- **Boot loop or dead Android Auto:** the ModKit runs a `failsafe.sh` from the root of the SD card early in every boot. A ready-made one ships as `Mods/ClusterIntegration/failsafe.sh` (inert there); copy it to the card's root, boot once with the card in, then delete it from the root again or it rolls back on every boot. Output goes to `Logs/ClusterIntegration-failsafe.log`.

Keep the SD card: it is the only place the timestamped history lives. A factory firmware update rewrites `/mnt/app` and removes the mod along with the `.real` files anyway.

---

## Configuration

The config file `androidauto_cluster_config.json` is embedded in the JAR. To override without reflashing, place the file on a USB stick or SD card — the app checks these paths first:

```
/fs/usb0_0/androidauto_cluster_config.json
/fs/usb1_0/androidauto_cluster_config.json
/fs/sda0/androidauto_cluster_config.json
/fs/sdb0/androidauto_cluster_config.json
```

### Logging

| Key | Type | Default | Description |
|---|---|---|---|
| `enableFileLogging` | bool | `true` | Master switch for all file logging. |
| `enableFileHashing` | bool | `false` | Write a second log with road names/music hashed for privacy. Requires `enableFileLogging`. |
| `enableExternalLogging` | bool | `false` | Write logs to USB/SD if present; falls back to `logFilePath`. |
| `logFilePath` | string | `/tmp/androidauto_cluster.log` | Plain log file path. |
| `hashedLogFilePath` | string | `/tmp/androidauto_cluster_hashed.log` | Hashed log file path. |
| `logFileSize` | int (MB) | `50` | Max file size before deletion and restart. Range: 1–100. |

### Features

| Key | Type | Default | Description |
|---|---|---|---|
| `enableRoundaboutLatching` | bool | `true` | Cache and hold roundabout exit direction across ENTER→CONTINUE→EXIT events to prevent arrow flickering. |
| `enableHeartbeat` | bool | `true` | Periodically resend last maneuver+distance to prevent native nav from overwriting AA data. |
| `heartbeatInterval` | int (ms) | `2000` | Heartbeat period. Range: 100–10000. |
| `maneuverStateMask` | int | `0` | Bitmask for `updateManeuverState` calls. `0` = disabled. bit0=state1 FOLLOW (>500m), bit1=state2 PREPARE (200–500m), bit2=state3 DISTANCE (50–200m), bit3=state4 CALL_FOR_ACTION (<50m). Falls back to nearest lower enabled state. Example: `15` (0b1111) = all states. |

### Units and Display

| Key | Type | Default | Description |
|---|---|---|---|
| `forceImperial` | bool | `false` | Always use miles/yards, ignoring car settings. For testing on metric testbenches. |
| `forceRHD` | bool | `false` | Always treat vehicle as right-hand drive, overriding all detection. |
| `metricUnitThreshold` | int (m) | `100` | Below this, display switches from km to m. Range: 50–5000. |
| `imperialUnitThreshold` | int (m) | `161` | Below this (~0.1 mi), display switches from miles to yards. Range: 50–5000. |
| `destinationDisplayDuration` | int (ms) | `5000` | How long to hold the arrival maneuver on screen before clearing. Range: 0–10000. |

### Bargraph

| Key | Type | Default | Description |
|---|---|---|---|
| `bargraphMode` | string | `"auto"` | `"auto"` — detect car variant at runtime (Cayenne E3/911 992/Panamera G2 → `"always"`, Macan → `"distance"`). `"always"` — send both text and bargraph. `"distance"` — text only. `"dynamic"` — text when far, bargraph only within `dynamicBargraphDistance`. Explicit JSON values override auto-detection. |
| `dynamicBargraphDistance` | int (m) | `100` | In `"dynamic"` mode: distance below which bargraph replaces text. Range: 10–500. |
| `dynamicBargraphPercent` | int (%) | `50` | In `"dynamic"` mode: for short maneuvers (<2x threshold), switch point as % of initial distance. Range: 10–90. |

### Dynamic Thresholds

Controls how aggressively distance updates are sent per proximity zone. Each zone has a **boundary** (upper edge in metres), **distance** threshold (minimum change to trigger a send), and **rateLimit** (minimum ms between sends). `now` has no boundary — catch-all below `veryClose.boundary`.

| Zone | Default boundary | Default distance | Default rateLimit |
|---|---|---|---|
| `veryFar` | 5000 m | 100 m | 2000 ms |
| `far` | 1000 m | 50 m | 500 ms |
| `approaching` | 500 m | 25 m | 250 ms |
| `near` | 200 m | 15 m | 200 ms |
| `close` | 100 m | 15 m | 150 ms |
| `veryClose` | 50 m | 10 m | 120 ms |
| `now` | — | 5 m | 100 ms |

```json
"dynamicThresholds": {
  "veryFar":    { "boundary": 5000, "distance": 100, "rateLimit": 2000 },
  "far":        { "boundary": 1000, "distance": 50,  "rateLimit": 500  },
  "approaching":{ "boundary": 500,  "distance": 25,  "rateLimit": 250  },
  "near":       { "boundary": 200,  "distance": 15,  "rateLimit": 200  },
  "close":      { "boundary": 100,  "distance": 15,  "rateLimit": 150  },
  "veryClose":  { "boundary": 50,   "distance": 10,  "rateLimit": 120  },
  "now":        {                    "distance": 5,   "rateLimit": 100  }
}
```

### Countries

List of country overrides for when locale cannot be detected from platform services. Countries not listed default to LHD metric. `forceImperial` and `forceRHD` override this table entirely.

```json
{ "code": "GB", "name": "United Kingdom", "rhd": true,  "imperial": true  }
{ "code": "US", "name": "United States",  "rhd": false, "imperial": true  }
{ "code": "DE", "name": "Germany",        "rhd": false, "imperial": false }
```

| Field | Type | Description |
|---|---|---|
| `code` | string | ISO 3166-1 alpha-2 country code |
| `name` | string | Human-readable name (informational only) |
| `rhd` | bool | `true` if vehicles drive on the left (right-hand drive countries) |
| `imperial` | bool | `true` if distances should be shown in miles/yards |
---

## Contributing
			
This project is the result of extensive reverse-engineering and testing on mh2p. Contributions welcome!

**Troubleshooting / bug reports**
- Format a FAT32 SD card
- Put SD in PCM before turning the ignition on
- Drive → afterwards when you turn the ignition off → pull SD card → look in the log for event that was wrong → match that event to the same one in the hashed log (time stamps can be useful)
- Share the hashed logs + expected vs actual cluster behaviour

**Known limitations**
- RHD/LHD and metric/imperial still requires testing
- Roundabouts limited (Waze lacks exit angles so they can be wrong)
- Data granularity and slow update rate limit accuracy
- Some glitches are inherent to Android Auto.

---

## Changelog

### v1_main — Beta candidate
Testing compatibility across Porsche Cayenne, Macan, Panamera and 911.

---

## Credits
- **LukaDev** — Managing screens in VW/MIB2 and CarPlay RGHI and GAL hacking: https://github.com/luka-dev/mib2q-carplay-rgi/
- **One1Blt** — Android Auto/VNC to MIB2 rendering: https://github.com/OneB1t/VcMOSTRenderMqb
- **adi961** — Turn-by-turn to VC integration: https://github.com/adi961/mib2-android-auto-vc
- **LawPaul** — MH2P Modkit: https://lawpaul.github.io/MH2p_SD_ModKit_Site/
- **litdreams10** — General platform knowledge, Android Auto testing

---

*Not affiliated with Porsche, Volkswagen, Audi, or Google. This software is free and not for commercial use.*
