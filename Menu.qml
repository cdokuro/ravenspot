// RavenSpot — KDE/CachyOS Spotlight Search & Application Menu.
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Effects
import org.kde.plasma.private.batterymonitor
import "MenuModel.js" as MenuModel

Window {
    id: root
    visible: !Shell.startHidden
    // Sabit yüzey boyutu: pencere kartla birlikte boyutlanırsa Wayland layer
    // yüzeyi her animasyon karesinde yeniden configure edilir ve yazarken
    // titrer. Kart bu alan içinde animasyonla büyür; dışı input mask ile
    // tıklamayı alttaki pencereye geçirir (LayerShellWindow::setCardHeight).
    width: root.cardWidth + 48
    height: root.maxCardHeight + 48
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

    InhibitionControl {
        id: kdeInhibitCtrl
        onIsManuallyInhibitedChanged: {
            root.sleepInhibited = isManuallyInhibited
        }
    }

    property bool canCloseOnInactive: false

    // ── Async arama durumu ────────────────────────────────
    // fd ve KWin WindowsRunner aramaları UI thread'ini bloklamaz; sonuçlar
    // sinyalle gelir. searchSeq ile eski (geç gelen) yanıtlar elenir.
    property int searchSeq: 0
    property string fileResultsQuery: ""
    property var fileResults: []
    property bool fileSearchBusy: false
    property string winResultsQuery: ""
    property var winResults: []
    Timer {
        id: searchDebounce
        interval: 90 // hızlı yazarken her tuşta süreç/D-Bus çağrısı başlatma
        repeat: false
        onTriggered: root.dispatchAsyncSearch()
    }

    function dispatchAsyncSearch() {
        var q = root.filterText.trim()
        if (!q) return
        root.searchSeq++
        if (q.startsWith("*")) {
            var fq = q.substring(1).trim()
            if (!fq) return
            root.fileSearchBusy = true
            Shell.requestFileSearch(fq, root.searchSeq)
        } else if (Shell.requestWindowSearch) {
            Shell.requestWindowSearch(q, root.searchSeq)
        }
    }

    // Async sonuç geldiğinde seçili satırı itemId ile koru (liste üstüne satır eklenince kaymasın)
    function rebuildPreservingSelection() {
        var keepId = ""
        if (root.selectedIndex > 0 && root.selectedIndex < displayModel.count)
            keepId = displayModel.get(root.selectedIndex).itemId
        rebuildDisplay()
        if (keepId) {
            for (var i = 0; i < displayModel.count; i++) {
                if (displayModel.get(i).itemId === keepId) {
                    root.selectedIndex = i
                    resultList.currentIndex = i
                    return
                }
            }
        }
    }
    Timer {
        id: enableCloseOnInactiveTimer
        interval: 350
        repeat: false
        onTriggered: {
            if (root.active) {
                root.canCloseOnInactive = true
            }
        }
    }
    onActiveChanged: {
        if (active) {
            enableCloseOnInactiveTimer.restart()
        } else if (root.opened && root.canCloseOnInactive && !Shell.isSwitchingScreen()) {
            // Sürüklerken başka monitöre geçişte yüzey yeniden yaratılır; o anki odak kaybı kapatmaz
            closeMenu()
        }
    }

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
    property int cardWidth: 495
    readonly property int maxCardHeight: 720
    property int rowHeight: 48
    // Arama sonuç grubu başlığı (Top Hit, Applications, …) yüksekliği ve sayısı
    property int sectionHeaderHeight: 26
    property int sectionHeaderCount: 0
    property int headerHeight: 114
    property int cpuPercent: 0
    property int gpuPercent: 0
    property int appSoundVolume: 10
    property bool appSoundMuted: false
    property bool audioFlyoutOpen: false
    property int sysVolume: 50
    property bool sysMuted: false
    property var sysMonitor: ({ "ram_text": "--", "ram_pct": 0, "disk_text": "--", "disk_pct": 0, "temp": "--", "uptime": "--" })
    property var sysUpdateInfo: null
    property int footerHeight: 38

    property string currentPowerProfile: "balanced"
    property bool sleepInhibited: false
    property bool powerFlyoutOpen: false
    property bool depModalOpen: false
    property string depModalTitle: ""
    property string depModalDesc: ""
    property string depModalPkg: ""
    property string depModalCmd: ""

    function showDependencyPrompt(pkg, title, desc, cmd) {
        depModalPkg = pkg
        depModalTitle = title
        depModalDesc = desc
        depModalCmd = cmd
        depModalOpen = true
    }

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
    // Uygulaması olmayan kategoriler (ör. Office (0)) gezinmede gizlenir
    property var emptyCategories: ({})

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
            var empty = ({})
            for (var cId in catMap) {
                if (cId !== "apps.all" && counts[catMap[cId]] === 0) empty[cId] = true
                if (root.items[cId] && counts[catMap[cId]] !== undefined) {
                    var baseName = root.items[cId].label.replace(/\s*\(\d+\)$/, "")
                    root.items[cId].label = baseName + " (" + counts[catMap[cId]] + ")"
                }
                loadAppsCategory(cId, catMap[cId], true)
                root.providersLoaded[cId] = true
            }
            root.emptyCategories = empty
        } catch(e) {
            loadAppsCategory("apps.all", "all", true)
            root.providersLoaded["apps.all"] = true
        }

        evaluateGuards()
        rebuildDisplay()
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
        } else if (entry.provider === "recent") {
            loadRecentFiles(id)
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
                label: c.label, title: "", target: "", description: c.truncated ? "Copy to clipboard · truncated to 4 KB" : "Copy to clipboard",
                action: "", provider: "clipboard-item", aliases: [], searchTerms: [],
                when: "", checked: "", disabled: "", value: c.value, order: j
            })
        }
        var merged = MenuModel.swapProviderRows(root.items, root.itemOrder, id, rows)
        root.items = merged.items
        root.itemOrder = merged.itemOrder
        rebuildDisplay()
    }

    function loadRecentFiles(id) {
        var arr = []
        try { arr = JSON.parse(Shell.recentFiles("") || "[]") } catch (e) {}
        var rows = []
        for (var j = 0; j < arr.length; j++) {
            var f = arr[j]
            rows.push({
                id: id + ".recent-" + j, parent: id, kind: "action",
                icon: f.isDir ? "󰉋" : "󰈔", iconFont: "", iconName: f.isDir ? "folder" : "document-open-recent",
                label: f.name, title: "", target: "", description: (f.modified ? f.modified + "  •  " : "") + f.path,
                action: "", provider: "file-item", aliases: [f.name], searchTerms: [f.name],
                when: "", checked: "", disabled: "", filePath: f.path, isDir: !!f.isDir, order: j
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
                when: "", checked: "", disabled: "", filePath: f.path, isDir: !!f.is_dir, order: j
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

    function loadAppsCategory(id, cat, skipRebuild) {
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
        if (cat === "games") {
            // Games'in en üstünde: Steam kütüphanelerinin steamapps/common oyun klasörleri
            rows.unshift({
                id: id + ".steam-library", parent: id, kind: "menu",
                icon: "", iconFont: "", iconName: "steam", appIcon: "", appId: "",
                label: "Steam Library", title: "", target: "",
                description: "Installed game folders (steamapps/common)",
                action: "", provider: "files-steam", aliases: ["steam", "steam library", "steamapps", "common"],
                searchTerms: [], when: "", checked: "", disabled: "", order: -1
            })
        }
        var merged = MenuModel.swapProviderRows(root.items, root.itemOrder, id, rows)
        root.items = merged.items
        root.itemOrder = merged.itemOrder
        if (!skipRebuild) {
            rebuildDisplay()
        }
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
            var detailStr = "F: add to favorites"
            if (c.current) {
                iconStr = "✓"
                detailStr = "Current theme"
            } else if (c.isFavorite) {
                iconStr = "★"
                detailStr = "Favorite theme"
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
            parent: entry.parent || "",
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
            themeSection: entry.themeSection || "",
            filePath: entry.filePath || "",
            isDir: !!entry.isDir
        }
    }

    function tryEvalMath(q) {
        var expr = q.startsWith("=") ? q.substring(1).trim() : q.trim()
        if (!expr) return null

        // Yüzde kalıpları: "20% of 150" veya "150 + 20%"
        expr = expr.replace(/(\d+(\.\d+)?)\s*%\s*(?:of|\*)\s*(\d+(\.\d+)?)/gi, "($1/100*$3)")
        expr = expr.replace(/(\d+(\.\d+)?)\s*([+-])\s*(\d+(\.\d+)?)\s*%/gi, "($1 $3 ($1 * ($4/100)))")
        expr = expr.replace(/(\d+(\.\d+)?)\s*%/g, "($1/100)")

        var fnExpr = expr
            .replace(/\bpi\b/gi, "Math.PI")
            .replace(/\be\b/gi, "Math.E")
            .replace(/\bsqrt\(/gi, "Math.sqrt(")
            .replace(/\bsin\(/gi, "Math.sin(")
            .replace(/\bcos\(/gi, "Math.cos(")
            .replace(/\btan\(/gi, "Math.tan(")
            .replace(/\blog\(/gi, "Math.log10(")
            .replace(/\bln\(/gi, "Math.log(")
            .replace(/\babs\(/gi, "Math.abs(")
            .replace(/\bround\(/gi, "Math.round(")
            .replace(/\bfloor\(/gi, "Math.floor(")
            .replace(/\bceil\(/gi, "Math.ceil(")
            .replace(/x|X/g, "*")
            .replace(/÷/g, "/")
            .replace(/\^/g, "**")

        if (!/^[\d\s+\-*/()^.,%*MathPIEsqrtincosagblorfed]+$/.test(fnExpr)) return null
        if (!q.startsWith("=") && !/[+\-*/^()]|Math\./.test(fnExpr)) return null

        try {
            var res = Function("return (" + fnExpr + ")")()
            if (typeof res === "number" && !isNaN(res) && isFinite(res)) {
                return (Math.round(res * 100000000) / 100000000).toString()
            }
        } catch (e) {}
        return null
    }

    function tryEvalUnit(q) {
        var str = q.trim().toLowerCase()
        var m = str.match(/^([\d.]+)\s*([a-z°]+)\s*(?:in|to|\/|->)\s*([a-z°]+)$/i)
        if (!m) return null
        var val = parseFloat(m[1])
        var u1 = m[2].toLowerCase()
        var u2 = m[3].toLowerCase()
        if (isNaN(val)) return null

        var res = null
        var unitName = ""

        if (u1 === "km" && (u2 === "m" || u2 === "meter")) { res = val * 1000; unitName = "m" }
        else if ((u1 === "m" || u1 === "meter") && u2 === "km") { res = val / 1000; unitName = "km" }
        else if (u1 === "miles" || u1 === "mi") {
            if (u2 === "km") { res = val * 1.60934; unitName = "km" }
            else if (u2 === "m") { res = val * 1609.34; unitName = "m" }
        }
        else if (u1 === "km" && (u2 === "miles" || u2 === "mi")) { res = val / 1.60934; unitName = "mil" }
        else if (u1 === "cm" && u2 === "m") { res = val / 100; unitName = "m" }
        else if (u1 === "m" && u2 === "cm") { res = val * 100; unitName = "cm" }
        else if ((u1 === "in" || u1 === "inch") && u2 === "cm") { res = val * 2.54; unitName = "cm" }
        else if (u2 === "in" || u2 === "inch") {
            if (u1 === "cm") { res = val / 2.54; unitName = "in" }
        }
        else if (u1 === "gb" && u2 === "mb") { res = val * 1024; unitName = "MB" }
        else if (u1 === "mb" && u2 === "gb") { res = val / 1024; unitName = "GB" }
        else if (u1 === "tb" && u2 === "gb") { res = val * 1024; unitName = "GB" }
        else if (u1 === "gb" && u2 === "tb") { res = val / 1024; unitName = "TB" }
        else if (u1 === "kb" && u2 === "mb") { res = val / 1024; unitName = "MB" }
        else if (u1 === "mb" && u2 === "kb") { res = val * 1024; unitName = "KB" }
        else if ((u1 === "c" || u1 === "°c") && (u2 === "f" || u2 === "°f")) { res = (val * 9/5) + 32; unitName = "°F" }
        else if ((u1 === "f" || u1 === "°f") && (u2 === "c" || u2 === "°c")) { res = (val - 32) * 5/9; unitName = "°C" }
        else if (u1 === "kg" && (u2 === "lbs" || u2 === "lb")) { res = val * 2.20462; unitName = "lbs" }
        else if ((u1 === "lbs" || u1 === "lb") && u2 === "kg") { res = val / 2.20462; unitName = "kg" }
        else if (u1 === "kg" && (u2 === "g" || u2 === "gram")) { res = val * 1000; unitName = "g" }
        else if ((u1 === "g" || u1 === "gram") && u2 === "kg") { res = val / 1000; unitName = "kg" }

        if (res !== null) {
            var formatted = Math.round(res * 10000) / 10000
            return {
                original: val + " " + u1,
                result: formatted + " " + unitName,
                copyVal: formatted.toString()
            }
        }
        return null
    }

    // RAVENSPOT_PERF=1 ile her yeniden kurulumun süresi loglanır (varsayılan kapalı).
    readonly property bool perfLogging: Shell.perfLogging()

    function rebuildDisplay() {
        if (!root.perfLogging) { rebuildDisplayInner(); return }
        var t0 = Date.now()
        rebuildDisplayInner()
        console.log("rebuildDisplay", Date.now() - t0, "ms rows=", displayModel.count)
    }

    // Satırlar MenuModel.syncRows ile yerinde güncellenir: delegate'ler her
    // tuşta yok edilip yeniden yaratılmaz, yalnızca kuyruk eklenir/silinir.
    function rebuildDisplayInner() {
        if (!root.loaded) { MenuModel.syncRows(displayModel, []); return }

        var active = root.item(root.activeMenu) ? root.activeMenu : "root"
        root.activeMenu = active
        var rows = []
        var query = root.filterText.trim()

        if (query) {
            // Everything tarzı anlık dosya ve dizin araması (* öneki)
            if (query.startsWith("*")) {
                var fileQuery = query.substring(1).trim()
                if (!fileQuery) {
                    rows.push({
                        itemId: "__file_search_hint__",
                        kind: "action",
                        icon: "󰉋",
                        iconName: "system-search",
                        iconFont: "",
                        appIcon: "",
                        appId: "",
                        label: "Search Files and Folders (* <name>)",
                        target: "",
                        detail: "Type a name to search files and folders instantly",
                        path: "File Search",
                        childCount: 0,
                        action: "",
                        provider: "none",
                        value: "",
                        desktop: "",
                        disabled: true,
                        score: 0,
                        section: ""
                    })
                } else {
                    try {
                        var haveFresh = root.fileResultsQuery === fileQuery
                        // Yazmaya devam ederken (sorgu öncekinin devamıysa) önceki sonuçları
                        // göster ki liste titremesin; alakasız eski sonuçları gösterme.
                        var isRefinement = root.fileResultsQuery.length > 0
                            && fileQuery.toLowerCase().indexOf(root.fileResultsQuery.toLowerCase()) === 0
                        var fileResults = (haveFresh || isRefinement) ? root.fileResults : []
                        for (var fi = 0; fi < fileResults.length; fi++) {
                            var f = fileResults[fi]
                            var desc = (f.dir || "")
                            if (f.size) desc += (desc ? "  •  " : "") + f.size
                            if (f.date) desc += (desc ? "  •  " : "") + f.date
                            rows.push({
                                itemId: "__file_" + fi + "__",
                                kind: "action",
                                icon: f.icon || (f.isDir ? "󰝰" : "󰈙"),
                                iconName: f.iconName || (f.isDir ? "folder" : "text-plain"),
                                iconFont: "",
                                appIcon: "",
                                appId: "",
                                label: f.name,
                                target: "",
                                detail: desc,
                                path: f.dir || "",
                                childCount: 0,
                                action: "",
                                provider: "file-item",
                                value: f.path,
                                desktop: "",
                                disabled: false,
                                score: fi,
                                section: "",
                                filePath: f.path,
                                isDir: !!f.isDir
                            })
                        }
                        if (fileResults.length === 0 && (!haveFresh || root.fileSearchBusy)) {
                            rows.push({
                                itemId: "__file_searching__",
                                kind: "action",
                                icon: "󰍉",
                                iconName: "edit-find",
                                iconFont: "",
                                appIcon: "",
                                appId: "",
                                label: "Searching…",
                                target: "",
                                detail: "Scanning files for '" + fileQuery + "'",
                                path: "File Search",
                                childCount: 0,
                                action: "",
                                provider: "none",
                                value: "",
                                desktop: "",
                                disabled: true,
                                score: 0,
                                section: ""
                            })
                        } else if (fileResults.length === 0) {
                            rows.push({
                                itemId: "__no_files__",
                                kind: "action",
                                icon: "󰅙",
                                iconName: "edit-find",
                                iconFont: "",
                                appIcon: "",
                                appId: "",
                                label: "No matching files or folders",
                                target: "",
                                detail: "Nothing found for '" + fileQuery + "'",
                                path: "File Search",
                                childCount: 0,
                                action: "",
                                provider: "none",
                                value: "",
                                desktop: "",
                                disabled: true,
                                score: 0,
                                section: ""
                            })
                        }
                    } catch (e) {
                        console.log("Dosya arama hatası:", e)
                    }
                }
                root.sectionHeaderCount = 0
                MenuModel.syncRows(displayModel, rows)
                settleCursor()
                return
            }

            var currentRows = []
            var drilldownRows = []
            for (var i = 0; i < root.itemOrder.length; i++) {
                var entry = root.item(root.itemOrder[i])
                if (!entry || entry.id === "root") continue
                if (!root.isDescendantOf(entry.id, active)) continue
                if (root.emptyCategories[entry.id]) continue
                if (!MenuModel.matchesQuery(entry, query, root.isVisible(entry) && !root.isDisabled(entry))) continue
                var detail = root.parentPathFor(entry.id)
                var row = displayRow(entry, detail, MenuModel.searchScore(root.items, entry, query))
                if (entry.parent === active) currentRows.push(row)
                else drilldownRows.push(row)
            }
            currentRows.sort(function(a,b){ return a.score !== b.score ? a.score - b.score : a.path.localeCompare(b.path) })
            drilldownRows.sort(function(a,b){ return a.score !== b.score ? a.score - b.score : a.path.localeCompare(b.path) })

            // Uygulama tekrarlarını filtrele (aynı uygulama All Apps ve alt kategori altında çift çıkmasın)
            function deduplicateAppList(list) {
                var seen = ({})
                var out = []
                for (var idx = 0; idx < list.length; idx++) {
                    var r = list[idx]
                    if (r.kind !== "app") {
                        out.push(r)
                        continue
                    }
                    var k = r.desktop ? r.desktop.toLowerCase() : (r.appId ? r.appId.toLowerCase() : (r.label ? r.label.toLowerCase() : r.itemId))
                    if (!seen[k]) {
                        seen[k] = r
                        out.push(r)
                    } else {
                        var existing = seen[k]
                        var existingIsAll = existing.parent === "apps.all" || (existing.itemId && existing.itemId.indexOf("apps.all.") === 0)
                        var newIsSpecific = r.parent !== "apps.all" && (r.itemId && r.itemId.indexOf("apps.all.") !== 0)
                        if (existingIsAll && newIsSpecific) {
                            var outIdx = out.indexOf(existing)
                            if (outIdx >= 0) {
                                out[outIdx] = r
                            }
                            seen[k] = r
                        }
                    }
                }
                return out
            }

            currentRows = deduplicateAppList(currentRows)
            drilldownRows = deduplicateAppList(drilldownRows)

            // currentRows içinde olan uygulama drilldownRows içinde tekrar etmesin
            var currentAppKeys = ({})
            for (var c = 0; c < currentRows.length; c++) {
                var cr = currentRows[c]
                if (cr.kind === "app") {
                    var ck = cr.desktop ? cr.desktop.toLowerCase() : (cr.appId ? cr.appId.toLowerCase() : (cr.label ? cr.label.toLowerCase() : cr.itemId))
                    currentAppKeys[ck] = true
                }
            }
            if (Object.keys(currentAppKeys).length > 0) {
                var filteredDrilldown = []
                for (var dd = 0; dd < drilldownRows.length; dd++) {
                    var dr = drilldownRows[dd]
                    if (dr.kind === "app") {
                        var dk = dr.desktop ? dr.desktop.toLowerCase() : (dr.appId ? dr.appId.toLowerCase() : (dr.label ? dr.label.toLowerCase() : dr.itemId))
                        if (currentAppKeys[dk]) continue
                    }
                    filteredDrilldown.push(dr)
                }
                drilldownRows = filteredDrilldown
            }

            rows = currentRows.concat(drilldownRows)

            // 0. Açık Pencereler (KWin WindowsRunner)
            if (query.trim().length > 0 && root.winResults.length > 0) {
                try {
                    var winResults = root.winResults
                    if (root.winResultsQuery !== query) {
                        // Taze yanıt gelene kadar eski sonuçları yerelde süz
                        var ql = query.toLowerCase()
                        winResults = winResults.filter(function(w) {
                            return (w.title || "").toLowerCase().indexOf(ql) >= 0
                        })
                    }
                    for (var wi = winResults.length - 1; wi >= 0; wi--) {
                        var w = winResults[wi]
                        rows.unshift({
                            itemId: "__win_" + w.id + "__",
                            kind: "action",
                            icon: "󰖲",
                            iconName: w.iconName || "window",
                            iconFont: root.iconFont,
                            appIcon: w.iconName || "",
                            appId: "",
                            label: w.title,
                            target: "",
                            detail: "Open Window • Press Enter to switch",
                            path: "Windows",
                            childCount: 0,
                            action: "",
                            provider: "window-item",
                            value: w.id,
                            desktop: "",
                            disabled: false,
                            score: -80000,
                            section: ""
                        })
                    }
                } catch(e) {}
            }

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
                    detail: "Calculator · Enter: copy result",
                    path: "Calculator",
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

            // 1.b Birim Dönüştürücü (KRunner)
            var unitObj = tryEvalUnit(query)
            if (unitObj !== null) {
                rows.unshift({
                    itemId: "__unit__",
                    kind: "action",
                    icon: "󰑮",
                    iconName: "accessories-calculator",
                    iconFont: "",
                    appIcon: "",
                    appId: "",
                    label: unitObj.original + " = " + unitObj.result,
                    target: "",
                    detail: "Unit conversion · Enter: copy result",
                    path: "Convert",
                    childCount: 0,
                    action: "",
                    provider: "clipboard-item",
                    value: unitObj.copyVal,
                    desktop: "",
                    disabled: false,
                    score: -99999,
                    section: ""
                })
            }

            // 2. KRunner Sistem Eylemleri (Kapat, Yeniden Başlat, Kilitle, vb.)
            var qLower = query.toLowerCase()
            // Güç/oturum eylemleri Power menüsündeki girişlerden gelir (kopya satır yok);
            // yalnızca menüde karşılığı olmayan Restart Plasma burada.
            var sysActions = [
                {
                    triggers: ["restart plasma", "plasma restart", "kwin restart", "plasma"],
                    label: "Restart KDE Plasma",
                    detail: "Refresh and restart desktop shell",
                    icon: "󰑐",
                    actionId: "restart_plasma"
                }
            ]

            for (var sa = 0; sa < sysActions.length; sa++) {
                var act = sysActions[sa]
                for (var st = 0; st < act.triggers.length; st++) {
                    if ((qLower.length >= 3 && act.triggers[st].indexOf(qLower) === 0) || qLower.indexOf(act.triggers[st]) !== -1) {
                        rows.unshift({
                            itemId: "__sys_" + act.actionId + "__",
                            kind: "action",
                            icon: act.icon,
                            iconName: "system-run",
                            iconFont: "",
                            appIcon: "",
                            appId: "",
                            label: act.label,
                            target: "",
                            detail: act.detail,
                            path: "Power",
                            childCount: 0,
                            action: "",
                            provider: "sys-action",
                            value: act.actionId,
                            desktop: "",
                            disabled: false,
                            score: -99990,
                            section: ""
                        })
                        break
                    }
                }
            }

            // 3. Ses ve Mute Komutları (örn. "ses 60", "vol 80", "mute", "sessiz")
            var volMatch = qLower.match(/^(?:ses|vol|volume)\s*(\d{1,3})$/)
            if (volMatch) {
                var targetVol = Math.max(0, Math.min(100, parseInt(volMatch[1])))
                rows.unshift({
                    itemId: "__set_vol__",
                    kind: "action",
                    icon: "󰕾",
                    iconName: "audio-volume-high",
                    iconFont: "",
                    appIcon: "",
                    appId: "",
                    label: "Set Volume to " + targetVol + "%",
                    target: "",
                    detail: "Change the system volume",
                    path: "Volume",
                    childCount: 0,
                    action: "",
                    provider: "set-volume",
                    value: targetVol.toString(),
                    desktop: "",
                    disabled: false,
                    score: -99995,
                    section: ""
                })
            } else if (qLower === "mute" || qLower === "sessiz") {
                rows.unshift({
                    itemId: "__toggle_mute__",
                    kind: "action",
                    icon: "󰝟",
                    iconName: "audio-volume-muted",
                    iconFont: "",
                    appIcon: "",
                    appId: "",
                    label: "Mute / Unmute",
                    target: "",
                    detail: "Toggle system sound",
                    path: "Volume",
                    childCount: 0,
                    action: "",
                    provider: "toggle-mute",
                    value: "",
                    desktop: "",
                    disabled: false,
                    score: -99995,
                    section: ""
                })
            }

            // 4. Süreç sonlandırıcı ("kill <ad>"), pano ("clip <metin>"), son dosyalar
            //    ("recent <ad>") ve emoji (":<ad>") — kullanıcının kendi süreçleri, bellekteki pano geçmişi
            if (qLower.startsWith("kill ")) {
                var pFilter = query.substring(5).trim()
                if (pFilter) {
                    try { rows = MenuModel.pcRows("kill", JSON.parse(Shell.listProcesses(pFilter) || "[]")).concat(rows) } catch (e) {}
                }
            } else if (qLower.startsWith("clip ")) {
                try { rows = MenuModel.pcRows("clip", JSON.parse(Shell.clipboardMatching(query.substring(5)) || "[]")).concat(rows) } catch (e) {}
            } else if (qLower.startsWith("recent ") || qLower === "recent") {
                try { rows = MenuModel.pcRows("recent", JSON.parse(Shell.recentFiles(query.substring(6)) || "[]")).concat(rows) } catch (e) {}
            } else if (query.startsWith(":") && query.length >= 2 && query.indexOf(" ") < 0) {
                try { rows = MenuModel.pcRows("emoji", JSON.parse(Shell.searchEmoji(query.substring(1)) || "[]")).concat(rows) } catch (e) {}
            }

            // 5. Dizin ve Dosya Yolu Açıcı (~/ veya /)
            if (query.startsWith("~") || query.startsWith("/")) {
                var cleanPath = query
                rows.unshift({
                    itemId: "__open_dir__",
                    kind: "action",
                    icon: "󰝰",
                    iconName: "folder",
                    iconFont: "",
                    appIcon: "",
                    appId: "",
                    label: "Open Folder: " + cleanPath,
                    target: "",
                    detail: "Open this location in Dolphin",
                    path: "Files",
                    childCount: 0,
                    action: "",
                    provider: "open-folder-path",
                    value: cleanPath,
                    desktop: "",
                    disabled: false,
                    score: -99996,
                    section: ""
                })
                rows.push({
                    itemId: "__open_term_dir__",
                    kind: "action",
                    icon: "",
                    iconName: "terminal",
                    iconFont: "",
                    appIcon: "",
                    appId: "",
                    label: "Open Terminal Here: " + cleanPath,
                    target: "",
                    detail: "Start Konsole in this folder",
                    path: "Terminal",
                    childCount: 0,
                    action: "",
                    provider: "open-folder-terminal",
                    value: cleanPath,
                    desktop: "",
                    disabled: false,
                    score: -99995,
                    section: ""
                })
            }

            // 6. Raycast / KRunner Web Bangs
            var bangs = [
                { prefix: "g ", name: "Google", icon: "󰖟", url: "https://www.google.com/search?q=" },
                { prefix: "yt ", name: "YouTube", icon: "󰗃", url: "https://www.youtube.com/results?search_query=" },
                { prefix: "gh ", name: "GitHub", icon: "󰊤", url: "https://github.com/search?q=" },
                { prefix: "w ", name: "Wikipedia", icon: "󰖬", url: "https://tr.wikipedia.org/w/index.php?search=" },
                { prefix: "tr ", name: "Google Translate", icon: "󰗊", url: "https://translate.google.com/?sl=auto&tl=tr&text=" },
                { prefix: "map ", name: "Maps", icon: "󰍎", url: "https://www.google.com/maps/search/" },
                { prefix: "ddg ", name: "DuckDuckGo", icon: "󰇧", url: "https://duckduckgo.com/?q=" }
            ]

            var hasBang = false
            for (var bi = 0; bi < bangs.length; bi++) {
                var bg = bangs[bi]
                if (qLower.startsWith(bg.prefix)) {
                    hasBang = true
                    var bangQ = query.substring(bg.prefix.length).trim()
                    if (bangQ) {
                        rows.unshift({
                            itemId: "__bang_" + bg.name + "__",
                            kind: "action",
                            icon: bg.icon,
                            iconName: "web-browser",
                            iconFont: "",
                            appIcon: "",
                            appId: "",
                            label: "Search " + bg.name + ": \"" + bangQ + "\"",
                            target: "",
                            detail: "Opens in your default browser",
                            path: "Web",
                            childCount: 0,
                            action: "xdg-open '" + bg.url + encodeURIComponent(bangQ) + "'",
                            provider: "",
                            value: "",
                            desktop: "",
                            disabled: false,
                            score: -99998,
                            section: ""
                        })
                    }
                }
            }

            // 7. Yapıştır-çalıştır komutları ('>' öneki zorlar; açık bir komut
            //    metni öneksiz de algılanır, tek kelime asla algılanmaz)
            var cmdText = query.startsWith(">") ? query.substring(1).trim() : query
            var cmdAnalysis = Shell.analyzeCommand(cmdText)
            var cmdResult = MenuModel.commandRows(query, cmdAnalysis,
                                                  query.startsWith(">") ? Shell.commandHistory(cmdText) : [])
            if (cmdResult.position === "first") rows = cmdResult.rows.concat(rows)
            else if (cmdResult.position === "last") rows = rows.concat(cmdResult.rows)

            // 8. Web Arama (Fallback veya '?' öneki)
            var isWebQuery = query.startsWith("?")
            var rawSearch = isWebQuery ? query.substring(1).trim() : query
            if (!hasBang && rawSearch && (isWebQuery || rows.length === 0)) {
                var webRows = [
                    {
                        itemId: "__web_google__",
                        kind: "action",
                        icon: "󰖟",
                        iconName: "web-browser",
                        iconFont: "",
                        appIcon: "",
                        appId: "",
                        label: "Search Google: \"" + rawSearch + "\"",
                        target: "",
                        detail: "Opens in your default browser",
                        path: "Web",
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
                        label: "Search DuckDuckGo: \"" + rawSearch + "\"",
                        target: "",
                        detail: "Privacy-focused search",
                        path: "Web",
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

            // Sonuçları sabit sıralı, başlıklı gruplara ayır (Top Hit, Applications,
            // Settings, …). Kökten aramada grup başına sınır; alt menüde sınır yok.
            rows = MenuModel.groupSearchRows(rows, {
                capped: active === "root",
                commandsFirst: cmdResult.position === "first"
            })
        } else {
            for (var j = 0; j < root.itemOrder.length; j++) {
                var child = root.item(root.itemOrder[j])
                if (!child || child.parent !== active) continue
                if (active === "root" && (child.id === "help" || child.id === "cachyupdate")) continue
                if (root.emptyCategories[child.id]) continue
                if (!root.isVisible(child)) continue
                rows.push(displayRow(child, child.description, child.order))
            }
            if (active.startsWith("apps.")) {
                // Alt menüler (ör. Steam Library) uygulamaların üstünde kalır, sonra ada göre
                rows.sort(function(a,b){
                    var am = a.kind === "menu", bm = b.kind === "menu"
                    if (am !== bm) return am ? -1 : 1
                    return String(a.label).toLowerCase().localeCompare(String(b.label).toLowerCase())
                })
            }
        }

        var headers = 0
        for (var hs = 0; hs < rows.length; hs++)
            if (rows[hs].section && (hs === 0 || rows[hs - 1].section !== rows[hs].section)) headers++
        root.sectionHeaderCount = headers
        MenuModel.syncRows(displayModel, rows)
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
        if (root.outputPanelOpen && next !== root.filterText) root.outputPanelOpen = false // yeni arama
        if (next !== root.filterText) { root.armedRunId = ""; root.lastKill = null } // metin değişti: yeniden onay
        root.filterText = next
        root.selectedIndex = 0
        if (!next.trim()) {
            searchDebounce.stop()
            root.searchSeq++ // uçuştaki yanıtları geçersiz kıl
            root.fileSearchBusy = false
            root.winResults = []
            root.winResultsQuery = ""
        } else {
            searchDebounce.restart()
        }
        rebuildDisplay() // yerel sonuçlar (uygulamalar, hesap makinesi) anında
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

    // Tehlikeli komut için ilk Enter'da "kurulan" komut METNİ; başka bir tuş
    // ya da arama metninin değişmesi sıfırlar.
    property string armedRunId: ""
    property var lastKill: null // { rowId, at }: ikinci Enter ile SIGKILL
    property real enterGuardUntil: 0
    Timer {
        id: killRefresh
        interval: 700
        onTriggered: {
            rebuildPreservingSelection() // biten süreç listeden düşsün
            if (!root.lastKill) return
            for (var i = 0; i < displayModel.count; i++) {
                if (displayModel.get(i).itemId === root.lastKill.rowId) {
                    // hâlâ çalışıyor: seçili kalsın, "zorla" ipucu korunsun
                    root.selectedIndex = i
                    resultList.currentIndex = i
                    displayModel.setProperty(i, "detail", "Sent SIGTERM \u00b7 Enter again within 5 s to force kill (SIGKILL)")
                    return
                }
            }
            // süreç bitti: alttan kayan satır ikinci Enter'ı almasın
            root.lastKill = null
            root.enterGuardUntil = Date.now() + 1000
        }
    }
    property bool outputPanelOpen: false

    function runCommandRow(index, row, inline) {
        // Tehlike kontrolü çalıştırma anında (geçmiş satırları dahil)
        if (MenuModel.armDecision(root.armedRunId, row.value, Shell.analyzeCommand(row.value).dangerous) === "arm") {
            root.armedRunId = row.value // kurulma komut metnine bağlı
            displayModel.setProperty(index, "detail", "\u26a0 Press Enter again to run this dangerous command")
            Shell.playThock()
            return
        }
        root.armedRunId = ""
        Shell.playCreamy()
        if (inline) {
            if (Shell.runner.running) { root.outputPanelOpen = true; return } // önce mevcut çalışmayı göster
            if (Shell.runCommandInline(row.value)) { root.outputPanelOpen = true; return }
            // false: sudo/etkileşimli -> terminalde açıldı
        } else {
            Shell.runCommandInTerminal(row.value)
        }
        root.opened = false
        Qt.callLater(closeMenu)
    }

    function activateIndex(index, opts) {
        if (index < 0 || index >= displayModel.count) return
        var row = displayModel.get(index)
        if (row.disabled) return
        if (MenuModel.enterGuarded(root.enterGuardUntil, Date.now())) return
        if (row.provider === "run-command") {
            runCommandRow(index, row, !!(opts && opts.inline))
            return
        }
        if (row.kind === "menu" || row.kind === "link") {
            setActiveMenu(row.target || row.itemId, true)
        } else if (row.kind === "app") {
            Shell.playCreamy()
            root.opened = false
            Shell.launchApp(row.desktop)
            Qt.callLater(closeMenu)
        } else if (row.provider === "window-item") {
            Shell.playCreamy()
            root.opened = false
            Shell.activateWindow(row.value)
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
        } else if (row.provider === "sys-action") {
            Shell.playCreamy()
            root.opened = false
            if (row.value === "poweroff") Shell.powerOff()
            else if (row.value === "reboot") Shell.rebootSystem()
            else if (row.value === "lock") Shell.lockScreen()
            else if (row.value === "sleep") Shell.suspendSystem()
            else if (row.value === "logout") Shell.logoutUser()
            else if (row.value === "save_session") Shell.saveSession()
            else if (row.value === "restart_plasma") Shell.restartPlasma()
            Qt.callLater(closeMenu)
        } else if (row.provider === "set-volume") {
            Shell.playCreamy()
            var nv = parseInt(row.value)
            root.sysVolume = nv
            root.sysMuted = false
            Shell.setSystemVolume(nv)
            Qt.callLater(closeMenu)
        } else if (row.provider === "toggle-mute") {
            Shell.playCreamy()
            Shell.toggleSystemMute()
            root.sysMuted = !root.sysMuted
            Qt.callLater(closeMenu)
        } else if (row.provider === "kill-process") {
            var now = Date.now()
            var decision = MenuModel.killDecision(root.lastKill, row.itemId, now)
            if (decision === "ignore") return
            if (decision === "kill") {
                Shell.forceKillProcess(row.value)
                root.lastKill = null
                Shell.playCreamy()
                Qt.callLater(rebuildPreservingSelection)
            } else {
                Shell.killProcess(row.value) // SIGTERM; menü açık kalır, 5 s içinde Enter = zorla
                root.lastKill = { rowId: row.itemId, at: now }
                displayModel.setProperty(index, "detail", "Sent SIGTERM \u00b7 Enter again within 5 s to force kill (SIGKILL)")
                Shell.playThock()
                killRefresh.restart()
            }
        } else if (row.provider === "emoji") {
            Shell.playCreamy()
            root.opened = false
            Shell.setClipboard(row.value)
            Qt.callLater(closeMenu)
        } else if (row.provider === "open-folder-path") {
            Shell.playCreamy()
            root.opened = false
            Shell.openPath(row.value) // argv; eskiden "xdg-open <yol>" /bin/sh'ta genişliyordu
            Qt.callLater(closeMenu)
        } else if (row.provider === "open-folder-terminal") {
            Shell.playCreamy()
            root.opened = false
            Shell.openInTerminal(row.value)
            Qt.callLater(closeMenu)
        } else {
            Shell.playCreamy()
            root.opened = false
            if (row.provider === "colorscheme") {
                Shell.applyColorscheme(row.value)
            } else if (row.provider === "power-profile") {
                Shell.applyPowerProfile(row.value)
            } else if (row.action) {
                if ((row.itemId && row.itemId.indexOf("tools.nashi") === 0) || (row.action && row.action.indexOf("nashi") !== -1)) {
                    var nashiExec = Shell.getNashiPath()
                    if (nashiExec === "") {
                        showDependencyPrompt(
                            "nashi-app",
                            "Nashi Radial Hub",
                            "The Nashi quick-action and format-converter wheels need the <b>Nashi</b> app. Download and install it from GitHub (<b>cdokuro/nashi-app</b>)?",
                            "install-nashi"
                        )
                        return
                    }
                    var actCmd = row.action
                    actCmd = actCmd.replace("/home/dokuro/Masaüstü/orkunprogram/nashi-linux/nashi", nashiExec)
                    Shell.runDetached(actCmd)
                    Qt.callLater(closeMenu)
                    return
                }
                Shell.runDetached(row.action)
            }
            Qt.callLater(closeMenu)
        }
    }

    function openItemLocation(rowItem) {
        if (!rowItem || rowItem.disabled) return
        var path = rowItem.filePath || rowItem.desktop || ""
        if (!path) {
            path = Shell.resolveItemPath(rowItem.itemId || "", rowItem.provider || "", rowItem.action || "", rowItem.value || "")
        }
        if (path) {
            Shell.playCreamy()
            root.opened = false
            if (Shell.isDirectory(path)) {
                Shell.openPath(path)
            } else {
                Shell.openContainingFolder(path)
            }
            Qt.callLater(closeMenu)
            return
        }
        if (rowItem.action) {
            Shell.playCreamy()
            root.opened = false
            Shell.runDetached(rowItem.action)
            Qt.callLater(closeMenu)
        }
    }

    // ── Sağ Tık Bağlam Menüsü (Context Menu) ──────────────────────
    property var contextRow: null
    // Sağ tık popup modu: "list" | "openWith" | "properties" | "rename"
    property string contextMode: "list"
    property string contextPath: ""
    property var contextMainEntries: []
    property string renameError: ""

    function snapshotContextEntries() {
        var out = []
        for (var i = 0; i < contextMenuModel.count; i++) {
            var e = contextMenuModel.get(i)
            out.push({ label: e.label, icon: e.icon, actionType: e.actionType, actionPayload: e.actionPayload, isDanger: e.isDanger })
        }
        return out
    }
    function showContextSubmenu(mode, entries) {
        if (root.contextMode === "list") root.contextMainEntries = snapshotContextEntries()
        contextMenuModel.clear()
        contextMenuModel.append({ label: "\u2039 Back", icon: "", actionType: "contextBack", actionPayload: "", isDanger: false })
        for (var i = 0; i < entries.length; i++) contextMenuModel.append(entries[i])
        root.contextMode = mode
        contextList.currentIndex = entries.length ? 1 : 0
    }
    function contextBack() {
        contextMenuModel.clear()
        for (var i = 0; i < root.contextMainEntries.length; i++) contextMenuModel.append(root.contextMainEntries[i])
        root.contextMode = "list"
        root.renameError = ""
        contextList.currentIndex = 0
        contextList.forceActiveFocus()
    }
    function submitRename(newName) {
        var err = Shell.renamePath(root.contextPath, newName)
        if (err) { root.renameError = err; Shell.playThock(); return }
        Shell.playCreamy()
        contextPopup.close()
        refreshAfterFileChange()
    }
    // Yeniden adlandırma/çöpe taşıma sonrası görünür listeleri tazele
    function refreshAfterFileChange() {
        if (root.activeMenu === "downloads") {
            root.providersLoaded["downloads"] = false
            loadFolderFiles("downloads", "downloads")
        }
        if (root.filterText.trim().startsWith("*")) { root.fileResultsQuery = ""; dispatchAsyncSearch() }
    }
    ListModel { id: contextMenuModel }

    function openContextMenu(rowItem, globalX, globalY) {
        if (!rowItem) return
        root.contextRow = rowItem
        contextMenuModel.clear()

        var resolvedPath = rowItem.filePath || ""
        if (!resolvedPath) {
            resolvedPath = Shell.resolveItemPath(rowItem.itemId || "", rowItem.provider || "", rowItem.action || "", rowItem.value || "")
        }
        var isDir = (rowItem.isDir !== undefined && rowItem.isDir !== null) ? !!rowItem.isDir : (resolvedPath ? Shell.isDirectory(resolvedPath) : false)

        if (rowItem.kind === "app" || (rowItem.desktop && rowItem.desktop.length > 0)) {
            var acts = []
            try { acts = JSON.parse(Shell.getAppActions(rowItem.desktop) || "[]") } catch (e) {}
            var appEntries = MenuModel.appContextEntries({
                desktop: rowItem.desktop, actions: acts,
                autostart: Shell.isAutostart(rowItem.desktop),
                canUninstall: Shell.canUninstall(rowItem.desktop)
            })
            for (var ai = 0; ai < appEntries.length; ai++) contextMenuModel.append(appEntries[ai])
        } else if (resolvedPath && resolvedPath.length > 0) {
            var isRootDownloads = (rowItem.itemId === "downloads" || rowItem.provider === "files-downloads")
            var actuallyDir = (rowItem.isDir === true) || (isRootDownloads) || (Shell.isDirectory(resolvedPath))
            var fileEntries = MenuModel.fileContextEntries({
                path: resolvedPath,
                isDir: actuallyDir,
                isArchive: !actuallyDir && Shell.isArchive(resolvedPath),
                isRootDownloads: isRootDownloads,
                inMenu: rowItem.kind === "menu" || rowItem.kind === "link" || (rowItem.provider && rowItem.provider.startsWith("files-")),
                // Çöpe taşı: yalnızca dosya öğeleri (kök menü kategorileri hariç)
                canTrash: rowItem.provider === "file-item" || (!!rowItem.filePath && rowItem.kind !== "menu")
            })
            for (var fi = 0; fi < fileEntries.length; fi++) contextMenuModel.append(fileEntries[fi])
        } else {
            // General menu items
            contextMenuModel.append({
                label: "Select / Execute",
                icon: "",
                actionType: "activateCurrent",
                actionPayload: "",
                isDanger: false
            })
            var toCopy = rowItem.action || rowItem.value || rowItem.label || ""
            if (toCopy) {
                contextMenuModel.append({
                    label: "Copy Action / Text",
                    icon: "󰅇",
                    actionType: "copyPath",
                    actionPayload: toCopy,
                    isDanger: false
                })
            }
        }

        // Konum hesaplama
        var pw = contextPopup.width
        var ph = contextPopup.height
        contextPopup.x = Math.max(16, Math.min(globalX, root.width - pw - 16))
        // Yüzey artık sabit/uzun; popup kartın görünen alanında kalsın (dışı tıklama geçirir)
        contextPopup.y = Math.max(16, Math.min(globalY, card.y + card.height + 24 - ph - 16))
        contextPopup.open()
    }

    function executeContextAction(actionType, payload) {
        // Popup içinde kalan eylemler (alt menü / özellikler / yeniden adlandırma)
        if (actionType === "contextBack") { contextBack(); return }
        if (actionType === "openWithMenu") {
            root.contextPath = payload
            var apps = []
            try { apps = JSON.parse(Shell.openWithApps(payload) || "[]") } catch (e) {}
            var rows = apps.map(function(a) {
                return { label: a.name + (a.isDefault ? "  (default)" : ""), icon: "", actionType: "openWithApp",
                         actionPayload: a.file, isDanger: false }
            })
            if (!rows.length) rows = [{ label: "No applications found", icon: "", actionType: "", actionPayload: "", isDanger: false }]
            showContextSubmenu("openWith", rows)
            return
        }
        if (actionType === "showProperties") {
            var pr = Shell.fileProperties(payload)
            var info = [
                ["Name", pr.name], ["Type", (pr.mimeComment || "") + "  (" + pr.mime + ")"],
                [pr.isDir ? "Contents" : "Size", pr.sizeText], ["Modified", pr.modified],
                ["Permissions", pr.permissions + "  " + pr.owner], ["Location", pr.path]
            ]
            if (pr.isSymlink) info.push(["Link target", pr.target])
            showContextSubmenu("properties", info.map(function(kv) {
                // tıklayınca değer panoya kopyalanır
                return { label: kv[0] + ":  " + kv[1], icon: "", actionType: "copyText", actionPayload: String(kv[1]), isDanger: false }
            }))
            return
        }
        if (actionType === "startRename") {
            if (root.contextMode === "list") root.contextMainEntries = snapshotContextEntries()
            root.contextPath = payload
            root.renameError = ""
            root.contextMode = "rename"
            var nm = payload.split("/").pop()
            renameInput.text = nm
            var dot = nm.lastIndexOf(".")
            renameInput.select(0, dot > 0 ? dot : nm.length) // uzantı hariç seç
            renameInput.forceActiveFocus()
            return
        }
        if (actionType === "copyText") { Shell.setClipboard(payload); Shell.playCreamy(); return }
        if (actionType === "") return

        contextPopup.close()
        var r = root.contextRow
        if (!r) return

        if (actionType === "launchApp") {
            Shell.playCreamy()
            root.opened = false
            Shell.launchApp(r.desktop)
            Qt.callLater(closeMenu)
        } else if (actionType === "runAction") {
            Shell.playCreamy()
            root.opened = false
            Shell.runDetached(payload)
            Qt.callLater(closeMenu)
        } else if (actionType === "openPath") {
            Shell.playCreamy()
            root.opened = false
            Shell.openPath(payload || r.filePath)
            Qt.callLater(closeMenu)
        } else if (actionType === "openLocation") {
            Shell.playCreamy()
            root.opened = false
            Shell.openContainingFolder(payload || r.filePath || r.desktop)
            Qt.callLater(closeMenu)
        } else if (actionType === "openInTerminal") {
            Shell.playCreamy()
            root.opened = false
            Shell.openInTerminal(payload || r.filePath)
            Qt.callLater(closeMenu)
        } else if (actionType === "runInTerminal") {
            Shell.playCreamy()
            root.opened = false
            Shell.runInTerminal(r.desktop)
            Qt.callLater(closeMenu)
        } else if (actionType === "openInDiscover") {
            Shell.playCreamy()
            root.opened = false
            Shell.openInDiscover(r.desktop)
            Qt.callLater(closeMenu)
        } else if (actionType === "editDesktop") {
            Shell.playCreamy()
            root.opened = false
            Shell.editDesktopFile(r.desktop)
            Qt.callLater(closeMenu)
        } else if (actionType === "openWithApp") {
            root.opened = false
            Shell.openWith(root.contextPath, payload)
            Qt.callLater(closeMenu)
        } else if (actionType === "copyFile") {
            Shell.copyFileToClipboard(payload)
        } else if (actionType === "compress") {
            Shell.compressPath(payload)
        } else if (actionType === "extract") {
            Shell.extractHere(payload)
        } else if (actionType === "toggleAutostart") {
            Shell.setAutostart(payload, !Shell.isAutostart(payload))
        } else if (actionType === "uninstall") {
            root.opened = false
            Shell.uninstallApp(payload) // terminal açılır; paket yöneticisi onay sorar
            Qt.callLater(closeMenu)
        } else if (actionType === "copyPath") {
            Shell.setClipboard(payload)
        } else if (actionType === "moveToTrash") {
            Shell.moveToTrash(payload || r.filePath)
            refreshAfterFileChange()
        } else if (actionType === "activateCurrent") {
            activateIndex(resultList.currentIndex)
        }
    }

    function openMenu(initialMenu) {
        root.canCloseOnInactive = false
        enableCloseOnInactiveTimer.stop()
        Shell.centerWindowOnActiveScreen(root)
        root.activeMenu = root.item(initialMenu) ? initialMenu : "root"
        root.navStack = []
        root.filterText = ""
        searchInput.text = ""
        root.selectedIndex = 0
        root.powerFlyoutOpen = false
        if (Shell.getPowerProfile) root.currentPowerProfile = Shell.getPowerProfile()
        if (typeof kdeInhibitCtrl !== "undefined" && kdeInhibitCtrl) {
            root.sleepInhibited = kdeInhibitCtrl.isManuallyInhibited
        } else if (Shell.isSleepInhibited) {
            root.sleepInhibited = Shell.isSleepInhibited()
        }

        // Ses seviyesini anında çek (%50 flash olmasını önler)
        var ainfo = Shell.getInitialAudioInfo()
        if (ainfo) {
            root.sysVolume = ainfo.volume
            root.sysMuted = ainfo.muted
        }

        root.canCloseOnInactive = false
        root.visible = true
        root.opened = true
        if (!root.loaded) {
            loadItems()
        } else {
            // Hotkey open path: guards evaluated asynchronously so UI opens instantaneously
            Qt.callLater(evaluateGuards)
        }
        rebuildDisplay()
        startProvider(root.activeMenu)
        Qt.callLater(function(){
            root.requestActivate()
            searchInput.forceActiveFocus()
            enableCloseOnInactiveTimer.restart()
        })
    }

    function closeMenu() {
        root.outputPanelOpen = false // komut arka planda sürer; bitince bildirim gelir
        root.armedRunId = ""
        root.lastKill = null
        if (root.depModalOpen) root.depModalOpen = false
        if (root.powerFlyoutOpen) root.powerFlyoutOpen = false
        if (root.audioFlyoutOpen) root.audioFlyoutOpen = false
        if (contextPopup.opened) contextPopup.close()
        enableCloseOnInactiveTimer.stop()
        root.canCloseOnInactive = false
        root.opened = false
        closeTimer.restart()
    }
    Timer {
        id: closeTimer
        interval: 90
        running: false
        repeat: false
        onTriggered: {
            root.visible = false
            // Gizliyken JS çöpü toplanmadan kalırsa (artımlı GC henüz koşmadıysa) kirli
            // sayfalar daemon'un ömrü boyunca kalıyordu: kapanışta bir kez topla, ardından
            // C++ tarafı malloc_trim yapar. Deney: açılışta ~3 MB JS çöpü -> gc'siz 65.5 MB,
            // gc ile 49.6 MB (tests/perf/post_show_memory.sh).
            gc()
            Shell.onMenuVisibilityChanged(false)
        }
    }

    Connections {
        target: Shell
        function onToggleRequested() {
            handleHotkeyTrigger()
        }
        function onShowRequested() {
            openMenu("root")
        }
        function onHideRequested() {
            closeMenu()
        }
        function onReloadRequested() {
            loadItems()
            rebuildDisplay()
        }
        function onFileSearchFinished(requestId, pattern, json) {
            if (requestId !== root.searchSeq) return // eski sorgu
            try { root.fileResults = JSON.parse(json || "[]") } catch (e) { root.fileResults = [] }
            root.fileResultsQuery = pattern
            root.fileSearchBusy = false
            rebuildPreservingSelection()
        }
        function onWindowSearchFinished(requestId, query, json) {
            if (requestId !== root.searchSeq) return
            try { root.winResults = JSON.parse(json || "[]") } catch (e) { root.winResults = [] }
            root.winResultsQuery = query
            rebuildPreservingSelection()
        }
        function onStatsUpdated(cpu, gpu, vol, muted) {
            root.cpuPercent = cpu
            root.gpuPercent = gpu
            root.sysVolume = vol
            root.sysMuted = muted
            root.sysMonitor = Shell.getSysMonitorInfo()
            root.sysUpdateInfo = Shell.getUpdateInfo()
        }
        function onThemeChanged() {
            root.syncTheme()
        }
        function onAppAudioChanged(vol, muted) {
            root.appSoundVolume = vol
            root.appSoundMuted = muted
        }
    }

    function handleHotkeyTrigger() {
        if (!root.visible || !root.opened) {
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
            NumberAnimation { target: card; property: "opacity"; to: 0.15; duration: 90; easing.type: Easing.InCubic }
            NumberAnimation { target: card; property: "scale"; to: 0.97; duration: 90; easing.type: Easing.InCubic }
        }
        ScriptAction {
            script: {
                openMenu("root")
            }
        }
        ParallelAnimation {
            NumberAnimation { target: card; property: "opacity"; to: 1.0; duration: 180; easing.type: Easing.OutQuint }
            NumberAnimation { target: card; property: "scale"; to: 1.0; duration: 180; easing.type: Easing.OutQuint }
        }
    }

    // ── UI ─────────────────────────────────────────────────
    readonly property string dbgScroll: "trackH=" + scrollTrack.height + " thumbH=" + thumb.height + " thumbY=" + thumb.y + " contentY=" + resultList.contentY + " maxCY=" + (resultList.contentHeight - resultList.height) + " visible=" + scrollTrack.visible
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
        Behavior on opacity { id: glowOpacityB; OpenCloseAnimation { behavior: glowOpacityB; threshold: 0.5 } }
    }

    Rectangle {
        id: card
        objectName: "card"
        width: root.cardWidth
        height: {
            var contentH = root.outputPanelOpen ? 420
                : (root.activeMenu === "system" && !root.filterText.length)
                ? Math.ceil(displayModel.count / 2) * 64 + 18
                : displayModel.count * rowHeight + root.sectionHeaderCount * root.sectionHeaderHeight + 12
            return Math.min(headerHeight + 1 + contentH + footerHeight, root.maxCardHeight)
        }
        anchors.top: parent.top
        anchors.topMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter
        color: root.bgColor
        border.color: root.bgBorder
        border.width: 1
        radius: 14
        clip: true
        opacity: root.opened ? 1.0 : 0.0
        scale: root.opened ? 1.0 : 0.97

        // Pencere içine tıklamak pencereyi kapatmasın; sağ tık veya mouse back bir önceki sayfaya döndürsün
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton | Qt.BackButton | Qt.ForwardButton
            propagateComposedEvents: true
            onClicked: function(mouse) {
                if (mouse.button === Qt.BackButton || mouse.button === 8) {
                    Shell.playCreamy()
                    goBack()
                    mouse.accepted = true
                } else if (mouse.button === Qt.ForwardButton || mouse.button === 16) {
                    Shell.playCreamy()
                    activateIndex(root.selectedIndex)
                    mouse.accepted = true
                } else if (mouse.button === Qt.RightButton) {
                    Shell.playCreamy()
                    goBack()
                    mouse.accepted = true
                }
            }
        }
        Behavior on opacity { id: cardOpacityB; enabled: !reopenAnim.running; OpenCloseAnimation { behavior: cardOpacityB; threshold: 0.5 } }
        // Yumuşak, hedefi aşmayan açılış (OutBack zıplatıyordu); kapanış hızlı ve sakin
        Behavior on scale { id: cardScaleB; enabled: !reopenAnim.running; OpenCloseAnimation { behavior: cardScaleB; threshold: 0.985 } }
        Behavior on height { NumberAnimation { duration: 130; easing.type: Easing.OutQuint } }

        // Kartı sürükle: Alt veya Meta tuşuna basılı tutulduğunda KWin pencereyi doğrudan pürüzsüz taşır
        MouseArea {
            id: windowDragArea
            anchors.fill: parent
            z: -1
            acceptedButtons: Qt.LeftButton
            onPressed: function(mouse) {
                if (mouse.modifiers & (Qt.AltModifier | Qt.MetaModifier)) {
                    root.startSystemMove()
                }
            }
        }
        DragHandler {
            id: cardDragAlt
            target: null
            acceptedButtons: Qt.LeftButton
            acceptedModifiers: Qt.AltModifier
            onActiveChanged: if (active) root.startSystemMove()
        }
        DragHandler {
            id: cardDragMeta
            target: null
            acceptedButtons: Qt.LeftButton
            acceptedModifiers: Qt.MetaModifier
            onActiveChanged: if (active) root.startSystemMove()
        }

        // Blur ve input bölgesini kartın gerçek yüksekliğine eşitle (her kare;
        // double-buffered, yüzey commit'iyle birlikte uygulanır).
        onHeightChanged: Shell.setCardHeight(Math.ceil(height))
        Component.onCompleted: Shell.setCardHeight(Math.ceil(height))

        // ── Klavye işleyici ─────────────────────────────────
        // Odağı searchInput tutar; tuşlar Keys.forwardTo ile buraya gelir.
        // TextInput kendi select(int,int) metoduna sahip olduğu için
        // Keys.onPressed'i TextInput'ta tutmak root.select(delta)'yı gölgeliyordu.
        Item {
            id: keyHandler
            Keys.onPressed: function(event) {
                if (root.armedRunId && event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter
                        && event.key !== Qt.Key_Shift) {
                    root.armedRunId = ""
                    rebuildPreservingSelection() // uyarı metnini geri al
                }
                if (event.key === Qt.Key_Escape) {
                    if (root.outputPanelOpen) {
                        // Esc paneli kapatır, menüyü değil (komut çalışmaya devam eder)
                        root.outputPanelOpen = false
                        event.accepted = true
                        return
                    }
                    if (root.depModalOpen) {
                        root.depModalOpen = false
                        event.accepted = true
                        return
                    }
                    if (root.powerFlyoutOpen) {
                        root.powerFlyoutOpen = false
                        event.accepted = true
                        return
                    }
                    if (contextPopup.opened) {
                        contextPopup.close()
                        event.accepted = true
                        return
                    }
                    if (root.filterText.length) {
                        setFilter("")
                        searchInput.text = ""
                    } else if (root.activeMenu !== "root") {
                        goBack()
                    } else {
                        closeMenu()
                    }
                    event.accepted = true
                } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                    if (root.selectedIndex >= 0 && root.selectedIndex < displayModel.count) {
                        openContextMenu(displayModel.get(root.selectedIndex), root.width / 2, 180)
                        event.accepted = true
                    }
                } else if (event.key === Qt.Key_Backspace && !root.filterText.length) {
                    goBack()
                    event.accepted = true
                } else if (event.key === Qt.Key_Left && !root.filterText.length) {
                    if (root.activeMenu === "system" && (root.selectedIndex % 2 === 1)) {
                        select(-1)
                        event.accepted = true
                    } else {
                        goBack()
                        event.accepted = true
                    }
                } else if (event.key === Qt.Key_Right && !root.filterText.length) {
                    if (root.activeMenu === "system" && (root.selectedIndex % 2 === 0) && (root.selectedIndex + 1 < displayModel.count)) {
                        select(1)
                        event.accepted = true
                    } else {
                        activateIndex(root.selectedIndex)
                        event.accepted = true
                    }
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
                    if (root.activeMenu === "system" && !root.filterText.length) {
                        select(-2)
                    } else {
                        select(-1)
                    }
                    event.accepted = true
                } else if (event.key === Qt.Key_Down) {
                    if (root.activeMenu === "system" && !root.filterText.length) {
                        select(2)
                    } else {
                        select(1)
                    }
                    event.accepted = true
                } else if (event.key === Qt.Key_PageUp) {
                    select(-6); event.accepted = true
                } else if (event.key === Qt.Key_PageDown) {
                    select(6); event.accepted = true
                } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                    openItemLocation(displayModel.get(root.selectedIndex))
                    event.accepted = true
                } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ShiftModifier)) {
                    // Shift+Enter: komutu RavenSpot içinde çalıştır (çıktı paneli)
                    activateIndex(root.selectedIndex, { inline: true })
                    event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Right) {
                    var selRow = displayModel.count > root.selectedIndex ? displayModel.get(root.selectedIndex) : null
                    if (event.key === Qt.Key_Right && selRow && !MenuModel.rightArrowActivates(selRow.provider)) {
                        event.accepted = false // metin kutusunda imleç hareket etsin
                        return
                    }
                    if (event.isAutoRepeat) { event.accepted = true; return } // basılı Enter tekrar etkinleştirmesin
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

            // ── Üst Bar: Ses Kontrol Paneli (Mute + Slider) & RavenSpot Başlığı ──
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

                // Sol: RavenSpot Başlığı (Sadece Metin, Saf & Estetik Tipografi + Tutup Taşıma)
                Item {
                    id: brandHeaderContainer
                    anchors.left: parent.left
                    anchors.right: powerProfileBtn.left
                    anchors.rightMargin: 8
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    z: 15

                    Row {
                        id: brandHeaderRow
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 0

                        Text {
                            text: "Raven"
                            color: root.fgColor
                            font.pixelSize: 13
                            font.family: "Fira Sans, Inter, sans-serif"
                            font.weight: Font.Bold
                            font.letterSpacing: 0.8
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: "Spot"
                            color: root.selFg
                            font.pixelSize: 13
                            font.family: "Fira Sans, Inter, sans-serif"
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.8
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: titleDragMouse
                        anchors.fill: parent
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                        acceptedButtons: Qt.LeftButton

                        property real lastX: 0
                        property real lastY: 0
                        property real pressX: 0
                        property real pressY: 0
                        property bool isDragging: false

                        onPressed: function(mouse) {
                            pressX = mouse.x
                            pressY = mouse.y
                            lastX = mouse.x
                            lastY = mouse.y
                            isDragging = false
                        }

                        onPositionChanged: function(mouse) {
                            if (!pressed) return

                            if (!isDragging) {
                                var dist = Math.hypot(mouse.x - pressX, mouse.y - pressY)
                                if (dist < 4) return // 4px deadzone: clicking never jumps
                                isDragging = true
                                // Deadzone mesafesini de uygula: imleç tutma noktasında kalsın.
                                // Wayland'de sonraki hareket zwp_relative_pointer'dan gelir;
                                // aşağıdaki dragBy yalnızca protokol yoksa (X11) kullanılır.
                                Shell.beginDrag(mouse.x - pressX, mouse.y - pressY)
                                lastX = mouse.x
                                lastY = mouse.y
                                return
                            }

                            var dx = mouse.x - lastX
                            var dy = mouse.y - lastY
                            if (dx === 0 && dy === 0) return

                            lastX = mouse.x
                            lastY = mouse.y

                            Shell.dragBy(dx, dy)
                        }

                        onReleased: {
                            if (isDragging) Shell.endDrag()
                            isDragging = false
                        }

                        onCanceled: {
                            if (isDragging) Shell.endDrag()
                            isDragging = false
                        }
                    }
                }

                // Orta: Güç Profili Butonu (Tıklanınca Pop-up Slider / Engelleme Paneli Açar)
                Rectangle {
                    id: powerProfileBtn
                    z: 10
                    anchors.centerIn: parent
                    height: 20
                    width: powerBtnContent.width + 14
                    radius: 5
                    color: powerProfileMouse.containsMouse || root.powerFlyoutOpen ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.06)
                    border.color: powerProfileMouse.containsMouse || root.powerFlyoutOpen ? root.selFg : Qt.rgba(root.bgBorder.r, root.bgBorder.g, root.bgBorder.b, 0.25)
                    border.width: 1

                    Row {
                        id: powerBtnContent
                        spacing: 5
                        anchors.centerIn: parent

                        Text {
                            text: root.currentPowerProfile === "power-saver" ? "󰌪" : (root.currentPowerProfile === "performance" ? "󰓅" : "󰾆")
                            color: root.currentPowerProfile === "power-saver" ? "#22c55e" : (root.currentPowerProfile === "performance" ? "#f97316" : root.selFg)
                            font.pixelSize: 13
                            font.family: root.iconFont
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: root.currentPowerProfile === "power-saver" ? "Power Saver" : (root.currentPowerProfile === "performance" ? "Performance" : "Balanced")
                            color: root.fgColor
                            font.pixelSize: 10
                            font.family: root.textFont
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: root.powerFlyoutOpen ? "󰅃" : "󰅀"
                            color: root.fgDim
                            font.pixelSize: 10
                            font.family: root.iconFont
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: powerProfileMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: Shell.playThock()
                        onClicked: {
                            Shell.playCreamy()
                            root.powerFlyoutOpen = !root.powerFlyoutOpen
                        }
                    }
                }

                // Sağ: RavenSpot Uygulama İçi Ses Butonu (Tıklanınca Pop-up Slider Açar)
                Rectangle {
                    id: appVolumeBtn
                    z: 10
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 22
                    width: 26
                    radius: 5
                    color: appVolMouse.containsMouse || root.audioFlyoutOpen ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.06)
                    border.color: appVolMouse.containsMouse || root.audioFlyoutOpen ? root.selFg : Qt.rgba(root.bgBorder.r, root.bgBorder.g, root.bgBorder.b, 0.25)
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: root.appSoundMuted || root.appSoundVolume === 0 ? "󰝟" : (root.appSoundVolume > 50 ? "󰕾" : "󰖀")
                        color: root.appSoundMuted || root.appSoundVolume === 0 ? "#ef4444" : root.bgBorder
                        font.pixelSize: 13
                        font.family: root.iconFont
                    }

                    MouseArea {
                        id: appVolMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: Shell.playThock()
                        onClicked: {
                            Shell.playCreamy()
                            root.audioFlyoutOpen = !root.audioFlyoutOpen
                        }
                    }
                }
            }

            // ── Apple Spotlight Style Frosted Glass Search Pill Bar ──
            Item {
                id: searchPillContainer
                anchors.top: topMetricsBar.bottom
                anchors.topMargin: 8
                anchors.left: parent.left
                anchors.right: parent.right
                height: 42
                z: 5

                Rectangle {
                    id: searchPill
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    height: 42
                    radius: 12
                    color: searchInput.activeFocus ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.07)
                    border.color: searchInput.activeFocus ? Qt.rgba(255, 255, 255, 0.28) : Qt.rgba(255, 255, 255, 0.10)
                    border.width: searchInput.activeFocus ? 1.2 : 1

                    Behavior on border.color { ColorAnimation { duration: 120 } }
                    Behavior on color { ColorAnimation { duration: 120 } }

                    // Arama çubuğu genel metin imleci (I-Beam) ve tıklama odağı
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.IBeamCursor
                        acceptedButtons: Qt.LeftButton
                        z: 0
                        onClicked: searchInput.forceActiveFocus()
                    }

                    // Sol: Büyüteç İkonu (Apple Spotlight Style)
                    Text {
                        id: searchIcon
                        z: 1
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: "󰍉"
                        font.family: root.iconFont
                        font.pixelSize: 17
                        color: searchInput.text.length > 0 ? root.fgColor : root.fgDim
                        Behavior on color { ColorAnimation { duration: 120 } }
                    }

                    // Alt Kategori Dizin Breadcrumb Rozeti
                    Rectangle {
                        id: categoryBadge
                        z: 2
                        visible: root.activeMenu !== "root" && !root.filterText.length
                        anchors.left: searchIcon.right
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        height: 24
                        radius: 6
                        color: Qt.rgba(255, 255, 255, 0.12)
                        width: catBadgeContent.width + 16

                        Row {
                            id: catBadgeContent
                            anchors.centerIn: parent
                            spacing: 4
                            Text {
                                text: "‹"
                                color: root.fgDim
                                font.pixelSize: 13
                                font.bold: true
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: MenuModel.pathFor(root.items, root.activeMenu)
                                color: root.fgColor
                                font.pixelSize: 12
                                font.family: root.textFont
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Shell.playCreamy()
                                goBack()
                            }
                        }
                    }

                    // Placeholder Metni
                    Text {
                        id: placeholderText
                        z: 1
                        anchors.left: categoryBadge.visible ? categoryBadge.right : searchIcon.right
                        anchors.leftMargin: 10
                        anchors.right: clearBtn.visible ? clearBtn.left : escBadge.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.activeMenu === "root" ? "> run command, * find files, = calculate..." : "Search in this category..."
                        color: root.fgDim
                        opacity: 0.55
                        font.pixelSize: 14
                        font.family: root.textFont
                        elide: Text.ElideRight
                        visible: !root.filterText.length
                    }

                    // Arama Girişi (TextInput)
                    TextInput {
                        id: searchInput
                        z: 1
                        anchors.left: categoryBadge.visible ? categoryBadge.right : searchIcon.right
                        anchors.leftMargin: 10
                        anchors.right: clearBtn.visible ? clearBtn.left : escBadge.left
                        anchors.rightMargin: 8
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        verticalAlignment: TextInput.AlignVCenter
                        clip: true  // autoScroll metni sola kaydırır; kırpılmazsa kutu dışına taşar
                        color: root.fgColor
                        font.pixelSize: 15
                        font.family: root.textFont
                        focus: true
                        selectByMouse: true
                        onTextChanged: setFilter(text)
                        Keys.forwardTo: [keyHandler]

                        // Metin üzerine gelindiğinde I-Beam imleci göster
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.IBeamCursor
                            acceptedButtons: Qt.NoButton
                        }
                    }

                    // Temizleme Butonu (×)
                    Rectangle {
                        id: clearBtn
                        z: 2
                        visible: root.filterText.length > 0
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 22
                        height: 22
                        radius: 11
                        color: clearMouse.containsMouse ? Qt.rgba(255, 255, 255, 0.22) : Qt.rgba(255, 255, 255, 0.1)

                        Text {
                            anchors.centerIn: parent
                            text: "󰅖"
                            font.family: root.iconFont
                            font.pixelSize: 12
                            color: root.fgColor
                        }

                        MouseArea {
                            id: clearMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                searchInput.text = ""
                                setFilter("")
                            }
                        }
                    }

                    // ESC Kısayol Rozeti (Arama boşken görünür)
                    Rectangle {
                        id: escBadge
                        z: 2
                    visible: !root.filterText.length
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    height: 18
                    radius: 4
                    color: Qt.rgba(255, 255, 255, 0.07)
                    border.color: Qt.rgba(255, 255, 255, 0.12)
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "ESC"
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                        font.family: root.textFont
                        color: root.fgDim
                    }
                }
            }
        }

        // ── Hızlı Eylemler Çubuğu: Update + Reboot + Shutdown (Arama Çubuğunun Altında) ──
        Row {
            id: quickActionsRow
            z: 6
            anchors.top: searchPillContainer.bottom
            anchors.topMargin: 6
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            height: 22

            // CachyOS Canlı Güncelleme Butonu
            Rectangle {
                id: cachyUpdateBadge
                height: 22
                width: updateContentRow.width + 12
                radius: 5
                color: updateBadgeMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.06)
                border.color: (root.sysUpdateInfo && root.sysUpdateInfo.has_updates) ? "#ff5252" : Qt.rgba(root.bgBorder.r, root.bgBorder.g, root.bgBorder.b, 0.25)
                border.width: 1

                Row {
                    id: updateContentRow
                    spacing: 5
                    anchors.centerIn: parent

                    Image {
                        width: 14
                        height: 14
                        sourceSize.width: 14
                        sourceSize.height: 14
                        source: (root.sysUpdateInfo && root.sysUpdateInfo.icon_path) ? "file://" + root.sysUpdateInfo.icon_path : "file:///usr/share/icons/hicolor/scalable/apps/cachy-update-blue.svg"
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: (root.sysUpdateInfo && root.sysUpdateInfo.has_updates) ? (root.sysUpdateInfo.count > 0 ? root.sysUpdateInfo.count + " Updates" : "Update") : "Up to date"
                        color: (root.sysUpdateInfo && root.sysUpdateInfo.has_updates) ? "#ff5252" : root.fgDim
                        font.pixelSize: 10
                        font.family: root.textFont
                        font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    id: updateBadgeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: Shell.playThock()
                    onClicked: {
                        Shell.playCreamy()
                        root.opened = false
                        Shell.launchUpdate()
                        Qt.callLater(closeMenu)
                    }
                }
            }

            // Reboot Butonu
            Rectangle {
                id: quickRebootBtn
                height: 22
                width: rebootContentRow.width + 14
                radius: 5
                color: rebootMouse.containsMouse ? Qt.rgba(255, 167, 38, 0.22) : Qt.rgba(1, 1, 1, 0.06)
                border.color: rebootMouse.containsMouse ? "#ffa726" : Qt.rgba(root.bgBorder.r, root.bgBorder.g, root.bgBorder.b, 0.25)
                border.width: 1

                Row {
                    id: rebootContentRow
                    spacing: 5
                    anchors.centerIn: parent

                    Image {
                        width: 14
                        height: 14
                        sourceSize.width: 16
                        sourceSize.height: 16
                        source: "image://themeicon/system-reboot?v=" + root.themeVersion
                        anchors.verticalCenter: parent.verticalCenter
                        smooth: true
                    }

                    Text {
                        text: "Reboot"
                        color: rebootMouse.containsMouse ? "#ffa726" : root.fgColor
                        font.pixelSize: 10
                        font.family: root.textFont
                        font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    id: rebootMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: Shell.playThock()
                    onClicked: {
                        Shell.playCreamy()
                        root.opened = false
                        Shell.rebootSystem()
                        Qt.callLater(closeMenu)
                    }
                }
            }

            // Shutdown Butonu
            Rectangle {
                id: quickShutdownBtn
                height: 22
                width: shutdownContentRow.width + 14
                radius: 5
                color: shutdownMouse.containsMouse ? Qt.rgba(255, 82, 82, 0.22) : Qt.rgba(1, 1, 1, 0.06)
                border.color: shutdownMouse.containsMouse ? "#ff5252" : Qt.rgba(root.bgBorder.r, root.bgBorder.g, root.bgBorder.b, 0.25)
                border.width: 1

                Row {
                    id: shutdownContentRow
                    spacing: 5
                    anchors.centerIn: parent

                    Image {
                        width: 14
                        height: 14
                        sourceSize.width: 16
                        sourceSize.height: 16
                        source: "image://themeicon/system-shutdown?v=" + root.themeVersion
                        anchors.verticalCenter: parent.verticalCenter
                        smooth: true
                    }

                    Text {
                        text: "Shutdown"
                        color: shutdownMouse.containsMouse ? "#ff5252" : root.fgColor
                        font.pixelSize: 10
                        font.family: root.textFont
                        font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    id: shutdownMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: Shell.playThock()
                    onClicked: {
                        Shell.playCreamy()
                        root.opened = false
                        Shell.powerOff()
                        Qt.callLater(closeMenu)
                    }
                }
            }
        }
    }

        Rectangle {
            width: parent.width
            height: 1
            color: root.bgBorder
            anchors.top: header.bottom
        }

        // Sonuç listesi
        // Satır içi komut çıktısı (Shift+Enter); açıkken sonuç listesinin yerini alır
        CommandOutputPanel {
            id: outputPanel
            visible: root.outputPanelOpen
            anchors.top: parent.top
            anchors.topMargin: headerHeight + 1 + 6
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: footerMonitorBar.top
            anchors.bottomMargin: 4
            runner: Shell.runner
            fgColor: root.fgColor
            dimColor: root.fgDim
            accentColor: root.selBg
            onCopyRequested: function(text) { Shell.setClipboard(text); Shell.playCreamy() }
            onCloseRequested: { root.outputPanelOpen = false; searchInput.forceActiveFocus() }
        }

        ListView {
            id: resultList
            visible: !root.outputPanelOpen && (root.activeMenu !== "system" || root.filterText.length > 0)
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
            highlightMoveDuration: 110
            highlightMoveVelocity: -1

            // Grup başlıkları satır değildir: klavye gezinmesi ve seçim etkilenmez
            section.property: "section"
            section.criteria: ViewSection.FullString
            section.delegate: Item {
                required property string section
                width: resultList.width
                height: section.length ? root.sectionHeaderHeight : 0
                visible: section.length > 0
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 5
                    text: parent.section.toUpperCase()
                    color: root.fgDim
                    font.family: root.textFont
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.2
                }
            }

            delegate: Rectangle {
                id: row
                width: resultList.width
                height: root.rowHeight
                radius: 6
                // Yalnızca kuyruğa eklenen satırlar belirir; yerinde güncellenenler
                // (syncRows) hareket etmez: hızlı yazarken liste sakin kalır.
                NumberAnimation {
                    id: addFade
                    target: row; property: "opacity"; from: 0; to: 1
                    duration: 120; easing.type: Easing.OutCubic
                }
                ListView.onAdd: addFade.restart()
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
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton | Qt.BackButton | Qt.ForwardButton
                    onEntered: {
                        if (!model.disabled && root.selectedIndex !== index) {
                            root.selectedIndex = index
                            resultList.currentIndex = index
                            Shell.playThock()
                        }
                    }
                    onClicked: function(mouse) {
                        if (mouse.button === Qt.BackButton || mouse.button === 8) {
                            Shell.playCreamy()
                            goBack()
                            mouse.accepted = true
                        } else if (mouse.button === Qt.ForwardButton || mouse.button === 16) {
                            Shell.playCreamy()
                            activateIndex(index)
                            mouse.accepted = true
                        } else if (mouse.button === Qt.RightButton) {
                            root.selectedIndex = index
                            resultList.currentIndex = index
                            var pt = mapToItem(root.contentItem, mouse.x, mouse.y)
                            openContextMenu(displayModel.get(index), pt.x, pt.y)
                        } else if (mouse.button === Qt.MiddleButton) {
                            root.selectedIndex = index
                            resultList.currentIndex = index
                            openItemLocation(displayModel.get(index))
                        } else {
                            activateIndex(index)
                        }
                    }
                }
            }
        }

        // ── 2 Sütunlu Sistem Güç Eylemleri Izgarası (Grid View) ──
        GridView {
            id: systemGridView
            visible: root.activeMenu === "system" && !root.filterText.length
            anchors.top: parent.top
            anchors.topMargin: headerHeight + 1 + 10
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.bottom: footerMonitorBar.top
            anchors.bottomMargin: 8
            clip: true
            model: displayModel
            cellWidth: width / 2
            cellHeight: 64
            currentIndex: root.selectedIndex

            delegate: Item {
                width: systemGridView.cellWidth
                height: systemGridView.cellHeight

                Rectangle {
                    id: gridCard
                    anchors.fill: parent
                    anchors.margins: 4
                    radius: 10
                    color: (index === root.selectedIndex) ? root.selBg : (gridMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05))
                    border.color: (index === root.selectedIndex) ? Qt.rgba(root.selFg.r, root.selFg.g, root.selFg.b, 0.3) : Qt.rgba(root.bgBorder.r, root.bgBorder.g, root.bgBorder.b, 0.20)
                    border.width: 1

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 12

                        // İkon Kutusu (Candy Icons)
                        Rectangle {
                            width: 38
                            height: 38
                            radius: 8
                            color: (index === root.selectedIndex) ? Qt.rgba(255, 255, 255, 0.2) : Qt.rgba(1, 1, 1, 0.08)
                            anchors.verticalCenter: parent.verticalCenter

                            Image {
                                id: gridCandyIcon
                                anchors.centerIn: parent
                                width: 26
                                height: 26
                                sourceSize.width: 32
                                sourceSize.height: 32
                                source: (model.iconName && model.iconName.length) ? ("image://themeicon/" + model.iconName + "?v=" + root.themeVersion) : ""
                                smooth: true
                                fillMode: Image.PreserveAspectFit
                                visible: (model.iconName && model.iconName.length) && (status === Image.Ready)
                            }

                            Text {
                                anchors.centerIn: parent
                                text: model.icon || ""
                                font.family: root.iconFont
                                font.pixelSize: 18
                                visible: !gridCandyIcon.visible
                                color: (index === root.selectedIndex) ? root.selFg : (
                                    model.itemId === "system.shutdown" ? "#ff5252" :
                                    model.itemId === "system.reboot" ? "#ffa726" :
                                    model.itemId === "system.lock" ? "#42a5f5" :
                                    model.itemId === "system.logout" ? "#ab47bc" :
                                    model.itemId === "system.save_session" ? "#66bb6a" :
                                    model.itemId === "system.suspend" ? "#26c6da" : root.fgColor
                                )
                            }
                        }

                        // Metin Başlığı ve Açıklaması
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            width: parent.width - 52

                            Text {
                                text: model.label
                                font.family: root.textFont
                                font.pixelSize: 13
                                font.weight: Font.Bold
                                color: (index === root.selectedIndex) ? root.selFg : root.fgColor
                                elide: Text.ElideRight
                                width: parent.width
                            }

                            Text {
                                text: model.detail || model.description || ""
                                font.family: root.textFont
                                font.pixelSize: 10
                                color: (index === root.selectedIndex) ? Qt.rgba(root.selFg.r, root.selFg.g, root.selFg.b, 0.8) : root.fgDim
                                elide: Text.ElideRight
                                width: parent.width
                                visible: text.length > 0
                            }
                        }
                    }

                    MouseArea {
                        id: gridMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton | Qt.BackButton | Qt.ForwardButton
                        onEntered: {
                            if (root.selectedIndex !== index) {
                                root.selectedIndex = index
                                Shell.playThock()
                            }
                        }
                        onPositionChanged: {
                            if (root.selectedIndex !== index) {
                                root.selectedIndex = index
                                Shell.playThock()
                            }
                        }
                        onClicked: function(mouse) {
                            if (mouse.button === Qt.BackButton || mouse.button === 8) {
                                Shell.playCreamy()
                                goBack()
                                mouse.accepted = true
                            } else if (mouse.button === Qt.ForwardButton || mouse.button === 16) {
                                Shell.playCreamy()
                                activateIndex(index)
                                mouse.accepted = true
                            } else if (mouse.button === Qt.RightButton) {
                                Shell.playCreamy()
                                goBack()
                                mouse.accepted = true
                            } else {
                                Shell.playCreamy()
                                activateIndex(index)
                            }
                        }
                    }
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
            visible: (root.activeMenu !== "system" || root.filterText.length > 0) && (resultList.contentHeight - resultList.height) > 16

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
                spacing: root.cardWidth < 520 ? 6 : 8

                // CPU
                Rectangle {
                    height: 22
                    width: cpuRow.width + (root.cardWidth < 520 ? 8 : 12)
                    radius: 5
                    color: cpuMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                    anchors.verticalCenter: parent.verticalCenter

                    Row {
                        id: cpuRow
                        spacing: 4
                        anchors.centerIn: parent
                        Text {
                            text: ""
                            color: root.cpuColor
                            font.pixelSize: 13
                            font.family: root.iconFont
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "CPU"
                            color: root.cpuColor
                            font.pixelSize: 12
                            font.family: root.textFont
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.cardWidth >= 520
                        }
                        Text {
                            text: root.cpuPercent + "%"
                            color: root.fgDim
                            font.pixelSize: 12
                            font.family: root.textFont
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: cpuMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: Shell.playThock()
                        onClicked: {
                            Shell.playCreamy()
                            if (!Shell.isCommandAvailable("cpu-x")) {
                                showDependencyPrompt(
                                    "cpu-x",
                                    "CPU-X System Monitor",
                                    "Viewing CPU details and the hardware monitor needs the <b>cpu-x</b> package. Install it now?",
                                    "sudo pacman -S --noconfirm cpu-x || yay -S --noconfirm cpu-x"
                                )
                                return
                            }
                            root.opened = false
                            Shell.openSystemMonitor("cpu")
                            Qt.callLater(closeMenu)
                        }
                    }
                }

                // GPU
                Rectangle {
                    height: 22
                    width: gpuRow.width + (root.cardWidth < 520 ? 8 : 12)
                    radius: 5
                    color: gpuMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                    anchors.verticalCenter: parent.verticalCenter

                    Row {
                        id: gpuRow
                        spacing: 4
                        anchors.centerIn: parent
                        Text {
                            text: "󰢮"
                            color: root.gpuColor
                            font.pixelSize: 13
                            font.family: root.iconFont
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "GPU"
                            color: root.gpuColor
                            font.pixelSize: 12
                            font.family: root.textFont
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.cardWidth >= 520
                        }
                        Text {
                            text: root.gpuPercent + "%"
                            color: root.fgDim
                            font.pixelSize: 12
                            font.family: root.textFont
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: gpuMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: Shell.playThock()
                        onClicked: {
                            Shell.playCreamy()
                            var vendor = Shell.detectGpuVendor()
                            if (vendor === "amd") {
                                if (!Shell.isCommandAvailable("lact")) {
                                    showDependencyPrompt(
                                        "lact",
                                        "LACT (Linux AMD Controller)",
                                        "Managing your AMD Radeon GPU and its telemetry needs the <b>lact</b> package. Install it now?",
                                        "sudo pacman -S --noconfirm lact || yay -S --noconfirm lact-bin && sudo systemctl enable --now lactd && lact"
                                    )
                                    return
                                }
                            } else if (vendor === "nvidia") {
                                if (!Shell.isCommandAvailable("nvidia-settings") && !Shell.isCommandAvailable("nvtop")) {
                                    showDependencyPrompt(
                                        "nvidia-settings",
                                        "NVIDIA Settings & NVTOP",
                                        "Managing your NVIDIA GPU needs the <b>nvidia-settings</b> and <b>nvtop</b> packages. Install them now?",
                                        "sudo pacman -S --noconfirm nvidia-settings nvtop"
                                    )
                                    return
                                }
                            }
                            root.opened = false
                            Shell.openSystemMonitor("gpu")
                            Qt.callLater(closeMenu)
                        }
                    }
                }

                // RAM
                Rectangle {
                    height: 22
                    width: ramRow.width + (root.cardWidth < 520 ? 8 : 12)
                    radius: 5
                    color: ramMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                    anchors.verticalCenter: parent.verticalCenter

                    Row {
                        id: ramRow
                        spacing: 4
                        anchors.centerIn: parent
                        Text {
                            text: "󰘚"
                            color: root.bgBorder
                            font.pixelSize: 13
                            font.family: root.iconFont
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "RAM"
                            color: root.bgBorder
                            font.pixelSize: 12
                            font.family: root.textFont
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.cardWidth >= 520
                        }
                        Text {
                            text: root.cardWidth < 520 ? ((root.sysMonitor ? root.sysMonitor.ram_pct : 0) + "%") : ((root.sysMonitor ? root.sysMonitor.ram_text : "--") + " (" + (root.sysMonitor ? root.sysMonitor.ram_pct : 0) + "%)")
                            color: root.fgDim
                            font.pixelSize: 12
                            font.family: root.textFont
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: ramMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: Shell.playThock()
                        onClicked: {
                            Shell.playCreamy()
                            root.opened = false
                            Shell.openSystemMonitor("ram")
                            Qt.callLater(closeMenu)
                        }
                    }
                }

                // Disk (Root /)
                Rectangle {
                    height: 22
                    width: diskRow.width + (root.cardWidth < 520 ? 8 : 12)
                    radius: 5
                    color: diskMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                    anchors.verticalCenter: parent.verticalCenter

                    Row {
                        id: diskRow
                        spacing: 4
                        anchors.centerIn: parent
                        Text {
                            text: "󰋊"
                            color: root.bgBorder
                            font.pixelSize: 13
                            font.family: root.iconFont
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "Disk"
                            color: root.bgBorder
                            font.pixelSize: 12
                            font.family: root.textFont
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.cardWidth >= 520
                        }
                        Text {
                            text: root.cardWidth < 520 ? ((root.sysMonitor ? root.sysMonitor.disk_pct : 0) + "%") : ((root.sysMonitor ? root.sysMonitor.disk_text : "--") + " (" + (root.sysMonitor ? root.sysMonitor.disk_pct : 0) + "%)")
                            color: root.fgDim
                            font.pixelSize: 12
                            font.family: root.textFont
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: diskMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: Shell.playThock()
                        onClicked: {
                            Shell.playCreamy()
                            root.opened = false
                            Shell.openSystemMonitor("disk")
                            Qt.callLater(closeMenu)
                        }
                    }
                }

                // Sıcaklık
                Rectangle {
                    height: 22
                    width: tempRow.width + (root.cardWidth < 520 ? 8 : 12)
                    radius: 5
                    color: tempMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                    anchors.verticalCenter: parent.verticalCenter

                    Row {
                        id: tempRow
                        spacing: 4
                        anchors.centerIn: parent
                        Text {
                            text: ""
                            color: root.cpuColor
                            font.pixelSize: 13
                            font.family: root.iconFont
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "Temp"
                            color: root.cpuColor
                            font.pixelSize: 12
                            font.family: root.textFont
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.cardWidth >= 520
                        }
                        Text {
                            text: root.sysMonitor ? root.sysMonitor.temp : "--"
                            color: root.fgDim
                            font.pixelSize: 12
                            font.family: root.textFont
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: tempMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: Shell.playThock()
                        onClicked: {
                            Shell.playCreamy()
                            root.opened = false
                            Shell.openSystemMonitor("temp")
                            Qt.callLater(closeMenu)
                        }
                    }
                }

                // Uptime
                Rectangle {
                    height: 22
                    width: uptimeRow.width + (root.cardWidth < 520 ? 8 : 12)
                    radius: 5
                    color: uptimeMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                    anchors.verticalCenter: parent.verticalCenter

                    Row {
                        id: uptimeRow
                        spacing: 4
                        anchors.centerIn: parent
                        Text {
                            text: "󰔚"
                            color: root.gpuColor
                            font.pixelSize: 13
                            font.family: root.iconFont
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "Uptime"
                            color: root.gpuColor
                            font.pixelSize: 12
                            font.family: root.textFont
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.cardWidth >= 520
                        }
                        Text {
                            text: root.sysMonitor ? root.sysMonitor.uptime : "--"
                            color: root.fgDim
                            font.pixelSize: 12
                            font.family: root.textFont
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: uptimeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: Shell.playThock()
                        onClicked: {
                            Shell.playCreamy()
                            root.opened = false
                            Shell.openSystemMonitor("uptime")
                            Qt.callLater(closeMenu)
                        }
                    }
                }
            }
        }

        // Flyout dışına tıklandığında kapatıcı overlay
        MouseArea {
            anchors.fill: parent
            visible: root.powerFlyoutOpen || root.audioFlyoutOpen
            z: 90
            acceptedButtons: Qt.AllButtons
            onClicked: {
                root.powerFlyoutOpen = false
                root.audioFlyoutOpen = false
            }
        }

        // ── RavenSpot UI Ses Düzeyi Pop-up Paneli (Audio Flyout) ──
        Rectangle {
            id: audioFlyout
            visible: root.audioFlyoutOpen
            z: 100
            anchors.top: parent.top
            anchors.topMargin: 34
            anchors.right: parent.right
            anchors.rightMargin: 16
            width: 210
            height: 68
            radius: 12
            color: Qt.rgba(24/255, 27/255, 34/255, 0.98)
            border.color: Qt.rgba(root.bgBorder.r, root.bgBorder.g, root.bgBorder.b, 0.40)
            border.width: 1

            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#80000000"
                shadowBlur: 0.8
                shadowOpacity: 0.6
                shadowVerticalOffset: 6
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
            }

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                Row {
                    width: parent.width

                    Text {
                        text: "UI Sounds"
                        color: root.fgColor
                        font.pixelSize: 11
                        font.family: root.textFont
                        font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Item {
                        width: parent.width - 62 - flyoutMuteBox.width - 26
                        height: 1
                    }

                    Rectangle {
                        id: flyoutMuteBox
                        height: 18
                        width: 18
                        radius: 4
                        color: flyoutMuteMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.15) : "transparent"
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            anchors.centerIn: parent
                            text: root.appSoundMuted || root.appSoundVolume === 0 ? "󰝟" : "󰕾"
                            color: root.appSoundMuted || root.appSoundVolume === 0 ? "#ef4444" : root.selFg
                            font.pixelSize: 12
                            font.family: root.iconFont
                        }

                        MouseArea {
                            id: flyoutMuteMouse
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Shell.playCreamy()
                                Shell.toggleAppMute()
                            }
                        }
                    }

                    Text {
                        text: root.appSoundMuted ? "0%" : root.appSoundVolume + "%"
                        color: root.appSoundMuted ? "#ef4444" : root.fgDim
                        font.pixelSize: 10
                        font.family: root.textFont
                        font.weight: Font.Bold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                Rectangle {
                    id: appVolTrack
                    width: parent.width
                    height: 5
                    radius: 2.5
                    color: "#30ffffff"

                    Rectangle {
                        id: appVolFill
                        height: parent.height
                        radius: 2.5
                        width: root.appSoundMuted ? 0 : Math.max(0, Math.min(parent.width, (root.appSoundVolume / 100.0) * parent.width))
                        color: root.bgBorder
                        Behavior on width { NumberAnimation { duration: 40 } }
                    }

                    Rectangle {
                        width: 9
                        height: 9
                        radius: 4.5
                        anchors.verticalCenter: parent.verticalCenter
                        x: Math.max(0, Math.min(appVolTrack.width - 9, appVolFill.width - 4.5))
                        color: root.selFg
                        border.color: root.bgBorder
                        border.width: 1
                        visible: !root.appSoundMuted
                    }

                    WheelHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: function(event) {
                            var delta = event.angleDelta.y > 0 ? 5 : -5
                            var v = Math.max(0, Math.min(100, root.appSoundVolume + delta))
                            root.appSoundVolume = v
                            root.appSoundMuted = false
                            Shell.setAppVolume(v)
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        anchors.topMargin: -8
                        anchors.bottomMargin: -8
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor

                        function updateVol(mouse) {
                            var ratio = Math.max(0.0, Math.min(1.0, mouse.x / appVolTrack.width))
                            var newVol = Math.round(ratio * 100)
                            root.appSoundVolume = newVol
                            root.appSoundMuted = false
                            Shell.setAppVolume(newVol)
                        }

                        onPressed: function(mouse) {
                            updateVol(mouse)
                        }
                        onPositionChanged: function(mouse) {
                            if (pressed) updateVol(mouse)
                        }
                    }
                }
            }
        }

        // ── Güç Profili & Uyku Engelleme Pop-up Paneli (Power Flyout) ──
        Rectangle {
            id: powerFlyout
            visible: root.powerFlyoutOpen
            z: 100
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 34
            width: Math.min(390, card.width - 24)
            height: powerFlyoutCol.height + 26
            radius: 12
            color: Qt.rgba(24/255, 27/255, 34/255, 0.98)
            border.color: Qt.rgba(255, 255, 255, 0.20)
            border.width: 1

            // MultiEffect gölge
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#80000000"
                shadowBlur: 0.8
                shadowOpacity: 0.6
                shadowVerticalOffset: 6
            }

            // Tıklamaların arka plana geçmesini engelle
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
            }

            Column {
                id: powerFlyoutCol
                anchors.top: parent.top
                anchors.topMargin: 14
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.right: parent.right
                anchors.rightMargin: 16
                spacing: 12

                // 1. Başlık Satırı
                Item {
                    width: parent.width
                    height: 20

                    Row {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6

                        Text {
                            text: "󰂄"
                            font.family: root.iconFont
                            font.pixelSize: 14
                            color: root.selFg
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: "Power & Performance"
                            font.family: root.textFont
                            font.pixelSize: 13
                            font.weight: Font.Bold
                            color: root.fgColor
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    // Kapat Butonu (×)
                    Rectangle {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 20
                        height: 20
                        radius: 10
                        color: closeFlyoutMouse.containsMouse ? Qt.rgba(255, 255, 255, 0.15) : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "󰅖"
                            font.family: root.iconFont
                            font.pixelSize: 11
                            color: root.fgDim
                        }

                        MouseArea {
                            id: closeFlyoutMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Shell.playCreamy()
                                root.powerFlyoutOpen = false
                            }
                        }
                    }
                }

                // 2. Güç Profili 3 Kademeli Slider (Kompakt ve tam hizalı)
                Item {
                    width: parent.width
                    height: 56

                    Item {
                        id: sliderTrackContainer
                        width: 250
                        height: 52
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: 4

                        // Slider Arka Plan Rayı
                        Rectangle {
                            id: profileTrack
                            width: parent.width
                            height: 4
                            radius: 2
                            color: Qt.rgba(255, 255, 255, 0.16)
                            y: 10

                            // Aktif Doluluk Çizgisi
                            Rectangle {
                                height: parent.height
                                radius: 2
                                width: root.currentPowerProfile === "power-saver" ? 0 : (root.currentPowerProfile === "performance" ? 250 : 125)
                                color: root.currentPowerProfile === "power-saver" ? "#22c55e" : (root.currentPowerProfile === "performance" ? "#f97316" : root.selFg)
                                Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutQuad } }
                            }

                            // 3 Referans Noktası (Rayın tam başlangıcında, tam ortasında, tam bitişinde)
                            Rectangle { width: 6; height: 6; radius: 3; color: "#ffffff"; opacity: 0.5; x: -3; y: -1 }
                            Rectangle { width: 6; height: 6; radius: 3; color: "#ffffff"; opacity: 0.5; x: 125 - 3; y: -1 }
                            Rectangle { width: 6; height: 6; radius: 3; color: "#ffffff"; opacity: 0.5; x: 250 - 3; y: -1 }

                            // Slider Thumb (Kaydırıcı / Seçici Düğmesi)
                            Rectangle {
                                id: profileThumb
                                width: 18
                                height: 18
                                radius: 9
                                color: root.currentPowerProfile === "power-saver" ? "#22c55e" : (root.currentPowerProfile === "performance" ? "#f97316" : root.selFg)
                                border.color: "#ffffff"
                                border.width: 2
                                y: -7
                                x: (root.currentPowerProfile === "power-saver" ? 0 : (root.currentPowerProfile === "performance" ? 250 : 125)) - 9
                                Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutQuad } }
                                Behavior on color { ColorAnimation { duration: 150 } }
                            }
                        }

                        // Etiket 1: Power Saver (Nokta 0 hizasında)
                        Item {
                            anchors.horizontalCenter: parent.left
                            anchors.top: profileTrack.bottom
                            anchors.topMargin: 12
                            width: 80
                            height: 20

                            Row {
                                anchors.centerIn: parent
                                spacing: 4
                                Text {
                                    text: "󰌪"
                                    color: root.currentPowerProfile === "power-saver" ? "#22c55e" : root.fgDim
                                    font.pixelSize: 11
                                    font.family: root.iconFont
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: "Power Saver"
                                    color: root.currentPowerProfile === "power-saver" ? "#22c55e" : root.fgDim
                                    font.pixelSize: 10
                                    font.family: root.textFont
                                    font.weight: root.currentPowerProfile === "power-saver" ? Font.Bold : Font.Normal
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    Shell.playCreamy()
                                    root.currentPowerProfile = "power-saver"
                                    Shell.setPowerProfile("power-saver")
                                }
                            }
                        }

                        // Etiket 2: Balanced (Nokta 1 hizasında - Merkez)
                        Item {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: profileTrack.bottom
                            anchors.topMargin: 12
                            width: 70
                            height: 20

                            Row {
                                anchors.centerIn: parent
                                spacing: 4
                                Text {
                                    text: "󰾆"
                                    color: root.currentPowerProfile === "balanced" ? root.selFg : root.fgDim
                                    font.pixelSize: 11
                                    font.family: root.iconFont
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: "Balanced"
                                    color: root.currentPowerProfile === "balanced" ? root.selFg : root.fgDim
                                    font.pixelSize: 10
                                    font.family: root.textFont
                                    font.weight: root.currentPowerProfile === "balanced" ? Font.Bold : Font.Normal
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    Shell.playCreamy()
                                    root.currentPowerProfile = "balanced"
                                    Shell.setPowerProfile("balanced")
                                }
                            }
                        }

                        // Etiket 3: Performance (Nokta 2 hizasında)
                        Item {
                            anchors.horizontalCenter: parent.right
                            anchors.top: profileTrack.bottom
                            anchors.topMargin: 12
                            width: 80
                            height: 20

                            Row {
                                anchors.centerIn: parent
                                spacing: 4
                                Text {
                                    text: "󰓅"
                                    color: root.currentPowerProfile === "performance" ? "#f97316" : root.fgDim
                                    font.pixelSize: 11
                                    font.family: root.iconFont
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: "Performance"
                                    color: root.currentPowerProfile === "performance" ? "#f97316" : root.fgDim
                                    font.pixelSize: 10
                                    font.family: root.textFont
                                    font.weight: root.currentPowerProfile === "performance" ? Font.Bold : Font.Normal
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    Shell.playCreamy()
                                    root.currentPowerProfile = "performance"
                                    Shell.setPowerProfile("performance")
                                }
                            }
                        }

                        // Ray üzerine tıklama
                        MouseArea {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: 24
                            cursorShape: Qt.PointingHandCursor
                            onClicked: (mouse) => {
                                Shell.playCreamy()
                                var p = "balanced"
                                if (mouse.x < 83) p = "power-saver"
                                else if (mouse.x > 167) p = "performance"
                                root.currentPowerProfile = p
                                Shell.setPowerProfile(p)
                            }
                        }
                    }
                }

                // Ayırıcı Çizgi
                Rectangle {
                    width: parent.width
                    height: 1
                    color: Qt.rgba(255, 255, 255, 0.08)
                }

                // 3. "Manually Block Sleep and Screen Locking" (KDE Tray ile tam senkron)
                Rectangle {
                    width: parent.width
                    height: 40
                    radius: 8
                    color: inhibitMouse.containsMouse ? Qt.rgba(255, 255, 255, 0.06) : "transparent"

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 12

                        // Candy Sleep İkonu
                        Image {
                            id: sleepIconImg
                            width: 22
                            height: 22
                            sourceSize.width: 24
                            sourceSize.height: 24
                            source: "image://themeicon/sleep?v=" + root.themeVersion
                            smooth: true
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        // iOS / KDE Tarzı Toggle Switch
                        Rectangle {
                            id: toggleTrack
                            width: 38
                            height: 20
                            radius: 10
                            color: root.sleepInhibited ? "#22c55e" : Qt.rgba(1, 1, 1, 0.18)
                            border.color: root.sleepInhibited ? "#16a34a" : Qt.rgba(255, 255, 255, 0.15)
                            border.width: 1
                            anchors.verticalCenter: parent.verticalCenter
                            Behavior on color { ColorAnimation { duration: 150 } }

                            // Toggle Düğmesi (Thumb)
                            Rectangle {
                                width: 16
                                height: 16
                                radius: 8
                                color: "#ffffff"
                                anchors.verticalCenter: parent.verticalCenter
                                x: root.sleepInhibited ? 20 : 2
                                Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
                            }
                        }

                        // Metin Etiketi (İngilizce - KDE ile birebir)
                        Text {
                            text: "Manually Block Sleep and Screen Locking"
                            color: root.sleepInhibited ? "#ffffff" : root.fgColor
                            font.pixelSize: 12
                            font.family: root.textFont
                            font.weight: Font.Medium
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: inhibitMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            Shell.playCreamy()
                            if (typeof kdeInhibitCtrl !== "undefined" && kdeInhibitCtrl) {
                                if (kdeInhibitCtrl.isManuallyInhibited) {
                                    kdeInhibitCtrl.uninhibit()
                                } else {
                                    kdeInhibitCtrl.inhibit("RavenSpot")
                                }
                                root.sleepInhibited = kdeInhibitCtrl.isManuallyInhibited
                            } else {
                                var nextState = !root.sleepInhibited
                                root.sleepInhibited = Shell.setSleepInhibited(nextState)
                            }
                        }
                    }
                }
            }
        }

        // ── Paket / Uygulama Kurulum Onay Modalı (Dependency Installer Modal) ──
        Rectangle {
            id: dependencyModalBackdrop
            visible: root.depModalOpen
            z: 200
            anchors.fill: card
            radius: card.radius
            color: Qt.rgba(0, 0, 0, 0.65)

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    root.depModalOpen = false
                }
            }

            Rectangle {
                id: dependencyModalCard
                anchors.centerIn: parent
                width: Math.min(420, card.width - 32)
                height: depCol.height + 36
                radius: 12
                color: Qt.rgba(24/255, 27/255, 34/255, 0.98)
                border.color: root.selFg
                border.width: 1

                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: "#a0000000"
                    shadowBlur: 0.85
                    shadowOpacity: 0.8
                    shadowVerticalOffset: 8
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.AllButtons
                }

                Column {
                    id: depCol
                    anchors.top: parent.top
                    anchors.topMargin: 18
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    spacing: 14

                    // Başlık Satırı
                    Row {
                        spacing: 8
                        width: parent.width

                        Text {
                            text: "󰏔"
                            font.family: root.iconFont
                            font.pixelSize: 18
                            color: root.selFg
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: root.depModalTitle
                            font.family: root.textFont
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: root.fgColor
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            width: parent.width - 28
                        }
                    }

                    // Açıklama Metni
                    Text {
                        text: root.depModalDesc
                        textFormat: Text.RichText
                        font.family: root.textFont
                        font.pixelSize: 12
                        color: root.fgDim
                        wrapMode: Text.Wrap
                        width: parent.width
                        lineHeight: 1.25
                    }

                    // Butonlar Satırı (Kur / İptal)
                    Row {
                        anchors.right: parent.right
                        spacing: 10

                        // İptal Butonu
                        Rectangle {
                            width: 75
                            height: 28
                            radius: 6
                            color: cancelMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.06)
                            border.color: Qt.rgba(255, 255, 255, 0.15)
                            border.width: 1

                            Text {
                                text: "Cancel"
                                anchors.centerIn: parent
                                font.family: root.textFont
                                font.pixelSize: 12
                                color: root.fgDim
                            }

                            MouseArea {
                                id: cancelMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    Shell.playCreamy()
                                    root.depModalOpen = false
                                }
                            }
                        }

                        // Kur Butonu
                        Rectangle {
                            width: 90
                            height: 28
                            radius: 6
                            color: installMouse.containsMouse ? Qt.lighter(root.selFg, 1.15) : root.selFg

                            Row {
                                anchors.centerIn: parent
                                spacing: 5
                                Text {
                                    text: "󰄬"
                                    font.family: root.iconFont
                                    font.pixelSize: 12
                                    color: "#ffffff"
                                }
                                Text {
                                    text: "Install"
                                    font.family: root.textFont
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    color: "#ffffff"
                                }
                            }

                            MouseArea {
                                id: installMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    Shell.playCreamy()
                                    root.depModalOpen = false
                                    Shell.installPackage(root.depModalPkg, root.depModalCmd)
                                    root.opened = false
                                    Qt.callLater(closeMenu)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Bağlam Menüsü Görünümü (Context Menu Popup) ──────────────
    Popup {
        id: contextPopup
        parent: Overlay.overlay
        width: root.contextMode === "list" ? 250 : 320
        height: root.contextMode === "rename" ? renameBox.implicitHeight + 16
                                              : Math.min(contextList.contentHeight + 16, 380)
        padding: 8
        modal: true
        dim: false
        Overlay.modal: Item {}
        focus: true
        // Esc'yi kendimiz ele alıyoruz: alt menüde geri döner, ana listede kapatır
        closePolicy: Popup.CloseOnPressOutside
        onOpened: { contextList.currentIndex = 0; contextList.forceActiveFocus() }
        onClosed: { root.contextMode = "list"; root.renameError = ""; searchInput.forceActiveFocus() }

        background: Rectangle {
            color: root.bgColor
            border.color: root.bgBorder
            border.width: 1
            radius: 10
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: root.glowColor
                shadowBlur: 0.6
                shadowOpacity: 0.5
            }
        }

        contentItem: Item {
          // Yeniden adlandırma kutusu (contextMode === "rename")
          Column {
            id: renameBox
            visible: root.contextMode === "rename"
            width: parent.width
            spacing: 6
            Text { text: "Rename"; color: root.fgDim; font.family: root.textFont; font.pixelSize: 11 }
            Rectangle {
                width: parent.width
                height: 30
                radius: 6
                color: Qt.rgba(1, 1, 1, 0.08)
                border.color: root.renameError ? "#ff5252" : root.selBg
                border.width: 1
                TextInput {
                    id: renameInput
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    verticalAlignment: TextInput.AlignVCenter
                    clip: true
                    selectByMouse: true
                    color: root.fgColor
                    font.family: root.textFont
                    font.pixelSize: 13
                    Keys.onReturnPressed: root.submitRename(text)
                    Keys.onEnterPressed: root.submitRename(text)
                    Keys.onEscapePressed: root.contextBack()
                    onTextEdited: root.renameError = ""
                }
            }
            Text {
                width: parent.width
                visible: !!root.renameError
                text: root.renameError
                color: "#ff5252"
                wrapMode: Text.WordWrap
                font.family: root.textFont
                font.pixelSize: 11
            }
            Text { text: "Enter: rename  \u00b7  Esc: back"; color: root.fgDim; font.family: root.textFont; font.pixelSize: 10 }
          }

          ListView {
            id: contextList
            visible: root.contextMode !== "rename"
            anchors.fill: parent
            model: contextMenuModel
            clip: true
            spacing: 3
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Down) {
                    contextList.currentIndex = Math.min(contextMenuModel.count - 1, contextList.currentIndex + 1)
                    Shell.playThock(); event.accepted = true
                } else if (event.key === Qt.Key_Up) {
                    contextList.currentIndex = Math.max(0, contextList.currentIndex - 1)
                    Shell.playThock(); event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Right) {
                    var e = contextMenuModel.get(contextList.currentIndex)
                    var k = event.key === Qt.Key_Right ? "right" : "enter"
                    if (e && MenuModel.contextKeyActivates(k, e.actionType)) {
                        Shell.playCreamy(); executeContextAction(e.actionType, e.actionPayload)
                    }
                    event.accepted = true
                } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Left || event.key === Qt.Key_Backspace) {
                    if (root.contextMode !== "list") root.contextBack()
                    else if (event.key === Qt.Key_Escape) contextPopup.close()
                    event.accepted = true
                }
            }

            delegate: Rectangle {
                id: cItem
                width: contextList.width
                height: 32
                radius: 6
                readonly property bool hot: mouseItem.containsMouse || ListView.isCurrentItem
                color: hot ? (model.isDanger ? "#8b0000" : root.selBg) : "transparent"

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 10

                    Text {
                        width: 18
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignHCenter
                        text: model.icon
                        font.family: root.iconFont
                        font.pixelSize: 14
                        color: cItem.hot ? root.selFg : (model.isDanger ? "#ff5252" : root.fgColor)
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: model.label
                        font.family: root.textFont
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        color: cItem.hot ? root.selFg : (model.isDanger ? "#ff5252" : root.fgColor)
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: mouseItem
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: {
                        contextList.currentIndex = index
                        Shell.playThock()
                    }
                    onClicked: {
                        Shell.playCreamy()
                        executeContextAction(model.actionType, model.actionPayload)
                    }
                }
            }
          }
        }
    }

    Component.onCompleted: {
        syncTheme()
        var appAudio = Shell.getAppAudioInfo()
        if (appAudio) {
            root.appSoundVolume = (appAudio.volume !== undefined) ? appAudio.volume : 5
            root.appSoundMuted = !!appAudio.muted
        }
        var ainfo = Shell.getInitialAudioInfo()
        if (ainfo) {
            root.sysVolume = ainfo.volume
            root.sysMuted = ainfo.muted
        }
        root.sysUpdateInfo = Shell.getUpdateInfo()
        root.sysMonitor = Shell.getSysMonitorInfo()

        // Preload menu items and application catalog in background
        loadItems()

        if (!Shell.startHidden) {
            openMenu("root")
        } else {
            root.visible = false
            root.opened = false
            Shell.onMenuVisibilityChanged(false)
        }
    }
}
