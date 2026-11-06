import QtQuick
import QtTest
import "../../MenuModel.js" as MenuModel

// syncRows: satırları silmeden yerinde günceller (delegate'ler yeniden
// yaratılmaz) ve eksik role'leri varsayılana sıfırlar (eski değer sızmaz).
TestCase {
    name: "SyncRows"

    ListModel { id: m }

    property int removedCount: 0
    Connections {
        target: m
        function onRowsRemoved() { removedCount++ }
    }

    function rowsOf(labels) {
        return labels.map(function(l) { return { itemId: "id_" + l, label: l } })
    }

    function init() {
        m.clear()
        removedCount = 0
    }

    function test_growAndUpdate() {
        MenuModel.syncRows(m, rowsOf(["a", "b"]))
        MenuModel.syncRows(m, rowsOf(["w", "x", "y", "z"]))
        compare(m.count, 4)
        compare(m.get(0).label, "w")
        compare(m.get(3).itemId, "id_z")
    }

    function test_shrinkToEmpty() {
        MenuModel.syncRows(m, rowsOf(["a", "b", "c"]))
        MenuModel.syncRows(m, [])
        compare(m.count, 0)
    }

    function test_staleRolesReset() {
        MenuModel.syncRows(m, [{ itemId: "f", label: "File", filePath: "/x", isDir: true }])
        MenuModel.syncRows(m, [{ itemId: "a", label: "App" }])
        compare(m.get(0).filePath, "")
        compare(m.get(0).isDir, false)
        compare(m.get(0).label, "App")
    }

    function test_unknownRoleNotLeaked() {
        MenuModel.syncRows(m, [{ itemId: "c", label: "Calc", copyVal: "42" }])
        MenuModel.syncRows(m, [{ itemId: "a", label: "App" }])
        compare(m.get(0).copyVal, "")
    }

    function test_inPlaceKeepsCount() {
        MenuModel.syncRows(m, rowsOf(["a", "b", "c"]))
        removedCount = 0
        MenuModel.syncRows(m, rowsOf(["d", "e", "f"]))
        compare(m.count, 3)
        compare(removedCount, 0)
        compare(m.get(1).label, "e")
    }

    // ListModel role türü ilk değerle sabitlenir; set() farklı türü reddedip
    // eski değeri bırakır. normalizeRow değeri varsayılanın türüne çevirmeli.
    function test_typeMismatchCoerced() {
        MenuModel.syncRows(m, [{ itemId: "a", label: "App", value: "firefox.desktop" }])
        MenuModel.syncRows(m, [{ itemId: "k", label: "Kill", value: 4242 }])
        compare(m.get(0).value, "4242")
    }
}
