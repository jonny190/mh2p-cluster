/*
 * Stock MH2p class (lsd.jxe, MH2p_ER_AUG35_P2711), decompiled with CFR 0.152;
 * every method still forwards to the navigation's CombiBAPListener. Shadowed
 * through the mod's jar on the boot classpath. Java 1.4 source level, so the
 * generic super class is used raw.
 *
 * Added for the CarPlay instrument-cluster video ("altScreen"): this is where
 * the cockpit's navigation BAP indications enter the navigation. Each call is
 * logged to /tmp/altscreen_java.log (capped) to find the indication that
 * carries the cockpit view, and Map_Presentation (largeMapView) is also
 * written to /tmp/altscreen_view for the dio_manager hook.
 */
package de.audi.tghu.navi.app.osgi.wrapper;

import de.audi.atip.interapp.combi.bap.navi.CombiBAPServiceNaviListener;
import de.audi.atip.interapp.combi.bap.navi.data.CombiBAPNaviDestination;
import de.audi.atip.interapp.combi.bap.navi.data.MapContentSettings;
import de.audi.tghu.navi.app.util.AbstractNaviServiceWrapper;

import java.io.FileOutputStream;

public class CombiBAPServiceNaviListenerWrapper
extends AbstractNaviServiceWrapper
implements CombiBAPServiceNaviListener {
    private static final String LOG_PATH = "/tmp/altscreen_java.log";
    private static final long LOG_LIMIT = 256L * 1024L;
    private static long logged;

    public CombiBAPServiceNaviListenerWrapper(CombiBAPServiceNaviListener instance) {
        super(instance);
    }

    private CombiBAPServiceNaviListener target() {
        return (CombiBAPServiceNaviListener)this.get();
    }

    private static void write(String path, String text, boolean append) {
        FileOutputStream out = null;
        try {
            out = new FileOutputStream(path, append);
            out.write(text.getBytes());
        } catch (Throwable t) {
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
        String text = System.currentTimeMillis() + " BAP " + line + "\n";
        synchronized (CombiBAPServiceNaviListenerWrapper.class) {
            if (logged > LOG_LIMIT) {
                return;
            }
            logged += text.length();
        }
        write(LOG_PATH, text, true);
    }

    public void startRouteGuidanceToHomeAddress() {
        log("startRouteGuidanceToHomeAddress()");
        this.target().startRouteGuidanceToHomeAddress();
    }

    public void startRouteGuidanceToBusinessAddress() {
        log("startRouteGuidanceToBusinessAddress()");
        this.target().startRouteGuidanceToBusinessAddress();
    }

    public void startRouteGuidance(CombiBAPNaviDestination address) {
        log("startRouteGuidance(" + address + ")");
        this.target().startRouteGuidance(address);
    }

    public void stopRouteGuidance() {
        log("stopRouteGuidance()");
        this.target().stopRouteGuidance();
    }

    public void repeatLastNavAnnouncement() {
        log("repeatLastNavAnnouncement()");
        this.target().repeatLastNavAnnouncement();
    }

    public void setVoiceGuidanceState(int state) {
        log("setVoiceGuidanceState(" + state + ")");
        this.target().setVoiceGuidanceState(state);
    }

    public void setActiveRGType(int rgType) {
        log("setActiveRGType(" + rgType + ")");
        this.target().setActiveRGType(rgType);
    }

    public void setMapColor(int color) {
        log("setMapColor(" + color + ")");
        this.target().setMapColor(color);
    }

    public void setMapType(int activeMapType, int mainMapSetup) {
        log("setMapType(" + activeMapType + ", " + mainMapSetup + ")");
        this.target().setMapType(activeMapType, mainMapSetup);
    }

    public void setMapView(int mapView, int supplementaryMapView) {
        log("setMapView(" + mapView + ", " + supplementaryMapView + ")");
        this.target().setMapView(mapView, supplementaryMapView);
    }

    public void setMapVisibility(boolean lvdsMapVisible, boolean supplementaryMapViewVisible) {
        log("setMapVisibility(" + lvdsMapVisible + ", " + supplementaryMapViewVisible + ")");
        this.target().setMapVisibility(lvdsMapVisible, supplementaryMapViewVisible);
    }

    public void setMapOrientation(int orientation) {
        log("setMapOrientation(" + orientation + ")");
        this.target().setMapOrientation(orientation);
    }

    public void setMapScale(int steps) {
        log("setMapScale(" + steps + ")");
        this.target().setMapScale(steps);
    }

    public void setMapScaleSetting(int autoZoomSetting) {
        log("setMapScaleSetting(" + autoZoomSetting + ")");
        this.target().setMapScaleSetting(autoZoomSetting);
    }

    public void startPOISearch(int searchType, int poiType) {
        log("startPOISearch(" + searchType + ", " + poiType + ")");
        this.target().startPOISearch(searchType, poiType);
    }

    public void setMapPresentation(boolean largeMapView, boolean leftSideMenuOpen, boolean rightSideMenuOpen) {
        log("setMapPresentation(" + largeMapView + ", " + leftSideMenuOpen + ", " + rightSideMenuOpen + ")");
        write("/tmp/altscreen_view", largeMapView ? "large\n" : "small\n", false);
        this.target().setMapPresentation(largeMapView, leftSideMenuOpen, rightSideMenuOpen);
    }

    public void resetSupportedMapViews() {
        log("resetSupportedMapViews()");
        this.target().resetSupportedMapViews();
    }

    public void setMapContentSettings(MapContentSettings mapContentSettings) {
        log("setMapContentSettings(" + mapContentSettings + ")");
        this.target().setMapContentSettings(mapContentSettings);
    }
}
