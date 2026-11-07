import QtQuick

// Temaya göre ikon: Edna stilinde ikon temasından (kora) çizim, diğer
// temalarda Nerd Font glifi. Aynı yeri kaplar; çağıran yalnızca ikisini verir.
Item {
    id: g

    property string glyph: ""
    property string iconName: ""
    property bool useIcon: false
    property color color: "white"
    property string glyphFont: ""
    property int size: 14
    property int version: 0

    readonly property bool showIcon: useIcon && iconName.length > 0

    implicitWidth: showIcon ? size : glyphText.implicitWidth
    implicitHeight: size
    width: implicitWidth
    height: implicitHeight

    Text {
        id: glyphText
        anchors.centerIn: parent
        visible: !g.showIcon
        text: g.glyph
        color: g.color
        font.family: g.glyphFont
        font.pixelSize: g.size
    }

    Image {
        anchors.fill: parent
        visible: g.showIcon
        source: g.showIcon ? ("image://themeicon/" + g.iconName + "?v=" + g.version) : ""
        sourceSize: Qt.size(g.size * 2, g.size * 2)
        smooth: true
        mipmap: true
        fillMode: Image.PreserveAspectFit
    }
}
