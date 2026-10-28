// RavenSpot — KDE/CachyOS Spotlight Search & Application Menu.
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Effects
import org.kde.layershell as LayerShell
import "MenuModel.js" as MenuModel

Window {
    id: root
    visible: true
    width: Screen.width
    height: Screen.height
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool

    // Katman-shell: KWin layer-shell v5 destekler; klavye odağı Exclusive ile gelir.
    LayerShell.Window.scope: "ravenspot"
    LayerShell.Window.layer: LayerShell.Window.Layer.LayerOverlay
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivity.KeyboardInteractivityOnDemand
    LayerShell.Window.activateOnShow: true
    LayerShell.Window.anchors: LayerShell.Window.Anchors.AnchorTop | LayerShell.Window.Anchors.AnchorBottom | LayerShell.Window.Anchors.AnchorLeft | LayerShell.Window.Anchors.AnchorRight

    // ── Tema ──────────────────────────────────────────────
    property color bgColor: "#cc0b0c10"
    property color bgBorder: "#ffd700"
    property color glowColor: "#ffd700"
    property color fgColor: "#f0f2f5"
    property color fgDim: "#8b949e"
    property color selBg: "#1f51ff"
    property color selFg: "#ffffff"
    property color scrimColor: "#66000000"
    property color cpuColor: "#ffd700"
    property color gpuColor: "#00f0ff"
    property string iconFont: "FantasqueSansM Nerd Font"
    property string textFont: "Inter"
    property int cardWidth: 620
    property int rowHeight: 48
    property int headerHeight: 70
    property int cpuPercent: 0
    property int gpuPercent: 0
    property int sysVolume: 50
    property bool sysMuted: false
    property var sysMonitor: ({ "ram_text": "--", "ram_pct": 0, "disk_text": "--", "disk_pct": 0, "temp": "--", "uptime": "--" })
    property int footerHeight: 30

    property int themeVersion: 1

    function syncTheme() {
        var t = Shell.currentTheme()
        if (!t) return
        root.bgColor = t.bgColor
        root.bgBorder = t.bgBorder
        root.glowColor = t.glowColor
        root.fgColor = t.fgColor
        root.fgDim = t.fgDim
        root.selBg = t.selBg
        root.selFg = t.selFg
        root.cpuColor = t.cpuColor
        root.gpuColor = t.gpuColor
        root.textFont = t.font || "Inter"
        root.themeVersion += 1
    }

    // ── Durum ─────────────────────────────────────────────
    property var items: ({})
    property var itemOrder: []
    property var navStack: []
    property var providersLoaded: ({})
    property string activeMenu: "root"
    property string filterText: ""
    property int selectedIndex: 0
    property bool opened: false
    property var whenResults: ({})
    property var checkedResults: ({})
    property var disabledResults: ({})
    property bool loaded: false

    // ── Veri yükleme ──────────────────────────────────────
    function loadItems() {
        var def = MenuModel.parseMenuJsonc(Shell.readFile(Shell.menuPath())) || []
        var user = MenuModel.parseMenuJsonc(Shell.readFile(Shell.userMenuPath())) || []
        var merged = MenuModel.mergeMenuSources(def, user, "Go")
        root.items = merged.items
        root.itemOrder = merged.itemOrder
        root.loaded = true

        try {
            var counts = JSON.parse(Shell.getAppCategoryCounts() || "{}")
            var catMap = {
                "apps.games": "games",
                "apps.internet": "internet",
                "apps.development": "development",
                "apps.multimedia": "multimedia",
                "apps.graphics": "graphics",
                "apps.office": "office",
                "apps.system": "system",
                "apps.utilities": "utilities",
                "apps.all": "all"
            }
            for (var cId in catMap) {
                if (root.items[cId] && counts[catMap[cId]] !== undefined) {
                    var baseName = root.items[cId].label.replace(/\s*\(\d+\)$/, "")
                    root.items[cId].label = baseName + " (" + counts[catMap[cId]] + ")"
                }
            }
        } catch(e) {}

        // Global arama için tüm uygulamaları arka planda root.items içine önceden yükle
        loadAppsCategory("apps.all", "all")
        root.providersLoaded["apps.all"] = true

        evaluateGuards()
    }

    function evaluateGuards() {
        var script = MenuModel.guardScript(root.items)
        var nextWhen = ({})
        var nextChecked = ({})
        var nextDisabled = ({})
        if (!script) {
            root.whenResults = nextWhen
            root.checkedResults = nextChecked
            root.disabledResults = nextDisabled
            rebuildDisplay()
            return
        }
        var out = Shell.bash(script)
        var lines = out.split("\n")
        for (var i = 0; i < lines.length; i++) {
            var line = lines[i].trim()
            if (!line) continue
            var colon = line.lastIndexOf(":")
            if (colon < 0) continue
            var value = line.substring(colon + 1) === "1"
            var rest = line.substring(0, colon)
            var tagAt = rest.lastIndexOf(":")
            if (tagAt < 0) continue
            var id = rest.substring(0, tagAt)
            var tag = rest.substring(tagAt + 1)
            if (tag === "w") nextWhen[id] = value
            else if (tag === "c") nextChecked[id] = value
            else if (tag === "d") nextDisabled[id] = value
        }
        // QML'de var özelliğini yerinde değiştirmek binding tetiklemez;
        // bu yüzden yeni nesneyi tam atama olarak veriyoruz (Omarchy ile aynı).
        root.whenResults = nextWhen
        root.checkedResults = nextChecked
        root.disabledResults = nextDisabled
        rebuildDisplay()
    }

    // ── Menü modeli yardımcıları (MenuModel.js ince sarmalayıcılar) ──
    function item(id) { return root.items[id] || null }

    function isVisible(entry) {
        return MenuModel.isVisible(root.items, root.itemOrder, root.whenResults, entry)
    }
    function isDisabled(entry) {
        return MenuModel.isDisabled(root.disabledResults, entry)
    }
    function labelFor(entry) {
        return MenuModel.labelFor(entry, root.checkedResults, root.disabledResults)
    }
    function depthFor(id) { return MenuModel.depthFor(root.items, id) }
    function pathFor(id) { return MenuModel.pathFor(root.items, id) }
    function parentPathFor(id) { return MenuModel.parentPathFor(root.items, id) }
    function isDescendantOf(id, anc) { return MenuModel.isDescendantOf(root.items, id, anc) }
    function childCount(id) { return MenuModel.childCount(root.items, root.itemOrder, id) }

    // ── Provider'lar ──────────────────────────────────────
    function startProvider(id) {
        var entry = root.item(id)
        if (!entry || !entry.provider || root.providersLoaded[id]) return
        root.providersLoaded[id] = true
        if (entry.provider === "apps") {
            loadApps(id)
        } else if (entry.provider.startsWith("apps-")) {
            loadAppsCategory(id, entry.provider.substring(5))
        } else if (entry.provider === "colorschemes") {
            loadColorschemes(id)
        } else if (entry.provider === "power-profiles") {
            loadPowerProfiles(id)
        } else if (entry.provider === "clipboard") {
            loadClipboard(id)
        } else if (entry.provider && entry.provider.startsWith("files-")) {
            loadFolderFiles(id, entry.provider.substring(6))
        } else if (entry.provider === "ravenspot-themes") {
            loadRavenThemes(id)
        }
    }

    function loadClipboard(id) {
        var arr = JSON.parse(Shell.listClipboard() || "[]")
        var rows = []
        for (var j = 0; j < arr.length; j++) {
            var c = arr[j]
            var rowId = id + ".clip-" + j
            rows.push({
                id: rowId, parent: id, kind: "action",
                icon: "󰅇", iconFont: "", iconName: "org.kde.plasma.clipboard",
                label: c.label, title: "", target: "", description: "Panoya kopyala",
                action: "", provider: "clipboard-item", aliases: [], searchTerms: [],
                when: "", checked: "", disabled: "", value: c.value, order: j
            })
        }
        var merged = MenuModel.swapProviderRows(root.items, root.itemOrder, id, rows)
        root.items = merged.items
        root.itemOrder = merged.itemOrder
        rebuildDisplay()
    }

    function loadFolderFiles(id, folderName) {
        var arr = JSON.parse(Shell.listFolderFiles(folderName) || "[]")
        var rows = []
        for (var j = 0; j < arr.length; j++) {
            var f = arr[j]
            var rowId = id + ".file-" + j
            rows.push({
                id: rowId, parent: id, kind: "action",
                icon: f.is_dir ? "󰉋" : "󰈔", iconFont: "", iconName: f.iconName,
                label: f.name, title: "", target: "", description: f.desc,
                action: "", provider: "file-item", aliases: [f.name], searchTerms: [f.name],
                when: "", checked: "", disabled: "", filePath: f.path, order: j
            })
        }
        var merged = MenuModel.swapProviderRows(root.items, root.itemOrder, id, rows)
        root.items = merged.items
        root.itemOrder = merged.itemOrder
        rebuildDisplay()
    }

    function loadRavenThemes(id) {
        var arr = JSON.parse(Shell.listRavenThemes() || "[]")
        var rows = []
        for (var j = 0; j < arr.length; j++) {
            var t = arr[j]
            var rowId = id + "." + t.key
            rows.push({
                id: rowId, parent: id, kind: "action",
                icon: "󰔉", iconFont: "", iconName: "cs-themes",
                label: t.name + (t.isCurrent ? " (Aktif)" : ""),
                title: "", target: "", description: t.desc + " • Font: " + t.font,
                action: "", provider: "ravenspot-theme-item", aliases: [t.name, t.key], searchTerms: [t.name, t.key],
                when: "", checked: "", disabled: "", themeKey: t.key, isCurrent: t.isCurrent, order: j
            })
        }
        var merged = MenuModel.swapProviderRows(root.items, root.itemOrder, id, rows)
        root.items = merged.items
        root.itemOrder = merged.itemOrder
        rebuildDisplay()
    }

    function loadAppsCategory(id, cat) {
        var arr = JSON.parse(Shell.listAppsCategory(cat) || "[]")
        var rows = []
        var taken = ({})
        for (var j = 0; j < arr.length; j++) {
            var a = arr[j]
            var slug = MenuModel.slugify(a.id)
            var rowId = id + "." + slug
            var n = 0
            while (taken[rowId]) { rowId = id + "." + slug + "-" + (++n) }
            taken[rowId] = true
            rows.push({
                id: rowId,
                parent: id,
                kind: "app",
                icon: "",
                iconFont: "",
                appIcon: a.id,   // image://appicon/<desktop-id>
                appId: a.id,
                label: a.label,
                title: "",
                target: "",
                description: "",
                action: "",
                provider: "",
                aliases: a.aliases || [],
                searchTerms: [],
                when: "", checked: "", disabled: "",
                desktop: a.desktop,
                order: 0
            })
        }
        var merged = MenuModel.swapProviderRows(root.items, root.itemOrder, id, rows)
        root.items = merged.items
        root.itemOrder = merged.itemOrder
        rebuildDisplay()
    }

    function loadApps(id) {
        loadAppsCategory(id, "all")
    }

    function loadColorschemes(id) {
        var arr = JSON.parse(Shell.listColorschemes() || "[]")
        var rows = []
        var taken = ({})
        for (var j = 0; j < arr.length; j++) {
            var c = arr[j]
            var slug = MenuModel.slugify(c.value)
            var rowId = id + "." + slug
            while (taken[rowId]) rowId += "-"
            taken[rowId] = true

            var iconStr = "󰸌"
            var detailStr = "F tuşu: Favorile"
            if (c.current) {
                iconStr = "✓"
                detailStr = "Şu An Kullanılan Tema"
            } else if (c.isFavorite) {
                iconStr = "★"
                detailStr = "Favori Tema"
            }

            rows.push({
                id: rowId, parent: id, kind: "action",
                icon: iconStr, iconFont: "",
                label: c.label, title: "", target: "", description: detailStr,
                action: "", provider: "colorscheme", aliases: [], searchTerms: [],
                when: "", checked: "", disabled: "", value: c.value, order: j,
                isFavorite: !!c.isFavorite, isCurrent: !!c.current, themeSection: c.section
            })
        }
        var merged = MenuModel.swapProviderRows(root.items, root.itemOrder, id, rows)
        root.items = merged.items
        root.itemOrder = merged.itemOrder
        rebuildDisplay()
    }

    function loadPowerProfiles(id) {
        var arr = JSON.parse(Shell.listPowerProfiles() || "[]")
        var rows = []
        var taken = ({})
        for (var j = 0; j < arr.length; j++) {
            var p = arr[j]
            var slug = MenuModel.slugify(p.value)
            var rowId = id + "." + slug
            while (taken[rowId]) rowId += "-"
            taken[rowId] = true
            rows.push({
                id: rowId, parent: id, kind: "action",
                icon: p.current ? "✓" : "󰂖", iconFont: "",
                label: p.label, title: "", target: "", description: "",
                action: "", provider: "power-profile", aliases: [], searchTerms: [],
                when: "", checked: "", disabled: "", value: p.value, order: 0
            })
        }
        var merged = MenuModel.swapProviderRows(root.items, root.itemOrder, id, rows)
        root.items = merged.items
        root.itemOrder = merged.itemOrder
        rebuildDisplay()
    }

    // ── Görüntüleme modeli ─────────────────────────────────
    ListModel { id: displayModel }

    function displayRow(entry, detail, score, section) {
        var target = entry.kind === "link" ? entry.target : entry.id
        return {
            itemId: entry.id,
            kind: entry.kind,
            icon: entry.icon || "",
            iconName: entry.iconName || "",
            iconFont: entry.iconFont || "",
            appIcon: entry.appIcon || "",
            appId: entry.appId || "",
            label: labelFor(entry),
            target: target,
            detail: detail || "",
            path: pathFor(entry.id),
            childCount: (entry.kind === "menu" || entry.kind === "link") ? childCount(target) : 0,
            action: entry.action || "",
            provider: entry.provider || "",
            value: entry.value || "",
            desktop: entry.desktop || "",
            disabled: isDisabled(entry),
            score: score || 0,
            section: section || "",
            isFavorite: !!entry.isFavorite,
            isCurrent: !!entry.isCurrent,
            themeKey: entry.themeKey || entry.value || "",
            themeSection: entry.themeSection || ""
        }
    }

    function tryEvalMath(q) {
        var expr = q.startsWith("=") ? q.substring(1).trim() : q
        if (!expr) return null
        if (!/^[\d\s+\-*/xX÷^().,%]+$/.test(expr)) return null
        if (!q.startsWith("=") && !/[+\-*/xX÷^%]/.test(expr)) return null
        try {
            var sanitized = expr.replace(/x|X/g, "*").replace(/÷/g, "/").replace(/\^/g, "**")
            sanitized = sanitized.replace(/(\d+(\.\d+)?)%/g, "($1/100)")
            var res = Function("return (" + sanitized + ")")()
            if (typeof res === "number" && !isNaN(res) && isFinite(res)) {
                return (Math.round(res * 100000000) / 100000000).toString()
            }
        } catch (e) {}
        return null
    }

    function rebuildDisplay() {
        displayModel.clear()
        if (!root.loaded) return

        var active = root.item(root.activeMenu) ? root.activeMenu : "root"
        root.activeMenu = active
        var rows = []
        var query = root.filterText.trim()

        if (query) {
            var currentRows = []
            var drilldownRows = []
            for (var i = 0; i < root.itemOrder.length; i++) {
                var entry = root.item(root.itemOrder[i])
                if (!entry || entry.id === "root") continue
                if (!root.isDescendantOf(entry.id, active)) continue
                if (!MenuModel.matchesQuery(entry, query, root.isVisible(entry) && !root.isDisabled(entry))) continue
                var detail = root.parentPathFor(entry.id)
                var row = displayRow(entry, detail, MenuModel.searchScore(root.items, entry, query))
                if (entry.parent === active) currentRows.push(row)
                else drilldownRows.push(row)
            }
            currentRows.sort(function(a,b){ return a.score !== b.score ? a.score - b.score : a.path.localeCompare(b.path) })
            drilldownRows.sort(function(a,b){ return a.score !== b.score ? a.score - b.score : a.path.localeCompare(b.path) })
            if (currentRows.length && drilldownRows.length) {
                for (var d = 0; d < drilldownRows.length; d++) drilldownRows[d].section = "drilldown"
            }
            rows = currentRows.concat(drilldownRows)

            // 1. Hesap Makinesi
            var mathVal = tryEvalMath(query)
            if (mathVal !== null) {
                rows.unshift({
                    itemId: "__calc__",
                    kind: "action",
                    icon: "󰪚",
                    iconName: "accessories-calculator",
                    iconFont: "",
                    appIcon: "",
                    appId: "",
                    label: "= " + mathVal,
                    target: "",
                    detail: "Hesap Makinesi Sonucu (Enter: Panoya Kopyala)",
                    path: "Hesapla",
                    childCount: 0,
                    action: "",
                    provider: "clipboard-item",
                    value: mathVal,
                    desktop: "",
                    disabled: false,
                    score: -99999,
                    section: ""
                })
            }

            // 2. Terminal komut modu ('>' öneki)
            if (query.startsWith(">")) {
                var termCmd = query.substring(1).trim()
                if (termCmd) {
                    rows.unshift({
                        itemId: "__term__",
                        kind: "action",
                        icon: "",
                        iconName: "terminal",
                        iconFont: "",
                        appIcon: "",
                        appId: "",
                        label: "Terminal: " + termCmd,
                        target: "",
                        detail: "Konsole ile çalıştır",
                        path: "Terminal",
                        childCount: 0,
                        action: "konsole -e bash -c \"" + termCmd.replace(/"/g, '\\"') + "; exec bash\"",
                        provider: "",
                        value: "",
                        desktop: "",
                        disabled: false,
                        score: -99998,
                        section: ""
                    })
                }
            }

            // 3. Web Arama (Fallback veya '?' öneki)
            var isWebQuery = query.startsWith("?")
            var rawSearch = isWebQuery ? query.substring(1).trim() : query
            if (rawSearch && (isWebQuery || rows.length === 0)) {
                var webRows = [
                    {
                        itemId: "__web_google__",
                        kind: "action",
                        icon: "󰖟",
                        iconName: "web-browser",
                        iconFont: "",
                        appIcon: "",
                        appId: "",
                        label: "Google'da Ara: \"" + rawSearch + "\"",
                        target: "",
                        detail: "Varsayılan web tarayıcısında aç",
                        path: "Web Arama",
                        childCount: 0,
                        action: "xdg-open 'https://www.google.com/search?q=" + encodeURIComponent(rawSearch) + "'",
                        provider: "",
                        value: "",
                        desktop: "",
                        disabled: false,
                        score: 99998,
                        section: ""
                    },
                    {
                        itemId: "__web_ddg__",
                        kind: "action",
                        icon: "󰇧",
                        iconName: "system-search",
                        iconFont: "",
                        appIcon: "",
                        appId: "",
                        label: "DuckDuckGo'da Ara: \"" + rawSearch + "\"",
                        target: "",
                        detail: "Gizlilik odaklı arama",
                        path: "Web Arama",
                        childCount: 0,
                        action: "xdg-open 'https://duckduckgo.com/?q=" + encodeURIComponent(rawSearch) + "'",
                        provider: "",
                        value: "",
                        desktop: "",
                        disabled: false,
                        score: 99999,
                        section: ""
                    }
                ]
                if (isWebQuery) {
                    rows = webRows
                } else {
                    rows = rows.concat(webRows)
                }
            }
        } else {
            for (var j = 0; j < root.itemOrder.length; j++) {
                var child = root.item(root.itemOrder[j])
                if (!child || child.parent !== active) continue
                if (!root.isVisible(child)) continue
                rows.push(displayRow(child, child.description, child.order))
            }
            if (active.startsWith("apps.")) {
                rows.sort(function(a,b){ return String(a.label).toLowerCase().localeCompare(String(b.label).toLowerCase()) })
            }
        }

        for (var k = 0; k < rows.length; k++) displayModel.append(rows[k])
        settleCursor()
    }

    function rowSelectable(index) {
        if (index < 0 || index >= displayModel.count) return false
        return !displayModel.get(index).disabled
    }

    function nextSelectable(from, direction) {
        var count = displayModel.count
        if (count === 0) return -1
        var step = direction < 0 ? -1 : 1
        var index = ((from % count) + count) % count
        for (var i = 0; i < count; i++) {
            if (rowSelectable(index)) return index
            index = (index + step + count) % count
        }
        return -1
    }

    function settleCursor() {
        var t = nextSelectable(root.selectedIndex, 1)
        root.selectedIndex = t >= 0 ? t : 0
        resultList.currentIndex = root.selectedIndex
    }

    // ── Navigasyon ─────────────────────────────────────────
    function select(delta) {
        if (displayModel.count === 0) return
        var from = root.selectedIndex + delta
        var t = nextSelectable(from, delta)
        if (t < 0) return
        root.selectedIndex = t
        resultList.currentIndex = t
        Shell.playThock()
    }

    function setFilter(next) {
        root.filterText = next
        root.selectedIndex = 0
        rebuildDisplay()
    }

    function setActiveMenu(id, pushHistory) {
        if (!root.item(id)) id = "root"
        if (pushHistory && id !== root.activeMenu) root.navStack = root.navStack.concat([root.activeMenu])
        root.activeMenu = id
        root.filterText = ""
        searchInput.text = ""
        root.selectedIndex = 0
        rebuildDisplay()
        startProvider(id)
    }

    function goBack() {
        if (root.activeMenu === "root") return false
        if (root.navStack.length > 0) {
            var prev = root.navStack[root.navStack.length - 1]
            root.navStack = root.navStack.slice(0, root.navStack.length - 1)
            setActiveMenu(prev, false)
            return true
        }
        var a = root.item(root.activeMenu)
        setActiveMenu((a && a.parent) ? a.parent : "root", false)
        return true
    }

    function activateIndex(index) {
        if (index < 0 || index >= displayModel.count) return
        var row = displayModel.get(index)
        if (row.disabled) return
        if (row.kind === "menu" || row.kind === "link") {
            setActiveMenu(row.target || row.itemId, true)
        } else if (row.kind === "app") {
            Shell.playCreamy()
            root.opened = false
            Shell.launchApp(row.desktop)
            Qt.callLater(closeMenu)
        } else if (row.provider === "clipboard-item") {
            root.opened = false
            Shell.setClipboard(row.value)
            Qt.callLater(closeMenu)
        } else if (row.provider === "file-item") {
            Shell.playCreamy()
            root.opened = false
            Shell.openPath(row.filePath)
            Qt.callLater(closeMenu)
        } else if (row.provider === "ravenspot-theme-item") {
            Shell.playCreamy()
            var k = row.themeKey || row.value || ""
            if (k) {
                Shell.applyRavenTheme(k)
                root.syncTheme()
                loadRavenThemes(root.activeMenu)
            }
            return
        } else {
            Shell.playCreamy()
            root.opened = false
            if (row.provider === "colorscheme") {
                Shell.applyColorscheme(row.value)
            } else if (row.provider === "power-profile") {
                Shell.applyPowerProfile(row.value)
            } else if (row.action) {
                Shell.runDetached(row.action)
            }
            Qt.callLater(closeMenu)
        }
    }

    function openMenu(initialMenu) {
        root.activeMenu = root.item(initialMenu) ? initialMenu : "root"
        root.navStack = []
        root.filterText = ""
        searchInput.text = ""
        root.selectedIndex = 0

        // Ses seviyesini anında çek (%50 flash olmasını önler)
        var ainfo = Shell.getInitialAudioInfo()
        if (ainfo) {
            root.sysVolume = ainfo.volume
            root.sysMuted = ainfo.muted
        }

        root.opened = true
        if (!root.loaded) loadItems()
        else { evaluateGuards() }
        rebuildDisplay()
        startProvider(root.activeMenu)
        card.x = (root.width - card.width) / 2
        card.y = (root.height - card.height) / 2
        Qt.callLater(function(){ searchInput.forceActiveFocus() })
    }

    function closeMenu() {
        root.opened = false
        closeTimer.restart()
    }
    Timer { id: closeTimer; interval: 140; running: false; repeat: false; onTriggered: Qt.quit() }

    Connections {
        target: Shell
        function onReopenRequested() {
            handleHotkeyTrigger()
        }
        function onStatsUpdated(cpu, gpu, vol, muted) {
            root.cpuPercent = cpu
            root.gpuPercent = gpu
            root.sysVolume = vol
            root.sysMuted = muted
            root.sysMonitor = Shell.getSysMonitorInfo()
        }
        function onThemeChanged() {
            root.syncTheme()
        }
    }

    function handleHotkeyTrigger() {
        if (!root.opened) {
            openMenu("root")
            return
        }
        if (root.activeMenu !== "root" || root.filterText !== "") {
            reopenAnim.restart()
        } else {
            closeMenu()
        }
    }

    SequentialAnimation {
        id: reopenAnim
        ParallelAnimation {
            NumberAnimation { target: card; property: "opacity"; to: 0.15; duration: 90; easing.type: Easing.OutQuad }
            NumberAnimation { target: card; property: "scale"; to: 0.94; duration: 90; easing.type: Easing.OutQuad }
        }
        ScriptAction {
            script: {
                openMenu("root")
            }
        }
        ParallelAnimation {
            NumberAnimation { target: card; property: "opacity"; to: 1.0; duration: 140; easing.type: Easing.OutBack }
            NumberAnimation { target: card; property: "scale"; to: 1.0; duration: 140; easing.type: Easing.OutBack }
        }
    }

    // ── UI ─────────────────────────────────────────────────
    readonly property string dbgScroll: "trackH=" + scrollTrack.height + " thumbH=" + thumb.height + " thumbY=" + thumb.y + " contentY=" + resultList.contentY + " maxCY=" + (resultList.contentHeight - resultList.height) + " visible=" + scrollTrack.visible
    Rectangle {
        id: scrim
        anchors.fill: parent
        color: root.scrimColor
        opacity: root.opened ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        MouseArea {
            anchors.fill: parent
            onClicked: closeMenu()
        }
    }

    // ── Neon LED parlaması: tek ince parlak şerit + dışa doğru sönen hale ──
    // glowSource görünmezdir; MultiEffect onu çizer (kendi halkası + bulanık hale).
    Rectangle {
        id: glowSource
        anchors.fill: card
        radius: card.radius
        color: "transparent"
        border.color: root.bgBorder
        border.width: 2
        visible: false
    }
    MultiEffect {
        anchors.fill: glowSource
        source: glowSource
        shadowEnabled: true
        shadowColor: root.bgBorder
        shadowBlur: 0.6
        shadowHorizontalOffset: 0
        shadowVerticalOffset: 0
        opacity: root.opened ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
    }

    Rectangle {
        id: card
        width: root.cardWidth
        height: Math.min(headerHeight + 1 + displayModel.count * rowHeight + 12 + footerHeight, 720)
        x: (Screen.width - width) / 2
        y: (Screen.height - height) / 2
        color: root.bgColor
        border.color: root.bgBorder
        border.width: 1
        radius: 14
        clip: true
        opacity: root.opened ? 1.0 : 0.0
        scale: root.opened ? 1.0 : 0.94

        // Pencere içine tıklamak pencereyi kapatmasın; sağ tık ise bir önceki sayfaya döndürsün
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            propagateComposedEvents: true
            onClicked: function(mouse) {
                if (mouse.button === Qt.RightButton) {
                    Shell.playCreamy()
                    goBack()
                    mouse.accepted = true
                }
            }
        }
        Behavior on opacity { enabled: !reopenAnim.running; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        Behavior on scale { enabled: !reopenAnim.running; NumberAnimation { duration: 160; easing.type: Easing.OutBack } }
        Behavior on height { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }

        // Kartı sürükle: Standart pencere davranışı — Yalnızca Alt veya Meta tuşuna basılı tutulduğunda sürüklenir
        DragHandler {
            id: cardDragAlt
            target: card
            acceptedButtons: Qt.LeftButton
            acceptedModifiers: Qt.AltModifier
        }
        DragHandler {
            id: cardDragMeta
            target: card
            acceptedButtons: Qt.LeftButton
            acceptedModifiers: Qt.MetaModifier
        }

        onHeightChanged: {
            // Keep card at its dragged position or reasonable visible range
        }

        // ── Klavye işleyici ─────────────────────────────────
        // Odağı searchInput tutar; tuşlar Keys.forwardTo ile buraya gelir.
        // TextInput kendi select(int,int) metoduna sahip olduğu için
        // Keys.onPressed'i TextInput'ta tutmak root.select(delta)'yı gölgeliyordu.
        Item {
            id: keyHandler
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Escape) {
                    if (root.filterText.length) {
                        setFilter("")
                        searchInput.text = ""
                    } else if (root.activeMenu !== "root") {
                        goBack()
                    } else {
                        closeMenu()
                    }
                    event.accepted = true
                } else if ((event.key === Qt.Key_Backspace || event.key === Qt.Key_Left) && !root.filterText.length) {
                    goBack()
                    event.accepted = true
                } else if (event.key === Qt.Key_F) {
                    if (root.activeMenu === "appearance.colorscheme" && root.filterText.length === 0) {
                        var curIndex = root.selectedIndex
                        if (curIndex >= 0 && curIndex < displayModel.count) {
                            var curRow = displayModel.get(curIndex)
                            if (curRow && curRow.value && curRow.provider === "colorscheme") {
                                Shell.toggleFavoriteTheme(curRow.value)
                                root.providersLoaded["appearance.colorscheme"] = false
                                loadColorschemes("appearance.colorscheme")
                                event.accepted = true
                                return
                            }
                        }
                    }
                } else if (event.key === Qt.Key_Up) {
                    select(-1); event.accepted = true
                } else if (event.key === Qt.Key_Down) {
                    select(1); event.accepted = true
                } else if (event.key === Qt.Key_PageUp) {
                    select(-6); event.accepted = true
                } else if (event.key === Qt.Key_PageDown) {
                    select(6); event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Right) {
                    activateIndex(root.selectedIndex)
                    event.accepted = true
                }
            }
        }

        // Başlık / arama + Telemetri (CPU & GPU)
        Rectangle {
            id: header
            width: parent.width
            height: root.headerHeight
            color: "transparent"

            // ── Üst Bar: Ses Kontrol Paneli (Mute + Slider) ──
            Item {
                id: topMetricsBar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                anchors.top: parent.top
                anchors.topMargin: 8
                height: 20
                z: 5

                // Orta: Ses Kontrol Paneli (Mute butonu + Ses Slider)
                Row {
                    id: volumeControlRow
                    anchors.centerIn: parent
                    spacing: 8

                    // Mute Butonu
                    Text {
                        id: muteBtn
                        text: root.sysMuted ? "󰝟" : (root.sysVolume > 50 ? "󰕾" : (root.sysVolume > 0 ? "󰖀" : "󰕿"))
                        color: root.sysMuted ? "#ef4444" : root.bgBorder
                        font.pixelSize: 14
                        font.family: root.iconFont
                        anchors.verticalCenter: parent.verticalCenter

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Shell.playCreamy()
                                Shell.toggleSystemMute()
                                root.sysMuted = !root.sysMuted
                            }
                        }
                    }

                    // Ses Barı / Slider (Tıklama, Sürükleme ve WheelHandler)
                    Rectangle {
                        id: volumeSliderTrack
                        width: 110
                        height: 6
                        radius: 3
                        color: "#30ffffff"
                        anchors.verticalCenter: parent.verticalCenter

                        // Dolu kısım
                        Rectangle {
                            id: volumeSliderFill
                            height: parent.height
                            radius: 3
                            width: root.sysMuted ? 0 : Math.max(0, Math.min(parent.width, (root.sysVolume / 100.0) * parent.width))
                            color: root.bgBorder
                            Behavior on width { NumberAnimation { duration: 40 } }
                        }

                        // Thumb gösterge
                        Rectangle {
                            width: 10
                            height: 10
                            radius: 5
                            anchors.verticalCenter: parent.verticalCenter
                            x: Math.max(0, Math.min(volumeSliderTrack.width - 10, volumeSliderFill.width - 5))
                            color: root.selFg
                            border.color: root.bgBorder
                            border.width: 1
                            visible: !root.sysMuted
                        }

                        WheelHandler {
                            id: volWheelHandler
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            onWheel: function(event) {
                                var delta = event.angleDelta.y > 0 ? 5 : -5
                                var v = Math.max(0, Math.min(100, root.sysVolume + delta))
                                root.sysVolume = v
                                root.sysMuted = false
                                Shell.setSystemVolume(v)
                            }
                        }

                        MouseArea {
                            id: volDragArea
                            anchors.fill: parent
                            anchors.topMargin: -8
                            anchors.bottomMargin: -8
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor

                            function updateVol(mouse) {
                                var ratio = Math.max(0.0, Math.min(1.0, mouse.x / volumeSliderTrack.width))
                                var newVol = Math.round(ratio * 100)
                                root.sysVolume = newVol
                                root.sysMuted = false
                                Shell.setSystemVolume(newVol)
                            }

                            onPressed: function(mouse) {
                                updateVol(mouse)
                            }
                            onPositionChanged: function(mouse) {
                                if (pressed) updateVol(mouse)
                            }
                            onWheel: function(wheel) {
                                var delta = wheel.angleDelta.y > 0 ? 5 : -5
                                var v = Math.max(0, Math.min(100, root.sysVolume + delta))
                                root.sysVolume = v
                                root.sysMuted = false
                                Shell.setSystemVolume(v)
                            }
                        }
                    }

                    // Ses Yüzdesi Metni
                    Text {
                        text: root.sysMuted ? "MUTE" : root.sysVolume + "%"
                        color: root.sysMuted ? "#ef4444" : root.fgDim
                        font.pixelSize: 10
                        font.family: root.textFont
                        font.weight: Font.Bold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            Text {
                id: pathLabel
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 11
                text: root.activeMenu === "root" ? "RavenSpot" : MenuModel.pathFor(root.items, root.activeMenu)
                color: root.fgDim
                font.pixelSize: 13
                font.family: root.textFont
                visible: !root.filterText.length
            }

            TextInput {
                id: searchInput
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 8
                color: root.fgColor
                font.pixelSize: 18
                font.family: root.textFont
                focus: true
                selectByMouse: true
                onTextChanged: setFilter(text)
                Keys.forwardTo: [keyHandler]
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: root.bgBorder
            anchors.top: header.bottom
        }

        // Sonuç listesi
        ListView {
            id: resultList
            anchors.top: parent.top
            anchors.topMargin: headerHeight + 1 + 6
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.rightMargin: scrollTrack.visible ? 10 : 6
            anchors.leftMargin: 6
            anchors.bottom: footerMonitorBar.top
            anchors.bottomMargin: 4
            clip: true
            model: displayModel
            boundsBehavior: Flickable.DragAndOvershootBounds
            highlight: Rectangle {
                color: root.selBg
                radius: 6
                border.color: Qt.rgba(root.selFg.r, root.selFg.g, root.selFg.b, 0.18)
                border.width: 1
            }
            highlightMoveDuration: 55
            highlightMoveVelocity: -1

            delegate: Rectangle {
                id: row
                width: resultList.width
                height: root.rowHeight
                radius: 6
                color: (index === resultList.currentIndex && !model.disabled) ? "transparent" : "transparent"

                Text {
                    id: glyph
                    width: 40
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignHCenter
                    text: model.iconName ? "" : model.icon
                    font.family: root.iconFont
                    font.pixelSize: 18
                    color: (index === resultList.currentIndex && !model.disabled) ? root.selFg : root.fgColor
                }

                // Tema ikonu için hafif arka plan (açık renkli ikonlar koyu zeminde kaybolmasın)
                Rectangle {
                    width: 26
                    height: 26
                    radius: 6
                    anchors.left: glyph.left
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    color: "transparent"
                    visible: !!model.iconName
                }

                // Tema ikonu (menü girdileri için renkli candy-icons ikonu)
                Image {
                    id: themeIconImg
                    width: 26
                    height: 26
                    anchors.left: glyph.left
                    anchors.leftMargin: 9
                    anchors.verticalCenter: parent.verticalCenter
                    source: model.iconName ? ("image://themeicon/" + model.iconName + "?v=" + root.themeVersion) : ""
                    sourceSize: Qt.size(26, 26)
                    visible: !!model.iconName
                    smooth: true
                    fillMode: Image.PreserveAspectFit
                }

                // Uygulama ikonu (image provider) — sabit kare, oranı koru
                Rectangle {
                    width: 26
                    height: 26
                    radius: 6
                    anchors.left: glyph.left
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    color: "transparent"
                    visible: !!model.appIcon
                }
                Image {
                    id: appIconImg
                    width: 26
                    height: 26
                    anchors.left: glyph.left
                    anchors.leftMargin: 9
                    anchors.verticalCenter: parent.verticalCenter
                    source: model.appIcon ? ("image://appicon/" + model.appIcon + "?v=" + root.themeVersion) : ""
                    sourceSize: Qt.size(26, 26)
                    visible: !!model.appIcon
                    smooth: true
                    fillMode: Image.PreserveAspectFit
                }

                readonly property bool hasDetail: (root.filterText.length > 0 && !!model.detail && model.detail.length > 0)

                Column {
                    anchors.left: glyph.right
                    anchors.right: favStar.visible ? favStar.left : (rightDetail.visible ? rightDetail.left : chevron.left)
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3

                    Text {
                        id: labelText
                        width: parent.width
                        text: model.label
                        color: (index === resultList.currentIndex && !model.disabled) ? root.selFg
                             : (model.disabled ? root.fgDim : root.fgColor)
                        font.pixelSize: row.hasDetail ? 14 : 15
                        font.family: root.textFont
                        font.weight: row.hasDetail ? Font.Medium : Font.Normal
                        elide: Text.ElideRight
                    }

                    // Detay (arama sırasında alt yol / açıklama)
                    Text {
                        id: subDetailText
                        width: parent.width
                        text: model.detail || ""
                        color: (index === resultList.currentIndex && !model.disabled) ? Qt.rgba(root.selFg.r, root.selFg.g, root.selFg.b, 0.75) : root.fgDim
                        font.pixelSize: 11
                        font.family: root.textFont
                        elide: Text.ElideRight
                        visible: row.hasDetail
                    }
                }

                // Sağ taraf: Durum rozeti / favori yıldızı
                Text {
                    id: rightDetail
                    anchors.right: (chevron.text.length > 0) ? chevron.left : parent.right
                    anchors.rightMargin: (chevron.text.length > 0) ? 6 : 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: (root.filterText.length === 0 && model.detail) ? model.detail : ""
                    color: model.isCurrent ? "#69f0ae" : (model.isFavorite ? "#ffe082" : root.fgDim)
                    font.pixelSize: 12
                    font.family: root.textFont
                    visible: text.length > 0
                }

                Text {
                    id: favStar
                    anchors.right: rightDetail.visible ? rightDetail.left : ((chevron.text.length > 0) ? chevron.left : parent.right)
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: model.provider === "colorscheme" ? (model.isFavorite ? "★" : "☆") : ""
                    color: model.isFavorite ? "#ffb300" : root.fgDim
                    font.pixelSize: 15
                    visible: model.provider === "colorscheme"
                }

                Text {
                    id: chevron
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: (model.kind === "menu" || model.kind === "link") ? "›" : ""
                    color: (index === resultList.currentIndex && !model.disabled) ? root.selFg : root.fgDim
                    font.pixelSize: 18
                    font.family: root.textFont
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: {
                        if (!model.disabled && root.selectedIndex !== index) {
                            root.selectedIndex = index
                            resultList.currentIndex = index
                            Shell.playThock()
                        }
                    }
                    onClicked: activateIndex(index)
                }
            }
        }

        // ── Özel kaydırma çubuğu ─────────────────────────────
        // Sabit ~1cm boyutlu, parlak altın sarısı, fareyle tutulup sürüklenebilir.
        // Qt'nin hazır ScrollBar'ı custom tema altında thumb genişliğini 0 yapıyordu;
        // bu yüzden kendi basit çubuğumuzu çiziyoruz.
        Item {
            id: scrollTrack
            anchors.top: resultList.top
            anchors.topMargin: 4
            anchors.bottom: resultList.bottom
            anchors.bottomMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 3
            width: 6
            visible: (resultList.contentHeight - resultList.height) > 16

            // Arka plan rayı (çok hafif, tutulabilirlik hissi verir)
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 1
                anchors.rightMargin: 1
                radius: 2
                color: "#16000000"
            }

            // Thumb — zarif, modern parlak şerit
            Rectangle {
                id: thumb
                width: 5
                height: 36
                radius: 2.5
                x: 0.5
                color: thumbDrag.pressed ? "#ffef9a"
                     : thumbDrag.containsMouse ? "#ffe08a"
                     : root.bgBorder
                y: {
                    var travel = scrollTrack.height - thumb.height
                    if (travel <= 0) return 0
                    var maxContentY = resultList.contentHeight - resultList.height
                    if (maxContentY <= 0) return 0
                    var pos = (resultList.contentY / maxContentY) * travel
                    return Math.max(0, Math.min(travel, pos))
                }
            }

            MouseArea {
                id: thumbDrag
                anchors.fill: parent
                hoverEnabled: true
                property real pressOffset: 0

                onPressed: function(mouse) {
                    // Tıklanan konuma atla + sürüklemeye başla
                    var travel = scrollTrack.height - thumb.height
                    var maxContentY = resultList.contentHeight - resultList.height
                    if (travel > 0 && maxContentY > 0) {
                        var targetThumbY = mouse.y - thumb.height / 2
                        var frac = Math.max(0, Math.min(1, targetThumbY / travel))
                        resultList.contentY = frac * maxContentY
                    }
                    pressOffset = mouse.y
                }

                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    var travel = scrollTrack.height - thumb.height
                    var maxContentY = resultList.contentHeight - resultList.height
                    if (travel > 0 && maxContentY > 0) {
                        var targetThumbY = mouse.y - thumb.height / 2
                        var frac = Math.max(0, Math.min(1, targetThumbY / travel))
                        resultList.contentY = frac * maxContentY
                    }
                }
            }
        }

        // ── Alt Sistem Monitör Paneli (RAM, Disk, Sıcaklık, Uptime) ──
        Rectangle {
            id: footerMonitorBar
            width: parent.width
            height: root.footerHeight
            anchors.bottom: parent.bottom
            color: "#18000000"

            Rectangle {
                width: parent.width
                height: 1
                anchors.top: parent.top
                color: Qt.rgba(root.bgBorder.r, root.bgBorder.g, root.bgBorder.b, 0.20)
            }

            Row {
                anchors.centerIn: parent
                spacing: 14

                // CPU
                Row {
                    spacing: 5
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: " CPU"
                        color: root.cpuColor
                        font.pixelSize: 11
                        font.family: root.iconFont
                        font.weight: Font.Bold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: root.cpuPercent + "%"
                        color: root.fgDim
                        font.pixelSize: 11
                        font.family: root.textFont
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                // GPU
                Row {
                    spacing: 5
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: "󰢮 GPU"
                        color: root.gpuColor
                        font.pixelSize: 11
                        font.family: root.iconFont
                        font.weight: Font.Bold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: root.gpuPercent + "%"
                        color: root.fgDim
                        font.pixelSize: 11
                        font.family: root.textFont
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                // RAM
                Row {
                    spacing: 5
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: "󰘚 RAM"
                        color: root.bgBorder
                        font.pixelSize: 11
                        font.family: root.iconFont
                        font.weight: Font.Bold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: (root.sysMonitor ? root.sysMonitor.ram_text : "--") + " (" + (root.sysMonitor ? root.sysMonitor.ram_pct : 0) + "%)"
                        color: root.fgDim
                        font.pixelSize: 11
                        font.family: root.textFont
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                // Disk (Root /)
                Row {
                    spacing: 5
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: "󰋊 Disk"
                        color: root.bgBorder
                        font.pixelSize: 11
                        font.family: root.iconFont
                        font.weight: Font.Bold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: (root.sysMonitor ? root.sysMonitor.disk_text : "--") + " (" + (root.sysMonitor ? root.sysMonitor.disk_pct : 0) + "%)"
                        color: root.fgDim
                        font.pixelSize: 11
                        font.family: root.textFont
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                // Sıcaklık
                Row {
                    spacing: 5
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: " Isı"
                        color: root.cpuColor
                        font.pixelSize: 11
                        font.family: root.iconFont
                        font.weight: Font.Bold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: root.sysMonitor ? root.sysMonitor.temp : "--"
                        color: root.fgDim
                        font.pixelSize: 11
                        font.family: root.textFont
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                // Uptime
                Row {
                    spacing: 5
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: "󰔚 Süre"
                        color: root.gpuColor
                        font.pixelSize: 11
                        font.family: root.iconFont
                        font.weight: Font.Bold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: root.sysMonitor ? root.sysMonitor.uptime : "--"
                        color: root.fgDim
                        font.pixelSize: 11
                        font.family: root.textFont
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        syncTheme()
        var ainfo = Shell.getInitialAudioInfo()
        if (ainfo) {
            root.sysVolume = ainfo.volume
            root.sysMuted = ainfo.muted
        }
        root.sysMonitor = Shell.getSysMonitorInfo()
        openMenu("root")
    }
}
