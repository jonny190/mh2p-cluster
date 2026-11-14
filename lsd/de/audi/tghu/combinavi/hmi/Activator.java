/*
 * Shadows the stock de.audi.tghu.combinavi.hmi.Activator (the cockpit
 * navigation menus, HMI module 40) of the MH2p P2711 HMI. Identical to the
 * stock class except that the model bank and the screen factory come from
 * CarPlayMapMenu, which adds the "Carplay Map" row to the cockpit navigation
 * menu; both fall back to the stock bank and factory on an error.
 *
 * Java 1.4 source level.
 */
package de.audi.tghu.combinavi.hmi;

import de.audi.atip.activator.AbstractHMIActivator;
import de.audi.atip.activator.FrameworkException;
import de.audi.atip.hmi.HMIConditionBank;
import de.audi.atip.hmi.view.AbstractScreenFactory;
import de.audi.tghu.combinavi.hmi.highg35.CombiNaviConditionBank;
import de.audi.tghu.combinavi.hmi.highg35.CombiNaviScreenFactory;
import de.eso.widgets.guide.IWidgetFactoryGuide;
import org.osgi.framework.BundleContext;
import org.osgi.framework.ServiceReference;

public class Activator extends AbstractHMIActivator {
    private static final String WIDGET_FACTORY_GUIDE = "de.eso.widgets.guide.IWidgetFactoryGuide";

    private CombiNaviScreenFactory instance = null;
    private CombiNaviConditionBank conditionBank;
    private IWidgetFactoryGuide widgetFactoryGuide;

    public Activator() {
        super(40, "CombiNavi", System.getProperty("variant.skin", "HighG35"), CarPlayMapMenu.newBank());
    }

    public void start(BundleContext context) {
        ServiceReference widgetFactoryGuideServiceReference = context.getServiceReference(WIDGET_FACTORY_GUIDE);
        if (widgetFactoryGuideServiceReference != null) {
            this.widgetFactoryGuide = (IWidgetFactoryGuide) context.getService(widgetFactoryGuideServiceReference);
        }
        if (this.widgetFactoryGuide == null) {
            throw new FrameworkException("IWidgetFactoryGuide service not available, starting of " + this.getName() + " aborted");
        }
        super.start(context);
    }

    public void stop(BundleContext context) {
        if (this.widgetFactoryGuide != null) {
            context.ungetService(context.getServiceReference(WIDGET_FACTORY_GUIDE));
            this.widgetFactoryGuide = null;
        }
        super.stop(context);
    }

    public synchronized AbstractScreenFactory getScreenFactory() {
        if (this.instance == null) {
            if (this.widgetFactoryGuide == null) {
                throw new IllegalStateException("IWidgetFactoryGuide is not usable!");
            }
            this.instance = CarPlayMapMenu.newFactory(this.getFramework(), this.widgetFactoryGuide);
        }
        return this.instance;
    }

    public HMIConditionBank getConditionBank() {
        if (this.conditionBank == null) {
            this.conditionBank = new CombiNaviConditionBank((CombiNaviScreenFactory) this.getScreenFactory());
        }
        return this.conditionBank;
    }
}
