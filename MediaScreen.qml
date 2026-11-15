import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DeskBuddy.Media 1.0

Rectangle {
    id: screen
    required property var controller
    property var state: controller.current
    property var options: controller.options
    property real positionMs: 0
    property color primary: options.gradients && state.palette ? state.palette.primary : "#25354b"
    property color secondary: options.gradients && state.palette ? state.palette.secondary : "#422c42"
    function luminance(c) { function linear(v) { return v <= 0.04045 ? v/12.92 : Math.pow((v+0.055)/1.055,2.4) } return 0.2126*linear(c.r*0.4)+0.7152*linear(c.g*0.4)+0.0722*linear(c.b*0.4) }
    property real backgroundLuminance: Math.max(luminance(primary), luminance(secondary))
    property color foreground: (1.05/(backgroundLuminance+0.05)) >= ((backgroundLuminance+0.05)/0.05) ? "#ffffff" : "#10151e"
    // A dark translucent layer bounds luminance even for an unhelpful remote palette.
    gradient: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: 0; color: screen.primary }
        GradientStop { position: 1; color: screen.secondary }
    }
    Behavior on primary { ColorAnimation { duration: screen.options.animations && screen.visible ? 320 : 0 } }
    Behavior on secondary { ColorAnimation { duration: screen.options.animations && screen.visible ? 320 : 0 } }
    Rectangle { anchors.fill: parent; color: "#99000000" }
    function time(ms) { let s = Math.max(0, Math.floor(ms / 1000)); return Math.floor(s/60) + ":" + (s%60 < 10 ? "0" : "") + s%60 }
    Timer { interval: 250; repeat: true; running: screen.visible && !!screen.state.connected && screen.state.status === "playing"; onTriggered: screen.positionMs = screen.controller.position() }
    Connections { target: screen.controller; function onChanged() { screen.positionMs = screen.controller.position() } }
    onVisibleChanged: if (visible) positionMs = controller.position()
    RowLayout {
        anchors.fill: parent; anchors.margins: 32; spacing: 30
        RoundedArtwork {
            controller: screen.controller
            Layout.preferredWidth: Math.min(screen.width * 0.43, 344)
            Layout.preferredHeight: width
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 10
            Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: (screen.state.host_name || "No computer connected") + (screen.state.connected ? " · " + (screen.state.session_id || "").replace("org.mpris.MediaPlayer2.", "") : " · Disconnected"); color: "#c7cbd3"; font.pixelSize: 14; elide: Text.ElideRight }
            Text { objectName: "mediaTitle"; textFormat: Text.PlainText; Layout.fillWidth: true; Layout.topMargin: 12; text: screen.state.title || "Ready when you are"; color: screen.foreground; font.pixelSize: 32; font.weight: Font.DemiBold; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }
            Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: screen.state.artist || "DeskBuddy"; color: "#e0e1e6"; font.pixelSize: 23; elide: Text.ElideRight }
            Text { textFormat: Text.PlainText; Layout.fillWidth: true; visible: screen.options.showAlbum; text: screen.state.album || ""; color: "#c7cbd3"; font.pixelSize: 16; elide: Text.ElideRight }
            Slider {
                id: progress
                objectName: "mediaProgress"
                Layout.fillWidth: true; Layout.topMargin: 16
                from: 0; to: Math.max(1, screen.state.duration_ms || 0)
                value: screen.positionMs
                enabled: !!screen.state.connected && !!screen.state.can_control && !!screen.state.can_seek && screen.state.duration_ms > 0
                onMoved: screen.positionMs = value
                onPressedChanged: if (!pressed) screen.controller.command("seek", Math.round(value))
            }
            RowLayout {
                Layout.fillWidth: true
                Text { textFormat: Text.PlainText; text: screen.time(screen.positionMs); color: "#d0d2d9"; font.pixelSize: 14 }
                Item { Layout.fillWidth: true }
                Text { textFormat: Text.PlainText; text: screen.state.duration_ms > 0 ? screen.time(screen.state.duration_ms) : "LIVE"; color: "#d0d2d9"; font.pixelSize: 14 }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Repeater {
                    model: [ { label: "⏮", action: "previous", capability: "can_previous", name: "Previous track" },
                        { label: screen.state.status === "playing" ? "Ⅱ" : "▶", action: "toggle", capability: screen.state.status === "playing" ? "can_pause" : "can_play", name: "Play or pause" },
                        { label: "⏭", action: "next", capability: "can_next", name: "Next track" } ]
                    Button {
                        required property var modelData
                        objectName: "media-" + modelData.action
                        Layout.fillWidth: true; Layout.preferredHeight: 64
                        enabled: !!screen.state.connected && !!screen.state.can_control && !!screen.state[modelData.capability]
                        Accessible.name: modelData.name
                        contentItem: Text { textFormat: Text.PlainText; text: modelData.label; color: parent.enabled ? "white" : "#77808a"; font.pixelSize: 30; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 22; color: parent.down ? "#55ffffff" : "#22ffffff" }
                        onClicked: screen.controller.command(modelData.action)
                    }
                }
            }
        }
    }
    Button {
        objectName: "mediaDismiss"
        anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 12
        width: 80; height: 44; onClicked: screen.controller.dismiss(); Accessible.name: "Return to clock"
        contentItem: Text { textFormat: Text.PlainText; text: "Clock"; color: "#e0e1e6"; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: 16; color: parent.down ? "#44ffffff" : "#18ffffff" }
    }
}
