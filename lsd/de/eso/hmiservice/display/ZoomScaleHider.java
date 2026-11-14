/*
 * Hides the Audi map's zoom scale (magnifier or auto-zoom icon, "400 m",
 * scale bar), traffic provider logo ("TMC", "TMC PRO", online traffic) and
 * satellite map provider logo ("Google", with Google Earth / satellite maps,
 * bottom right) in the cockpit while the CarPlay cluster video is shown;
 * they belong to the hidden Audi map and are meaningless over the phone's
 * map.
 *
 * HandCodedMapWidgetFactory.createMapZoomGroupCombi builds the zoom group as
 * a layout container of three widgets, the label bound to model 403861
 * (ZoomViewFPKRevo's magnification). The status bar is rendered into a
 * subtexture (SubframeController) that the cluster places itself, so neither
 * the display contexts nor the opacity of the FPK layer 41 reach it (tested
 * 2026-10-31). The logo is the icon bound to model 500137 that
 * createMapControllerCombi puts into the cockpit's MapControllerCombi; the
 * same factory method also builds one for the navigation sidebar on the MMI,
 * so only icons below a MapControllerCombi are touched. The satellite map
 * provider logo is the icon on model 4000022 (createSatelliteMapProviderLogoCombi,
 * same MapControllerCombi; the MMI's own one is on 404796). Both are found by
 * walking the widget trees of the current screens of every terminal and
 * switched with setVisibleByDisclaimer, a visibility bit the stock HMI does
 * not use on these widgets; the enclosing subframe is then invalidated so
 * the subtexture is drawn again.
 */
package de.eso.hmiservice.display;

import de.audi.atip.hmi.HMIService;
import de.audi.atip.hmi.IRootWindow;
import de.audi.atip.hmi.view.HMIView;
import de.audi.atip.hmi.view.Screen;
import de.eso.widgets.controllers.AbstractWidget;
import de.eso.widgets.map.MapControllerCombi;
import de.eso.widgets.subframe.SubframeController;
import java.util.List;
import java.util.Vector;

final class ZoomScaleHider {
    private static final int MODEL_ZOOM_SCALE = 403861;
    private static final int MODEL_TRAFFIC_LOGO = 500137;
    private static final int MODEL_SATELLITE_LOGO = 4000022;
    private static final int TERMINALS = 10;
    private static final long WALK_MS = 5000L;

    private static Vector groups = new Vector();
    private static Vector logos = new Vector();
    private static long lastWalk;
    private static int lastFound = -1;
    private static int lastLogos = -1;
    private static int lastOtherLogos = -1;
    private static boolean lastHide;

    private ZoomScaleHider() {
    }

    /* returns a line to log when the state or the widgets found changed */
    static synchronized String apply(HMIService hmi, boolean hide) {
        String msg = null;
        long now = System.currentTimeMillis();
        if (hide && (groups.isEmpty() || logos.isEmpty() || now - lastWalk >= WALK_MS)
                && now - lastWalk >= 1000L) {
            lastWalk = now;
            int[] walked = new int[2];
            Vector found = new Vector();
            Vector foundLogos = new Vector();
            StringBuffer where = new StringBuffer();
            for (int t = 0; t < TERMINALS; ++t) {
                try {
                    IRootWindow root = hmi.getRootWindow(t);
                    if (root == null) {
                        continue;
                    }
                    collectScreen(root.getCurrentScreen(t), t, found, foundLogos, walked, where);
                    collectScreen(root.getCurrentMainScreen(t), t, found, foundLogos, walked, where);
                    collectScreen(root.getCurrentPopupScreen(t), t, found, foundLogos, walked, where);
                }
                catch (Throwable e) {
                    where.append(' ').append(t).append(":!").append(e.getClass().getName());
                }
            }
            restore(groups, found);
            restore(logos, foundLogos);
            groups = found;
            logos = foundLogos;
            if (found.size() != lastFound || foundLogos.size() != lastLogos || walked[1] != lastOtherLogos) {
                lastFound = found.size();
                lastLogos = foundLogos.size();
                lastOtherLogos = walked[1];
                msg = "zoom scale: " + found.size() + " group(s) and " + foundLogos.size()
                        + " map logo(s) found (" + walked[1] + " logo(s) outside the cockpit map left alone), "
                        + walked[0] + " widgets walked, screens" + where;
            }
        }
        boolean changed = false;
        for (int i = 0; i < groups.size(); ++i) {
            changed |= setVisible((AbstractWidget)groups.elementAt(i), !hide);
        }
        for (int i = 0; i < logos.size(); ++i) {
            changed |= setVisible((AbstractWidget)logos.elementAt(i), !hide);
        }
        if (changed) {
            for (int t = 0; t < TERMINALS; ++t) {
                try {
                    IRootWindow root = hmi.getRootWindow(t);
                    if (root != null) {
                        root.triggerRepaint();
                    }
                }
                catch (Throwable e) {
                }
            }
        }
        if (hide != lastHide) {
            lastHide = hide;
            String state = "zoom scale and map logos " + (hide ? "hidden" : "shown") + " (" + groups.size()
                    + " group(s), " + logos.size() + " icon(s))";
            msg = msg == null ? state : msg + "; " + state;
        }
        return msg;
    }

