import QtQuick
import QtTest
import "../../MenuModel.js" as MenuModel

// groupSearchRows(rows, opts): arama sonuçlarını sabit sıralı, başlıklı
// gruplara ayırır (Top Hit, Applications, Settings, …), grup başına sınır koyar.
TestCase {
    name: "SearchGroups"

    function r(id, extra) {
        var o = { itemId: id, kind: "action", provider: "", filePath: "", score: 50000, label: id, section: "" }
        for (var k in extra) o[k] = extra[k]
        return o
    }
    function ids(rows) { return rows.map(function(x) { return x.itemId }) }
    function sections(rows) { return rows.map(function(x) { return x.section }) }

    function test_classify() {
        compare(MenuModel.searchGroupKey(r("__calc__", { provider: "clipboard-item" })), "instant")
        compare(MenuModel.searchGroupKey(r("__bang_Google__")), "instant")
        compare(MenuModel.searchGroupKey(r("__set_vol__", { provider: "set-volume" })), "instant")
        compare(MenuModel.searchGroupKey(r("__open_dir__", { provider: "open-folder-path" })), "instant")
        compare(MenuModel.searchGroupKey(r("__clip_0__", { provider: "clipboard-item" })), "pc")
        compare(MenuModel.searchGroupKey(r("__kill_42__", { provider: "kill-process" })), "pc")
        compare(MenuModel.searchGroupKey(r("__run__", { provider: "run-command" })), "commands")
        compare(MenuModel.searchGroupKey(r("__web_google__")), "web")
        compare(MenuModel.searchGroupKey(r("__win_1__", { provider: "window-item" })), "windows")
        compare(MenuModel.searchGroupKey(r("__sys_restart_plasma__", { provider: "sys-action" })), "system")
        compare(MenuModel.searchGroupKey(r("apps.all.firefox", { kind: "app" })), "apps")
        compare(MenuModel.searchGroupKey(r("apps.games", { kind: "menu" })), "apps")
        compare(MenuModel.searchGroupKey(r("apps.games.steam-library.file-0", { provider: "file-item", filePath: "/x" })), "files")
        compare(MenuModel.searchGroupKey(r("files.downloads", { kind: "menu" })), "files")
        compare(MenuModel.searchGroupKey(r("settings.audio")), "settings")
        compare(MenuModel.searchGroupKey(r("appearance.nightlight")), "settings")
        compare(MenuModel.searchGroupKey(r("system.lock")), "system")
        compare(MenuModel.searchGroupKey(r("tools.capture.region")), "tools")
        compare(MenuModel.searchGroupKey(r("help.cachyos")), "help")
    }

    function test_fixedGroupOrderAndHeaders() {
        var rows = [
            r("settings.audio", { score: 40000 }),
            r("__web_google__", { score: 99998 }),
            r("apps.all.audacity", { kind: "app", score: 35000 }),
            r("system.lock", { score: 45000 })
        ]
        var out = MenuModel.groupSearchRows(rows, { capped: true })
        compare(ids(out), ["apps.all.audacity", "system.lock", "settings.audio", "__web_google__"])
        compare(sections(out), ["Applications", "Power & Session", "Settings", "Search the Web"])
    }

    function test_topHitPromotesStrongMatch() {
        var rows = [
            r("apps.all.firefox", { kind: "app", score: 35000, label: "Firefox" }),
            r("settings.network", { score: 10100, label: "Network & Wi-Fi" }),
            r("settings.audio", { score: 40000 })
        ]
        var out = MenuModel.groupSearchRows(rows, { capped: true })
        compare(out[0].itemId, "settings.network")
        compare(out[0].section, "Top Hit")
        compare(ids(out), ["settings.network", "apps.all.firefox", "settings.audio"])
    }

    function test_noTopHitForWeakMatches() {
        var rows = [
            r("apps.all.firefox", { kind: "app", score: 35000 }),
            r("settings.audio", { score: 40000 })
        ]
        var out = MenuModel.groupSearchRows(rows, { capped: true })
        compare(out[0].section, "Applications")
    }

    function test_noTopHitWhenInstantAnswer() {
        var rows = [
            r("__calc__", { provider: "clipboard-item", score: -99999 }),
            r("apps.all.qalc", { kind: "app", score: -5000 }),
            r("settings.audio", { score: 10000 })
        ]
        var out = MenuModel.groupSearchRows(rows, { capped: true })
        compare(out[0].itemId, "__calc__")
        compare(out[0].section, "Quick Answer")
        compare(out[1].section, "Applications")
    }

    function test_windowsNeverTopHit() {
        var rows = [
            r("__win_1__", { provider: "window-item", score: -80000 }),
            r("apps.all.firefox", { kind: "app", score: -5000 })
        ]
        var out = MenuModel.groupSearchRows(rows, { capped: true })
        compare(ids(out), ["apps.all.firefox", "__win_1__"])
        compare(sections(out), ["Top Hit", "Open Windows"])
    }

    function test_singleGroupHasNoHeader() {
        var rows = [r("settings.audio", { score: 30000 }), r("settings.network", { score: 31000 })]
        var out = MenuModel.groupSearchRows(rows, { capped: false })
        compare(sections(out), ["", ""])
    }

    function test_capsOnlyWhenCapped() {
        var rows = []
        for (var i = 0; i < 12; i++) rows.push(r("apps.all.a" + i, { kind: "app", score: 35000 + i }))
        rows.push(r("settings.audio", { score: 40000 }))
        compare(MenuModel.groupSearchRows(rows, { capped: true }).length, 8 + 1)
        compare(MenuModel.groupSearchRows(rows, { capped: false }).length, 13)
    }

    function test_strongCommandsComeFirst() {
        var rows = [
            r("apps.all.htop", { kind: "app", score: 35000 }),
            r("__run__", { provider: "run-command", score: 0 })
        ]
        compare(MenuModel.groupSearchRows(rows, { capped: true, commandsFirst: true })[0].itemId, "__run__")
        compare(MenuModel.groupSearchRows(rows, { capped: true, commandsFirst: false })[0].itemId, "apps.all.htop")
    }

    function test_withinGroupByScoreStable() {
        var rows = [
            r("__win_b__", { provider: "window-item", score: -80000 }),
            r("__win_a__", { provider: "window-item", score: -80000 }),
            r("settings.x", { score: 40000 }),
            r("settings.y", { score: 39000 })
        ]
        var out = MenuModel.groupSearchRows(rows, { capped: true })
        compare(ids(out), ["__win_b__", "__win_a__", "settings.y", "settings.x"])
    }

    function test_pcHeaderNamesMode() {
        var rows = MenuModel.pcRows("kill", [{ pid: 1, name: "a", rssText: "1 MB" }])
        rows.push(r("apps.all.a", { kind: "app", score: 35000 }))
        var out = MenuModel.groupSearchRows(rows, { capped: true })
        compare(out[0].section, "Processes")
    }

    function test_doesNotMutateInput() {
        var rows = [r("settings.audio", { score: 10 }), r("apps.all.a", { kind: "app", score: 35000 })]
        MenuModel.groupSearchRows(rows, { capped: true })
        compare(rows[0].section, "")
    }

    function test_strongGroupBeforeWeakGroup() {
        // "net": Settings > Network (önek) güçlü; Apps > "Yönetimi" (kelime ortası) zayıf
        var rows = [
            r("apps.all.power", { kind: "app", score: 25000, label: "Güç Yönetimi" }),
            r("apps.all.audio", { kind: "app", score: 25100, label: "Ses Denetimi" }),
            r("settings.network", { score: 10100, label: "Network & Wi-Fi" }),
            r("settings.netx", { score: 10200, label: "Network Extra" }),
            r("__web_google__", { score: 99998 })
        ]
        var out = MenuModel.groupSearchRows(rows, { capped: true })
        compare(ids(out), ["settings.network", "settings.netx", "apps.all.power", "apps.all.audio", "__web_google__"])
        compare(sections(out), ["Top Hit", "Settings", "Applications", "Applications", "Search the Web"])
    }

    function test_turkishFolding() {
        var e = { id: "settings.firewall", label: "Firewall", aliases: ["güvenlik duvarı"], searchTerms: [], description: "" }
        verify(MenuModel.matchesQuery(e, "fırewall", true))
        verify(MenuModel.matchesQuery(e, "guvenlik", true))
        verify(MenuModel.matchesQuery(e, "DUVARI", true))
    }

    function test_shortTermsNeedWordStart() {
        var e = { id: "apps.all.x", label: "Güç Yönetimi", aliases: [], searchTerms: [], description: "" }
        verify(!MenuModel.matchesQuery(e, "net", true)) // "Yö-net-imi": kelime ortası, kısa
        verify(MenuModel.matchesQuery(e, "yo", true))
        verify(MenuModel.matchesQuery(e, "netim", true)) // 4+ harf: kelime ortası serbest ama zayıf
    }

    function test_wordStartScoresStrong() {
        var items = { "settings": { id: "settings", parent: "root" }, "root": { id: "root", parent: "" } }
        var e = { id: "settings.x", parent: "settings", kind: "action", label: "Default Applications", aliases: [], searchTerms: [], description: "", order: 3 }
        items["settings.x"] = e
        verify(MenuModel.searchScore(items, e, "app") < 25000)
        var m = { id: "settings.y", parent: "settings", kind: "action", label: "Snapper", aliases: [], searchTerms: [], description: "", order: 3 }
        items["settings.y"] = m
        verify(MenuModel.searchScore(items, m, "app") >= 25000)
    }

    function test_sysActionNeverTopHit() {
        var rows = [
            r("__sys_restart_plasma__", { provider: "sys-action", score: -99990 }),
            r("settings.x", { score: 10000 }),
            r("apps.all.a", { kind: "app", score: 35000 })
        ]
        var out = MenuModel.groupSearchRows(rows, { capped: true })
        compare(out[0].itemId, "settings.x")
        compare(out[0].section, "Top Hit")
        verify(ids(out).indexOf("__sys_restart_plasma__") > 0)
    }

    function test_exactAliasIsStrong() {
        var items = { root: { id: "root", parent: "" }, settings: { id: "settings", parent: "root" }, system: { id: "system", parent: "root" } }
        var audio = { id: "settings.audio", parent: "settings", kind: "action", label: "Audio & Sound", aliases: ["sound", "ses"], searchTerms: [], description: "", order: 50 }
        var save = { id: "system.save_session", parent: "system", kind: "action", label: "Save Session", aliases: [], searchTerms: [], description: "", order: 70 }
        items[audio.id] = audio; items[save.id] = save
        verify(MenuModel.searchScore(items, audio, "ses") < MenuModel.searchScore(items, save, "ses"))
        verify(MenuModel.searchScore(items, audio, "ses") < 25000)
    }

    function test_generatedLeafIdsNotSearchable() {
        var e = { id: "files.recent.recent-3", label: "notes.txt", aliases: ["notes.txt"], searchTerms: [], description: "" }
        verify(!MenuModel.matchesQuery(e, "recent", true))
        var f = { id: "files.downloads.file-0", label: "a.zip", aliases: [], searchTerms: [], description: "" }
        verify(!MenuModel.matchesQuery(f, "file", true))
        var g = { id: "settings.firewall", label: "Firewall", aliases: [], searchTerms: [], description: "" }
        verify(MenuModel.matchesQuery(g, "firewall", true))
    }
}
