/*
 * Copyright (c) 2026 CrEaK (https://github.com/CrEaK)
 *
 * Licensed under CC BY-NC-SA 4.0
 * https://creativecommons.org/licenses/by-nc-sa/4.0/
 * NOT FOR COMMERCIAL USE
 */
package de.audi.tghu.combinavi.hmi;

import de.audi.app.terminalmode.smartphone.androidauto2.nav.CarPlayMapSetting;
import de.audi.atip.base.IFrameworkAccess;
import de.audi.atip.hmi.model.ChoiceModel;
import de.audi.atip.hmi.model.HMIModel;
import de.audi.atip.hmi.model.listener.DefaultChoiceListener;
import de.audi.atip.hmi.view.HMIView;
import de.audi.atip.hmi.view.Screen;
import de.audi.atip.model.CombiNaviModelBank;
import de.audi.tghu.combinavi.hmi.highg35.CombiNaviScreenFactory;
import de.audi.tghu.combinavi.hmi.highg35.FontFactory;
import de.eso.widgets.guide.IAbstractWidgetGuide;
import de.eso.widgets.guide.ICheckboxGuide;
import de.eso.widgets.guide.ICheckboxMenuItemConfigGuide;
import de.eso.widgets.guide.IFocusCursorGuide;
import de.eso.widgets.guide.IIconGuide;
import de.eso.widgets.guide.ILabelGuide;
import de.eso.widgets.guide.ILabelMenuItemConfigGuide;
import de.eso.widgets.guide.IMenuCombiGuide;
import de.eso.widgets.guide.IMenuItemGuide;
import de.eso.widgets.guide.IScreenAreaContainerGuide;
import de.eso.widgets.guide.IScreenGuide;
import de.eso.widgets.guide.ISelectionCursorGuide;
import de.eso.widgets.guide.ISubmenuMenuItemConfigGuide;
import de.eso.widgets.guide.IWidgetFactoryGuide;

/**
 * "Smartphone Map" row (called "Carplay Map" up to beta 2) with a checkbox
 * in the cockpit's navigation menu
 * (screen nAV_FPK_MENU, 4000003, drawn by the MMI on the cluster terminal),
 * directly below "Karte". The checkbox is CarPlayMapSetting: on, the cockpit
 * keeps the CarPlay map while the phone sends the cockpit video, whatever
 * the MMI shows; off, it shows the Audi map. The same goes for Android
 * Auto's cluster map (KombiMapGuard). The row is only visible while CarPlay
 * or Android Auto sends a cockpit video (CarPlayMapSetting.isMenuVisible).
 *
 * How, the way luka-dev adds the CarPlay UpMix row on MIB2Q:
 * - Model: ChoiceModel MODEL_ID = module 40 index 33 (the stock CombiNavi
 *   bank has 0..32), added to the stock bank by the subclass Bank, which the
 *   shadowed Activator hands the HMI; the HMI resolves it like the stock
 *   checkbox models (4000024, 405177: value 1 = checked, the listener
 *   toggles it).
 * - Screen: Factory builds the menu with buildMenu, a copy of the generated
 *   CombiNaviScreenBag1.nAV_FPK_MENU (P2711 HMI) plus the row. The label
 *   text is set directly (ILabelGuide.setText), so it reads "Carplay Map" in
 *   every language and no text table changes.
 * - Visibility: MODEL_ID is also a condition id of the screen, so the HMI
 *   calls executeCondition for it when the screen connects and whenever the
 *   model fires; CarPlayMapSwitch fires it (forceUpdate) when the cockpit
 *   video starts or stops.
 * Every step falls back to the stock bank, factory or screen on an error.
 *
 * Java 1.4 source level.
 */
public final class CarPlayMapMenu {

    static final int SCREEN_MENU = 4000003;
    static final int MODEL_ID = 4000033;
    private static final int INDEX = MODEL_ID % 100000;
    private static final String LABEL = "Smartphone Map";

