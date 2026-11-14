import QtQuick
import QtQuick.Controls

Window {
    id: window
    required property var brightnessController
    required property var settingsManager
    width: 800
    height: 480
    visible: true
    title: "DeskPI"
    color: settingsManager.theme === "light" ? "#f4f6fa" : "#000000"

    property date currentTime: new Date()
    property bool settingsOpen: false
    property bool settingsRevealed: false

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: {
            window.currentTime = new Date();
        }
    }

    Column {
        visible: !window.settingsOpen
        anchors.centerIn: parent
        spacing: 12
        Text {
            objectName: "clockText"
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatTime(window.currentTime,
                window.settingsManager.clock24
                    ? (window.settingsManager.showSeconds ? "HH:mm:ss" : "HH:mm")
                    : (window.settingsManager.showSeconds ? "h:mm:ss AP" : "h:mm AP"))
            color: window.settingsManager.theme === "light" ? "#182332" : "#f5f5f5"
            font.pixelSize: Math.round(Math.min(window.width * (window.settingsManager.showSeconds ? 0.10 : 0.15), window.height * 0.30))
        }
        Text {
            objectName: "clockDate"
            visible: window.settingsManager.showDate
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatDate(window.currentTime, window.settingsManager.dateFormat)
            color: window.settingsManager.accent
            font.pixelSize: 28
        }
    }

    Item {
        anchors.fill: parent
        TapHandler {
            enabled: !window.settingsOpen
            longPressThreshold: 2.0
            onLongPressed: window.settingsRevealed = true
        }
    }

    Button {
        id: settingsButton
        objectName: "openSettings"
        visible: window.settingsRevealed && !window.settingsOpen
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        width: 56
        height: 56
        Accessible.name: "Settings"
        contentItem: Item {
            Canvas {
                anchors.centerIn: parent
                width: 32
                height: 32
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = "#f5f5f5"
                    ctx.beginPath()
                    for (let i = 0; i < 32; ++i) {
                        const angle = (i - 0.5) * Math.PI / 16
                        const radius = i % 4 === 0 || i % 4 === 3 ? 12 : 16
                        const x = 16 + Math.cos(angle) * radius
                        const y = 16 + Math.sin(angle) * radius
                        if (i === 0) ctx.moveTo(x, y)
                        else ctx.lineTo(x, y)
                    }
                    ctx.closePath()
                    ctx.fill()
                    ctx.globalCompositeOperation = "destination-out"
                    ctx.beginPath()
                    ctx.arc(16, 16, 6, 0, 2 * Math.PI)
                    ctx.fill()
                }
            }
        }
        background: Rectangle {
            radius: 8
            color: settingsButton.down ? "#404040" : "#202020"
            border.color: window.settingsManager.accent
            border.width: 1
        }
        onClicked: {
            window.settingsRevealed = false
            window.settingsOpen = true
        }
    }

    BrightnessSettings {
        anchors.fill: parent
        visible: window.settingsOpen
        controller: window.brightnessController
        lightTheme: window.settingsManager.theme === "light"
        accent: window.settingsManager.accent
        onClosed: window.settingsOpen = false
    }
}
