import QtQuick
import QtTest
import "../../MenuModel.js" as MenuModel

// Benchmark (tst_ öneki yok: normal koşuya dahil değil).
// Gerçek sonuç delegate'ine yakın ağırlıkta (≈29 öğe) delegate ile
// clear()+append ile syncRows'u, yazarken değişen sorgu dizisinde karşılaştırır.
// Çalıştır: QT_QPA_PLATFORM=offscreen qmltestrunner -input tests/qml/bench_rebuild.qml
TestCase {
    name: "BenchRebuild"
    width: 495; height: 720; when: windowShown

    ListModel { id: m }
    ListView {
        id: list
        width: 495; height: 648
        model: m
        delegate: Rectangle {
            width: 495; height: 48; radius: 6; color: "transparent"
            Text { x: 10; width: 40; text: model.icon }
            Image { x: 10; width: 26; height: 26 }
            Image { x: 10; width: 26; height: 26 }
            Image { x: 10; width: 26; height: 26 }
            Column {
                x: 60
                Row { Text { text: model.label } Rectangle { width: 4; height: 4 } Text { text: model.path } }
                Row { Text { text: model.detail } Rectangle { width: 4; height: 4 } Text { text: model.section } }
            }
            Column { anchors.right: parent.right
                Text { text: model.kind } Text { text: model.provider } Text { text: model.value } }
            Item { anchors.fill: parent
                Rectangle { anchors.fill: parent; color: "transparent" }
                Rectangle { width: 2; height: 2 } Rectangle { width: 2; height: 2 }
                Rectangle { width: 2; height: 2 } Rectangle { width: 2; height: 2 }
                Rectangle { width: 2; height: 2 } Rectangle { width: 2; height: 2 } }
            MouseArea { anchors.fill: parent }
            MouseArea { width: 20; height: 20 }
            MouseArea { width: 20; height: 20 }
        }
    }

    function rowsFor(q, n) {
        var out = []
        for (var i = 0; i < n; i++) out.push({ itemId: q + i, label: q + " item " + i, icon: "x",
            detail: "detail " + i, path: "Apps", kind: "app", provider: "", value: "", section: "" })
        return out
    }
    // "f","fi","fir","fire","firef","firefo","firefox" yazılırken sonuç sayıları
    readonly property var steps: [[ "f", 13 ], [ "fi", 12 ], [ "fir", 9 ], [ "fire", 6 ], [ "firef", 3 ], [ "firefo", 2 ], [ "firefox", 2 ]]

    function run(update) {
        var t0 = Date.now()
        for (var r = 0; r < 40; r++) {
            for (var s = 0; s < steps.length; s++) {
                update(rowsFor(steps[s][0], steps[s][1]))
                list.forceLayout()
            }
        }
        return (Date.now() - t0) / (40 * steps.length)
    }

    function test_compare() {
        var clearAppend = run(function(rows) { m.clear(); for (var i = 0; i < rows.length; i++) m.append(rows[i]) })
        m.clear()
        var sync = run(function(rows) { MenuModel.syncRows(m, rows) })
        console.log("BENCH per keystroke: clear+append=" + clearAppend.toFixed(3) + "ms  syncRows=" + sync.toFixed(3) + "ms")
        verify(sync < clearAppend, "syncRows should be cheaper")
    }
}
