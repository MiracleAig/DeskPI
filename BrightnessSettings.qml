pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel
    required property var controller
    signal closed()
    property bool lightTheme: false
    property color accent: "#59caff"
    color: lightTheme ? "#f4f6fa" : "#000000"
    property color foreground: lightTheme ? "#182332" : "#f5f5f5"

    component Caption: Label {
        color: panel.foreground
        font.pixelSize: 19
    }
    component TouchSpinBox: SpinBox {
        id: spin
        implicitWidth: 170
        implicitHeight: 48
        leftPadding: 48
        rightPadding: 48
        contentItem: Text {
            text: spin.textFromValue(spin.value, spin.locale)
            color: panel.foreground
            font.pixelSize: 19
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { color: panel.lightTheme ? "#ffffff" : "#202020"; radius: 4 }
        up.indicator: Rectangle {
            x: spin.width - width
            width: 48; height: spin.height
            color: spin.up.pressed ? panel.accent : panel.lightTheme ? "#dbe3ed" : "#333333"
            Text { anchors.centerIn: parent; text: "+"; color: panel.foreground; font.pixelSize: 26 }
        }
        down.indicator: Rectangle {
            width: 48; height: spin.height
            color: spin.down.pressed ? panel.accent : panel.lightTheme ? "#dbe3ed" : "#333333"
            Text { anchors.centerIn: parent; text: "−"; color: panel.foreground; font.pixelSize: 26 }
        }
    }
    component TimePicker: RowLayout {
        id: picker
        property int minutes: 0
        signal edited(int value)
        spacing: 2
        TouchSpinBox {
            from: 0; to: 23; value: Math.floor(picker.minutes / 60)
            implicitWidth: 170; implicitHeight: 48
            onValueModified: picker.edited(value * 60 + picker.minutes % 60)
        }
        Caption { text: ":" }
        TouchSpinBox {
            from: 0; to: 59; value: picker.minutes % 60
            implicitWidth: 170; implicitHeight: 48
            textFromValue: function(value) { return value.toString().padStart(2, "0") }
            onValueModified: picker.edited(Math.floor(picker.minutes / 60) * 60 + value)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Button { text: "Back"; implicitWidth: 100; implicitHeight: 48; onClicked: panel.closed() }
            BrightnessIcon { ink: panel.foreground }
            Caption { text: "Display brightness"; font.pixelSize: 23; Layout.fillWidth: true }
        }
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: scroll.availableWidth
                spacing: 8
                Caption {
                    Layout.fillWidth: true
                    text: panel.controller.available ? panel.controller.status : "Hardware control unavailable"
                    wrapMode: Text.WordWrap
                }
                Label {
                    Layout.fillWidth: true
                    visible: panel.controller.error.length > 0
                    text: panel.controller.error
                    color: "#ffb4a9"; wrapMode: Text.WordWrap; font.pixelSize: 16
                }
                RowLayout {
                    Layout.fillWidth: true
                    BrightnessIcon { opacity: 0.45; ink: panel.foreground }
                    Slider {
                        id: manualSlider
                        objectName: "manualBrightnessSlider"
                        palette.highlight: panel.accent
                        Layout.fillWidth: true
                        implicitHeight: 52
                        from: 0; to: 100; stepSize: 1
                        handle: Rectangle {
                            x: manualSlider.leftPadding + manualSlider.visualPosition * (manualSlider.availableWidth - width)
                            y: manualSlider.topPadding + (manualSlider.availableHeight - height) / 2
                            width: 32; height: 32; radius: 16
                            color: manualSlider.pressed ? panel.accent : panel.foreground
                        }
                        enabled: panel.controller.available
                        onMoved: panel.controller.setManual(Math.round(value))
                        Binding {
                            target: manualSlider; property: "value"
                            value: panel.controller.percentage
                            when: !manualSlider.pressed
                            restoreMode: Binding.RestoreBindingOrValue
                        }
                    }
                    BrightnessIcon { ink: panel.foreground }
                    Caption {
                        Layout.preferredWidth: 64
                        text: (manualSlider.pressed ? Math.max(panel.controller.minimum, Math.round(manualSlider.value)) : panel.controller.percentage) + "%"
                    }
                }
                Button {
                    visible: panel.controller.overrideActive
                    text: "Resume schedule now"
                    implicitHeight: 48
                    onClicked: panel.controller.resumeSchedule()
                }
                Switch {
                    id: automaticSwitch
                    text: "Automatic nighttime dimming"
                    checked: panel.controller.automatic
                    enabled: panel.controller.available
                    implicitHeight: 48
                    onToggled: panel.controller.automatic = checked
                    contentItem: Text {
                        text: automaticSwitch.text
                        color: panel.foreground
                        font.pixelSize: 19
                        leftPadding: automaticSwitch.indicator.width + automaticSwitch.spacing
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                GridLayout {
                    columns: 3
                    Layout.fillWidth: true
                    enabled: panel.controller.available
                    columnSpacing: 12
                    Caption { text: "Period" }
                    Caption { text: "Brightness (%)" }
                    Caption { text: "Starts (24-hour)" }
                    Caption { text: "Daytime" }
                    TouchSpinBox {
                        from: panel.controller.minimum; to: 100; value: panel.controller.day
                        implicitWidth: 170; implicitHeight: 48
                        onValueModified: panel.controller.day = value
                    }
                    TimePicker {
                        minutes: panel.controller.dayStart
                        onEdited: function(value) { panel.controller.dayStart = value }
                    }
                    Caption { text: "Nighttime" }
                    TouchSpinBox {
                        from: panel.controller.minimum; to: 100; value: panel.controller.night
                        implicitWidth: 170; implicitHeight: 48
                        onValueModified: panel.controller.night = value
                    }
                    TimePicker {
                        minutes: panel.controller.nightStart
                        onEdited: function(value) { panel.controller.nightStart = value }
                    }
                }
                Caption {
                    text: "A manual change overrides the schedule until its next transition. Start times must differ."
                    font.pixelSize: 15; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                RowLayout {
                    Caption { text: "Minimum brightness"; Layout.fillWidth: true }
                    TouchSpinBox {
                        from: 1; to: 100; value: panel.controller.minimum
                        enabled: panel.controller.available
                        implicitWidth: 170; implicitHeight: 48
                        onValueModified: panel.controller.minimum = value
                    }
                    Caption { text: "%" }
                }
                Caption {
                    Layout.fillWidth: true
                    text: panel.controller.device.length > 0 ? panel.controller.device + " · range 0–" + panel.controller.maximum : "No backlight detected"
                    font.pixelSize: 14; wrapMode: Text.WordWrap; opacity: 0.6
                }
            }
        }
    }
}
