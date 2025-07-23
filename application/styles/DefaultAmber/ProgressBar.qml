import QtQuick 2.15
import QtQuick.Templates 2.15 as T
import QtQuick.Controls 2.15

import Theme 1.0
import Misc 1.0

T.ProgressBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    contentItem: Item {
        id: content
        anchors.fill: parent
        anchors.margins: bg.border.width

        Text {
            id: brightText
            antialiasing: Mitigations.fontRenderingFix
            visible: !control.indeterminate
            color: Theme.color.bluepurple1
            text:  Math.round(control.value) + "%"
            anchors.centerIn: parent

            font.pixelSize: 48
            font.family: "HaxrCorp 4089"
        }

        // Zastępujemy animationText nowym animowanym wskaźnikiem kropek
        Item {
            id: animatedDots
            visible: control.indeterminate
            anchors.centerIn: parent
            width: 60
            height: 48

            property int dotCount: 4
            property int activeDot: 0
            property int dotSpacing: 16
            property int dotSize: 12
            property color dotColor: Theme.color.bluepurple1

            Timer {
                id: dotTimer
                interval: 150
                running: control.indeterminate
                repeat: true
                onTriggered: {
                    animatedDots.activeDot = (animatedDots.activeDot + 1) % animatedDots.dotCount;
                }
            }

            Row {
                id: dotRow
                anchors.centerIn: parent
                spacing: animatedDots.dotSpacing
                Repeater {
                    model: animatedDots.dotCount
                    Rectangle {
                        property int dotSize: animatedDots.dotSize
                        property int activeDot: animatedDots.activeDot
                        property color dotColor: animatedDots.dotColor
                        required property int index
                        width: dotSize
                        height: dotSize
                        radius: width/2
                        color: dotColor
                        opacity: Math.max(0.2, 1 - Math.abs(index - activeDot))
                        Behavior on opacity {
                            NumberAnimation { duration: 300; easing.type: Easing.InOutQuad }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: barFill
            clip: true
            visible: !control.indeterminate
            color: Theme.color.bluepurple1
            width: visualPosition * parent.width
            height: parent.height

            Text {
                id: darkText

                x: brightText.x
                y: brightText.y

                width: brightText.width
                height: brightText.height

                color: Theme.color.bluepurple5
                text: brightText.text
                font: brightText.font
                antialiasing: Mitigations.fontRenderingFix
            }
        }
    }

    background: Rectangle {
        id: bg
        anchors.fill: parent
        color: Theme.color.transparent
        border.color: Theme.color.bluepurple1
        border.width: 3
        radius: 9
    }
}