    private static volatile ChoiceModel model;

    private CarPlayMapMenu() {
    }

    static CombiNaviModelBank newBank() {
        try {
            return new Bank();
        } catch (Throwable t) {
            CarPlayMapSetting.log("Carplay Map: model bank failed, stock menu: " + t);
            return new CombiNaviModelBank();
        }
    }

    static CombiNaviScreenFactory newFactory(IFrameworkAccess fw, IWidgetFactoryGuide widgetFactory) {
        try {
            return new Factory(fw, widgetFactory);
        } catch (Throwable t) {
            CarPlayMapSetting.log("Carplay Map: screen factory failed, stock menu: " + t);
            return new CombiNaviScreenFactory(fw, widgetFactory);
        }
    }

    /* fires the model so that open menu screens run executeCondition again */
    static void refresh() {
        ChoiceModel m = model;
        if (m == null) {
            return;
        }
        synchronized (m) {
            m.forceUpdate(true);
            try {
                m.setValue(CarPlayMapSetting.isEnabled() ? 1 : 0);
            } finally {
                m.forceUpdate(false);
            }
        }
    }

    static final class Bank extends CombiNaviModelBank {
        Bank() {
            super();
            HMIModel[] grown = new HMIModel[INDEX + 1];
            System.arraycopy(this.models, 0, grown, 0, this.models.length);
            this.models = grown;
            int[] ids = new int[this.modelIDs.length + 1];
            System.arraycopy(this.modelIDs, 0, ids, 0, this.modelIDs.length);
            ids[ids.length - 1] = MODEL_ID;
            this.modelIDs = ids;
        }

        protected synchronized void createModel(int modelIdx) {
            if (modelIdx != INDEX) {
                super.createModel(modelIdx);
                return;
            }
            if (this.models[modelIdx] != null) {
                return;
            }
            final ChoiceModel m = new ChoiceModel(MODEL_ID);
            m.setChoiceListener(new DefaultChoiceListener() {
                public void itemSelected(int id, int itemID, int col, int terminalID) {
                    try {
                        int value = m.getValue() == 1 ? 0 : 1;
                        m.setValue(value);
                        CarPlayMapSetting.setEnabled(value == 1);
                    } catch (Throwable t) {
                        CarPlayMapSetting.log("Carplay Map: toggle failed: " + t);
                    }
                }
            });
            m.setValue(CarPlayMapSetting.isEnabled() ? 1 : 0);
            this.models[modelIdx] = m;
            model = m;
            CarPlayMapSetting.setMenuListener(new Runnable() {
                public void run() {
                    refresh();
                }
            });
        }
    }

    static final class Factory extends CombiNaviScreenFactory {
        private boolean logged;

        Factory(IFrameworkAccess fw, IWidgetFactoryGuide widgetFactory) {
            super(fw, widgetFactory);
        }

        protected Screen createScreen(int screenID, int terminalID) {
            if (screenID == SCREEN_MENU) {
                try {
                    Screen s = buildMenu(this, terminalID);
                    if (!logged) {
                        logged = true;
                        CarPlayMapSetting.log("Carplay Map: menu row added (terminal " + terminalID + ")");
                    }
                    return s;
                } catch (Throwable t) {
                    CarPlayMapSetting.log("Carplay Map: menu row not added, stock menu: " + t);
                }
            }
            return super.createScreen(screenID, terminalID);
        }

        public void executeCondition(int modelId, int screenId, HMIView[] conditionWidgets, int terminalID) {
            if (screenId == SCREEN_MENU && modelId == MODEL_ID) {
                try {
                    ((IMenuItemGuide) conditionWidgets[0]).setVisibleByGuide(CarPlayMapSetting.isMenuVisible());
                } catch (Throwable t) {
                    CarPlayMapSetting.log("Carplay Map: row visibility failed: " + t);
                }
                return;
            }
            super.executeCondition(modelId, screenId, conditionWidgets, terminalID);
        }
    }

