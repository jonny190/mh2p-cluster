/*
 * Copyright (c) 2026 CrEaK (https://github.com/CrEaK)
 *
 * Licensed under CC BY-NC-SA 4.0
 * https://creativecommons.org/licenses/by-nc-sa/4.0/
 * NOT FOR COMMERCIAL USE
 */
package de.audi.app.terminalmode.smartphone.androidauto2.nav;

import java.lang.reflect.Field;

import de.audi.app.terminalmode.smartphone.carplay.CarPlayClusterIntegration;

/**
 * Decides whether the cockpit shows the CarPlay map, in altscreen mode.
 *
 * The shipped CarPlayClusterIntegration starts the cockpit picture when
 * CarPlay takes the MMI screen and stops it when the MMI takes it back
 * (radio, reverse camera, Audi menus). The phone keeps sending the cockpit
 * video in those moments (A6, 2026-11-09: frames went on while the MMI
 * showed the radio), so the cockpit can keep the CarPlay map.
 *
 * Instead of the real screen owner, this thread hands the integration
 * DEVICE while the "Carplay Map" setting is on and the hook's cockpit video
 * runs, MAINUNIT otherwise. The integration then sends "start"/"stop" to the
 * cluster daemon as before, and KombiMapGuard switches the cockpit display
 * as before. The real owner is only logged. Once a second, or at once when
 * the menu changes the setting.
 *
 * Before every start it sets the integration's mirror settings to the hook's
 * video (VIDEO_MODE, zoom 1, pan 0). The integration reads them from
 * cluster_config.json, where a carConfig entry for the car's class (the
 * Porsche entries, e.g. 5_3 "Cayenne E3", may match an Audi) or a
 * cluster_config.json on a USB stick or SD card can say "fill": the daemon
 * then mirrors the MMI screen instead of decoding the video, and zoom/pan
 * crop the video. If the fields cannot be set, nothing is started: the
 * cockpit keeps the Audi map rather than showing a mirror.
 *
 * Java 1.4 source level. Never throws out of its methods.
 */
public final class CarPlayMapSwitch implements Runnable {

    private static final int RESOURCE_MAIN_SCREEN = 1;
    private static final int OWNER_MAINUNIT = 1;
    private static final int OWNER_DEVICE = 2;
    private static final long TICK_MS = 1000L;
    static final String VIDEO_MODE = "fill capture=h264";

    private final CarPlayClusterIntegration integration;
    private volatile int owner;
    private boolean applied;
    private boolean lastVideo;
    private boolean lastEnabled;
    private boolean first = true;
    /* the mirror settings could not be set: never start (no screen mirror) */
    private boolean videoModeFailed;

    private CarPlayMapSwitch(CarPlayClusterIntegration integration) {
        this.integration = integration;
    }

    /** Starts the thread; returns null when it could not be started. */
    public static CarPlayMapSwitch start(CarPlayClusterIntegration integration) {
        CarPlayMapSetting.setLogTarget(integration);
        try {
            CarPlayMapSwitch s = new CarPlayMapSwitch(integration);
            Thread t = new Thread(s, "CarPlayMapSwitch");
            t.setDaemon(true);
            /* NORM_PRIORITY for the same reason as KombiMapGuard: a lower
             * priority thread gets no CPU while the HMI starts up. */
            t.setPriority(Thread.NORM_PRIORITY);
            t.start();
            return s;
        } catch (Throwable t) {
            CarPlayMapSetting.log("Carplay Map: switch not started, the MMI screen owner decides: " + t);
            return null;
        }
    }

    /** The real MMI screen owner from DSI; only logged. */
    public void ownerChanged(int newOwner) {
        if (newOwner != OWNER_MAINUNIT && newOwner != OWNER_DEVICE) {
            return;
        }
        if (newOwner == owner) {
            return;
        }
        owner = newOwner;
        CarPlayMapSetting.log("Carplay Map: MMI screen owner " + ownerName(newOwner)
                + ", cockpit follows the setting (" + (CarPlayMapSetting.isEnabled() ? "on" : "off") + ")");
    }

    public void run() {
        CarPlayMapSetting.log("Carplay Map: switch started, setting " + (CarPlayMapSetting.isEnabled() ? "on" : "off"));
        while (true) {
            try {
                tick();
            } catch (Throwable t) {
                CarPlayMapSetting.log("Carplay Map: tick error: " + t);
            }
            CarPlayMapSetting.await(TICK_MS);
        }
    }

    private void tick() {
        boolean video = CarPlayMapSetting.isConnected();
        boolean enabled = CarPlayMapSetting.isEnabled();
        if (first || video != lastVideo) {
            lastVideo = video;
            CarPlayMapSetting.fireMenu();
        }
        boolean want = enabled && video && !videoModeFailed;
        if (first || want != applied || enabled != lastEnabled) {
            first = false;
            lastEnabled = enabled;
            boolean change = want != applied;
            applied = want;
            CarPlayMapSetting.log("Carplay Map: cockpit " + (want ? "CarPlay map" : "Audi map")
                    + " (setting " + (enabled ? "on" : "off") + ", cockpit video " + (video ? "running" : "stopped")
                    + ", MMI screen owner " + ownerName(owner) + ")");
            if (change) {
                if (want && !forceVideoMode()) {
                    videoModeFailed = true;
                    applied = false;
                    return;
                }
                integration.onResourceChanged(RESOURCE_MAIN_SCREEN, want ? OWNER_DEVICE : OWNER_MAINUNIT);
            }
        }
    }

    /* true when the integration will start the video, never the mirror */
    private boolean forceVideoMode() {
        try {
            Class c = integration.getClass();
            Field mode = c.getDeclaredField("mirrorMode");
            Field[] f = new Field[] {
                c.getDeclaredField("mirrorZoomX"), c.getDeclaredField("mirrorZoomY"),
                c.getDeclaredField("mirrorPanX"), c.getDeclaredField("mirrorPanY")
            };
            float[] want = new float[] { 1.0f, 1.0f, 0.0f, 0.0f };
            mode.setAccessible(true);
            Object before = mode.get(integration);
            StringBuffer was = new StringBuffer();
            if (!VIDEO_MODE.equals(before)) {
                was.append(" mode=\"").append(before).append('"');
                mode.set(integration, VIDEO_MODE);
            }
            for (int i = 0; i < f.length; ++i) {
                f[i].setAccessible(true);
                float v = f[i].getFloat(integration);
                if (v != want[i]) {
                    was.append(' ').append(f[i].getName()).append('=').append(v);
                    f[i].setFloat(integration, want[i]);
                }
            }
            if (was.length() > 0) {
                CarPlayMapSetting.log("Carplay Map: config had" + was + ", using the cockpit video (" + VIDEO_MODE
                        + ", no zoom or pan)");
            }
            return true;
        } catch (Throwable t) {
            CarPlayMapSetting.log("Carplay Map: cannot set the video mode, cockpit keeps the Audi map: " + t);
            return false;
        }
    }

    private static String ownerName(int o) {
        if (o == OWNER_DEVICE) {
            return "DEVICE";
        }
        if (o == OWNER_MAINUNIT) {
            return "MAINUNIT";
        }
        return "unknown";
    }
}
