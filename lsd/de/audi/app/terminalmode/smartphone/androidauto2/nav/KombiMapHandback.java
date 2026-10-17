package de.audi.app.terminalmode.smartphone.androidauto2.nav;

import de.audi.app.terminalmode.IContext;
import de.audi.atip.log.LogChannel;
import org.osgi.framework.BundleContext;
import org.osgi.framework.ServiceReference;
import org.osgi.util.tracker.ServiceTracker;
import org.osgi.util.tracker.ServiceTrackerCustomizer;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.lang.reflect.Method;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Timer;
import java.util.TimerTask;

/**
 * Hands the cluster map back to the HMI's own navigation when Android Auto
 * ends, through the navigation bundle's IMapClusterService.
 *
 * AndroidAuto2Subsystem looked that service up once, in its constructor, with
 * IServiceManager.getServiceReferences(Class). On the e-tron that found
 * nothing (no "KOMBI_MAP:" lines in cluster.log) and the cockpit stayed black
 * after Android Auto disconnected, because the shipped cluster integration
 * never asked the Audi map to come back. The stock navigation code reaches
 * the same service with a ServiceTracker on the class NAME
 * (de.audi.tghu.navi.app.cluster.MapClusterServiceProxy), so it is found
 * whenever the navigation bundle registers it, before or after the terminal
 * mode app starts. This class does the same, calls the service by reflection
 * (no compile-time or link-time dependency: if anything is missing, Android
 * Auto keeps working and the log says what was missing) and logs to
 * /tmp/cluster.log with the KOMBI_MAP: prefix the rest of the mod uses.
 *
 * On the stock service (de.audi.tghu.navi.app.cluster.MapClusterService):
 *   switchKombiMapToHiddenContext() = kombiMap.switchToHiddenContext() + exitMapScreen()
 *   switchKombiMapToAShownContext() = kombiMap.startActivity(Intent.AUTO) + enterMapScreen()
 * Calling "shown" while the HMI believes the map is already shown is a no-op
 * (both calls are guarded against repeats), so the hand-back hides first and
 * shows again after a short delay, once the mod's video window is gone.
 *
 * Written for the HMI's Java 1.4 class library: no generics, no autoboxing,
 * no StringBuilder (the compiler is run with -target 1.4).
 */
public class KombiMapHandback implements ServiceTrackerCustomizer {

    public static final String BUILD = "kombi-handback 2026-10-17a";

    private static final String SERVICE_NAME = "de.audi.tghu.navi.app.cluster.IMapClusterService";
    private static final String LOG_FILE = "/tmp/cluster.log";
    private static final long SHOW_DELAY_MS = 1200;

    private final LogChannel logChannel;
    private final BundleContext bundleContext;
    private ServiceTracker tracker;
    private Object service;
    private Timer timer;
    private boolean handbackPending;
    private boolean servicesDumped;

    public KombiMapHandback(IContext context, LogChannel logChannel) {
        this.logChannel = logChannel;
        BundleContext bc = null;
        try {
            bc = context.getServiceManager().getBundleContext();
        } catch (Throwable t) {
            log("bundle context unavailable: " + t.toString());
        }
        this.bundleContext = bc;
        log(BUILD + " starting (bundleContext=" + (bc != null ? "ok" : "null") + ")");
        if (bc != null) {
            try {
                this.tracker = new ServiceTracker(bc, SERVICE_NAME, this);
                this.tracker.open();
                log("tracking " + SERVICE_NAME + " (" + (this.service != null ? "already registered" : "not registered yet, waiting") + ")");
            } catch (Throwable t) {
                log("ServiceTracker failed: " + t.toString());
            }
        }
    }

    // ---- ServiceTrackerCustomizer -------------------------------------------

    public Object addingService(ServiceReference reference) {
        Object s = null;
        try {
            s = bundleContext.getService(reference);
        } catch (Throwable t) {
            log("getService failed: " + t.toString());
        }
        this.service = s;
        log("IMapClusterService registered (class=" + (s != null ? s.getClass().getName() : "null") + ")");
        return s;
    }

    public void modifiedService(ServiceReference reference, Object s) {
    }

    public void removedService(ServiceReference reference, Object s) {
        if (this.service == s) {
            this.service = null;
        }
        try {
            bundleContext.ungetService(reference);
        } catch (Throwable t) {
            // nothing to do
        }
        log("IMapClusterService unregistered");
    }

    // ---- Android Auto session hooks (called from AndroidAuto2Subsystem) ------

