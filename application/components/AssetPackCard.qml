import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import Theme 1.0
import QFlipper 1.0

Item {
    id: control

    property string packId: ""
    property string title: "Asset Pack"
    property string author: "Author"
    property string description: "Description of asset pack."
    property var previewUrls: []
    property url sourceUrl: ""
    property url zipUrl: ""
    property url targzUrl: ""
    property string targzSha256: ""
    property int packs: 0
    property int anims: 0
    property int icons: 0
    property var fonts: []
    property var passport: []
    property date lastUpdated: new Date()
    property date added: new Date()

    property bool isInstalled: false
    property bool needsUpdate: false
    property bool installing: false
    property bool uninstalling: false
    property alias progress: installProgressBar.value

    property int previewIndex: 0

    // Properties for download handling
    property string pendingDownloadUrl: ""
    property bool isDownloading: false

    Connections {
        target: AssetPacks
        
        function onRequestSaveFile(fileUrl, suggestedFileName) {
            var zipUrlString = control.zipUrl.toString();
            
            if (fileUrl !== zipUrlString) {
                return;
            }
            
            control.pendingDownloadUrl = fileUrl
            
            var acceptedConnection = function() {
                if (control.pendingDownloadUrl !== "") {
                    AssetPacks.performDownload(control.pendingDownloadUrl, SystemFileDialog.fileUrl.toString().replace("file:///", ""))
                    control.pendingDownloadUrl = ""
                }
                SystemFileDialog.accepted.disconnect(acceptedConnection)
            }
            
            SystemFileDialog.accepted.connect(acceptedConnection)
            
            SystemFileDialog.beginSaveFile(
                SystemFileDialog.DownloadsLocation,
                ["ZIP files (*.zip)", "All files (*.*)"],
                suggestedFileName
            )
        }
        
        function onDownloadStarted() {
        }
        
        function onDownloadFinished(success, message, fileUrl) {
            var zipUrlString = control.zipUrl.toString();
            if (fileUrl !== zipUrlString) {
                return;
            }
            
            control.isDownloading = false
            if (!success) {
                console.error("Download failed for", control.title, ":", message)
            }
        }
        
        function onUninstallFinished(success, message, packName) {
            if (packName !== control.title) {
                return;
            }
            
            control.uninstalling = false
            if (success) {
                console.log("Uninstall completed for", control.title)
            } else {
                console.error("Uninstall failed for", control.title, ":", message)
            }
        }
    }

    width: 225
    height: 288

    Rectangle {
        id: card
        anchors.fill: parent
        color: Theme.color.bluepurple7
        border.color: Theme.color.bluepurple3
        border.width: 2
        radius: 8
        z: 0
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 5

        Item {
            Layout.preferredHeight: 98
            Layout.preferredWidth: 196
            Layout.alignment: Qt.AlignTop | Qt.AlignHCenter

            AnimatedImage {
                id: gifPreview

                anchors.fill: parent
                Layout.preferredWidth: 196
                Layout.preferredHeight: 98

                source: previewUrls.length > 0 ? previewUrls[previewIndex] : ""
                smooth: true

                playing: control.visible

                onSourceChanged: {
                    gifPreview.playing = false;
                    gifPreview.playing = true;
                }
            }

            MouseArea {
                id: gifPreviewMouseArea
                anchors.fill: parent
                enabled: true
                hoverEnabled: true
            }

            RowLayout {
                Layout.fillWidth: true
                anchors.fill: parent
                z: 2

                visible: previewUrls.length > 1

                Item {
                    id: previousPreviewItem
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter

                    IconImage {
                        id: previousPreviewImage
                        
                        anchors.fill: parent

                        source: "qrc:/assets/gfx/symbolic/arrow-back.svg"
                        color: Theme.color.bluepurple1
                        opacity: gifPreviewMouseArea.containsMouse || previousPreviewMouseArea.containsMouse || nextPreviewMouseArea.containsMouse ? 1.0 : 0.3

                        scale: previousPreviewMouseArea.pressed ? 0.78 : 1.0

                        Behavior on opacity {
                            NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
                        }
                        Behavior on scale {
                            NumberAnimation { duration: 115; easing.type: Easing.InOutQuad }
                        }
                    }
                    MouseArea {
                        id: previousPreviewMouseArea
                        anchors.fill: parent
                        enabled: true
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.PointingHandCursor

                        onContainsMouseChanged: {
                            if (containsMouse) {
                                previousPreviewImage.color = Theme.color.bluepurple2;
                            } else {
                                previousPreviewImage.color = Theme.color.bluepurple1;
                            }
                        }
                        onClicked: {
                            control.previewIndex--;
                            if (control.previewIndex < 0) {
                                control.previewIndex = control.previewUrls.length - 1;
                            }
                        }
                    }
                }
                Item {
                    id: nextPreviewItem
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                    IconImage {
                        id: nextPreviewImage
                        
                        anchors.fill: parent

                        source: "qrc:/assets/gfx/symbolic/arrow-forward.svg"
                        color: Theme.color.bluepurple1
                        opacity: gifPreviewMouseArea.containsMouse || previousPreviewMouseArea.containsMouse || nextPreviewMouseArea.containsMouse ? 1.0 : 0.3

                        scale: nextPreviewMouseArea.pressed ? 0.78 : 1.0

                        Behavior on opacity {
                            NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
                        }
                        Behavior on scale {
                            NumberAnimation { duration: 115; easing.type: Easing.InOutQuad }
                        }
                    }
                    MouseArea {
                        id: nextPreviewMouseArea
                        anchors.fill: parent
                        enabled: true
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.PointingHandCursor

                        onContainsMouseChanged: {
                            if (containsMouse) {
                                nextPreviewImage.color = Theme.color.bluepurple2;
                            } else {
                                nextPreviewImage.color = Theme.color.bluepurple1;
                            }
                        }
                        onClicked: {
                            control.previewIndex++;
                            if (control.previewIndex >= control.previewUrls.length) {
                                control.previewIndex = 0;
                            }
                        }
                    }
                }
            }

            Item {
                id: previewDotsItem

                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                width: control.previewUrls.length * 16
                height: 24
                Layout.preferredHeight: 24
                Layout.alignment: Qt.AlignBottom | Qt.AlignHCenter

                visible: control.previewUrls.length > 1
                opacity: gifPreviewMouseArea.containsMouse || previousPreviewMouseArea.containsMouse || nextPreviewMouseArea.containsMouse ? 1.0 : 0.0

                Behavior on opacity {
                    NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
                }

                Row {
                    anchors.centerIn: parent
                    spacing: 10

                    Repeater {
                        model: control.previewUrls.length
                        Rectangle {
                            width: 9; height: 9
                            radius: 4.5
                            color: Theme.color.bluepurple1
                            opacity: index === control.previewIndex ? 1.0 : 0.55

                            Behavior on opacity {
                                NumberAnimation { duration: 120; easing.type: Easing.InOutQuad }
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.preferredHeight: 15
            Layout.fillWidth: true

            Item {
                id: titleTextItem
                Layout.fillWidth: true
                width: titleText.implicitWidth
                height: titleText.implicitHeight

                Text {
                    id: titleText
                    anchors.fill: parent
                    elide: Text.ElideRight
                    wrapMode: Text.WordWrap
                    maximumLineCount: 1
                    font.pixelSize: 14
                    font.bold: true
                    color: Theme.color.bluepurple1
                    text: title
                }
                MouseArea {
                    id: titleTextMouseArea
                    anchors.fill: parent
                    z: 2  // bring to front
                    enabled: true
                    hoverEnabled: true
                }
                ToolTip {
                    text: title
                    visible: titleTextMouseArea.containsMouse
                    delay: 750
                    y: parent.height + 7 // fix y position
                }
            }

            Item {
                id: sourceLinkItem

                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                Layout.rightMargin: 3
                implicitWidth: 18
                implicitHeight: 18

                IconImage {
                    id: sourceLinkImage
                    anchors.fill: parent
                    source: "qrc:/assets/gfx/symbolic/external-link.svg"
                    color: Theme.color.bluepurple1
                }
                MouseArea {
                    id: sourceLinkMouseArea
                    anchors.fill: parent
                    z: 2  // bring to front
                    enabled: true
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.PointingHandCursor

                    onContainsMouseChanged: {
                        if (containsMouse) {
                            sourceLinkImage.color = Theme.color.bluepurple2;
                        } else {
                            sourceLinkImage.color = Theme.color.bluepurple1;
                        }
                    }
                    onClicked:  Qt.openUrlExternally(sourceUrl)
                }
                ToolTip {
                    text: "Asset pack source"
                    visible: sourceLinkMouseArea.containsMouse
                    delay: 750
                    y: parent.height + 7 // fix y position
                }
            }

            Item {
                id: moreInfoItem

                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                implicitWidth: 22
                implicitHeight: 22

                IconImage {
                    id: moreInfoImage
                    anchors.fill: parent
                    source: "qrc:/assets/gfx/symbolic/info-small.svg"
                    color: Theme.color.bluepurple1
                }
                MouseArea {
                    id: moreInfoMouseArea
                    anchors.fill: parent
                    z: 2  // bring to front
                    enabled: true
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.PointingHandCursor
                }
                ToolTip {
                    text: {
                        var parts = [];
                        if (control.anims > 0)
                            parts.push(control.anims + qsTr(" animations"));
                        if (control.icons > 0)
                            parts.push(control.icons + qsTr(" icons"));
                        if (control.packs > 1)
                            parts.push(control.packs + qsTr(" Asset Packs"));

                        var containsText = "";
                        if (parts.length > 0)
                            containsText = qsTr("Contains ") + parts.join(parts.length === 2 ? qsTr(" and ") : (parts.length > 2 ? ", " : "")) + "\n";

                        var fontsText = (control.fonts && control.fonts.length > 0) ? qsTr("Fonts: ") + control.fonts.join(", ") + "\n" : "";
                        var passportText = (control.passport && control.passport.length > 0) ? qsTr("Passport: ") + control.passport.join(", ").replace("Background,", "Passport Background and") + "\n" : "";

                        return containsText
                            + fontsText
                            + passportText
                            + qsTr("Last updated: ") + Qt.formatDate(control.lastUpdated, "dd-MM-yyyy") + "\n"
                            + qsTr("Last added: ") + Qt.formatDate(control.added, "dd-MM-yyyy");
                    }
                    visible: moreInfoMouseArea.containsMouse
                    foregroundColor: Theme.color.bluepurple1
                    implicitWidth: 450
                    delay: 750
                    y: parent.height + 7 // fix y position
                }
            }
        }
        Text {
            id: authorText
            Layout.fillWidth: true
            
            font.pixelSize: 12
            color: Theme.color.bluepurple1
            
            elide: Text.ElideRight
            wrapMode: Text.WordWrap
            maximumLineCount: 2

            text: "by " + author
        }
        Text {
            id: descriptionText

            Layout.fillWidth: true
            Layout.fillHeight: true
            
            elide: Text.ElideRight
            wrapMode: Text.WordWrap
            maximumLineCount: 2

            font.pixelSize: 11
            color: Theme.color.bluepurple2

            text: description
        }

        SmallButton {
            Layout.fillWidth: true

            icon.source: control.isDownloading ? "" : "qrc:/assets/gfx/symbolic/save-symbolic.svg"
            icon.width: 20
            icon.height: 20

            text: control.isDownloading ? qsTr("DOWNLOADING...") : qsTr("DOWNLOAD")
            enabled: !control.isDownloading

            ToolTip {
                visible: parent.hovered
                text: qsTr("Download the Asset Pack to your computer (*.zip)")
                implicitWidth: 250
            }

            onClicked: {
                // if (AssetPacks) {
                //     control.isDownloading = true;
                //     AssetPacks.downloadAndSaveFile(control.zipUrl);
                // } else {
                //     console.error("AssetPacks is null or undefined");
                // }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            SmallButton {
                id: installButton

                Layout.fillWidth: true

                icon.source: isInstalled ? "qrc:/assets/gfx/symbolic/refresh-small.svg" : "qrc:/assets/gfx/symbolic/restore-symbolic.svg"
                icon.width: isInstalled ? 16 : 18
                icon.height: isInstalled ? 16 : 20

                text: isInstalled ? qsTr("REINSTALL") : qsTr("INSTALL")

                ToolTip {
                    visible: parent.hovered
                    text: isInstalled ? qsTr("Reinstall the Asset Pack on your Flipper Zero") : qsTr("Install the Asset Pack on your Flipper Zero")
                    implicitWidth: 250
                }

                visible: !installing && !uninstalling && !needsUpdate
            }
            SmallButtonGreen {
                id: updateButton

                Layout.fillWidth: true

                icon.source: "qrc:/assets/gfx/symbolic/restore-symbolic.svg"
                icon.width: 18
                icon.height: 20

                text: qsTr("UPDATE")

                ToolTip {
                    visible: parent.hovered
                    text: qsTr("Update the Asset Pack on your Flipper Zero")
                    implicitWidth: 250
                }

                visible: !installing && !uninstalling && needsUpdate
            }
            SmallButtonRed {
                id: uninstallButton

                Layout.preferredWidth: 34
                padding: 3

                icon.source: "qrc:/assets/gfx/symbolic/trashcan.svg"
                icon.width: 18
                icon.height: 20

                ToolTip {
                    visible: parent.hovered
                    text: qsTr("Uninstall the Asset Pack from your Flipper Zero")
                    implicitWidth: 250
                }

                onClicked: {
                    // if (AssetPacks) {
                    //     control.uninstalling = true;
                    //     AssetPacks.uninstallAssetPack(control.packId);
                    // } else {
                    //     console.error("AssetPacks is null or undefined");
                    // }
                }

                visible: (isInstalled || needsUpdate) && !installing && !uninstalling
            }


            ProgressBar {
                id: installProgressBar
                
                Layout.fillWidth: true
                Layout.preferredHeight: 34

                barColor: Theme.color.bluepurple2
                radius: 5
                fontSize: 32
                
                from: 0
                to: 100

                indeterminate: uninstalling

                visible: control.installing || control.uninstalling
            }
        }

        Text {
            id: lastUpdatedText
            
            elide: Text.ElideRight

            font.pixelSize: 11
            color: "#7F7F8F"

            text: qsTr("Last updated: ") + Qt.formatDate(lastUpdated, "dd-MM-yyyy")
        }
    }
}
