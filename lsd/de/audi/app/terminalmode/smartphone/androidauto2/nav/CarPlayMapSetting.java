/*
 * Copyright (c) 2026 CrEaK (https://github.com/CrEaK)
 *
 * Licensed under CC BY-NC-SA 4.0
 * https://creativecommons.org/licenses/by-nc-sa/4.0/
 * NOT FOR COMMERCIAL USE
 */
package de.audi.app.terminalmode.smartphone.androidauto2.nav;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;

import de.audi.app.terminalmode.smartphone.carplay.CarPlayClusterIntegration;

/**
 * The "Carplay Map" setting of the cockpit navigation menu, shared between
 * the menu row (de.audi.tghu.combinavi.hmi.CarPlayMapMenu, HMI side) and
 * CarPlayMapSwitch, which decides what the cockpit shows. All classes sit on
 * the HMI's boot class path, so the static state here is one per JVM.
 *
 * - enabled: the checkbox. On (the default) means the cockpit shows the
 *   CarPlay map whenever the phone sends the cockpit video, whatever the MMI
 *   screen shows; off means the Audi map. Stored in STORE ("0" or "1") so it
 *   survives a restart; /mnt/app is read-only for the HMI, /mnt/ota is where
 *   the hook keeps its session log as well.
 * - connected: the altScreen hook holds VIDEO_FLAG while the cockpit video
 *   of a CarPlay session runs.
 * - Android Auto video: KombiMapGuard reports while Android Auto renders its
 *   cluster map (setAaVideo). The same checkbox applies there: off, the
 *   guard leaves the Audi map in the cockpit. The menu row is shown while
 *   either phone sends a cockpit video (isMenuVisible).
 * - altscreen mode: the hook is installed and not switched off. Without it
 *   the CarPlay cockpit picture is the screen mirror, which only makes sense
 *   while CarPlay owns the MMI screen, so the old owner-driven path stays.
 *
 * Java 1.4 source level. Never throws.
 */
public final class CarPlayMapSetting {

    static final String VIDEO_FLAG = "/tmp/altscreen_video";
    static final String STORE = "/mnt/ota/modkit/carplay_map.txt";
    private static final String CLUSTER_DIR = "/mnt/app/eso/bin/apps/cluster/";

    private static final Object LOCK = new Object();
    private static boolean loaded;
    private static boolean enabled = true;
    private static volatile Runnable menuListener;
    private static volatile boolean aaVideo;
    private static volatile CarPlayClusterIntegration logTarget;

    private CarPlayMapSetting() {
    }

    /** true when the cockpit picture comes from the altScreen hook. */
    public static boolean isAltscreenMode() {
        return new File(CLUSTER_DIR + "altscreen.so").exists()
                && !new File(CLUSTER_DIR + "altscreen_off").exists();
    }

    /** true while the cockpit video of a CarPlay session runs. */
    public static boolean isConnected() {
        return new File(VIDEO_FLAG).exists();
    }

    /** true while Android Auto renders its cluster map (KombiMapGuard). */
    static void setAaVideo(boolean on) {
        if (aaVideo == on) {
            return;
        }
        aaVideo = on;
        fireMenu();
    }

    /** The menu row is shown while CarPlay or Android Auto sends a cockpit video. */
    public static boolean isMenuVisible() {
        return aaVideo || isConnected();
    }

    public static boolean isEnabled() {
        synchronized (LOCK) {
            load();
            return enabled;
        }
    }

    /** Called by the menu row; stores the value and wakes the switch. */
    public static void setEnabled(boolean on) {
        synchronized (LOCK) {
            load();
            if (enabled == on) {
                return;
            }
            enabled = on;
            String stored = store(on);
            log("Carplay Map: menu set " + (on ? "on" : "off") + stored);
            LOCK.notifyAll();
        }
    }

    /** Lets CarPlayMapSwitch sleep until the next tick or a menu change. */
    static void await(long ms) {
        synchronized (LOCK) {
            try {
                LOCK.wait(ms);
            } catch (InterruptedException e) {
                // next tick
            }
        }
    }

    /** The menu row registers here to be told when connected changes. */
    public static void setMenuListener(Runnable listener) {
        menuListener = listener;
    }

    static void fireMenu() {
        Runnable r = menuListener;
        if (r == null) {
            return;
        }
        try {
            r.run();
        } catch (Throwable t) {
            log("Carplay Map: menu refresh failed: " + t);
        }
    }

    static void setLogTarget(CarPlayClusterIntegration integration) {
        logTarget = integration;
    }

    /** Writes to /tmp/cluster.log once the CarPlay integration exists. */
    public static void log(String msg) {
        CarPlayClusterIntegration target = logTarget;
        if (target == null) {
            return;
        }
        try {
            target.logCluster(msg);
        } catch (Throwable t) {
            // logging must never take the HMI down
        }
    }

    private static void load() {
        if (loaded) {
            return;
        }
        loaded = true;
        FileInputStream in = null;
        try {
            File f = new File(STORE);
            if (!f.exists()) {
                return;
            }
            in = new FileInputStream(f);
            int c = in.read();
            enabled = c != '0';
        } catch (Throwable t) {
            enabled = true;
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (Throwable t) {
                    // ignore
                }
            }
        }
    }

    /* returns a note for the log line */
    private static String store(boolean on) {
        FileOutputStream out = null;
        try {
            out = new FileOutputStream(STORE);
            out.write(on ? '1' : '0');
            out.write('\n');
            return " (stored in " + STORE + ")";
        } catch (Throwable t) {
            return " (not stored, back to on after a restart: " + t + ")";
        } finally {
            if (out != null) {
                try {
                    out.close();
                } catch (Throwable t) {
                    // ignore
                }
            }
        }
    }
}
