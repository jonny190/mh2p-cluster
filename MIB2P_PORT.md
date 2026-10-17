# Audi P2873 local port

Base: jonny190/mh2p-cluster at ae1b8fe3a6a679f33dee9a93ab7adf43772c83d0.
Local branch: codex/mib2p-disconnect-reconnect. Upstream attribution and
CC BY-NC-SA 4.0 notices are retained.

This checkout is the new functional base. The separate
../mib2p-android-auto-cluster checkout retains the earlier implementation,
tests and firmware audits. Lifecycle requirements are adapted here; its GPL
implementation is not copied into the upstream CC source.

The existing builds/ archives remain unmodified upstream reference releases.
They do not contain our source changes. Published upstream source is older than
the packaged release; rebuilding it is not evidence of equivalence to that
release. This port does not install or execute any payload on an MMI.

Target: A6 C8 / MH2p_ER_AUG35_P2873. Upstream reports a working map on an
e-tron 55 with that firmware train. A6 operation and OEM display restoration
still require vehicle verification. QNX SDK and NvMedia/Screen headers are
required to build the native player.

## Disconnect/reconnect implementation

The new owner is wired into AndroidAutoClusterIntegration, reached by the
upstream Subsystem activation/deactivation/DSI-unavailable callbacks. Subsystem
deinit stops/joins it; init permits a fresh owner only after the old worker has
exited. This is an adaptation of our earlier lifecycle requirements, not a
binary-compatible replacement of our SQ5 transport or display manager hooks.

- One Java monitor serializes lifecycle and BAP producers. Disconnect invalidates
  heartbeat, delayed arrival and popup tasks, clears cached AA navigation and
  retires the player generation. Cleanup clears BAP only if we owned AA output;
  it does not clear OEM guidance at an untouched startup.
- Video loss also retires the child rather than leaving a paused splash on33.
  A later valid availability notification in the same live AA connection can
  start a fresh child after the stop/restore barrier.
- A versioned whole-file request/acknowledgement protocol replaces the lossy
  read/truncate cluster_ctl path in rebuilt AA-only players. Prepare and render
  are separate acknowledged actions, so a rapid pair cannot overwrite prepare.
- Random Java owner and daemon identities plus monotonically increasing
  generation/sequence counters reject old acknowledgements, replays and attempts
  to render a retired generation. A duplicate request cannot renew the lease.
- The daemon services control every100ms independently of new video frames.
  Java publishes every250ms. Frozen replies fail the Java owner after2s (an
  unacknowledged command after3s); absence of fresh native requests expires a5s
  lease. These are scheduled deadlines, not real-time guarantees.
- Native stop signals only its exact unreaped child, escalates from SIGTERM to
  SIGKILL after1s, then waits another1s. A failure to prove exit reports ERROR;
  it cannot acknowledge STOPPED or permit a replacement child. No idle/boot
  splash is spawned. Child exit is a process ownership barrier, not a claim
  that we have audited every Nvidia fence.
- A daemon lock prevents duplicate owners. A saved child PID blocks replacement
  while an orphan could still own33. A reservation is recorded before fork; a
  crash in the fork/PID-publication gap leaves PENDING and requires recovery,
  rather than reporting a false release. Saved PIDs are never killed blindly.
  The child checks parent loss in its decode polling loop; this cannot interrupt
  a blocked vendor call.
- OEM map recreation requests run only after an owned STOPPED acknowledgement.
  Missing/throwing IMapClusterService keeps restoration pending and blocks
  reconnect takeover. Accepted requests are logged as visibility UNCONFIRMED.
  We do not force an OEM hide/show cycle or replace its composition with our
  earlier custom-source routing assumptions.
- Helper failure, malformed/stalled status, command errors and daemon replacement
  retire the owner. They require a fresh activation, not automatic resurrection
  from another video callback. Invalid/out-of-session DSI notifications are ignored.

Configuration defaults to `enableLifecyclePort: false`, including the source
config. Map control additionally requires enableMapRender and h264. There is
no fallback to the old command file, and an old release daemon without the new
status protocol cannot start this port. No installer/archive was rebuilt.

