import QtQuick
import QtTest
import "../../MenuModel.js" as MenuModel

TestCase {
    name: "ContextEntries"

    function types(entries) { return entries.map(function(e) { return e.actionType }) }

    function test_fileEntries_orderAndOpenWith() {
        var e = MenuModel.fileContextEntries({ path: "/h/a.txt", isDir: false, isArchive: false,
                                               isRootDownloads: false, inMenu: false, canTrash: true })
        compare(types(e), ["openPath", "openWithMenu", "openLocation", "openInTerminal", "copyFile",
                           "copyPath", "startRename", "compress", "showProperties", "moveToTrash"])
        compare(e[0].actionPayload, "/h/a.txt")
    }
    function test_fileEntries_archiveShowsExtract() {
        var e = MenuModel.fileContextEntries({ path: "/h/a.zip", isDir: false, isArchive: true,
                                               isRootDownloads: false, inMenu: false, canTrash: true })
        verify(types(e).indexOf("extract") >= 0)
        verify(types(e).indexOf("extract") < types(e).indexOf("compress"))
    }
    function test_dirEntries_noOpenWithNoExtract() {
        var e = MenuModel.fileContextEntries({ path: "/h/dir", isDir: true, isArchive: false,
                                               isRootDownloads: false, inMenu: true, canTrash: true })
        var t = types(e)
        compare(t[0], "activateCurrent")
        verify(t.indexOf("openWithMenu") < 0)
        verify(t.indexOf("extract") < 0)
        verify(t.indexOf("startRename") >= 0)
    }
    function test_rootDownloads_noRenameNoTrash() {
        var e = MenuModel.fileContextEntries({ path: "/h/Downloads", isDir: true, isArchive: false,
                                               isRootDownloads: true, inMenu: true, canTrash: false })
        var t = types(e)
        verify(t.indexOf("startRename") < 0)
        verify(t.indexOf("moveToTrash") < 0)
        verify(t.indexOf("compress") < 0)
    }
    function test_trashIsDanger() {
        var e = MenuModel.fileContextEntries({ path: "/h/a.txt", isDir: false, isArchive: false,
                                               isRootDownloads: false, inMenu: false, canTrash: true })
        var trash = e.filter(function(x) { return x.actionType === "moveToTrash" })[0]
        compare(trash.isDanger, true)
        compare(e.filter(function(x) { return x.isDanger }).length, 1)
    }
    function test_appEntries_autostartLabelToggles() {
        var on = MenuModel.appContextEntries({ desktop: "/usr/share/applications/k.desktop", actions: [],
                                               autostart: true, canUninstall: true })
        var off = MenuModel.appContextEntries({ desktop: "/usr/share/applications/k.desktop", actions: [],
                                                autostart: false, canUninstall: true })
        var a = on.filter(function(x) { return x.actionType === "toggleAutostart" })[0]
        var b = off.filter(function(x) { return x.actionType === "toggleAutostart" })[0]
        verify(a.label.indexOf("✓") === 0)
        verify(b.label.indexOf("✓") < 0)
    }
    function test_appEntries_desktopActionsAfterLaunch() {
        var e = MenuModel.appContextEntries({ desktop: "/d/k.desktop", autostart: false, canUninstall: false,
                                              actions: [{ name: "New Window", exec: "k --new" }] })
        compare(e[0].actionType, "launchApp")
        compare(e[1].actionType, "runAction")
        compare(e[1].actionPayload, "k --new")
    }
    function test_appEntries_hideUninstallWithoutOwner() {
        var e = MenuModel.appContextEntries({ desktop: "/home/u/.local/share/applications/x.desktop",
                                              actions: [], autostart: false, canUninstall: false })
        verify(types(e).indexOf("uninstall") < 0)
        var u = MenuModel.appContextEntries({ desktop: "/usr/share/applications/x.desktop",
                                              actions: [], autostart: false, canUninstall: true })
        var un = u.filter(function(x) { return x.actionType === "uninstall" })[0]
        compare(un.isDanger, true)
    }

    // contextKeyActivates(key, actionType): Enter her şeyi, sağ ok yalnızca alt menü açar
    function test_rightArrowOnlyOpensSubmenu() {
        verify(MenuModel.contextKeyActivates("right", "openWithMenu"))
        verify(!MenuModel.contextKeyActivates("right", "moveToTrash"))
        verify(!MenuModel.contextKeyActivates("right", "uninstall"))
        verify(MenuModel.contextKeyActivates("enter", "moveToTrash"))
    }
}
