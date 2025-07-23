import QtQuick 2.15
import QtQuick.Controls 2.15

import Theme 1.0
import Primitives 1.0

Button {
    id: control
    implicitHeight: 34

    foregroundColor: ColorSet {
        normal: "#B1B1E3"
        hover: Theme.color.bluepurple1
        down: Theme.color.bluepurple5
        disabled: Theme.color.bluepurple2
    }

    backgroundColor: ColorSet {
        normal: Theme.color.transparent
        hover: "#7f7474b0" // bluepurple2 with 50% opacity
        down: Theme.color.bluepurple2
        disabled: Theme.color.transparent
    }

    strokeColor: ColorSet {
        normal: Theme.color.bluepurple4
        hover: Theme.color.bluepurple3
        down: Theme.color.bluepurple2
        disabled: "#3D3D73"
    }
}