## Verification performed on the PC

1. Production C admission/parser/lease code compiled and passed retirement,
   owner/daemon mismatch, replay, busy-child, rollback, malformed and overflow
   controls. The actual POSIX daemon adapter also passed an MSVC syntax build
   with warnings treated as errors and POSIX declarations supplied; this is not
   a QNX build, fork test or Screen test.
2. Changed Integration, Subsystem and lifecycle helpers compiled using pinned
   Eclipse4.20 with source/target1.4 against the normalized saved P2873 stock
   API and upstream Audi dependency stub. Production output is class major48.
   Nine decompiler Object casts around typed enum comparisons had to be removed
   for that source to compile. This is not stock method-body fidelity proof.
3. Java tests passed unplug/reconnect, delayed notifications, old acknowledgements,
   unavailable restoration, crash/restart, malformed/frozen status, write failure,
   clock rollback and command timeout. Tests execute our helpers, not firmware.
4. A separate compiled C peer using the production parser/admission code passed
   100 Java/native process connect/disconnect cycles. Child/display effects are
   simulated; QNX process/decoder/display behaviour is not simulated proof.
5. Actual-file tests passed read/write and malformed-size rejection. Windows JDK
   rename-over-existing fails; the test verifies that this preserves the old
   snapshot and fails closed. QNX J9 atomic replacement MUST be checked before
   enabling. A delete-before-rename workaround would reintroduce the race and
   is intentionally absent.

Reproduce in this checkout (use your installed Python/compiler paths):

```powershell
cmake -S tests -B build-local/native -G "Visual Studio 17 2022" -A x64
cmake --build build-local/native --config Debug
ctest --test-dir build-local/native -C Debug --output-on-failure
python scripts/test_lifecycle.py --stock-api ../mib2p-android-auto-cluster/android_auto/mh2p/build-java-routing-transport/stock-api.jar --compile-integration --native-peer build-local/native/Debug/lifecycle_native_peer.exe
```

## Still required before a vehicle candidate

- Close the release/source gap, build the complete AA-only class families and
  native player with the proper QNX/Nvidia headers. ECJ generates synthetic
  accessor names different from upstream javac: never combine its rebuilt
  Subsystem with old packaged/stock Subsystem inner classes. The shipped ZIPs
  and native binaries do not contain these changes.
- Verify the new receiver/player adapters on QNX: interception hits, stock
  callback ordering and mutex ordering, same-process receiver replacement,
  shared atomics, process permissions and real codec/Start ordering. The native
  implementation below is now wired and host-tested; it is not a QNX build.
  Java still cannot identify a late untagged DSI notification already delivered
  into a new connection without tagging its subscription/native boundary.
- Add authoritative decoder/presentation readiness and stream-generation/codec
  reset evidence. PREPARED/RUNNING here acknowledge process actions, not valid
  pictures, surface fences or physical pixels. The source33 binding and actual
  IMapClusterService availability/restoration need target observation. If that
  service is absent, reconnect remains blocked after takeover; no fallback has
  been invented from unsupported native calls.
- Verify J9 atomic file replacement, /tmp access, random source, process signals,
  child reaping and orphan handling on QNX. An interrupted PENDING reservation
  deliberately blocks startup until recovered; it does not authorise deleting
  the marker while a child may live.
- Validate the new stock rollback on the car. `modkit/build_audi_sd.py` prepares
  the runnable upstream diagnostic package with verified saved GAL, verbose
  rollback, the exact helper/JAR allowlist and installed Persist disable marker.
  It deliberately does not ship our uncompiled lifecycle/native changes. The
  earlier independent recovery kit remains scoped to the previous port.
- Confirm repeated unplug/reconnect, loss without final frames, helper/GAL crash,
  cold boot and NavCompass coexistence in the A6, including actual OEM map return.

The first draft is wired into this base's source and host-tested. It is not a
vehicle-ready installer or a claim of confirmed physical OEM restoration.

## Native receiver and buffer generation follow-up

