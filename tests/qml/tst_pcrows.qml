import QtQuick
import QtTest
import "../../MenuModel.js" as MenuModel

// pcRows(kind, items) -> satırlar; killDecision(lastKill, rowId, nowMs) -> "term" | "kill"
TestCase {
    name: "PcRows"

    function test_clipRows() {
        var r = MenuModel.pcRows("clip", [{ label: "hello", value: "hello\nworld" }])
        compare(r.length, 1)
        compare(r[0].provider, "clipboard-item")
        compare(r[0].value, "hello\nworld")
    }
    function test_killRows() {
        var r = MenuModel.pcRows("kill", [{ pid: 42, name: "firefox", cmd: "/usr/lib/firefox", rssText: "500 MB" }])
        compare(r[0].provider, "kill-process")
        compare(r[0].value, "42")
        verify(r[0].label.indexOf("firefox") >= 0)
        verify(r[0].detail.indexOf("500 MB") >= 0)
    }
    function test_recentRows() {
        var r = MenuModel.pcRows("recent", [{ path: "/h/a.pdf", name: "a.pdf", isDir: false, modified: "01.01.2026 10:00" }])
        compare(r[0].provider, "file-item")
        compare(r[0].filePath, "/h/a.pdf")
        compare(r[0].isDir, false)
    }
    function test_emojiRows() {
        var r = MenuModel.pcRows("emoji", [{ glyph: "👍", name: "thumbs up" }])
        compare(r[0].provider, "emoji")
        compare(r[0].value, "👍")
        verify(r[0].label.indexOf("thumbs up") >= 0)
    }
    function test_killSecondEnterEscalates() {
        var first = MenuModel.killDecision(null, "__kill_42__", 1000)
        compare(first, "term")
        compare(MenuModel.killDecision({ rowId: "__kill_42__", at: 1000 }, "__kill_42__", 4000), "kill")
        compare(MenuModel.killDecision({ rowId: "__kill_42__", at: 1000 }, "__kill_42__", 7000), "term") // 5 s geçti
    }
    function test_killOtherRowDoesNot() {
        compare(MenuModel.killDecision({ rowId: "__kill_42__", at: 1000 }, "__kill_43__", 2000), "term")
    }
}
