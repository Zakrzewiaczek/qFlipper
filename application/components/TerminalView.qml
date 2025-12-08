import QtQuick 2.15
import QtQuick.Controls 2.15

import Theme 1.0
import QFlipper 1.0

Item {
    id: root

    implicitWidth: 745
    implicitHeight: 302

    property alias cliManager: cliManagerInstance
    property bool active: false
    property bool coolingDown: false

    property int terminalTopOffset: 0
    property int terminalInputGap: 0

    CliManager {
        id: cliManagerInstance
    }

    // Global keyboard handling for Ctrl-C
    Keys.onPressed: function(event) {
        if (event.modifiers === Qt.ControlModifier && event.key === Qt.Key_C) {
            if (cliManagerInstance.isConnected) {
                // Send Ctrl-C (ASCII 0x03)
                cliManagerInstance.sendControlChar(0x03)
                event.accepted = true
            }
        }
    }

    focus: active

    function scrollTerminalToBottom() {
        Qt.callLater(function() {
            if (terminalScroll.flickableItem) {
                var flickable = terminalScroll.flickableItem
                var maxScroll = Math.max(flickable.contentHeight - flickable.height, 0)
                flickable.contentY = maxScroll
            } else if (terminalScroll.ScrollBar.vertical) {
                var scrollBar = terminalScroll.ScrollBar.vertical
                var maxPosition = Math.max(1.0 - scrollBar.size, 0)
                scrollBar.position = maxPosition
            }
        })
    }

    // Terminal output
    Rectangle {
        id: terminalBox
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: inputWrapper.top
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        anchors.topMargin: 0
        anchors.bottomMargin: -4
        color: "#000000"
        radius: 6
        border.color: Theme.color.bluepurple3
        border.width: 1
        clip: false

        ScrollView {
            id: terminalScroll
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            anchors.topMargin: 6
            anchors.bottomMargin: 8
            contentWidth: availableWidth
            clip: true

            TextEdit {
                id: terminalOutput
                width: parent.width
                text: cliManagerInstance.terminalOutput
                font.family: "Share Tech Mono"
                font.pixelSize: 12
                color: "#00ff00"
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                selectionColor: Theme.color.bluepurple1
                selectedTextColor: "#ffffff"
                onTextChanged: scrollTerminalToBottom()
            }
        }
    }

    Item {
        id: inputWrapper
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        anchors.bottomMargin: 0
        height: 40

        // Input bar
        Rectangle {
            id: inputBox
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 36
            color: Theme.color.bluepurple7
            radius: 6
            border.color: Theme.color.bluepurple4
            border.width: 1

            Item {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            anchors.topMargin: 4
            anchors.bottomMargin: 4

                Text {
                    id: promptText
                    text: ">"
                    color: Theme.color.bluepurple2
                    font.family: "Share Tech Mono"
                    font.pixelSize: 13
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                }

                TextField {
                    id: commandInput
                    anchors.left: promptText.right
                    anchors.right: buttonsRow.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    padding: 0
                    topPadding: 0
                    bottomPadding: 0
                    placeholderText: "Enter CLI command..."
                    placeholderTextColor: Theme.color.bluepurple2
                    font.family: "Share Tech Mono"
                    font.pixelSize: 12
                    color: Theme.color.bluepurple1
                    enabled: cliManagerInstance.isReady
                    verticalAlignment: TextInput.AlignVCenter
                    background: Rectangle {
                        color: "transparent"
                        border.color: "transparent"
                    }

                    Keys.onPressed: function(event) {
                        // Handle Ctrl-C (interrupt signal)
                        if (event.modifiers === Qt.ControlModifier && event.key === Qt.Key_C) {
                            if (cliManagerInstance.isConnected) {
                                // Send Ctrl-C (ASCII 0x03)
                                cliManagerInstance.sendControlChar(0x03)
                                event.accepted = true
                            }
                        }
                    }

                    onAccepted: {
                        if (text.trim().length > 0 && cliManagerInstance.isReady) {
                            cliManagerInstance.sendCommand(text.trim())
                            text = ""
                        }
                    }
                }

                Row {
                    id: buttonsRow
                    anchors.right: parent.right
                    anchors.rightMargin: 0
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    Button {
                        id: clearBtn
                        width: 24
                        height: 22
                        focusPolicy: Qt.NoFocus
                        icon.source: "qrc:/assets/gfx/symbolic/trashcan.svg"
                        icon.width: 12
                        icon.height: 12
                        leftPadding: 0
                        rightPadding: 0
                        topPadding: 0
                        bottomPadding: 0
                        enabled: cliManagerInstance.isConnected
                        background: Rectangle {
                            color: "transparent"
                            border.color: Theme.color.bluepurple4
                            border.width: 1
                            radius: 4
                        }
                        onClicked: cliManagerInstance.clearTerminal()
                    }

                    Button {
                        id: sendBtn
                        width: 24
                        height: 22
                        focusPolicy: Qt.NoFocus
                        icon.source: "qrc:/assets/gfx/symbolic/arrow-forward-small.svg"
                        icon.width: 12
                        icon.height: 12
                        leftPadding: 0
                        rightPadding: 0
                        topPadding: 0
                        bottomPadding: 0
                        enabled: cliManagerInstance.isReady && commandInput.text.trim().length > 0
                        background: Rectangle {
                            color: "transparent"
                            border.color: Theme.color.bluepurple4
                            border.width: 1
                            radius: 4
                        }
                        onClicked: {
                            if (commandInput.text.trim().length > 0) {
                                cliManagerInstance.sendCommand(commandInput.text.trim())
                                commandInput.text = ""
                            }
                        }
                    }
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        z: -1
        acceptedButtons: Qt.AllButtons
        onPressed: function(mouse) { mouse.accepted = true }
        onClicked: function(mouse) { mouse.accepted = true }
    }

    Connections {
        target: cliManagerInstance
        function onErrorOccurred(error) {
            console.error("CLI Error:", error)
        }
        function onTerminalOutputChanged() {
            scrollTerminalToBottom()
        }
    }

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

    Timer {
        id: cooldownTimer
        interval: 700
        repeat: false
        onTriggered: coolingDown = false
    }

    Timer {
        id: deferredActivate
        interval: 750
        repeat: false
        onTriggered: {
            if (!active) activate()
        }
    }

    function activate() {
        if (active) return
        if (coolingDown) {
            deferredActivate.restart()
            return
        }
        if (Backend.isSwitchingMode) {
            deferredActivate.restart()
            return
        }
        // Don't activate if CLI manager is already connected (shouldn't happen, but safety check)
        if (cliManagerInstance.isConnected) {
            console.warn("CLI already connected, skipping activate")
            return
        }
        active = true
        Backend.setCliActive(true)
        Backend.enterCliMode()
        connectDelay.restart()
        commandInput.forceActiveFocus()
    }

    function deactivate() {
        if (!active) return
        if (Backend.isSwitchingMode) {
            Qt.callLater(deactivate)
            return
        }
        active = false
        Backend.setCliActive(false)
        
        // Stop all timers to prevent reconnection
        connectDelay.stop()
        deferredActivate.stop()
        
        // Call exitCliMode immediately - don't wait for disconnect
        // This allows RPC to start as soon as the port is released
        Qt.callLater(function() {
            if (!Backend.isSwitchingMode) {
                Backend.exitCliMode()
            }
        })
        
        if (cliManagerInstance.isConnected) {
            cliManagerInstance.disconnectFromDevice()
        }
        
        cliManagerInstance.clearTerminal()
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
