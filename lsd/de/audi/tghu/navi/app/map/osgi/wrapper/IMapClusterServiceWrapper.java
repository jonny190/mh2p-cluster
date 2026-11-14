/*
 * Stock MH2p class (lsd.jxe, MH2p_ER_AUG35_P2711), decompiled with CFR 0.152;
 * every method still forwards to the wrapped navigation service. Shadowed
 * through the mod's jar on the boot classpath. Java 1.4 source level, so the
 * generic super class is used raw.
 *
 * Added for the CarPlay instrument-cluster video ("altScreen"): the cockpit
 * reaches the navigation through this wrapper, CombiBAPListener.setMapScale
 * (steering-wheel roller) as changeZoomLevel(steps) and
 * CombiBAPListener.setMapPresentation (cockpit view) as
 * notifyViewSizeChanged(isBigStage).
 * - While DisplayManagerMIB2High shows the video, zoom steps go to the
 *   dio_manager hook (lines "zoom <steps>" appended to /tmp/altscreen_cmd)
 *   instead of the hidden Audi map.
 * - Every view change still reaches the navigation and is also written to
 *   /tmp/altscreen_view ("large" = big map with small gauges, "small" = big
 *   gauges), which the hook turns into updateViewArea for the iPhone.
 * Both are logged to /tmp/altscreen_java.log (capped).
 */
package de.audi.tghu.navi.app.map.osgi.wrapper;

import de.audi.tghu.navi.app.cluster.IMapClusterService;
import de.audi.tghu.navi.app.map.enums.ESetupOption;
import de.audi.tghu.navi.app.map.mapcontent.MapContentList;
import de.audi.tghu.navi.app.util.AbstractNaviServiceWrapper;
import de.eso.hmiservice.display.DisplayManagerMIB2High;

import java.io.FileOutputStream;

public class IMapClusterServiceWrapper
extends AbstractNaviServiceWrapper
implements IMapClusterService {
    private static final String CMD_PATH = "/tmp/altscreen_cmd";
    private static final String VIEW_PATH = "/tmp/altscreen_view";
    private static final String LOG_PATH = "/tmp/altscreen_java.log";
    private static final long LOG_LIMIT = 256L * 1024L;
    private static long logged;

    public IMapClusterServiceWrapper(IMapClusterService instance) {
        super(instance);
    }

    private IMapClusterService target() {
        return (IMapClusterService)this.get();
    }

    private static synchronized void write(String path, String text, boolean append) {
        FileOutputStream out = null;
        try {
            out = new FileOutputStream(path, append);
            out.write(text.getBytes());
        } catch (Throwable t) {
            // the cockpit must never notice a failed side channel
        } finally {
            if (out != null) {
                try {
                    out.close();
                } catch (Throwable t) {
                }
            }
        }
    }

    private static void log(String line) {
        String text = System.currentTimeMillis() + " " + line + "\n";
        synchronized (IMapClusterServiceWrapper.class) {
            if (logged > LOG_LIMIT) {
                return;
            }
            logged += text.length();
        }
        write(LOG_PATH, text, true);
    }

    public void setKombiMapSetupOption(ESetupOption option, int optionValue) {
        this.target().setKombiMapSetupOption(option, optionValue);
    }

    public void switchKombiMapToAShownContext() {
        this.target().switchKombiMapToAShownContext();
    }

    public void switchKombiMapToHiddenContext() {
        this.target().switchKombiMapToHiddenContext();
    }

    public void changeMapContentListItemSelection(MapContentList list) {
        this.target().changeMapContentListItemSelection(list);
    }

    public void selectAllMapContents(boolean checked) {
        this.target().selectAllMapContents(checked);
    }

    public void changeMapContentStaticItemSelection(ESetupOption item) {
        this.target().changeMapContentStaticItemSelection(item);
    }

    public void changeZoomLevel(int steps) {
        boolean alt = false;
        try {
            alt = DisplayManagerMIB2High.isAltVideoActive();
        } catch (Throwable t) {
            alt = false;
        }
        log("changeZoomLevel " + steps + (alt ? " -> CarPlay" : " -> Audi map"));
        if (alt) {
            write(CMD_PATH, "zoom " + steps + "\n", true);
            return;
        }
        this.target().changeZoomLevel(steps);
    }

    public void selectAllMapContents(MapContentList list, boolean checked) {
        this.target().selectAllMapContents(list, checked);
    }

    public void notifyViewSizeChanged(boolean isBigStage) {
        log("notifyViewSizeChanged " + isBigStage);
        write(VIEW_PATH, isBigStage ? "large\n" : "small\n", false);
        this.target().notifyViewSizeChanged(isBigStage);
    }

    public void suspendSetup() {
        this.target().suspendSetup();
    }

    public void resumeSetupAndStore() {
        this.target().resumeSetupAndStore();
    }
}
