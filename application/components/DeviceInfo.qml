import QtQuick 2.15
import QtQuick.Layouts 1.15

import QFlipper 1.0
import Theme 1.0

Item {
    id: container

    implicitWidth: 360
    implicitHeight: control.implicitHeight + verticalPadding * 2

    readonly property int horizontalPadding: Math.floor((container.implicitWidth - control.implicitWidth) / 2)
    readonly property int verticalPadding: 10

    readonly property var deviceState: Backend.deviceState
    readonly property var deviceInfo: deviceState ? deviceState.info : undefined
    readonly property bool extraFields: deviceState ? !deviceState.isRecoveryMode : false

    RowLayout {
        id: control
        spacing: 30

        x: horizontalPadding + 5
        y: verticalPadding

        ColumnLayout {
            id: keys

            TextLabel {
                text: qsTr("Firmware")
                visible: extraFields
                color: Theme.color.bluepurple1
                horizontalAlignment: Text.AlignRight
                Layout.fillWidth: true
                Layout.bottomMargin: 6
            }

            TextLabel {
                text: qsTr("Build Date")
                visible: extraFields
                color: Theme.color.bluepurple3
                horizontalAlignment: Text.AlignRight
                Layout.fillWidth: true
            }

            TextLabel {
                text: qsTr("SD Card")
                visible: extraFields
                horizontalAlignment: Text.AlignRight
                color: Theme.color.bluepurple3
                Layout.fillWidth: true
            }

            TextLabel {
                text: qsTr("Databases")
                visible: extraFields
                horizontalAlignment: Text.AlignRight
                color: Theme.color.bluepurple3
                Layout.fillWidth: true
            }

            TextLabel {
                color: extraFields ? Theme.color.bluepurple3 : Theme.color.bluepurple2
                text: qsTr("Hardware")
                horizontalAlignment: Text.AlignRight
                Layout.fillWidth: true
            }

            TextLabel {
                text: qsTr("Radio FW")
                visible: extraFields
                horizontalAlignment: Text.AlignRight
                color: Theme.color.bluepurple3
                Layout.fillWidth: true
            }
        }

        ColumnLayout {
            id: values

            TextLabel {
                text: !deviceInfo ? text : deviceInfo.firmware.branch === "dev" ?
                       deviceInfo.firmware.commit : deviceInfo.firmware.version
                
                color: {
                    if(deviceInfo.firmware.channel === "development") {
                        return "darkorchid";
                    } else if(deviceInfo.firmware.channel === "release-candidate") {
                        return "darkorchid";
                    } else if(deviceInfo.firmware.channel === "release") {
                        return Theme.color.lightgreen;
                    } else {
                        return Theme.color.lightred4;
                    }
                }

                visible: extraFields
                Layout.bottomMargin: 6
            }

            TextLabel {
                text: deviceInfo ? deviceInfo.firmware.date.toLocaleDateString(Qt.locale("C"), Locale.ShortFormat) : text
                color: "#AFAFE0"
                visible: extraFields
            }

            TextLabel {
                text: deviceInfo && deviceInfo.storage.isExternalPresent ? deviceInfo.storage.externalFree + qsTr("% Free") : qsTr("Not present")
                color: deviceInfo && deviceInfo.storage.isExternalPresent ? "#AFAFE0" : Theme.color.lightred3
                visible: extraFields
            }

            TextLabel {
                text: deviceInfo && deviceInfo.storage.isAssetsInstalled ? qsTr("Installed") : qsTr("Missing")
                color: deviceInfo && deviceInfo.storage.isAssetsInstalled ? "#AFAFE0" : Theme.color.lightred3
                visible: extraFields
            }

            TextLabel {
                color: extraFields ? "#AFAFE0" : Theme.color.bluepurple1

                text: {
                    if(!deviceInfo) {
                        text
                    } else {
                        deviceInfo.hardware.version + "." +
                        deviceInfo.hardware.target +
                        deviceInfo.hardware.body +
                        deviceInfo.hardware.connect
                    }
                }

                Layout.fillWidth: true
            }

            TextLabel {
                text: deviceInfo && deviceInfo.radioVersion.length ? "%1 %2".arg(deviceInfo.radioVersion).arg(stackTypeString(deviceInfo.stackType)) : qsTr("Corrupted")
                color: deviceInfo && deviceInfo.radioVersion.length ? "#AFAFE0" : Theme.color.lightred3
                visible: extraFields
            }
        }
    }

    function stackTypeString(num) {
        switch(num) {
        case 1: return "Full";
        case 2: return "HCI";
        case 3: return "Lite";
        default: return num;
        }
    }
}
