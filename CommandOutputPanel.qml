import QtQuick

// Satır içi komut çıktısı paneli (Shift+Enter). runner: Shell.runner
// (running, output, command, exitCode, stop()). Panel kapanınca süreç durmaz.
Item {
    id: panel

    property var runner: null
    property color fgColor: "#f0f2f5"
    property color dimColor: "#8b949e"
    property color accentColor: "#1f51ff"
    property color dangerColor: "#ff5252"
    property string monoFont: "JetBrains Mono, Fira Code, monospace"

    readonly property bool running: !!(runner && runner.running)
    readonly property string titleText: {
        var c = runner ? String(runner.command || "") : ""
        var lines = c.split("\n")
        for (var i = 0; i < lines.length; i++) if (lines[i].trim().length) return lines[i].trim()
        return ""
    }
    readonly property string statusText: running ? "Running…" : ("Exit " + (runner ? runner.exitCode : 0))

    signal copyRequested(string text)
    signal closeRequested()

    function requestStop() { if (runner && running) runner.stop() }
    function requestCopy() { copyRequested(runner ? String(runner.output || "") : "") }
    function requestClose() { closeRequested() }

    // ── Başlık: komut + durum ─────────────────────────────
    Item {
        id: titleBar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 34

        Text {
            anchors { left: parent.left; leftMargin: 14; right: statusLabel.left; rightMargin: 10; verticalCenter: parent.verticalCenter }
            text: "$ " + panel.titleText
            color: panel.fgColor
            font.family: panel.monoFont
            font.pixelSize: 13
            elide: Text.ElideRight
        }
        Text {
            id: statusLabel
            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
            text: panel.statusText
            color: panel.running ? panel.accentColor
                 : (panel.runner && panel.runner.exitCode !== 0 ? panel.dangerColor : panel.dimColor)
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
    }

    // ── Çıktı ─────────────────────────────────────────────
    Flickable {
        id: outFlick
        anchors { left: parent.left; right: parent.right; top: titleBar.bottom; bottom: buttonRow.top
                  leftMargin: 12; rightMargin: 12; bottomMargin: 8 }
        clip: true
        contentWidth: width
        contentHeight: outText.paintedHeight
        boundsBehavior: Flickable.StopAtBounds
        // Kullanıcı yukarı kaydırmadıysa sona yapışık kal
        property bool stickToEnd: true
        onMovementEnded: stickToEnd = (contentY >= contentHeight - height - 4)
        onContentHeightChanged: if (stickToEnd) contentY = Math.max(0, contentHeight - height)

        TextEdit {
            id: outText
            width: outFlick.width
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.WrapAnywhere
            text: panel.runner ? String(panel.runner.output || "") : ""
            color: panel.fgColor
            selectionColor: panel.accentColor
            font.family: panel.monoFont
            font.pixelSize: 12
        }
    }

    // ── Düğmeler ──────────────────────────────────────────
    Row {
        id: buttonRow
        anchors { right: parent.right; rightMargin: 12; bottom: parent.bottom; bottomMargin: 10 }
        spacing: 8

        Repeater {
            model: [
                { label: "Stop", key: "stop" },
                { label: "Copy output", key: "copy" },
                { label: "Close  (Esc)", key: "close" }
            ]
            delegate: Rectangle {
                required property var modelData
                readonly property bool enabledBtn: modelData.key !== "stop" || panel.running
                width: btnText.implicitWidth + 22
                height: 26
                radius: 6
                opacity: enabledBtn ? 1.0 : 0.4
                color: btnMouse.containsMouse && enabledBtn ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.08)
                Behavior on color { ColorAnimation { duration: 120 } }
                Text {
                    id: btnText
                    anchors.centerIn: parent
                    text: modelData.label
                    color: modelData.key === "stop" ? panel.dangerColor : panel.fgColor
                    font.pixelSize: 12
                }
                MouseArea {
                    id: btnMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: parent.enabledBtn ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: {
                        if (!parent.enabledBtn) return
                        if (modelData.key === "stop") panel.requestStop()
                        else if (modelData.key === "copy") panel.requestCopy()
                        else panel.requestClose()
                    }
                }
            }
        }
    }
}
