package de.audi.app.terminalmode.smartphone.androidauto2.nav;

import de.audi.app.terminalmode.IContext;
import org.dsi.ifc.map.DSIMapViewerControl;
import org.osgi.framework.ServiceReference;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

/**
 * Hides the native Audi navigation map in the virtual cockpit while the
 * Android Auto cluster video is rendering, and shows it again afterwards.
 *
 * Why: on an Audi Q7 (MH2p P2711, AU_G35) the cockpit map context 25 holds
 * displayables {41, 33}. The cluster daemon posts the AA video into 33, but
 * the native navigation's cockpit map viewer (DSIMapViewerControl instance 3)
 * keeps drawing into 33 as well and stays on top. The shipped
 * AndroidAutoClusterIntegration has suspendNativeNavCluster() but never calls
 * it, and its one-time service lookups run too early at boot.
 *
 * How: a daemon thread polls AndroidAutoClusterIntegration.clusterState
 * (2 = RENDERING) once a second, looks the two navigation services up until
 * found, and on RENDERING suspends the kombi map viewer, calls
 * viewSetVisible(false) and IMapClusterService.switchKombiMapToHiddenContext()
 * (the mod's own suspendNativeNavCluster() sequence); on leaving RENDERING it
 * runs the mod's wakeNativeNavCluster() sequence. It never switches display contexts (context 25
 * is what keeps displayable 33 in the cockpit). Every call is guarded: a
 * failure is logged and Android Auto keeps running.
 *
 * Java 1.4 source level (the HMI JVM loads class-file version 48).
 */
public class KombiMapGuard implements Runnable {

    private static final String MAP_CLUSTER_CLASS = "de.audi.tghu.navi.app.cluster.IMapClusterService";
    private static final int CL_RENDERING = 2;
    private static final int KOMBI_INSTANCE = 3;
    private static final long POLL_MS = 1000L;
    private static final long LOOKUP_RETRY_MS = 5000L;
    // Fixed re-apply interval; make it configurable if the native
    // map ever flickers back between re-applies.
    private static final long REAPPLY_MS = 10000L;

    private final IContext context;
    private final AndroidAutoClusterIntegration integration;
    private Field stateField;
    private DSIMapViewerControl kombiViewer;
    private Object mapClusterService;
    private boolean hidden = false;
    private boolean suspended = false;
    private long lastLookup = 0L;
    private long lastApply = 0L;
    private boolean codingLogged = false;

    private KombiMapGuard(IContext context, AndroidAutoClusterIntegration integration) {
        this.context = context;
        this.integration = integration;
    }

    /** Starts the guard thread. Never throws. */
    public static void start(IContext context, AndroidAutoClusterIntegration integration) {
        try {
            KombiMapGuard guard = new KombiMapGuard(context, integration);
            Thread t = new Thread(guard, "KombiMapGuard");
            t.setDaemon(true);
            t.setPriority(Thread.MIN_PRIORITY);
            t.start();
        } catch (Throwable t) {
            safeLog(integration, "KOMBI_MAP: guard not started: " + t);
        }
    }

    public void run() {
        log("KOMBI_MAP: guard started");
        try {
            stateField = AndroidAutoClusterIntegration.class.getDeclaredField("clusterState");
            stateField.setAccessible(true);
        } catch (Throwable t) {
            log("KOMBI_MAP: clusterState field not found, guard stopped: " + t);
            return;
        }
        while (true) {
            try {
                tick();
            } catch (Throwable t) {
                log("KOMBI_MAP: tick error: " + t);
            }
            try {
                Thread.sleep(POLL_MS);
            } catch (InterruptedException e) {
                return;
            }
        }
    }

    private void tick() throws Exception {
        long now = System.currentTimeMillis();
        if ((kombiViewer == null || mapClusterService == null) && now - lastLookup >= LOOKUP_RETRY_MS) {
            lastLookup = now;
            lookup();
        }
        if (!codingLogged) {
            logNavMapTransmissionMode();
        }
        boolean rendering = stateField.getInt(integration) == CL_RENDERING;
        if (rendering && (!hidden || now - lastApply >= REAPPLY_MS)) {
            if (!hidden) {
                log("KOMBI_MAP: AA video rendering, hiding native cockpit map"
                        + " (viewer=" + (kombiViewer != null) + " mapCluster=" + (mapClusterService != null) + ")");
            }
            hideNative(!hidden || !suspended);
            hidden = true;
            lastApply = now;
        } else if (!rendering && hidden) {
            log("KOMBI_MAP: AA video stopped, showing native cockpit map");
            showNative();
            hidden = false;
        }
    }

