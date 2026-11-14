/*
 * Copyright (c) 2026 CrEaK (https://github.com/CrEaK)
 *
 * Licensed under CC BY-NC-SA 4.0
 * https://creativecommons.org/licenses/by-nc-sa/4.0/
 * NOT FOR COMMERCIAL USE
 */
package de.audi.app.terminalmode.smartphone.androidauto2.nav;

import org.dsi.ifc.carplay.AppState;
import org.dsi.ifc.carplay.CallState;
import org.dsi.ifc.carplay.DSICarplayListener;
import org.dsi.ifc.carplay.DeviceInfo;
import org.dsi.ifc.carplay.PlaybackInfo;
import org.dsi.ifc.carplay.Resource;
import org.dsi.ifc.carplay.TelephonyState;
import org.dsi.ifc.carplay.TrackData;
import org.dsi.ifc.global.ResourceLocator;

import org.dsi.ifc.carplay.DSICarplay;
import org.osgi.framework.ServiceReference;
import org.osgi.util.tracker.ServiceTrackerCustomizer;

import de.audi.app.terminalmode.IContext;
import de.audi.app.terminalmode.smartphone.carplay.CarPlayClusterIntegration;

/**
 * Second CarPlay DSI listener on Audi that tells CarPlayClusterIntegration
 * who owns the main screen. On Porsche the mod's CarPlayDSIManager forwards
 * updateMode; on Audi that manager stays stock, so this class does it:
 *
 * - A DSI listener only receives attribute updates it subscribed to, so the
 *   bridge tracks the DSICarplay service and subscribes itself to ATTR_MODE.
 *   Registering via registerDSIListener alone delivers responses only.
 * - Audi's DSICarplay numbers the resources RESOURCE_MAIN_AUDIO = 1 and
 *   RESOURCE_MAIN_SCREEN = 2; the integration tests for screen = 1. The
 *   bridge maps the Audi screen resource onto the integration's id and drops
 *   the audio resource. Owner values match (1 = main unit, 2 = device);
 *   owner 0 (undefined, sent in mode change responses) changes nothing.
 * - In altscreen mode (CarPlay map video from the dio_manager hook) the
 *   owner no longer decides: CarPlayMapSwitch keeps the CarPlay map in the
 *   cockpit while the "Carplay Map" menu setting is on and the cockpit video
 *   runs, also while the MMI shows the radio or the reverse camera. The
 *   bridge only passes it the real owner for the log.
 * - Without the hook nothing is forwarded: the cockpit keeps the Audi map
 *   and the cluster daemon never mirrors the MMI screen (both MMI displays,
 *   which is no use in the cockpit). Turn-by-turn data is not affected.
 *
 * Every other callback is a no-op. Java 1.4 source level.
 */
public class CarPlayScreenBridge implements DSICarplayListener, ServiceTrackerCustomizer {

    private static final int AUDI_RESOURCE_MAIN_SCREEN = DSICarplay.RESOURCE_MAIN_SCREEN;

    private final CarPlayClusterIntegration integration;
    /* null without the altScreen hook: then the owner decides as before */
    private final CarPlayMapSwitch mapSwitch;
    private boolean mirrorNoted;
    private IContext context;

    public CarPlayScreenBridge(CarPlayClusterIntegration integration) {
        this.integration = integration;
        CarPlayMapSwitch s = null;
        try {
            if (CarPlayMapSetting.isAltscreenMode()) {
                s = CarPlayMapSwitch.start(integration);
            }
        } catch (Throwable t) {
            log("DSI: Carplay Map switch failed, the MMI screen owner decides: " + t);
        }
        this.mapSwitch = s;
    }

    /**
     * Tracks the DSICarplay service and subscribes this listener to ATTR_MODE
     * whenever an instance appears. Never throws.
     */
    public void subscribe(IContext context) {
        this.context = context;
        try {
            context.getServiceManager().createServiceTracker(DSICarplay.class, this).open();
            integration.logCluster("DSI: screen bridge tracking DSICarplay for ATTR_MODE");
        } catch (Throwable t) {
            log("DSI: screen bridge tracker failed: " + t);
        }
    }

