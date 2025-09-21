import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import Theme 1.0
import QFlipper 1.0

Item {
    id: control

    // Ensure the view has a natural size inside TabPane
    implicitWidth: 745
    implicitHeight: 360
    anchors.fill: parent

    property alias cliManager: cliManagerInstance
    property bool active: false
    property bool coolingDown: false
    // Reserve space so the command bar stays above the LOGS/READY footer
    property int reservedBottom: 48

    CliManager {
        id: cliManagerInstance
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        // No top status bar
        Item { Layout.fillWidth: true; Layout.preferredHeight: 0 }

        // Terminal display
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 2
            Layout.bottomMargin: 0
            color: "#000000"
            border.color: "transparent"
            border.width: 0
            radius: 0

            ScrollView {
                id: scrollView
                anchors.fill: parent
                anchors.margins: 0
                contentWidth: availableWidth
                clip: true

                TextEdit {
                    id: terminalOutput
                    width: scrollView.contentWidth
                    
                    text: cliManagerInstance.terminalOutput
                    font.family: "Share Tech Mono"
                    font.pixelSize: 12
                    color: "#00ff00"
                    
                    readOnly: true
                    selectByMouse: true
                    selectByKeyboard: true
                    wrapMode: TextEdit.Wrap
                    
                    selectionColor: Theme.color.bluepurple1
                    selectedTextColor: "#ffffff"
                    padding: 0
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: 2
                    rightPadding: 2

                    onTextChanged: {
                        // Auto-scroll to bottom
                        if (scrollView.ScrollBar.vertical) {
                            scrollView.ScrollBar.vertical.position = 1.0 - scrollView.ScrollBar.vertical.size
                        }
                    }
                }
            }
        }

        // Command input
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            Layout.bottomMargin: 4
            Layout.topMargin: 2
            color: Theme.color.bluepurple7
            border.color: Theme.color.bluepurple5
            border.width: 1
            radius: 4

            RowLayout {
                anchors.fill: parent
                anchors.margins: 4
                anchors.rightMargin: 12
                anchors.leftMargin: 6
                spacing: 6

                TextLabel {
                    text: ">"
                    color: Theme.color.bluepurple1
                    font.family: "Share Tech Mono"
                    font.pixelSize: 12
                }

                TextField {
                    id: commandInput
                    Layout.fillWidth: true
                    
                    font.family: "Share Tech Mono"
                    font.pixelSize: 12
                    color: Theme.color.bluepurple1
                    
                    placeholderText: qsTr("Enter CLI command...")
                    placeholderTextColor: "#a1a1ce"
                    enabled: cliManagerInstance.isReady
                    
                    background: Rectangle {
                        color: "transparent"
                        border.color: "transparent"
                    }

                    onAccepted: {
                        if (text.trim().length > 0 && cliManagerInstance.isReady) {
                            cliManagerInstance.sendCommand(text.trim())
                            text = ""
                        }
                    }

                    Keys.onUpPressed: {
                        // TODO: Implement command history
                    }

                    Keys.onDownPressed: {
                        // TODO: Implement command history
                    }
                }

                // Clear button (icon-only)
                Button {
                    id: clearButton
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 18
                    focusPolicy: Qt.NoFocus
                    icon.source: "qrc:/assets/gfx/symbolic/trashcan.svg"
                    icon.width: 12
                    icon.height: 12
                    text: ""
                    enabled: cliManagerInstance.isConnected
                    background: Rectangle { color: "transparent"; border.color: Theme.color.bluepurple5; radius: 4; border.width: 1 }
                    onClicked: cliManagerInstance.clearTerminal()
                }

                // Send button (icon-only)
                Button {
                    id: sendButton
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 18
                    focusPolicy: Qt.NoFocus
                    icon.source: "qrc:/assets/gfx/symbolic/arrow-forward-small.svg"
                    icon.width: 12
                    icon.height: 12
                    text: ""
                    enabled: cliManagerInstance.isReady && commandInput.text.trim().length > 0
                    background: Rectangle { color: "transparent"; border.color: Theme.color.bluepurple5; radius: 4; border.width: 1 }
                    onClicked: {
                        if (commandInput.text.trim().length > 0) {
                            cliManagerInstance.sendCommand(commandInput.text.trim())
                            commandInput.text = ""
                        }
                    }
                }
            }
        }

        // Spacer to keep the command bar above the bottom LOGS/READY bar
        Item { Layout.fillWidth: true; Layout.preferredHeight: reservedBottom }
    }

    // Absorb background clicks (but allow controls to receive input)
    MouseArea {
        anchors.fill: parent
        z: -1
        acceptedButtons: Qt.AllButtons
        onPressed: function(mouse) { mouse.accepted = true }
        onClicked: function(mouse) { mouse.accepted = true }
    }

    // Error handling
    Connections {
        target: cliManagerInstance
        function onErrorOccurred(error) {
            console.error("CLI Error:", error)
        }
    }

    // Delayed connect after RPC stop
    Timer {
        id: connectDelay
        interval: 700
        repeat: false
        onTriggered: {
            const portName = Backend.deviceState && Backend.deviceState.info ? Backend.deviceState.info.systemLocation : ""
            if (portName && !cliManagerInstance.isConnected) {
                cliManagerInstance.connectToDevice(portName)
            }
        }
    }

    // Cooldown after CLI deactivation to avoid rapid re-open races
    Timer {
        id: cooldownTimer
        interval: 700
        repeat: false
        onTriggered: coolingDown = false
    }

    // If user re-enters during cooldown, delay activation until safe
    Timer {
        id: deferredActivate
        interval: 750
        repeat: false
        onTriggered: {
            if (!active) activate()
        }
    }

    function activate() {
        if (active)
            return
        if (coolingDown) {
            deferredActivate.restart()
            return
        }
        active = true
        Backend.setCliActive(true)
        Backend.enterCliMode()
        connectDelay.restart()
        commandInput.forceActiveFocus()
    }

    function deactivate() {
        if (!active)
            return
        active = false
        Backend.setCliActive(false)
        if (cliManagerInstance.isConnected) {
            cliManagerInstance.disconnectFromDevice()
        }
        // Give the OS a brief moment to release the handle, then rescan and resume RPC
        Qt.callLater(function() {
            Backend.exitCliMode()
        })
        // Start cooldown and cancel any pending re-activation
        deferredActivate.stop()
        coolingDown = true
        cooldownTimer.restart()
    }

    Connections {
        target: cliManagerInstance
        function onIsReadyChanged() {
            if (cliManagerInstance.isReady) {
                commandInput.forceActiveFocus()
            }
        }
    }
}