    /* widgets hidden before but not found again are shown */
    private static void restore(Vector before, Vector found) {
        for (int i = 0; i < before.size(); ++i) {
            if (!found.contains(before.elementAt(i))) {
                setVisible((AbstractWidget)before.elementAt(i), true);
            }
        }
    }

    private static void collectScreen(Screen screen, int terminal, Vector found, Vector foundLogos, int[] walked,
            StringBuffer where) {
        if (screen == null) {
            return;
        }
        where.append(' ').append(terminal).append(':').append(screen.getID());
        collectViews(screen.getViews(), found, foundLogos, walked);
        HMIView[][] replacements = screen.getReplacementWidgets();
        if (replacements != null) {
            for (int i = 0; i < replacements.length; ++i) {
                collectViews(replacements[i], found, foundLogos, walked);
            }
        }
    }

    private static void collectViews(HMIView[] views, Vector found, Vector foundLogos, int[] walked) {
        if (views == null) {
            return;
        }
        for (int i = 0; i < views.length; ++i) {
            if (views[i] instanceof AbstractWidget) {
                collect((AbstractWidget)views[i], found, foundLogos, walked, 0);
            }
        }
    }

    /* walked[0] counts the widgets, walked[1] the logos not below a MapControllerCombi */
    private static void collect(AbstractWidget w, Vector found, Vector foundLogos, int[] walked, int depth) {
        ++walked[0];
        if (w.getModelID() == MODEL_ZOOM_SCALE && w.getParent() instanceof AbstractWidget) {
            Object group = w.getParent();
            if (!found.contains(group)) {
                found.addElement(group);
            }
        }
        if (w.getModelID() == MODEL_TRAFFIC_LOGO || w.getModelID() == MODEL_SATELLITE_LOGO) {
            if (!inCockpitMap(w)) {
                ++walked[1];
            } else if (!foundLogos.contains(w)) {
                foundLogos.addElement(w);
            }
        }
        if (depth > 40) {
            return;
        }
        List children = w.getChildren();
        if (children == null) {
            return;
        }
        for (int i = 0; i < children.size(); ++i) {
            Object c = children.get(i);
            if (c instanceof AbstractWidget) {
                collect((AbstractWidget)c, found, foundLogos, walked, depth + 1);
            }
        }
    }

    private static boolean inCockpitMap(AbstractWidget w) {
        for (Object p = w.getParent(); p instanceof AbstractWidget; p = ((AbstractWidget)p).getParent()) {
            if (p instanceof MapControllerCombi) {
                return true;
            }
        }
        return false;
    }

    /* returns true when the visibility changed */
    private static boolean setVisible(AbstractWidget widget, boolean visible) {
        if (widget.isVisibleByDisclaimer() == visible) {
            return false;
        }
        widget.setVisibleByDisclaimer(visible);
        for (Object p = widget.getParent(); p instanceof AbstractWidget; p = ((AbstractWidget)p).getParent()) {
            if (p instanceof SubframeController) {
                ((SubframeController)p).setInvalid(true);
                break;
            }
        }
        return true;
    }
}
