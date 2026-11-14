import QtQuick
import QtQuick.Controls

Window {
    id: window
    required property var brightnessController
    width: 800
    height: 480
    visible: true
    title: "DeskPi"
    color: "#000000"

    property date currentTime: new Date()
    property bool settingsOpen: false

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: {
            window.currentTime = new Date();
        }
    }

     Text {
        visible: !window.settingsOpen
        anchors.centerIn: parent
        text: Qt.formatTime(window.currentTime, "h:mm AP")
        color: "#f5f5f5"
        font.pixelSize: Math.round(Math.min(window.width * 0.15,  window.height * 0.30))
    }

    Button {
        objectName: "openSettings"
        visible: !window.settingsOpen
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        width: 120
        height: 48
        text: "Settings"
        onClicked: window.settingsOpen = true
    }

    BrightnessSettings {
        anchors.fill: parent
        visible: window.settingsOpen
        controller: window.brightnessController
        onClosed: window.settingsOpen = false
    }
}