    public Object addingService(ServiceReference reference) {
        Object svc = null;
        try {
            svc = context.getServiceManager().getService(reference);
            if (svc instanceof DSICarplay) {
                ((DSICarplay) svc).setNotification(new int[] { DSICarplay.ATTR_MODE }, this);
                log("DSI: screen bridge subscribed to ATTR_MODE (" + svc.getClass().getName() + ")");
            } else {
                log("DSI: screen bridge got unexpected service " + svc);
            }
        } catch (Throwable t) {
            log("DSI: screen bridge subscribe failed: " + t);
        }
        return svc;
    }

    public void modifiedService(ServiceReference reference, Object service) { }

    public void removedService(ServiceReference reference, Object service) {
        try {
            if (service instanceof DSICarplay) {
                ((DSICarplay) service).clearNotification(new int[] { DSICarplay.ATTR_MODE }, this);
            }
            log("DSI: screen bridge: DSICarplay service removed");
        } catch (Throwable t) { }
    }

    private void log(String s) {
        try { integration.logCluster(s); } catch (Throwable ignore) { }
    }

    private void forward(String origin, Resource[] resources, AppState[] appStates, int validFlag) {
        try {
            StringBuffer sb = new StringBuffer(64);
            sb.append("DSI_IN: ").append(origin).append(" resources=");
            if (resources == null) {
                sb.append("null");
            } else {
                for (int i = 0; i < resources.length; i++) {
                    if (i > 0) sb.append(',');
                    sb.append(resources[i].getResourceID()).append(':').append(resources[i].getOwner());
                }
            }
            sb.append(" valid=").append(validFlag);
            integration.logCluster(sb.toString());
            if (resources != null) {
                for (int i = 0; i < resources.length; i++) {
                    if (resources[i].getResourceID() == AUDI_RESOURCE_MAIN_SCREEN) {
                        if (mapSwitch != null) {
                            mapSwitch.ownerChanged(resources[i].getOwner());
                        } else if (!mirrorNoted) {
                            mirrorNoted = true;
                            log("DSI: no altScreen hook, CarPlay screen mirror in the cockpit is off");
                        }
                    }
                }
            }
        } catch (Throwable t) {
            log("DSI_IN: bridge error: " + t);
        }
    }

    public void responseModeChange(Resource[] resources, AppState[] appStates, int validFlag) {
        forward("RESPONSE_MODE_CHANGE(bridge)", resources, appStates, validFlag);
    }

    public void updateMode(Resource[] resources, AppState[] appStates, int validFlag) {
        forward("UPDATE_MODE(bridge)", resources, appStates, validFlag);
    }

    public void requestBTDeactivation(String deviceName, int validFlag) { }
    public void updateDeviceInfo(DeviceInfo info, int validFlag) { }
    public void updateCallState(CallState[] states, int validFlag) { }
    public void updateTelephonyState(TelephonyState state, int validFlag) { }
    public void updateNowPlayingData(TrackData data, int validFlag) { }
    public void updatePlaybackState(PlaybackInfo info, int validFlag) { }
    public void updatePlayposition(int position, int validFlag) { }
    public void updateCoverArtUrl(ResourceLocator url, int validFlag) { }
    public void updateTextInputState(int state, int validFlag) { }
    public void duckAudio(int duration, double volume) { }
    public void unduckAudio(int duration) { }
    public void oemAppSelected() { }
    public void updateMainAudioType(int type, int sampleRate, int validFlag) { }
    public void updateStreamParameters(int w, int h, int m, int d, int f, int c, int v) { }
    public void launchAppResult(String appId, boolean success) { }
    public void asyncException(int errCode, String msg, int requestType) { }
}
