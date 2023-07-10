/*
    SPDX-FileCopyrightText: 2011 Viranch Mehta <viranch.mehta@gmail.com>
    SPDX-FileCopyrightText: 2012 Jacopo De Simoi <wilderkde@gmail.com>
    SPDX-FileCopyrightText: 2014 David Edmundson <davidedmundson@kde.org>
    SPDX-FileCopyrightText: 2016 Kai Uwe Broulik <kde@privat.broulik.de>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

import QtQuick 2.15
import QtQuick.Layouts 1.1
import org.kde.plasma.plasmoid 2.0
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.plasma5support 2.0 as P5Support

import org.kde.kcmutils // For KCMLauncher
import org.kde.config // KAuthorized

PlasmoidItem {
    id: devicenotifier

    readonly property bool openAutomounterKcmAuthorized: KAuthorized.authorizeControlModule("device_automounter_kcm")

    property string devicesType: {
        if (Plasmoid.configuration.allDevices) {
            return "all"
        } else if (Plasmoid.configuration.removableDevices) {
            return "removable"
        } else {
            return "nonRemovable"
        }
    }
    property string popupIcon: "device-notifier"

    property bool itemClicked: false
    property int currentIndex: -1
    property var connectedRemovables: []
    property int mountedRemovables: 0
    signal unmountAllRequested

    // QTBUG-50380: As soon as the item gets removed from the model, all of ListView's
    // properties (count, contentHeight) pretend the delegate doesn't exist anymore
    // causing our "No devices" heading to overlap with the remaining device
    property bool isMessageHighlightAnimatorRunning: false

    switchWidth: PlasmaCore.Units.gridUnit * 10
    switchHeight: PlasmaCore.Units.gridUnit * 10

    toolTipMainText: filterModel.count > 0 && filterModel.get(0) ? i18n("Most Recent Device") : i18n("No Devices Available")
    toolTipSubText: {
        if (filterModel.count > 0) {
            var data = filterModel.get(0)
            if (data && data.Description) {
                return data.Description
            }
        }
        return ""
    }
    Plasmoid.icon: {
        if (filterModel.count > 0) {
            var data = filterModel.get(0)
            if (data && data.Icon) {
                return data.Icon
            }
        }
        return "device-notifier"
    }

    Plasmoid.status: (filterModel.count > 0 || isMessageHighlightAnimatorRunning) ? PlasmaCore.Types.ActiveStatus : PlasmaCore.Types.PassiveStatus

    P5Support.DataSource {
        id: hpSource
        engine: "hotplug"
        connectedSources: sources
        interval: 0

        onSourceAdded: source => {
            disconnectSource(source);
            connectSource(source);
            sdSource.connectedSources = sources
        }
        onSourceRemoved: source => {
            disconnectSource(source);
        }
    }

    compactRepresentation: PlasmaCore.IconItem {
        source: devicenotifier.popupIcon
        width: PlasmaCore.Units.iconSizes.medium;
        height: PlasmaCore.Units.iconSizes.medium;
        active: compactMouse.containsMouse
        MouseArea {
            id: compactMouse
            anchors.fill: parent
            activeFocusOnTab: true
            hoverEnabled: true
            Accessible.name: Plasmoid.title
            Accessible.description: `${toolTipMainText}: ${toolTipSubText}`
            Accessible.role: Accessible.Button
            onClicked: devicenotifier.expanded = !devicenotifier.expanded
        }
    }
    fullRepresentation: FullRepresentation {}

    P5Support.DataSource {
        id: sdSource
        engine: "soliddevice"
        interval: 0
        property string last
        onSourceAdded: source => {
            disconnectSource(source);
            connectSource(source);
            last = source;
            processLastDevice(true);
            if (data[source].Removable) {
                devicenotifier.connectedRemovables.push(source);
                devicenotifier.connectedRemovables = devicenotifier.connectedRemovables;
            }
        }

        onSourceRemoved: source => {
            disconnectSource(source);
            var index = devicenotifier.connectedRemovables.indexOf(source);
            if (index >= 0) {
                devicenotifier.connectedRemovables.splice(index, 1);
                devicenotifier.connectedRemovables = devicenotifier.connectedRemovables;
            }
        }

        onDataChanged: {
            processLastDevice(true);
            var counter = 0;
            for (var i = 0; i < devicenotifier.connectedRemovables.length; i++) {
                if (isMounted(devicenotifier.connectedRemovables[i])) {
                    counter++;
                }
            }
            if (counter !== devicenotifier.mountedRemovables) {
                devicenotifier.mountedRemovables = counter;
            }
        }

        onNewData: (sourceName, data) => {
            last = sourceName;
            processLastDevice(false);
        }

        function isViableDevice(udi) {
            if (devicesType === "all") {
                return true;
            }

            var device = data[udi];
            if (!device) {
                return false;
            }

            return (devicesType === "removable" && device.Removable)
                || (devicesType === "nonRemovable" && !device.Removable);
        }

        function processLastDevice(expand) {
            if (last && isViableDevice(last)) {
                if (expand && hpSource.data[last] && hpSource.data[last].added) {
                    devicenotifier.popupIcon = "preferences-desktop-notification";
                    expandTimer.restart();
                    popupIconTimer.restart();
                }
                last = "";
            }
        }
    }

    PlasmaCore.SortFilterModel {
        id: filterModel
        sourceModel: P5Support.DataModel {
            dataSource: sdSource
        }
        filterRole: "Removable"
        filterRegExp: {
            if (devicesType === "removable") {
                return "true"
            } else if (devicesType === "nonRemovable") {
                return "false"
            } else {
                return ""
            }
        }
        sortRole: "Timestamp"
        sortOrder: Qt.DescendingOrder
    }

    P5Support.DataSource {
        id: statusSource
        engine: "devicenotifications"
        property string last
        property string lastUdi
        property string lastDescription
        property string lastMessage
        property string lastIcon
        onSourceAdded: source => {
            last = source;
            disconnectSource(source);
            connectSource(source);
        }
        onSourceRemoved: source => disconnectSource(source)
        onDataChanged: {
            if (last) {
                lastUdi = data[last].udi
                lastDescription = sdSource.data[lastUdi] ? sdSource.data[lastUdi].Description : ""
                lastMessage = data[last].error
                lastIcon = sdSource.data[lastUdi] ? sdSource.data[lastUdi].Icon : "device-notifier"

                if (sdSource.isViableDevice(lastUdi)) {
                    devicenotifier.expanded = true
                    fullRepresentationItem.spontaneousOpen = true;
                }
            }
        }

        function clearMessage() {
            last = ""
            lastUdi = ""
            lastDescription = ""
            lastMessage = ""
            lastIcon = ""
        }
    }

    PlasmaCore.Action {
        id: configureAction
        icon.name: "configure"
        text: i18nc("Open auto mounter kcm", "Configure Removable Devices…")
        visible: devicenotifier.openAutomounterKcmAuthorized
        shortcut: "Alt+D, S"
        onTriggered: KCMLauncher.openSystemSettings("kcm_device_automounter")
    }

    Component.onCompleted: {
        if (sdSource.connectedSources.count === 0) {
            Plasmoid.status = PlasmaCore.Types.PassiveStatus;
        }
        Plasmoid.setInternalAction("configure", configureAction);
    }

    PlasmaCore.ActionGroup {
        id: devicesGroup
    }
    Plasmoid.contextualActions: [
        PlasmaCore.Action {
            text: i18n("Remove All")
            visible: devicenotifier.mountedRemovables > 0
            onTriggered: devicenotifier.unmountAllRequested()
        },
        PlasmaCore.Action {
            isSeparator: true
        },
        PlasmaCore.Action {
            text: i18n("Removable Devices")
            icon.name: "drive-removable-media"
            checkable: true
            checked: Plasmoid.configuration.removableDevices
            actionGroup: devicesGroup
            onTriggered: checked => {
                if (!checked) {
                    return;
                }
                Plasmoid.configuration.removableDevices = true;
                Plasmoid.configuration.nonRemovableDevices = false;
                Plasmoid.configuration.allDevices = false;
            }
        },
        PlasmaCore.Action {
            text: i18n("Non Removable Devices")
            icon.name: "drive-harddisk"
            checkable: true
            checked: Plasmoid.configuration.nonRemovableDevices
            actionGroup: devicesGroup
            onTriggered: checked => {
                if (!checked) {
                    return;
                }
                Plasmoid.configuration.removableDevices = false;
                Plasmoid.configuration.nonRemovableDevices = true;
                Plasmoid.configuration.allDevices = false;
            }
        },
        PlasmaCore.Action {
            text: i18n("All Devices")
            checkable: true
            checked: Plasmoid.configuration.allDevices
            actionGroup: devicesGroup
            onTriggered: checked => {
                if (!checked) {
                    return;
                }
                Plasmoid.configuration.removableDevices = false;
                Plasmoid.configuration.nonRemovableDevices = false;
                Plasmoid.configuration.allDevices = true;
            }
        },
        PlasmaCore.Action {
            isSeparator: true
        },
        PlasmaCore.Action {
            text: i18n("Show popup when new device is plugged in")
            checkable: true
            checked: Plasmoid.configuration.popupOnNewDevice
            onTriggered: checked => {
                Plasmoid.configuration.popupOnNewDevice = checked;
            }
        },
        PlasmaCore.Action {
            isSeparator: true
        }
    ]

    onExpandedChanged: {
        popupEventSlot(devicenotifier.expanded);
    }

    function popupEventSlot(popped) {
        if (!popped) {
            // reset the property that lets us remember if an item was clicked
            // (versus only hovered) for autohide purposes
            devicenotifier.itemClicked = true;
            devicenotifier.currentIndex = -1;
        }
    }

    function isMounted(udi) {
        if (!sdSource.data[udi]) {
            return false;
        }

        var types = sdSource.data[udi]["Device Types"];
        if (types.indexOf("Storage Access") >= 0) {
            return sdSource.data[udi]["Accessible"];
        }

        return (types.indexOf("Storage Volume") >= 0 && types.indexOf("OpticalDisc") >= 0)
    }

    Timer {
        id: popupIconTimer
        interval: 3000
        onTriggered: devicenotifier.popupIcon  = "device-notifier";
    }

    Timer {
        id: expandTimer
        interval: 250
        onTriggered: {
            // We don't show a UI for it, but there is a hidden option to not
            // show the popup on new device attachment if the user has added
            // the text "popupOnNewDevice=false" to their
            // plasma-org.kde.plasma.desktop-appletsrc file.
            if (Plasmoid.configuration.popupOnNewDevice) { // Bug 351592
                devicenotifier.expanded = true;
                fullRepresentationItem.spontaneousOpen = true;
            }
        }
    }

}