    /* CombiNaviScreenBag1.nAV_FPK_MENU of the P2711 HMI with the added row */
    private static Screen buildMenu(CombiNaviScreenFactory screenFactory, int terminalID) {
        IWidgetFactoryGuide widgetFactory = screenFactory.getWidgetFactory(terminalID);
        IFocusCursorGuide audi2PlusFocusCursor = widgetFactory.createFocusCursor();
        ISelectionCursorGuide selection_Cursor = widgetFactory.createSelectionCursor();
        IIconGuide mapIcon = widgetFactory.createIcon();
        mapIcon.setBitmaps(new int[]{4000005});
        mapIcon.setBoundsByGuide(0, 0, 40, 40);
        mapIcon.setCoordinateSets(new int[]{1, 0, 0, 40, 40, 0, 0, 40, 40});
        mapIcon.setIntegerContentInterpretation(IIconGuide.INTEGER_AS_BITMAP_INDEX);
        ILabelGuide map_Label = widgetFactory.createLabel();
        map_Label.setTextIds(new int[]{4000001});
        ILabelMenuItemConfigGuide mapConfig = widgetFactory.createLabelMenuItemConfig();
        mapConfig.add(mapIcon);
        mapConfig.add(map_Label);
        IMenuItemGuide map = widgetFactory.createMenuItem(mapConfig);
        map.setModelID(4000027);
        map.setEvent(4000011);
        map.setWidgetId(0);
        /* added: the Carplay Map row (icon of the Karte row, text without a
         * text table, checkbox on MODEL_ID); shown by executeCondition */
        IIconGuide carplayMapIcon = widgetFactory.createIcon();
        carplayMapIcon.setBitmaps(new int[]{4000005});
        carplayMapIcon.setBoundsByGuide(0, 0, 40, 40);
        carplayMapIcon.setCoordinateSets(new int[]{1, 0, 0, 40, 40, 0, 0, 40, 40});
        carplayMapIcon.setIntegerContentInterpretation(IIconGuide.INTEGER_AS_BITMAP_INDEX);
        ILabelGuide carplayMap_Label = widgetFactory.createLabel();
        carplayMap_Label.setText(LABEL);
        ICheckboxGuide carplayMapCheckbox = widgetFactory.createCheckbox();
        carplayMapCheckbox.setModelID(MODEL_ID);
        ICheckboxMenuItemConfigGuide carplayMapConfig = widgetFactory.createCheckboxMenuItemConfig();
        carplayMapConfig.add(carplayMapIcon);
        carplayMapConfig.add(carplayMap_Label);
        carplayMapConfig.add(carplayMapCheckbox);
        IMenuItemGuide carplayMap = widgetFactory.createMenuItem(carplayMapConfig);
        carplayMap.setModelID(MODEL_ID);
        carplayMap.setVisibleByGuide(CarPlayMapSetting.isMenuVisible());
        IIconGuide lastDestIcon = widgetFactory.createIcon();
        lastDestIcon.setBitmaps(new int[]{4000006});
        lastDestIcon.setBoundsByGuide(0, 0, 40, 40);
        lastDestIcon.setCoordinateSets(new int[]{1, 0, 0, 40, 40, 0, 0, 40, 40});
        lastDestIcon.setIntegerContentInterpretation(IIconGuide.INTEGER_AS_BITMAP_INDEX);
        ILabelGuide lastDestinations_Label = widgetFactory.createLabel();
        lastDestinations_Label.setTextIds(new int[]{4000002});
        ILabelMenuItemConfigGuide last_destinationsConfig = widgetFactory.createLabelMenuItemConfig();
        last_destinationsConfig.add(lastDestIcon);
        last_destinationsConfig.add(lastDestinations_Label);
        IMenuItemGuide last_destinations = widgetFactory.createMenuItem(last_destinationsConfig);
        last_destinations.setModelID(4000027);
        last_destinations.setEvent(4000011);
        last_destinations.setWidgetId(2);
        IIconGuide favIcon = widgetFactory.createIcon();
        favIcon.setBitmaps(new int[]{4000002});
        favIcon.setBoundsByGuide(0, 0, 40, 40);
        favIcon.setCoordinateSets(new int[]{1, 0, 0, 40, 40, 0, 0, 40, 40});
        favIcon.setIntegerContentInterpretation(IIconGuide.INTEGER_AS_BITMAP_INDEX);
        ILabelGuide favorites_Label = widgetFactory.createLabel();
        favorites_Label.setTextIds(new int[]{4000128});
        ILabelMenuItemConfigGuide favoritesConfig = widgetFactory.createLabelMenuItemConfig();
        favoritesConfig.add(favIcon);
        favoritesConfig.add(favorites_Label);
        IMenuItemGuide favorites = widgetFactory.createMenuItem(favoritesConfig);
        favorites.setModelID(4000027);
        favorites.setEvent(4000011);
        favorites.setWidgetId(1);
        ILabelGuide empty_Label1 = widgetFactory.createLabel();
        ILabelMenuItemConfigGuide emptySpaceBeforeSeperatingLineConfig = widgetFactory.createLabelMenuItemConfig();
        emptySpaceBeforeSeperatingLineConfig.add(empty_Label1);
        IMenuItemGuide emptySpaceBeforeSeperatingLine = widgetFactory.createMenuItem(emptySpaceBeforeSeperatingLineConfig);
        emptySpaceBeforeSeperatingLine.setBoundsByGuide(0, 0, -1, 1);
        emptySpaceBeforeSeperatingLine.setEnabledByGuide(false);
        emptySpaceBeforeSeperatingLine.setInfoLineEnabled(false);
        emptySpaceBeforeSeperatingLine.setSeparatingLineType(1);
        emptySpaceBeforeSeperatingLine.setSkippedByCursor(true);
        ILabelGuide empty_Label2 = widgetFactory.createLabel();
        ILabelMenuItemConfigGuide emptySpaceAfterSeperatingLineConfig = widgetFactory.createLabelMenuItemConfig();
        emptySpaceAfterSeperatingLineConfig.add(empty_Label2);
        IMenuItemGuide emptySpaceAfterSeperatingLine = widgetFactory.createMenuItem(emptySpaceAfterSeperatingLineConfig);
        emptySpaceAfterSeperatingLine.setBoundsByGuide(0, 0, -1, 42);
        emptySpaceAfterSeperatingLine.setEnabledByGuide(false);
        emptySpaceAfterSeperatingLine.setInfoLineEnabled(false);
        emptySpaceAfterSeperatingLine.setSkippedByCursor(true);
        IIconGuide stopGuidanceIcon = widgetFactory.createIcon();
        stopGuidanceIcon.setBitmaps(new int[]{4000008});
        stopGuidanceIcon.setBoundsByGuide(0, 0, 40, 40);
        stopGuidanceIcon.setCoordinateSets(new int[]{1, 0, 0, 40, 40, 0, 0, 40, 40});
        stopGuidanceIcon.setIntegerContentInterpretation(IIconGuide.INTEGER_AS_BITMAP_INDEX);
        ILabelGuide stopGuidance_Label = widgetFactory.createLabel();
        stopGuidance_Label.setTextIds(new int[]{4000051});
        ILabelMenuItemConfigGuide stop_guidanceConfig = widgetFactory.createLabelMenuItemConfig();
        stop_guidanceConfig.add(stopGuidanceIcon);
        stop_guidanceConfig.add(stopGuidance_Label);
        IMenuItemGuide stop_guidance = widgetFactory.createMenuItem(stop_guidanceConfig);
        stop_guidance.setModelID(4000007);
        IIconGuide mapOrientIcon = widgetFactory.createIcon();
        mapOrientIcon.setBitmaps(new int[]{4000009});
        mapOrientIcon.setBoundsByGuide(0, 0, 40, 40);
        mapOrientIcon.setCoordinateSets(new int[]{1, 0, 0, 40, 40, 0, 0, 40, 40});
        mapOrientIcon.setIntegerContentInterpretation(IIconGuide.INTEGER_AS_BITMAP_INDEX);
        ILabelGuide mapDisplay_Label = widgetFactory.createLabel();
        mapDisplay_Label.setTextIds(new int[]{4000004});
        ISubmenuMenuItemConfigGuide map_orientationConfig = widgetFactory.createSubmenuMenuItemConfig();
        map_orientationConfig.add(mapOrientIcon);
        map_orientationConfig.add(mapDisplay_Label);
        IMenuItemGuide map_orientation = widgetFactory.createMenuItem(map_orientationConfig);
        map_orientation.setEvent(400488);
        IIconGuide autoZoomIcon = widgetFactory.createIcon();
        autoZoomIcon.setBitmaps(new int[]{4000010});
        autoZoomIcon.setBoundsByGuide(0, 0, 40, 40);
        autoZoomIcon.setCoordinateSets(new int[]{1, 0, 0, 40, 40, 0, 0, 40, 40});
        autoZoomIcon.setIntegerContentInterpretation(IIconGuide.INTEGER_AS_BITMAP_INDEX);
        ILabelGuide autoZoom_Label = widgetFactory.createLabel();
        autoZoom_Label.setTextIds(new int[]{4000005});
        ISubmenuMenuItemConfigGuide auto_zoom_not_AsiaConfig = widgetFactory.createSubmenuMenuItemConfig();
        auto_zoom_not_AsiaConfig.add(autoZoomIcon);
        auto_zoom_not_AsiaConfig.add(autoZoom_Label);
        IMenuItemGuide auto_zoom_not_Asia = widgetFactory.createMenuItem(auto_zoom_not_AsiaConfig);
        auto_zoom_not_Asia.setEvent(400491);
        IIconGuide autoZoom_Asia_Icon = widgetFactory.createIcon();
        autoZoom_Asia_Icon.setBitmaps(new int[]{4000010});
        autoZoom_Asia_Icon.setBoundsByGuide(0, 0, 40, 40);
        autoZoom_Asia_Icon.setCoordinateSets(new int[]{1, 0, 0, 40, 40, 0, 0, 40, 40});
        autoZoom_Asia_Icon.setIntegerContentInterpretation(IIconGuide.INTEGER_AS_BITMAP_INDEX);
        ICheckboxGuide autoZoom_Asia_Checkbox = widgetFactory.createCheckbox();
        autoZoom_Asia_Checkbox.setModelID(405177);
        ICheckboxMenuItemConfigGuide autoZoom_AsiaConfig = widgetFactory.createCheckboxMenuItemConfig();
        autoZoom_AsiaConfig.setTextIds(new int[]{4000142});
        autoZoom_AsiaConfig.add(autoZoom_Asia_Icon);
        autoZoom_AsiaConfig.add(autoZoom_Asia_Checkbox);
        IMenuItemGuide autoZoom_Asia = widgetFactory.createMenuItem(autoZoom_AsiaConfig);
        autoZoom_Asia.setModelID(405177);
        IIconGuide trafficStatusBarIcon = widgetFactory.createIcon();
        trafficStatusBarIcon.setBitmaps(new int[]{4000011});
        trafficStatusBarIcon.setBoundsByGuide(0, 0, 40, 40);
        trafficStatusBarIcon.setCoordinateSets(new int[]{1, 0, 0, 40, 40, 0, 0, 40, 40});
        trafficStatusBarIcon.setIntegerContentInterpretation(IIconGuide.INTEGER_AS_BITMAP_INDEX);
        ICheckboxGuide trafficStatusBarCheckbox = widgetFactory.createCheckbox();
        trafficStatusBarCheckbox.setModelID(4000024);
        ICheckboxMenuItemConfigGuide trafficStatusBar_AsiaConfig = widgetFactory.createCheckboxMenuItemConfig();
        trafficStatusBar_AsiaConfig.setTextIds(new int[]{4000107});
        trafficStatusBar_AsiaConfig.add(trafficStatusBarIcon);
        trafficStatusBar_AsiaConfig.add(trafficStatusBarCheckbox);
        IMenuItemGuide trafficStatusBar_Asia = widgetFactory.createMenuItem(trafficStatusBar_AsiaConfig);
        trafficStatusBar_Asia.setModelID(4000024);
        IIconGuide mapContentIcon = widgetFactory.createIcon();
        mapContentIcon.setBitmaps(new int[]{4000012});
        mapContentIcon.setBoundsByGuide(0, 0, 40, 40);
        mapContentIcon.setCoordinateSets(new int[]{1, 0, 0, 40, 40, 0, 0, 40, 40});
        mapContentIcon.setIntegerContentInterpretation(IIconGuide.INTEGER_AS_BITMAP_INDEX);
        ILabelGuide mapContent_Label = widgetFactory.createLabel();
        mapContent_Label.setTextIds(new int[]{4000006});
        ISubmenuMenuItemConfigGuide map_contentConfig = widgetFactory.createSubmenuMenuItemConfig();
        map_contentConfig.add(mapContentIcon);
        map_contentConfig.add(mapContent_Label);
        IMenuItemGuide map_content = widgetFactory.createMenuItem(map_contentConfig);
        map_content.setEvent(400432);
        IIconGuide warningIcon = widgetFactory.createIcon();
        warningIcon.setBitmaps(new int[]{4000013});
        warningIcon.setBoundsByGuide(0, 0, 40, 40);
        warningIcon.setCoordinateSets(new int[]{1, 0, 0, 40, 40, 0, 0, 40, 40});
        warningIcon.setIntegerContentInterpretation(IIconGuide.INTEGER_AS_BITMAP_INDEX);
        ILabelGuide warning_Label = widgetFactory.createLabel();
        warning_Label.setTextIds(new int[]{4000087});
        ISubmenuMenuItemConfigGuide warnings_AsiaConfig = widgetFactory.createSubmenuMenuItemConfig();
        warnings_AsiaConfig.add(warningIcon);
        warnings_AsiaConfig.add(warning_Label);
        IMenuItemGuide warnings_Asia = widgetFactory.createMenuItem(warnings_AsiaConfig);
        warnings_Asia.setEvent(4000006);
        IMenuCombiGuide menu = widgetFactory.createMenuCombi();
        menu.setBoundsByGuide(523, 216, 876, 367);
        menu.setCoordinateSets(new int[]{3, 523, 216, 876, 367, 703, 216, 516, 367});
        menu.setItemsGap(7);
        menu.add(audi2PlusFocusCursor, 1);
        menu.add(selection_Cursor, 2);
        menu.add(map);
        menu.add(carplayMap);
        menu.add(last_destinations);
        menu.add(favorites);
        menu.add(emptySpaceBeforeSeperatingLine);
        menu.add(emptySpaceAfterSeperatingLine);
        menu.add(stop_guidance);
        menu.add(map_orientation);
        menu.add(auto_zoom_not_Asia);
        menu.add(autoZoom_Asia);
        menu.add(trafficStatusBar_Asia);
        menu.add(map_content);
        menu.add(warnings_Asia);
        IScreenAreaContainerGuide mainArea = widgetFactory.createClusterGlassPlate();
        mainArea.setBoundsByGuide(0, 0, 1920, 720);
        mainArea.add(menu);
        ILabelGuide screenName = widgetFactory.createLabel();
        screenName.setTextIds(new int[]{4000016});
        screenName.setAlignment(2, 6);
        screenName.setBoundsByGuide(114, 0, 792, 62);
        screenName.setCoordinateSets(new int[]{3, 114, 0, 792, 62, 96, 0, 432, 62});
        screenName.setHighlightedByGuide(true);
        IIconGuide screenNameBackgroundImage_GB = widgetFactory.createIcon();
        screenNameBackgroundImage_GB.setBitmaps(new int[]{653});
        screenNameBackgroundImage_GB.setBoundsByGuide(0, 79, 1020, -1);
        screenNameBackgroundImage_GB.setCoordinateSets(new int[]{1, 0, 79, 1020, -1, 0, 79, 432, -1});
        screenNameBackgroundImage_GB.setIntegerContentInterpretation(IIconGuide.INTEGER_AS_BITMAP_INDEX);
        screenNameBackgroundImage_GB.setScaleMode(1);
        screenNameBackgroundImage_GB.setVisibleByGuide(false);
        IIconGuide screenNameBackgroundImage_KB = widgetFactory.createIcon();
        screenNameBackgroundImage_KB.setBitmaps(new int[]{658});
        screenNameBackgroundImage_KB.setBoundsByGuide(0, 79, 626, -1);
        screenNameBackgroundImage_KB.setCoordinateSets(new int[]{2, 0, 79, 626, -1, 0, 79, 626, -1});
        screenNameBackgroundImage_KB.setIntegerContentInterpretation(IIconGuide.INTEGER_AS_BITMAP_INDEX);
        screenNameBackgroundImage_KB.setScaleMode(1);
        screenNameBackgroundImage_KB.setVisibleByGuide(false);
        IScreenAreaContainerGuide screenNameArea = widgetFactory.createScreenAreaContainer();
        screenNameArea.setBoundsByGuide(452, 136, 1020, 62);
        screenNameArea.setCoordinateSets(new int[]{3, 452, 136, 1020, 62, 649, 136, 432, 62});
        screenNameArea.add(screenName);
        screenNameArea.add(screenNameBackgroundImage_GB);
        screenNameArea.add(screenNameBackgroundImage_KB);
        IScreenGuide nAV_FPK_MENUScreen = widgetFactory.createScreen(4000003, screenFactory, terminalID);
        nAV_FPK_MENUScreen.setFonts(FontFactory.getFonts(0));
        nAV_FPK_MENUScreen.setColorIndices(new int[]{7, 0, 4, 2, 0});
        nAV_FPK_MENUScreen.setColorPalettes(new int[][]{{-1, 255, -1, -1, 0x666666FF, -286331137, -1751672833, 0, -88398337, 278507007, -2085221633, -1526331649}, {-1, 255, -1526331649, -1550414337, 0x666666FF, -277975297, -1644166913, 0, -88398337, 278507007, -2085221633, -1526331649}, {-1, 255, -88398337, -35146753, 0x666666FF, -1005057, -373003009, 0, -88398337, 278507007, -2085221633, -1526331649}, {-1, 255, 278507007, 2093470463, 0x666666FF, -1863122945, 1421856255, 0, -88398337, 278507007, -2085221633, -1526331649}, {-1, 255, -2085221633, -1042643713, 0x666666FF, -721771009, -2137105665, 0, -88398337, 278507007, -2085221633, -1526331649}});
        nAV_FPK_MENUScreen.setPermanentElementsState(new int[]{0, 0});
        nAV_FPK_MENUScreen.add(mainArea, 14);
        nAV_FPK_MENUScreen.add(screenNameArea, 3);
        nAV_FPK_MENUScreen.setModelIDs(new int[]{4000027});
        nAV_FPK_MENUScreen.setEventIDs(new int[]{4000011});
        /* sorted: AbstractScreenBase looks the ids up with a binary search */
        nAV_FPK_MENUScreen.setConditionWidgets(new int[]{442, 403808, 403815, MODEL_ID}, new IAbstractWidgetGuide[][]{{auto_zoom_not_Asia, autoZoom_Asia, trafficStatusBar_Asia, warnings_Asia}, {auto_zoom_not_Asia, autoZoom_Asia}, {stop_guidance}, {carplayMap}});
        return (Screen) nAV_FPK_MENUScreen;
    }

}
