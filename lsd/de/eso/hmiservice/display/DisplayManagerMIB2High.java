/*
 * Stock MH2p class (lsd.jxe, MH2p_ER_AUG35_P2711), decompiled with CFR 0.152
 * and recompiled; the bytecode of the unchanged parts was compared with the
 * unit's class (compiler idioms only). Shadowed through the mod's jar on the
 * boot classpath.
 *
 * Added for the CarPlay instrument-cluster video ("altScreen"):
 * - The cluster daemon posts the video on displayable 62 instead of Audi's
 *   map displayable 33. Taking over 33 lost the native map for good: once
 *   the video window was gone, nothing bound the map to 33 again.
 * - Contexts 80 (41, 62) and 81 (20, 41, 62) are the stock cluster map
 *   contexts 25/27 and 26/28 with the video in place of the map. While the
 *   video is active, every cluster switch to a map context goes to its video
 *   twin; switching it off restores the context the HMI last asked for. The
 *   native map keeps running on 33/58 and is back the moment the stock
 *   context is shown again.
 * - The HMI keeps seeing its own context ids: getCurrentContextID and the
 *   listener callbacks of the cluster terminal translate 80/81 back.
 * - While the video is active, refreshZoomScale (polled by KombiMapGuard)
 *   hides the Audi map's zoom scale in the cockpit status bar, see
 *   ZoomScaleHider.
 */
package de.eso.hmiservice.display;

import de.audi.atip.base.IFrameworkAccess;
import de.audi.atip.hmi.HMIService;
import de.audi.atip.hmi.event.ScreenDebugInfoEvent;
import de.audi.atip.hmi.view.IDisplayListener;
import de.eso.hmiservice.core.IDisplayManagerKombiControl;
import de.eso.hmiservice.display.DisplayManager;
import de.esolutions.fw.util.commons.Buffer;
import de.esolutions.fw.util.commons.error.DumpInfoProvider;
import java.io.PrintStream;
import org.dsi.ifc.displaymanagement.DisplayContext;

