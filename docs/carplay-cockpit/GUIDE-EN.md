# CarPlay map in the Virtual Cockpit – test version

This package puts the CarPlay map (Google Maps or Apple Maps) into the Virtual Cockpit, where
the Audi map normally is. Developed and tested in an A6 C8; testers report it working in a Q8
and an e-tron. **Use at your own risk.**

## Requirements

- **Check the MMI software version:** in the MMI under *Settings → System information →
  Version* (or similar). It must say `MH2p_…_P2711`. On other versions, please get in touch
  first. CarPlay keeps working there, but nothing shows up in the cockpit.
- **Wireless CarPlay** already works. Wired CarPlay is not supported.
- **SD card** (or USB stick, if your car accepts ModKit updates from a stick), formatted as
  FAT32.
- **Included:** LawPaul's *NavCompassIgnore* mod (folder `Mods/NavCompassIgnore`,
  https://github.com/LawPaul/MH2p_NavCompassIgnore). Without it the cockpit switches to the
  compass as soon as a route starts in CarPlay, and then no map is shown. If you already have
  it, it is simply installed again.

## Install

1. Unzip the **contents** of the zip file straight onto the empty card. Afterwards the top
   level holds the folders `Data`, `Logs`, `Meta`, `Mods`.
2. Check that `Mods/ClusterIntegration/enable_carplay_altscreen.txt` is there. This empty file
   turns the CarPlay map on and is already included in the package.
3. Ignition on (ideally with the engine running), insert the card. The MMI restarts and
   installs. At the end it says it will now restart normally, and the screen goes black. Pull
   the card out at that point. The MMI then starts as usual.
4. **Do not delete or overwrite the card afterwards.** It holds the backups
   (`Mods/ClusterIntegration/Update/Backup`) that are needed to uninstall.

## Update to a new version

Do not uninstall first. Unzip the contents of the new zip onto the **same** card and let it
replace existing files; do not delete `Mods/ClusterIntegration/Update/Backup` (the zip has no such
folder, so it stays). Then insert the card as for the install. The installer sees what is already
installed and only replaces what changed.

## Use

- Connect the iPhone and open Google Maps or Apple Maps in CarPlay.
- In the cockpit, choose the map view, i.e. where the Audi map usually is. After a few seconds
  the CarPlay map appears.
- The steering-wheel roller zooms the map (Google Maps and Apple Maps; Waze ignores it). Both
  cockpit layouts (large map / large gauges) work.
- The zoom is remembered: after a reconnect or an MMI restart the map zooms back to where you
  left it, a few seconds after it appears.
- The cockpit map switches between day and night together with CarPlay on the MMI.
- The iPhone is asked to show its speed limit sign on the cockpit map. On the test car none
  has appeared so far; whether Maps draws one may depend on region and route. Please report
  what you see.
- The CarPlay map stays in the cockpit while the MMI shows something else, for example the
  radio, the reverse camera or the Audi navigation.
- **Switching in the cockpit:** the cockpit menu *Navigation* has a **Smartphone Map** row
  with a checkbox below "Map" (called "Carplay Map" in earlier versions). It only appears
  while CarPlay or Android Auto shows its map in the cockpit. Checked: the phone's map.
  Unchecked: Audi map. The setting is kept across restarts.
- The Audi map's zoom display at the bottom (magnifier, "400 m"), the traffic logo at the
  bottom left ("TMC") and the satellite map logo ("Google") are hidden while the CarPlay map
  is shown. Altitude, street and compass stay.
- **Android Auto (wired):** Google Maps appears in the cockpit as well, across the full width,
  with the turn card clear of the dials in both layouts. The roller does not zoom it; Google
  Maps zooms by itself there.

## Feedback

Most helpful:

- Photos of the cockpit.
- The file `Logs/ClusterIntegration.log` from the card. Please look through it before sending,
  it contains data about your MMI.
- A short description: car, MMI version, iPhone/iOS version, and what happens or doesn't.

If the cockpit keeps showing the Audi map for more than a few seconds after start-up even
though CarPlay is running: please let me know how long it took.

## Remove

- **Turn off only the CarPlay map:** delete `enable_carplay_altscreen.txt` on the card and
  insert the card again, as for the install.
- **Remove everything:** create an empty file `uninstall.txt` in the folder
  `Mods/ClusterIntegration` and insert the card again. Afterwards the MMI is back to factory
  state as far as this mod is concerned. NavCompassIgnore stays installed; to remove it too,
  put an `uninstall.txt` in `Mods/NavCompassIgnore` as well (skip that if you had it before).

Free, not for sale (CC BY-NC-SA 4.0 / GPL-3.0). NavCompassIgnore and the MH2p SD ModKit are by
LawPaul (https://github.com/LawPaul), CC BY-NC-SA 4.0, included unchanged.

- Download (latest version): https://github.com/CrEaK/mh2p-cluster/releases
- Bugs and questions: https://github.com/CrEaK/mh2p-cluster/issues
- Project status: https://github.com/jonny190/mh2p-cluster/pull/23
