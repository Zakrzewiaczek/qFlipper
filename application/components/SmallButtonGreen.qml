import QtQuick 2.15
import QtQuick.Controls 2.15

import Theme 1.0
import Primitives 1.0

SmallButton {
    id: control

    foregroundColor: ColorSet {
        normal: Theme.color.lightgreen
        hover: Theme.color.lightgreen
        down: Theme.color.darkgreen
        disabled: Theme.color.mediumgreen1
    }

    backgroundColor: ColorSet {
        normal: Theme.color.mediumgreen2
        hover: Theme.color.mediumgreen1
        down: Theme.color.lightgreen
        disabled: Theme.color.transparent
    }

    strokeColor: ColorSet {
        normal: Theme.color.lightgreen
        hover: Theme.color.lightgreen
        down: Theme.color.lightgreen
        disabled: Theme.color.mediumgreen1
    }
}
