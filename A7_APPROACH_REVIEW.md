# A7 P2873 display handover review

Reviewed 2026-10-14. Public fork: tearfalls1987/mh2p-cluster,
branch `audi-mh2p-original-contributions`, pinned at
`6fd951bd8d830d6df4c34fb1c1f0250ba7fcda20`.
Local comparison: this port at `2d6259739424d52884b6710e08fb06ce0f8501e0`.
This is a source/compatibility review. No MMI connection, vehicle test,
installation change or production code change was made.

## Conclusion

The A7 report supports preserving the OEM map source and selecting a separate
AA displayable through Audi's display composition policy. This agrees with our
earlier B6 research. The public contribution does not include the implementation
that makes this work: it cannot supply a complete replacement base for our port.
Obtain the missing DisplayManager and lifecycle changes before claiming that
its unplug restoration or layout handling can be reproduced on our A6.

The reported car is a C8 A7, `MH2P_US_AUG35_P2873`, car key `5_8`.
Our target is an A6, `MH2p_ER_AUG35_P2873`; the matching build number is useful,
but region, exact binaries, car key and effective cluster policy remain separate
compatibility checks. Published field results are the contributor's observations,
not an independent test by us.

## What is and is not public

- [Field guide](https://github.com/tearfalls1987/mh2p-cluster/blob/6fd951bd8d830d6df4c34fb1c1f0250ba7fcda20/audi-mh2p/README.md): reports AA map video in full-width and narrow-left VC layouts, native map return after unplug, and retention of video through page/layout changes.
- [Compatibility record](https://github.com/tearfalls1987/mh2p-cluster/blob/6fd951bd8d830d6df4c34fb1c1f0250ba7fcda20/audi-mh2p/COMPATIBILITY.md): attributes this to a later integration build with separate native-map 33 and AA-video 62 positioning, plus DisplayManager composition/restore and layout rearm. The implementation and exact live integration JAR are not included.
- The published JSON identifies build `v00646_tsr_buffer_main_cc3e388`. It is a captured profile for that build, not proof that the older public release understands every field.
- The published Java source is the optional wheel/menu helper. It calls the missing backend; it does not implement that backend.
- The wrappers, configuration, passive input experiment, and metadata tracer are available for inspection. The recursive Git tree is complete and contains no DisplayManager source implementing the new methods.
- [Upstream PR 17](https://github.com/fifthBro/mh2p-cluster/pull/17) is open and explicitly describes this as an incomplete reproduction guide.

## Findings that affect adoption

### P1: Display selection and unplug restoration cannot be ported from these files alone

`AudiClusterKeyDiagController.switchMap()` calls two public, zero-argument
methods through reflection: `activateClusterMirror62()` and
`restoreClusterMapFromMirror62()`. It requires a Boolean true result.
Neither method exists in our complete saved stock method index.
Reflection avoids a compile-time error; it does not provide the implementation.

`onMirrorCommand("stop...")` only clears `aaActive`, pending key state and
`nativeSelected`. It does not call `restoreClusterMapFromMirror62()`.
Therefore the public helper is not the reported disconnect/restoration fix.
The later integration's automatic restore and page/layout rearm remain
unreviewable until their actual source or a matching distributable build is supplied.

Our source still binds the native player's output to ID string `"33"` in
`src/cluster.c`. Our modified lifecycle waits for owned STOPPED, then requests
`switchKombiMapToAShownContext()`, `notifyViewSizeChanged(true)` and
`resumeSetupAndStore()` via IMapClusterService. Those requests do not implement
the A7's separate displayable composition or confirm visible native pixels.
Our modified native artifacts have not been built for QNX, and the current SD
package contains upstream binaries rather than this local lifecycle implementation.

### P1: A displayable-number substitution is insufficient

Changing `"33"` to `"62"` in the player would not add that displayable to Audi's
ordered context selection, preserve current layout geometry, or restore OEM
selection. Stock source 33/58, HMI layer 41 and KDK/subframe layers 20/61 must
remain governed by the target policy. Our B6 review established a KDK remap
that sends unfamiliar contexts to 24; an added context can disappear unless
that policy is adapted too.

The required adapter must observe current layout/context and selection session,
apply the AA composition, and handle asynchronous `showDisplayablesResult`
confirmation. On stop, deselection or failure it must restore the appropriate
current OEM composition, invalidate stale rearm callbacks, and retain our
owned-player release barrier. It must not always restore a fixed context 25.
No confirmation proves physical output by itself; parked vehicle observation
still has to establish visible AA/native images and clean handover.

### P2: The GAL supervision fix is useful, but the wrapper should not be copied wholesale

[A7 wrapper](https://github.com/tearfalls1987/mh2p-cluster/blob/6fd951bd8d830d6df4c34fb1c1f0250ba7fcda20/audi-mh2p/scripts/gal-wrapper.sh)
ends with `exec /mnt/app/eso/bin/apps/gal.real "$@"`. The real receiver replaces
the shell and keeps the supervised PID. Our current wrapper starts GAL as a
child, waits for it and logs its exit code; stopping that shell alone can leave
the child alive. An exec adaptation would preserve startup logging and daemon
checks, while forfeiting the shell's post-exit `gal_exit` log.

The A7 wrapper only enables its preload when BOTH the main hook and optional
rotary sidecar exist. Omitting the sidecar consequently disables the main hook
too. That coupling is unsuitable for our initial package. Its persistence
script also starts a daemon unconditionally; it does not provide our ownership
lock or a duplicate-start check.

### P2: Wheel controls require capability and lifecycle gating

The helper uses nav screen 4000010, OK/menu code 30, wheel code 20/event 10403,
and a 1.1-second hold. It buffers/replays short OK for native selection and
suppresses vertical wheel rotation while AA is selected on that screen.
Those behavior limits are useful design requirements, not verified A6 key codes.

The helper sets `aaActive` on prepare/start/resume COMMANDS, before a renderer
acknowledges operation. It can consume an OK hold even if the reflective map
switch fails. Before adopting it, gate interception on backend capability and
the owned session/actual selected view, integrate both key dispatch and
lifecycle callbacks, and verify native short OK, repeats, page changes,
disconnect during a hold and failed map switching. Do not infer Google Maps
zoom support from its wheel suppression.

### P1 for direct reuse: The experimental sidecar targets a different native binary

The A7 hook fingerprint is
`1e173663791bc58b4d73ed138ffa574990391cb74b6c4b15aed01a9f0ca0c7a5`;
our current SD hook is
`67fc6d69d8cc4f5b96d1ef7e207182924704330edcf846d13d14a740ed32555d`.
The sidecar reads a router pointer at module offset 0x113f0 and sends on input
channel 21. Its constructor checks the relative addresses of two symbols,
not the SHA-256 stated in the header. It has no equivalent of our receiver
generation/lifetime guard around the mutable router pointer. Do not load it
against our hook or transplant its offsets. Its author's rotary/pinch tests
did not achieve Google Maps zoom, so it is unnecessary for map restoration.

### P2: Profile and tracer are references, not drop-in fixes

The observed A7 profile is AUXILIARY display type 2, 1080p, DPI 180, insets
top/left/bottom/right 315/480/360/480 and rgType 4. Keep our first pass video
disabled; copying this entire JSON would enable video and import unverified
geometry/schema assumptions. Car key and margins need A6 observations.

The tracer opens the old `/dev/shmem/cluster_h264_shm` with O_RDONLY and maps
PROT_READ. It writes its own metadata logs, not video frames. Its raw-ring
header and magic differ from our `/mib2p_cluster_h264_v2` packet ring, so it
cannot trace our rebuilt port unchanged. Since it samples a concurrently
changing raw ring, NAL counts are diagnostic observations rather than proof
of complete frame/decoder correctness. Smearing and HUD arrows/distance remain
unresolved in the A7 report; page/layout survival does not mean dynamic
phone content-inset renegotiation is implemented.

## Verification performed

Read the pinned field/compatibility guides, JSON, wrappers, key helper,
sidecar and tracer, and checked the complete Git tree and PR scope. Compared
their requirements with our lifecycle, player binding, ring ABI, GAL wrapper,
saved native hash and B6 target-policy findings.

Compiled the unmodified published key helper with pinned ECJ 4.20,
`-source 1.4 -target 1.4`, against our normalized saved stock API. Compilation
succeeded with three deprecated-API warnings; javap confirms class major 48.
This proves the referenced compile-time key/framework API shape only, not the
missing reflective methods, integration call sites, J9 execution or car behavior.
Review inputs and compilation outputs are under ignored `build-local/a7-review/`.

## Concrete next inputs

Ask the contributor/fifthBro for the matching build revision and original
source changes implementing the two DisplayManager methods, the actual
DisplayManager class they extend, layer-62 window registration and selection,
layout/page rearm, and automatic unplug/failed-renderer restore order. Ask which
selection acknowledgements and geometry changes they observe. A public source
patch or matched distributable mod build is useful; another car's OEM files
and private firmware backup are not needed.

Adapt GAL exec supervision separately as a small reviewable change. Implement
the independent AA composition adapter only after its target policy is known,
while retaining our generation checks and native release acknowledgements.
Keep wheel controls and the rotary experiment outside the first restoration test.
