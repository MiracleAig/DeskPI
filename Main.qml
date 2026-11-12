import QtQuick
import QtQuick.Controls

Window {
    id: window
    width: 800
    height: 400
    visible: true
    title: "DeskPi"
    color: "#1E1E1E"

    property date currentTime: new Date()

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: {
            window.currentTime = new Date();
        }
    }

     Text {
        anchors.centerIn: parent
        text: Qt.formatTime(window.currentTime, "h:mm AP")
        color: "#f5f5f5"
        font.pixelSize: Math.round(Math.min(window.width * 0.15,  window.height * 0.30))
    }
}