public class DisplayManagerMIB2High
extends DisplayManager
implements IDisplayListener,
IDisplayManagerKombiControl {
    private static final int DISPLAYABLE_ID_FPK_LAYER = 41;
    private static final int DISPLAYABLE_ID_LOWER_LAYER = 36;
    private static final int DISPLAYABLE_ID_MAIN_LAYER = 16;
    private final DumpInfoProvider dumpInfoProvider;
    private float mainDisplayableScaleFactor;
    private int mainDisplayableX;
    private int mainDisplayableY;
    private float lowerDisplayableScaleFactor;
    private int lowerDisplayableX;
    private int lowerDisplayableY;
    private static final boolean SHOW_DM_INFO = Boolean.getBoolean("showDisplayManagerInfo");
    int[] visibleKDKs = new int[10];
    private int displayMappingTestMode = 0;
    public static final int DISPLAYABLE_ID_ALT_VIDEO = 62;
    private static final int TERMINAL_CLUSTER = 1;
    private static final int CTX_ALT_VIDEO = 80;
    private static final int CTX_ALT_VIDEO_KDK = 81;
    private static DisplayManagerMIB2High instance;
    private volatile boolean altVideoActive;
    private int lastStockClusterContext = -1;

    public DisplayManagerMIB2High(IFrameworkAccess framework) {
        super(framework);
        this.dumpInfoProvider = new DisplayManagerProvider();
        for (int i = 0; i < this.visibleKDKs.length; ++i) {
            this.visibleKDKs[i] = -1;
        }
        instance = this;
    }

    /**
     * Shows (true) or hides (false) the cluster video on displayable 62 in
     * place of the native map. Returns false while no display manager exists.
     */
    public static boolean setAltVideoActive(boolean on) {
        DisplayManagerMIB2High dm = instance;
        if (dm == null) {
            return false;
        }
        dm.applyAltVideo(on);
        return true;
    }

    public static boolean isAltVideoActive() {
        DisplayManagerMIB2High dm = instance;
        return dm != null && dm.altVideoActive;
    }

    /**
     * Hides the zoom scale of the cockpit status bar while the video is
     * active, shows it otherwise. Returns a line to log when something
     * changed, or null.
     */
    public static String refreshZoomScale() {
        DisplayManagerMIB2High dm = instance;
        if (dm == null) {
            return null;
        }
        try {
            return ZoomScaleHider.apply(dm.framework.getHMIService(), dm.altVideoActive);
        }
        catch (Throwable t) {
            return "zoom scale: " + t;
        }
    }

    private void applyAltVideo(boolean on) {
        int stock;
        synchronized (this) {
            if (this.altVideoActive == on) {
                return;
            }
            this.altVideoActive = on;
            stock = this.lastStockClusterContext >= 0 ? this.lastStockClusterContext : this.getCurrentContextID(TERMINAL_CLUSTER);
            this.log.log(1000000, "DisplayManager#altVideo %1, cluster stock context %2", on ? 1L : 0L, (long)stock);
        }
        if (stock >= 0) {
            this.switchContext(stock, TERMINAL_CLUSTER, null);
        }
    }

    /* video twin of a stock cluster map context, or the context itself */
    private int toAltContext(int ctx) {
        if (!this.altVideoActive) {
            return ctx;
        }
        if (ctx == 25 || ctx == 27) {
            return CTX_ALT_VIDEO;
        }
        if (ctx == 26 || ctx == 28) {
            return CTX_ALT_VIDEO_KDK;
        }
        return ctx;
    }

    /* stock context a video twin stands for (33 or 58 map as last asked) */
    private int toStockContext(int ctx) {
        boolean map58 = this.lastStockClusterContext == 27 || this.lastStockClusterContext == 28;
        if (ctx == CTX_ALT_VIDEO) {
            return map58 ? 27 : 25;
        }
        if (ctx == CTX_ALT_VIDEO_KDK) {
            return map58 ? 28 : 26;
        }
        return ctx;
    }

    public synchronized int getCurrentContextID(int terminalID) {
        int ctx = super.getCurrentContextID(terminalID);
        return terminalID == TERMINAL_CLUSTER ? this.toStockContext(ctx) : ctx;
    }

    /* reports stock context ids to the HMI listener of the cluster terminal */
    private class StockContextListener
    implements IDisplayListener {
        private final IDisplayListener inner;

        StockContextListener(IDisplayListener inner) {
            this.inner = inner;
        }

        public void activeContext(int context, int terminalID) {
            this.inner.activeContext(DisplayManagerMIB2High.this.toStockContext(context), terminalID);
        }
    }

    public DumpInfoProvider getDumpInfoProvider() {
        return this.dumpInfoProvider;
    }

    protected void defineContexts() {
        this.dc = new DisplayContext[100];
        this.dc[0] = Boolean.getBoolean("SplitViewBroswerHack") ? new DisplayContext(0, new int[]{18, 16}) : new DisplayContext(0, new int[]{16});
        this.dc[11] = new DisplayContext(11, new int[]{16, 26});
        this.dc[12] = new DisplayContext(12, new int[]{16, 25});
        this.dc[14] = new DisplayContext(14, new int[]{16, 34});
        this.dc[1] = new DisplayContext(1, new int[]{16, 17});
        this.dc[2] = new DisplayContext(2, new int[]{16, 18});
        this.dc[13] = new DisplayContext(13, new int[]{16, 27});
        this.dc[15] = new DisplayContext(15, new int[]{16, 37});
        this.dc[3] = Boolean.getBoolean("SplitViewBroswerHack") ? new DisplayContext(3, new int[]{18, 16, 19}) : new DisplayContext(3, new int[]{16, 19});
        this.dc[4] = new DisplayContext(4, new int[]{16, 20, 19});
        this.dc[6] = new DisplayContext(6, new int[]{16, 20, 39});
        this.dc[5] = new DisplayContext(5, new int[]{16, 39});
        this.dc[16] = new DisplayContext(16, new int[]{16, 31});
        this.dc[17] = new DisplayContext(17, new int[]{16, 43});
        this.dc[18] = new DisplayContext(18, new int[]{16, 44});
        this.dc[19] = new DisplayContext(19, new int[]{16, 101});
        this.dc[20] = new DisplayContext(20, new int[]{16, 102});
        this.dc[9] = new DisplayContext(9, new int[]{16, 51});
        this.dc[21] = new DisplayContext(21, new int[]{16, 59});
        this.dc[22] = new DisplayContext(22, new int[]{36});
        this.dc[35] = new DisplayContext(22, new int[]{36, 1802});
        this.dc[23] = new DisplayContext(23, new int[]{41});
        this.dc[24] = new DisplayContext(24, new int[]{20, 61});
        this.dc[25] = new DisplayContext(25, new int[]{41, 33});
        this.dc[26] = new DisplayContext(26, new int[]{20, 41, 33});
        this.dc[27] = new DisplayContext(27, new int[]{41, 58});
        this.dc[28] = new DisplayContext(28, new int[]{20, 41, 58});
        this.dc[94] = new DisplayContext(94, new int[]{33});
        this.dc[95] = new DisplayContext(95, new int[]{20});
        this.dc[32] = new DisplayContext(32, new int[]{16, 36, 41});
        this.dc[33] = new DisplayContext(33, new int[]{60});
        this.dc[29] = new DisplayContext(29, new int[]{16, 36});
        this.dc[30] = new DisplayContext(30, new int[]{16, 41});
        this.dc[31] = new DisplayContext(31, new int[]{36, 41});
        this.dc[34] = new DisplayContext(34, new int[0]);
        this.dc[36] = new DisplayContext(36, new int[]{61});
        this.dc[CTX_ALT_VIDEO] = new DisplayContext(CTX_ALT_VIDEO, new int[]{41, DISPLAYABLE_ID_ALT_VIDEO});
        this.dc[CTX_ALT_VIDEO_KDK] = new DisplayContext(CTX_ALT_VIDEO_KDK, new int[]{20, 41, DISPLAYABLE_ID_ALT_VIDEO});
    }

    public void activeContext(int context, int terminalID) {
    }

    public void setKDKVisible(int kDKID, int terminalID) {
        this.log.log(1000000, "DisplayManager#setKDKVisible visibleKDK %1 terminal %2 ", (long)kDKID, (long)terminalID);
        if (this.visibleKDKs[terminalID] != kDKID) {
            this.visibleKDKs[terminalID] = kDKID;
            int currentContext = this.getCurrentContextID(terminalID);
            if (kDKID == -1) {
                this.removeKDKFromContext(currentContext);
            } else {
                this.switchContext(currentContext, terminalID, null);
            }
        }
    }

    private void removeKDKFromContext(int currentContext) {
        currentContext = this.toStockContext(currentContext);
        if (currentContext == 26) {
            this.switchContext(25, 1, null);
        } else if (currentContext == 28) {
            this.switchContext(27, 1, null);
        } else if (currentContext == 24) {
            this.switchContext(23, 1, null);
        } else {
            this.switchContext(currentContext, 1, null);
        }
    }

    public boolean isKDKVisible(int terminalID) {
        return this.visibleKDKs[terminalID] != -1;
    }

    public int getVisibleKDK(int terminalID) {
        return this.visibleKDKs[terminalID];
    }

    public synchronized void switchContext(int _cnt, int hmiTerm, IDisplayListener listener) {
        if (hmiTerm == TERMINAL_CLUSTER) {
            _cnt = this.toStockContext(_cnt);
        }
        this.log.log(10000000, "DisplayManager#switchContext visibleKDK %1 terminal %2 ", (long)this.visibleKDKs[hmiTerm], (long)hmiTerm);
        if (this.visibleKDKs[hmiTerm] == 20) {
            _cnt = this.addKDKToContext(_cnt);
        }
        _cnt = this.adjustContextForTestModes(_cnt, hmiTerm);
        if (hmiTerm == TERMINAL_CLUSTER) {
            this.lastStockClusterContext = _cnt;
            _cnt = this.toAltContext(_cnt);
            if (listener != null && !(listener instanceof StockContextListener)) {
                listener = new StockContextListener(listener);
            }
        }
        super.switchContext(_cnt, hmiTerm, listener);
        if (SHOW_DM_INFO && this.getCurrentContextID(hmiTerm) > -1) {
            this.postStatisticInfoContextGeneral(_cnt, hmiTerm);
            this.postStatisticInfoContextOpacity(_cnt, hmiTerm);
        }
    }

    private int adjustContextForTestModes(int _cnt, int hmiTerm) {
        if (_cnt == -1) {
            return _cnt;
        }
        if (hmiTerm == 0) {
            switch (this.displayMappingTestMode) {
                case 1: {
                    _cnt = 22;
                    break;
                }
                case 2: {
                    _cnt = 23;
                    break;
                }
                case 3: {
                    _cnt = 32;
                    break;
                }
            }
        } else if (hmiTerm == 8) {
            switch (this.displayMappingTestMode) {
                case 1: {
                    _cnt = -1;
                    break;
                }
                case 2: {
                    _cnt = -1;
                    break;
                }
                case 3: {
                    _cnt = -1;
                    break;
                }
                case 5: {
                    _cnt = 23;
                    break;
                }
                case 4: {
                    _cnt = 22;
                    break;
                }
                case 6: {
                    _cnt = 31;
                    break;
                }
            }
        }
        return _cnt;
    }

    protected int getInternalDisplayID(int hmiTerminal) {
        if (this.displayMappingTestMode == 0) {
            return super.getInternalDisplayID(hmiTerminal);
        }
        if (hmiTerminal == 0) {
            return 0;
        }
        if (hmiTerminal == 8) {
            switch (this.displayMappingTestMode) {
                case 3: {
                    return 0;
                }
                case 1: {
                    return 0;
                }
            }
            return 4;
        }
        if (hmiTerminal == 1) {
            switch (this.displayMappingTestMode) {
                case 3: {
                    return 0;
                }
                case 2: {
                    return 0;
                }
                case 5: {
                    return 4;
                }
                case 6: {
                    return 4;
                }
            }
            return 2;
        }
        return 0;
    }

    private void postStatisticInfoContextGeneral(int _cnt, int hmiTerm) {
        HMIService hmiService = this.framework.getHMIService();
        String[] contextInfo = this.createContextInfo(_cnt);
        ScreenDebugInfoEvent ppDebugInfoEvent = new ScreenDebugInfoEvent(hmiService.getRootWindow(0), contextInfo, 19);
        hmiService.getEventDispatcher().postEvent(ppDebugInfoEvent);
    }

    private String[] createContextInfo(int _cnt) {
        int[] displayables = this.getDisplayables(_cnt);
        if (displayables == null) {
            return new String[]{"DM: no displayables"};
        }
        String[] contextInfo = new String[displayables.length + 1];
        contextInfo[0] = new Buffer("DM: context = ").append(_cnt).toString();
        for (int i = 0; i < displayables.length; ++i) {
            contextInfo[i + 1] = new Buffer("displayable: ").append(i).append(" = ").append(displayables[i]).toString();
        }
        return contextInfo;
    }

    private void postStatisticInfoContextOpacity(int _cnt, int hmiTerm) {
        HMIService hmiService = this.framework.getHMIService();
        String[] contextInfo = this.createOpacityInfo(_cnt, hmiTerm);
        ScreenDebugInfoEvent ppDebugInfoEvent = new ScreenDebugInfoEvent(hmiService.getRootWindow(0), contextInfo, 20);
        hmiService.getEventDispatcher().postEvent(ppDebugInfoEvent);
    }

    private String[] createOpacityInfo(int _cnt, int hmiTerm) {
        int[] displayables = this.getDisplayables(_cnt);
        if (displayables == null) {
            return new String[]{"DM: no displayables"};
        }
        String[] contextInfo = new String[displayables.length + 1];
        contextInfo[0] = "DM: displayable opacity";
        for (int i = 0; i < displayables.length; ++i) {
            contextInfo[i + 1] = new Buffer().append(displayables[i]).append(" = ").append(this.getOpacity(displayables[i], hmiTerm)).toString();
        }
        return contextInfo;
    }

    private int addKDKToContext(int _cnt) {
        switch (_cnt) {
            case 25: {
                _cnt = 26;
                break;
            }
            case 27: {
                _cnt = 28;
                break;
            }
            case 23: {
                _cnt = 24;
                break;
            }
            default: {
                _cnt = 24;
            }
        }
        this.log.log(1000000, "DisplayManager#addKDKToContext new context %1 ", (long)_cnt);
        return _cnt;
    }

    protected void configureDM() {
    }

    public void setKDKOpacity(int terminal, int opacity) {
        if (this.visibleKDKs[terminal] != -1) {
            super.setOpacity(this.visibleKDKs[terminal], terminal, opacity);
        }
    }

    public void setOpacity(int cid, int displayID, int opacity) {
        super.setOpacity(cid, displayID, opacity);
        if (SHOW_DM_INFO && this.getCurrentContextID(displayID) > -1) {
            this.postStatisticInfoContextOpacity(this.getCurrentContextID(displayID), displayID);
        }
    }

    public int getDisplayMappingTestMode() {
        return this.displayMappingTestMode;
    }

    public void setDisplayMappingTestMode(int displayMappingTestMode) {
        this.log.log(10000000, "DisplayManagerMIB2High.setDisplayMappingTestMode: setting test mode %1", (long)displayMappingTestMode);
        if (this.framework.isSimulator()) {
            return;
        }
        this.displayMappingTestMode = displayMappingTestMode;
        int UPPER_DISPLAY_WIDTH = 1920;
        int UPPER_DISPLAY_HEIGHT = 1200;
        int LOWER_DISPLAY_WIDTH = 1920;
        int LOWER_DISPLAY_HEIGHT = 1200;
        int FPK_WIDTH = 1920;
        int FPK_HEIGHT = 720;
        int MAIN_WIDTH = 1540;
        int MAIN_HEIGHT = 720;
        int LOWER_WIDTH = 1280;
        int LOWER_HEIGHT = 660;
        switch (displayMappingTestMode) {
            case 3: {
                float scaleFactorX = Math.min(1.0f, (float)UPPER_DISPLAY_WIDTH / (float)(FPK_WIDTH + FPK_HEIGHT));
                float scaleFactorY = Math.min(1.0f, (float)UPPER_DISPLAY_HEIGHT / (float)(FPK_HEIGHT + MAIN_HEIGHT));
                this.lowerDisplayableScaleFactor = this.mainDisplayableScaleFactor = Math.min(scaleFactorX, scaleFactorY);
                this.mainDisplayableX = (int)((float)FPK_WIDTH * this.mainDisplayableScaleFactor);
                this.mainDisplayableY = 0;
                this.lowerDisplayableX = UPPER_DISPLAY_WIDTH - (int)((float)LOWER_WIDTH * this.lowerDisplayableScaleFactor);
                this.lowerDisplayableY = (int)((float)MAIN_HEIGHT * this.lowerDisplayableScaleFactor);
                this.setCropping(41, 0, 0, 0, FPK_WIDTH, FPK_HEIGHT, 0, 0, (int)((float)FPK_WIDTH * this.mainDisplayableScaleFactor), (int)((float)FPK_HEIGHT * this.mainDisplayableScaleFactor));
                this.setCropping(16, 0, 0, 0, MAIN_WIDTH, MAIN_HEIGHT, this.mainDisplayableX, this.mainDisplayableY, (int)((float)MAIN_WIDTH * this.mainDisplayableScaleFactor), (int)((float)MAIN_HEIGHT * this.mainDisplayableScaleFactor));
                this.setCropping(36, 0, 0, 0, LOWER_WIDTH, LOWER_HEIGHT, this.lowerDisplayableX, this.lowerDisplayableY, (int)((float)LOWER_WIDTH * this.lowerDisplayableScaleFactor), (int)((float)LOWER_HEIGHT * this.lowerDisplayableScaleFactor));
                break;
            }
            case 2: {
                float clusterDisplayableScaleFactor = Math.min(1.0f, (float)UPPER_DISPLAY_WIDTH / (float)FPK_WIDTH);
                this.setCropping(41, 0, 0, 0, FPK_WIDTH, FPK_HEIGHT, 0, 0, (int)((float)FPK_WIDTH * clusterDisplayableScaleFactor), (int)((float)FPK_HEIGHT * clusterDisplayableScaleFactor));
                break;
            }
            case 1: {
                this.lowerDisplayableX = 0;
                this.lowerDisplayableY = 0;
                this.lowerDisplayableScaleFactor = Math.min(1.0f, (float)UPPER_DISPLAY_WIDTH / (float)LOWER_WIDTH);
                this.setCropping(36, 0, 0, 0, LOWER_WIDTH, LOWER_HEIGHT, 0, 0, (int)((float)LOWER_WIDTH * this.lowerDisplayableScaleFactor), (int)((float)LOWER_HEIGHT * this.lowerDisplayableScaleFactor));
                break;
            }
            case 4: {
                this.mainDisplayableScaleFactor = 1.0f;
                this.mainDisplayableX = 0;
                this.mainDisplayableY = 0;
                this.lowerDisplayableScaleFactor = 1.0f;
                this.lowerDisplayableX = 0;
                this.lowerDisplayableY = 0;
                this.setCropping(16, 0, 0, 0, MAIN_WIDTH, MAIN_HEIGHT, 0, 0, MAIN_WIDTH, MAIN_HEIGHT);
                this.setCropping(41, 4, 0, 0, LOWER_WIDTH, LOWER_HEIGHT, 0, 0, LOWER_WIDTH, LOWER_HEIGHT);
                break;
            }
            case 5: {
                this.mainDisplayableScaleFactor = 1.0f;
                this.mainDisplayableX = 0;
                this.mainDisplayableY = 0;
                this.setCropping(16, 0, 0, 0, MAIN_WIDTH, MAIN_HEIGHT, 0, 0, MAIN_WIDTH, MAIN_HEIGHT);
                float clusterDisplayableScaleFactor = Math.min(1.0f, (float)LOWER_DISPLAY_WIDTH / (float)FPK_WIDTH);
                this.setCropping(41, 4, 0, 0, FPK_WIDTH, FPK_HEIGHT, 0, 0, (int)((float)FPK_WIDTH * clusterDisplayableScaleFactor), (int)((float)FPK_HEIGHT * clusterDisplayableScaleFactor));
                break;
            }
            case 6: {
                this.mainDisplayableScaleFactor = 1.0f;
                this.mainDisplayableX = 0;
                this.mainDisplayableY = (int)((float)FPK_HEIGHT * this.lowerDisplayableScaleFactor);
                this.setCropping(16, 0, 0, 0, 1540, 720, 0, 0, 1540, 720);
                float scaleFactorX = Math.min(1.0f, (float)LOWER_DISPLAY_WIDTH / (float)FPK_WIDTH);
                float scaleFactorY = Math.min(1.0f, (float)LOWER_DISPLAY_HEIGHT / (float)(FPK_HEIGHT + LOWER_HEIGHT));
                this.lowerDisplayableScaleFactor = Math.min(scaleFactorX, scaleFactorY);
                this.lowerDisplayableX = 0;
                this.lowerDisplayableY = 0;
                this.setCropping(41, 4, 0, 0, FPK_WIDTH, FPK_HEIGHT, 0, 0, (int)((float)FPK_WIDTH * this.lowerDisplayableScaleFactor), (int)((float)FPK_HEIGHT * this.lowerDisplayableScaleFactor));
                this.setCropping(36, 4, 0, 0, LOWER_WIDTH, LOWER_HEIGHT, this.mainDisplayableX, this.mainDisplayableY, (int)((float)LOWER_WIDTH * this.lowerDisplayableScaleFactor), (int)((float)LOWER_HEIGHT * this.lowerDisplayableScaleFactor));
                break;
            }
            default: {
                this.mainDisplayableScaleFactor = 1.0f;
                this.mainDisplayableX = 0;
                this.mainDisplayableY = 0;
                this.lowerDisplayableScaleFactor = 1.0f;
                this.lowerDisplayableX = 0;
                this.lowerDisplayableY = 0;
                this.setCropping(16, 0, 0, 0, MAIN_WIDTH, MAIN_HEIGHT, 0, 0, MAIN_WIDTH, MAIN_HEIGHT);
                this.setCropping(36, 4, 0, 0, LOWER_WIDTH, LOWER_HEIGHT, 0, 0, LOWER_WIDTH, LOWER_HEIGHT);
                this.setCropping(41, 0, 0, 0, FPK_WIDTH, FPK_HEIGHT, 0, 0, FPK_WIDTH, FPK_HEIGHT);
            }
        }
        int previousMainContext = this.getCurrentContextID(0) % 100;
        int previousLowerContext = this.getCurrentContextID(8) % 100;
        this.switchContext(previousMainContext, 0, null);
        this.switchContext(previousLowerContext, 8, null);
    }

    public float getMainDisplayableScaleFactor() {
        return this.mainDisplayableScaleFactor;
    }

    public int getMainDisplayableX() {
        return this.mainDisplayableX;
    }

    public int getMainDisplayableY() {
        return this.mainDisplayableY;
    }

    public float getLowerDisplayableScaleFactor() {
        return this.lowerDisplayableScaleFactor;
    }

    public int getLowerDisplayableX() {
        return this.lowerDisplayableX;
    }

    public int getLowerDisplayableY() {
        return this.lowerDisplayableY;
    }

    private class DisplayManagerProvider
    implements DumpInfoProvider {
        private DisplayManagerProvider() {
        }

        public String getName() {
            return "DisplayManager-Info";
        }

        public void dump(PrintStream sink, String filename) {
            this.dumpTerminalInfo(sink, 0);
            this.dumpTerminalInfo(sink, 8);
            this.dumpTerminalInfo(sink, 1);
        }

        private void dumpTerminalInfo(PrintStream sink, int terminal) {
            sink.println("DM info for terminal: " + terminal);
            if (DisplayManagerMIB2High.this.getCurrentContextID(terminal) != -1) {
                int i;
                String[] contextInfo = DisplayManagerMIB2High.this.createContextInfo(DisplayManagerMIB2High.this.getCurrentContextID(terminal));
                String[] opacityInfo = DisplayManagerMIB2High.this.createOpacityInfo(DisplayManagerMIB2High.this.getCurrentContextID(terminal), terminal);
                for (i = 0; i < contextInfo.length; ++i) {
                    sink.println(contextInfo[i]);
                }
                for (i = 0; i < opacityInfo.length; ++i) {
                    sink.println(opacityInfo[i]);
                }
            } else {
                sink.println("no context activated ");
            }
        }
    }
}