    /** Android Auto session starts: the mod's video window takes the cluster map slot. */
    public void onAndroidAutoActivated() {
        cancelTimer();
        this.handbackPending = false;
        log("AA activated (service " + (this.service != null ? "available" : "NOT available") + ")");
        if (!this.servicesDumped) {
            this.servicesDumped = true;
            scheduleServiceDump(3000);
        }
    }

    /** Android Auto session ended: give the cluster map back to the Audi navigation. */
    public void onAndroidAutoTerminated() {
        if (this.handbackPending) {
            log("AA terminated again while hand-back pending; ignored");
            return;
        }
        if (this.service == null) {
            log("AA terminated but IMapClusterService NOT available; hand-back skipped");
            scheduleServiceDump(0);
            return;
        }
        this.handbackPending = true;
        log("AA terminated: handing the cluster map back (hide now, show in " + SHOW_DELAY_MS + " ms)");
        call("switchKombiMapToHiddenContext");
        cancelTimer();
        this.timer = new Timer(true);
        this.timer.schedule(new TimerTask() {
            public void run() {
                try {
                    call("switchKombiMapToAShownContext");
                } finally {
                    handbackPending = false;
                }
            }
        }, SHOW_DELAY_MS);
    }

    // ---- helpers -------------------------------------------------------------

    private boolean call(String methodName) {
        Object s = this.service;
        if (s == null) {
            log(methodName + " skipped - IMapClusterService not available");
            return false;
        }
        try {
            Method m = s.getClass().getMethod(methodName, new Class[0]);
            m.invoke(s, new Object[0]);
            log("IMapClusterService." + methodName + "() invoked");
            return true;
        } catch (NoSuchMethodException e) {
            log(methodName + " not found on " + s.getClass().getName());
        } catch (Throwable t) {
            log(methodName + " failed: " + t.toString());
        }
        return false;
    }

    private void cancelTimer() {
        if (this.timer != null) {
            try {
                this.timer.cancel();
            } catch (Throwable t) {
                // nothing to do
            }
            this.timer = null;
        }
    }

    /**
     * Lists the registered OSGi services whose class names look cluster/map
     * related, so a failed lookup leaves evidence of what this HMI offers.
     */
    private void scheduleServiceDump(long delayMs) {
        if (this.bundleContext == null) {
            return;
        }
        Timer t = new Timer(true);
        t.schedule(new TimerTask() {
            public void run() {
                dumpServices();
            }
        }, delayMs);
    }

    private void dumpServices() {
        try {
            ServiceReference[] refs = bundleContext.getServiceReferences(null, null);
            int total = (refs == null) ? 0 : refs.length;
            int listed = 0;
            StringBuffer sb = new StringBuffer();
            for (int i = 0; refs != null && i < refs.length && listed < 120; i++) {
                Object oc = refs[i].getProperty("objectClass");
                if (!(oc instanceof String[])) {
                    continue;
                }
                String[] names = (String[]) oc;
                for (int n = 0; n < names.length; n++) {
                    String name = names[n];
                    if (name.indexOf("Map") >= 0 || name.indexOf("Kombi") >= 0 || name.indexOf("Cluster") >= 0
                            || name.indexOf("Combi") >= 0 || name.indexOf("Zoom") >= 0 || name.indexOf("navi") >= 0) {
                        sb.setLength(0);
                        sb.append("service: ").append(name);
                        Object inst = refs[i].getProperty("DEVICE_INSTANCE");
                        Object dev = refs[i].getProperty("DEVICE_NAME");
                        if (inst != null) {
                            sb.append(" DEVICE_INSTANCE=").append(inst);
                        }
                        if (dev != null) {
                            sb.append(" DEVICE_NAME=").append(dev);
                        }
                        log(sb.toString());
                        listed++;
                        break;
                    }
                }
            }
            log("service dump done: " + listed + " of " + total + " registered services listed");
        } catch (Throwable t) {
            log("service dump failed: " + t.toString());
        }
    }

    private void log(String message) {
        String line = "KOMBI_MAP: " + message;
        try {
            if (this.logChannel != null) {
                this.logChannel.log(1000000, "[AndroidAuto2Subsystem] " + line);
            }
        } catch (Throwable t) {
            // nothing to do
        }
        PrintWriter writer = null;
        try {
            writer = new PrintWriter(new FileWriter(LOG_FILE, true));
            writer.println(new SimpleDateFormat("HH:mm:ss.SSS").format(new Date()) + " " + line);
        } catch (Throwable t) {
            // /tmp is RAM on the unit and may be missing on a test host
        } finally {
            if (writer != null) {
                writer.close();
            }
        }
    }
}