    /**
     * Same calls, same order as the mod's own (never called)
     * suspendNativeNavCluster(). The viewer is suspended once per session;
     * visibility and context are re-applied every REAPPLY_MS.
     */
    private void hideNative(boolean suspend) {
        if (kombiViewer != null) {
            if (suspend) {
                try {
                    kombiViewer.suspendMapViewer();
                    suspended = true;
                    log("KOMBI_MAP: native kombi map viewer suspended");
                } catch (Throwable t) {
                    log("KOMBI_MAP: suspendMapViewer failed: " + t);
                }
            }
            try {
                kombiViewer.viewSetVisible(false);
            } catch (Throwable t) {
                log("KOMBI_MAP: viewSetVisible(false) failed: " + t);
            }
        }
        callMapCluster("switchKombiMapToHiddenContext");
    }

    /** Same calls, same order as the mod's own wakeNativeNavCluster(). */
    private void showNative() {
        if (kombiViewer != null) {
            try {
                if (suspended) {
                    kombiViewer.wakeupMapViewer();
                }
                kombiViewer.viewSetVisible(true);
                kombiViewer.viewFreeze(false);
            } catch (Throwable t) {
                log("KOMBI_MAP: waking native viewer failed: " + t);
            }
        }
        suspended = false;
        callMapCluster("switchKombiMapToHiddenContext");
        callMapCluster("switchKombiMapToAShownContext");
    }

    private void callMapCluster(String method) {
        if (mapClusterService == null) {
            return;
        }
        try {
            Class c = Class.forName(MAP_CLUSTER_CLASS);
            Method m = c.getMethod(method, new Class[0]);
            m.invoke(mapClusterService, new Object[0]);
        } catch (Throwable t) {
            log("KOMBI_MAP: " + method + " failed: " + t);
        }
    }

    private void lookup() {
        if (kombiViewer == null) {
            try {
                ServiceReference[] refs = context.getServiceManager().getServiceReferences(DSIMapViewerControl.class);
                for (int i = 0; refs != null && i < refs.length; i++) {
                    Object inst = refs[i].getProperty("DEVICE_INSTANCE");
                    if (inst instanceof Integer && ((Integer) inst).intValue() == KOMBI_INSTANCE) {
                        kombiViewer = (DSIMapViewerControl) context.getServiceManager().getService(refs[i]);
                        if (kombiViewer != null) {
                            log("KOMBI_MAP: DSIMapViewerControl(KOMBI=3) found");
                        }
                        break;
                    }
                }
            } catch (Throwable t) {
                log("KOMBI_MAP: DSIMapViewerControl lookup failed: " + t);
            }
        }
        if (mapClusterService == null) {
            try {
                Class c = Class.forName(MAP_CLUSTER_CLASS);
                ServiceReference[] refs = context.getServiceManager().getServiceReferences(c);
                if (refs != null && refs.length > 0) {
                    mapClusterService = context.getServiceManager().getService(refs[0]);
                    if (mapClusterService != null) {
                        log("KOMBI_MAP: IMapClusterService found");
                    }
                }
            } catch (Throwable t) {
                log("KOMBI_MAP: IMapClusterService lookup failed: " + t);
            }
        }
    }

    /** One log line with the car's coding: 3 = virtual cockpit (arrows go by video, not BAP). */
    private void logNavMapTransmissionMode() {
        try {
            Class c = Class.forName("de.audi.mib.system.ISysServices");
            ServiceReference[] refs = context.getServiceManager().getServiceReferences(c);
            if (refs == null || refs.length == 0) {
                return;
            }
            Object o = context.getServiceManager().getService(refs[0]);
            String[] chain = {"config", "rawCoding", "displayConfig", "getNavMapTransmissionMode"};
            for (int i = 0; o != null && i < chain.length; i++) {
                Method m = o.getClass().getMethod(chain[i], new Class[0]);
                m.setAccessible(true); // implementation classes may be package-private
                o = m.invoke(o, new Object[0]);
            }
            log("KOMBI_MAP: coding navMapTransmissionMode=" + o + " (3 = virtual cockpit)");
        } catch (Throwable t) {
            log("KOMBI_MAP: navMapTransmissionMode not readable: " + t);
        }
        codingLogged = true;
    }

    private void log(String msg) {
        safeLog(integration, msg);
    }

    private static void safeLog(AndroidAutoClusterIntegration integration, String msg) {
        try {
            integration.logCluster(msg);
        } catch (Throwable t) {
            // logging must never take Android Auto down
        }
    }
}
