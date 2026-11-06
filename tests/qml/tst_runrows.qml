import QtQuick
import QtTest
import "../../MenuModel.js" as MenuModel

// commandRows(query, analysis, history) -> { position: "first"|"last"|"none", rows: [...] }
// analysis: Shell.analyzeCommand(...) çıktısı (signal 0/1/2, dangerous, firstLine, extraLines)
TestCase {
    name: "RunRows"

    function an(signal, extra) {
        return { signal: signal, dangerous: false, needsTerminal: false, firstLine: "ls -la", extraLines: extra || 0 }
    }

    function test_strongPrepended() {
        var r = MenuModel.commandRows("sudo ls", an(2), [])
        compare(r.position, "first")
        compare(r.rows[0].itemId, "__run__")
        compare(r.rows[0].provider, "run-command")
        compare(r.rows[0].value, "sudo ls")
    }
    function test_weakAppended() {
        var r = MenuModel.commandRows("ls -la", an(1), [])
        compare(r.position, "last")
        compare(r.rows.length, 1)
    }
    function test_bareWordNoRow() {
        var r = MenuModel.commandRows("firefox", an(0), ["firefox --x"])
        compare(r.position, "none")
        compare(r.rows.length, 0)
    }
    function test_prefixForcesRow() {
        var r = MenuModel.commandRows(">htop", an(0), [])
        compare(r.position, "first")
        compare(r.rows[0].value, "htop")
    }
    function test_multiLineLabel() {
        var a = { signal: 2, dangerous: false, needsTerminal: false, firstLine: "a", extraLines: 2 }
        var r = MenuModel.commandRows("a\nb\nc", a, [])
        compare(r.rows[0].label, "a  (+2 lines)")
    }
    function test_historyRowsForBarePrefix() {
        var r = MenuModel.commandRows(">", an(0), ["git status", "ls"])
        compare(r.position, "first")
        compare(r.rows.length, 2)
        compare(r.rows[0].value, "git status")
        compare(r.rows[0].provider, "run-command")
    }
    function test_dangerFlagged() {
        var a = { signal: 2, dangerous: true, needsTerminal: false, firstLine: "rm -rf ~", extraLines: 0 }
        var r = MenuModel.commandRows("rm -rf ~", a, [])
        compare(r.rows[0].isDanger, true)
    }
    function test_historyMatchesBelowRunRow() {
        var r = MenuModel.commandRows(">git", an(1), ["git status", "git log"])
        compare(r.rows.length, 3)
        compare(r.rows[1].value, "git status")
    }
}