The former process-wide `endpoint_installed` flag and shared fake endpoint are
replaced by distinct video/input endpoints owned by each native receiver lifetime.
MessageRouter init retires the old owner, forwards the stock init/result and
admits a fresh owner only into vacant14/21 cells. Legitimate stock registration
for either service takes priority: only our exact raw cells are detached before
forwarding the native registration. Native channel-open errors are preserved.
The source's IDs14/21 remain service IDs; actual transport channel numbers are
recorded at onChannelOpened and used for replies. They are not assumed to be14/21.

Receiver prepare/shutdown, local channel close, endpoint close and video Stop
retire ownership or clear stream admission. Endpoint close returns the native
previous-open gate, including0 on a duplicate close. Every callback checks its
receiver/endpoint/channel ownership; video callbacks capture the stream epoch
before the native monitor, rejecting ones overtaken by a restart. All custom
sends are scoped to the same owner and serialized with retirement. The stock
incoming dispatch and machine return values continue to be forwarded.

The exact saved receiver exports Start's session/configuration fields as1/2 and
Ack's session/count fields as1/2. A bounded protobuf reader tracks those values.
Duplicate live Start is idempotent; changed session/configuration or restart
after Stop gets a fresh stream epoch. Configuration delivered before the first
Start on a newly opened channel is retained. Playback restart requires fresh
codec/keyframe; changed codec does not silently relabel an existing decoder.

Frame ACKs now originate from accepted cluster video, on its own channel and
session, rather than copying the main stream's session/count. The upstream
reported a startup dependency on `80 04 08 00 10 01`; that pulse is generated
for the owned channel only while it is awaiting its first Start. Setup/focus
negotiation is retained. Source background fake-main/keepalive and in-GAL decode
are disabled because they retained global receiver pointers. Off-HMI background
behaviour may consequently differ from the upstream release; porting those
optional policies requires their own owned/joined implementation and target test.

The v2 mapping is `/mib2p_cluster_h264_v2`; the old raw mapping is not consumed.
It contains a random GAL boot nonce, writer PID, monotonic receiver/stream
counters, one random consumer identity and eight bounded512KiB packets, plus a
64KiB codec cache (about4.1MiB total). Compound metadata and byte copies use a
32-bit atomic try-lock; no Nvidia/Screen call holds it. Copies retry briefly
under contention. Persistent contention, overflow, malformed length or counter
exhaustion cannot turn into a silently broken prediction chain. Stop is an
independent atomic flag observable without another video frame or a copy lock.
Fresh decoding waits for Annex-B SPS/PPS and an IDR. Actual phone codec framing
and ordering remain target checks, especially restart on an already open channel.

The writer has a process lock and recreates the public mapping as a new inode
with a fresh nonce, never clearing an old player's existing mapped inode. The
player checks writer existence/public nonce every100ms, uses an exact-size RW
mapping, claims one immutable native connection and never adopts its replacement.
Private codec/frame copies and each H.264 Screen post check that binding. A
stop/generation change exits the child even while video is idle. Graceful exit
releases its own consumer identity; reclaim after a killed player requires OS
PID-absence proof and an exact identity/stamp recheck. PID reuse blocks reclaim.
These guards do not establish Nvidia fence completion or physical OEM pixels.

Retired endpoints remain in a bounded256-owner tombstone pool rather than being
freed while stock raw table pointers/comm work could still refer to them. Pool
exhaustion retires custom admission and needs a GAL restart; it does not steal
stock registrations. A fully audited comm drain could later allow reclamation.
No preload unload is supported while stock callbacks still reference its code.

Activation additionally requires `GAL_CLUSTER_LIFECYCLE=1` and
`GAL_CLUSTER_MERGE=1` for rebuilt native components, plus Java's existing
`enableLifecyclePort` opt-in. Defaults remain inactive. Both native components
must be rebuilt together; upstream ZIP binaries cannot use the new ABI. No
installer, SD recovery payload or car file was changed by this follow-up.

### Evidence

- Four host CTest suites pass: lifecycle, receiver/ring, actual receiver/send/player
  adapters with mocked stock/QNX/Screen effects, and two real shared-memory
  processes. The latter verifies100 generations and2,000 packet byte comparisons,
  including stale reads/releases and stop without new frames.
