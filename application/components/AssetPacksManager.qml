import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

import QFlipper 1.0
import Theme 1.0

Item {
    id: container

    implicitWidth: 745
    implicitHeight: 297

    RowLayout {
        visible: AssetPacks.errorOccured

        anchors.fill: parent
        anchors.margins: 20
        anchors.leftMargin: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true

            TextLabel {
                id: errorLabel

                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 25

                capitalized: false
                font.family: "Born2bSportyV2"
                font.pixelSize: 48

                text: qsTr("Error Occurred")
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Image {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: -15

                    sourceSize: Qt.size(246, 187)
                    source: "qrc:/assets/gfx/images/error-internet.svg" 
                }
            }
        }

        TextBox {
            Layout.preferredWidth: 335
            Layout.alignment: Qt.AlignVCenter

            style: ErrorStrings.errorStyle
            text: `There was a problem reading or parsing data from the remote host.<br><br>
=========== HOW TO FIX ============<br>
1. Check your internet connection.<br>
2. Ensure that the update server is not down.<br>
3. Check if qFlipper is up to date.<br>
-----------------------------------<br>
<center><a href='https://momentum-fw.dev/'>READ MORE</a></center>`
        }
    }

    ScrollView {
        visible: !AssetPacks.errorOccured

        anchors.fill: parent

        ScrollBar.vertical.policy: ScrollBar.AlwaysOn
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        
        background: Rectangle {
            color: Theme.color.transparent
        }
        
        Flickable {
            id: flickView
            anchors.fill: parent
            contentWidth: grid.width
            contentHeight: grid.height

            boundsBehavior: Flickable.StopAtBounds

            leftMargin: 15
            topMargin: 5
            bottomMargin: 5

            GridLayout {
                id: grid
                columns: 3
                columnSpacing: 12
                rowSpacing: 15
                width: parent.width

                Repeater {
                    model: AssetPacks.count
                    
                    delegate: AssetPackCard {
                        packId: AssetPacks.idsList[index]
                        title: AssetPacks.titlesList[index]
                        author: AssetPacks.authorsList[index]
                        description: AssetPacks.descriptionsList[index]
                        previewUrls: AssetPacks.previewUrlsList[index]
                        sourceUrl: AssetPacks.sourceUrlsList[index]
                        zipUrl: AssetPacks.zipUrlsList[index]
                        targzUrl: AssetPacks.targzUrlsList[index]
                        targzSha256: AssetPacks.targzSha256List[index]
                        isInstalled: AssetPacks.isInstalledList[index]
                        isInQueue: AssetPacks.isInQueueList[index]
                        needsUpdate: AssetPacks.needsUpdateList[index]
                        packs: AssetPacks.packsList[index]
                        anims: AssetPacks.animsList[index]
                        icons: AssetPacks.iconsList[index]
                        fonts: AssetPacks.fontsList[index]
                        passport: AssetPacks.passportList[index]
                        lastUpdated: AssetPacks.lastUpdatedList[index] ? new Date(Number(AssetPacks.lastUpdatedList[index]) * 1000) : new Date()
                        added: AssetPacks.addedList[index] ? new Date(Number(AssetPacks.addedList[index]) * 1000) : new Date()
                        visible: container.visible
                    }
                }
            }
        }
    }
}
