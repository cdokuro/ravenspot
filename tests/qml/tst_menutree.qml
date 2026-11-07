import QtQuick
import QtTest
import "../../MenuModel.js" as MenuModel

// menu.jsonc yapı kuralları: kök sırası, kopya yok, her ebeveyn var.
// Çalıştırma: QML_XHR_ALLOW_FILE_READ=1 qmltestrunner -input tests/qml
TestCase {
    name: "MenuTree"

    property var items: ({})
    property var order: []

    function initTestCase() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../menu.jsonc"), false)
        xhr.send()
        var merged = MenuModel.mergeMenuSources(MenuModel.parseMenuJsonc(xhr.responseText), [], "Go")
        items = merged.items
        order = merged.itemOrder
        verify(order.length > 20, "menu.jsonc loaded")
    }

    function childrenOf(id) {
        return order.filter(function(k) { return items[k].parent === id })
    }

    function test_rootOrder() {
        var kids = childrenOf("root").filter(function(k) { return k !== "help" })
        compare(kids, ["apps", "files", "tools", "appearance", "settings", "system"])
    }

    function test_rootEntriesAreMenus() {
        var kids = childrenOf("root")
        for (var i = 0; i < kids.length; i++)
            compare(items[kids[i]].kind, "menu", kids[i] + " should open a submenu")
    }

    function test_everyParentExists() {
        for (var i = 0; i < order.length; i++) {
            var e = items[order[i]]
            if (e.id === "root") continue
            verify(items[e.parent] !== undefined, e.id + " has missing parent " + e.parent)
        }
    }

    function test_noDuplicateActions() {
        var seen = ({})
        for (var i = 0; i < order.length; i++) {
            var e = items[order[i]]
            if (!e.action) continue
            verify(!seen[e.action], e.id + " duplicates action of " + seen[e.action])
            seen[e.action] = e.id
        }
    }

    function test_noDuplicateLabelsAcrossTree() {
        var seen = ({})
        for (var i = 0; i < order.length; i++) {
            var e = items[order[i]]
            if (e.id === "root" || e.kind === "menu") continue
            var l = String(e.label).toLowerCase()
            verify(!seen[l], e.id + " duplicates label of " + seen[l])
            seen[l] = e.id
        }
    }

    function test_powerOrderedLeastToMostDestructive() {
        compare(childrenOf("system"), ["system.lock", "system.suspend", "system.hibernate",
                                       "system.logout", "system.save_session", "system.reboot", "system.shutdown"])
    }

    function test_appsAllFirstThenAlphabetical() {
        var kids = childrenOf("apps")
        compare(kids[0], "apps.all")
        compare(kids[kids.length - 1], "apps.other") // Other Apps en sonda
        var rest = kids.slice(1, -1).map(function(k) { return items[k].label })
        var sorted = rest.slice().sort(function(a, b) { return a.localeCompare(b) })
        compare(rest, sorted)
    }

    function test_settingsStartsWithSystemSettings() {
        compare(childrenOf("settings")[0], "settings.general")
    }

    function test_noLegacyIds() {
        for (var i = 0; i < order.length; i++) {
            var id = order[i]
            verify(id.indexOf("trigger") !== 0, "legacy id " + id)
            verify(["shell", "browser", "discover", "downloads"].indexOf(id) < 0, "legacy root id " + id)
        }
    }
}