- Adapter tests use the real production send functions extracted at build time.
  They exercise real ARM32 IoBuffer/table byte offsets in low-address fixture
  memory, dynamic42/43 and77 channels, Start/session ACKs, duplicate Start,
  a callback blocked behind a stream restart, stock-registration priority,
  native return forwarding and rejection of old player posts. This is not an
  ARM binary, loader interception, vendor decoder or vehicle test.
- Existing Java compilation/P2873 API checks and100 Java/native lifecycle cycles
  still pass; their display effects remain simulated.
- [Native symbol audit](NATIVE_GENERATION_AUDIT.json) pins the original receiver
  hash, confirms20 native hook definitions and reads the Start/Ack field constants
  from actual ELF bytes. It does not prove interception hits or all imported ABIs.
  Reproduce with `python scripts/audit_native_generation.py` alongside our saved
  evidence and existing read-only ELF audit tool. PC test builds use pinned local
  Python through `-DPython3_EXECUTABLE=...` if CMake cannot find it.

Remaining installation prerequisites are the release/source gap, QNX build and
binding checks, authoritative decoder/presentation feedback, OEM map service/
source33 handover validation and vehicle verification of SD recovery. The missing comm/display
libraries still matter for stronger reclamation and physical restoration proof.

## Prepared upstream diagnostic card (2026-10-14)

`modkit/build_audi_sd.py --stock-gal <saved-gal> --output-directory <new-folder>`
pins the upstream Audi v0034 ZIP and the actual saved P2873 GAL hashes. It
preserves all Data/Meta bytes and six upstream native/Java binaries, overlays
our recovery scripts, defaults to diagnostic mode, verifies every output file
and ZIP entry, and writes a provenance receipt. The Windows PowerShell card
verifier and concrete car check/self-test/rollback instructions are included.
No root config override, uninstall marker, force-install or CarPlay opt-in is
shipped. Copy folder contents to an empty FAT32 card root, not the folder itself.

This provides a runnable upstream diagnostic candidate, not a vehicle build
of our disconnect/reconnect implementation. Rebuilding that still requires
matched QNX/ARM and Java outputs. The prepared card's rollback restores stock
GAL and removes this mod's added Java/native files; stock JARs and NavCompass
remain. Runtime recovery writes and early boot execution must be tested on QNX.

## Matched lifecycle package preparation (2026-10-14)

`modkit/port_build.py java` now builds a fresh AA-only JAR containing 26 Java
1.4 classes against the pinned saved P2873 API. It rebuilds the complete
Subsystem and Integration outer/inner families, event listener, lifecycle
helpers and map controller. The unused stale Activator and the mirror
placeholder requiring an unavailable IWidgetContext are excluded. External
and embedded config are identical: lifecycle enabled, rendering disabled for
the first diagnostic pass. The JAR alone is not a matched vehicle package.

`modkit/port_build.py native` is the QNX 6.6 ARMv7 build recipe for the modified
hook/player. Both artifacts embed the complete source fingerprint, and their
build receipt records their hashes and toolchain. The SDK is not installed;
the recipe has not compiled against QNX. The native prerequisite check was
run in the existing WSL2 Ubuntu-22.04 and correctly stopped for missing qcc.

`modkit/build_audi_sd.py --port-artifacts build-local/port-artifacts ...` now
assembles the actual port rather than the release: it replaces the AA JAR,
GAL hook, player and config together and adds GAL_CLUSTER_LIFECYCLE=1. It
rejects missing native receipts, old upstream binaries, wrong file sets,
modified files or stale fingerprints before creating a card directory/ZIP.
Default invocation without --port-artifacts still creates the separate
upstream diagnostic package. No patched SD ZIP has been emitted yet.

See [WSL/QNX build instructions](modkit/QNX_WSL_BUILD.md) for the official
legacy SDK download/login link and commands. WSL2 is a plausible build host;
SDP 6.6 officially documented Ubuntu 12.04, so Ubuntu 22.04 compatibility
needs testing with the actual licensed tools. No SDK installation or license
activation was performed. PC lifecycle and packaging checks do not establish
QNX loading, presentation or physical OEM map restoration.
