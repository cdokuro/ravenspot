# Design: Logical menu structure and grouped search results

**Date:** 2026-11-07 · **Size:** project (menu data + search pipeline + list UI)

## Intent
"Everything is listed in a jumble" — both the submenus and search results.
The user asked for an app-designer pass so every submenu and every result list
follows an obvious logic. They delegated the decisions and asked for the work
to be finished and pushed while they are away, so every choice below is my
call.

## Findings (evidence)
- Duplicates: Night Light, Wallpaper, Fonts, Colors live in both Appearance and
  Settings; searching "night" lists two rows. "lock"/"shutdown" list the menu
  entry *and* a hard-coded system-action row.
- Root mixes categories with one-shot actions (Shell, Discover, Browser →
  mozilla.org) and a vague "Trigger" menu.
- `settings` has an `action`, so it is an action row, not a submenu.
- Search output is one flat list assembled by `unshift` in source order
  (system actions above calculator above windows…), no headers.
- Instant rows are Turkish ("Klasörü Aç", "Sesi %60 Yap", "Google'da Ara");
  the UI language is English.
- `~/.config/ravenspot/menu.jsonc` is a stale full copy of the default menu
  (no real customization), so it overrides every default entry.

## Goals
1. Root menu: Apps, Files, Tools, Appearance, Settings, Power — each a real
   category; nothing duplicated across categories.
2. Every submenu ordered by a stated rule (alphabetical for catalogues,
   frequency/logical flow for settings, least → most destructive for Power).
3. Search results grouped under headers in a fixed, predictable order with an
   optional Top Hit, per-group caps and no duplicates.
4. All UI strings English.

## Non-goals
- Learning/frecency ranking. - New providers beyond Files/Recent.
- Changing the Power grid layout.

## Anti-goals
- Hiding something the user searches for (caps only apply when several groups
  compete; inside a submenu nothing is capped).
- Breaking keyboard navigation: headers are ListView section headers, not rows.

## Approaches
1. **Grouped sections + Top Hit (chosen).** Classify each result, order groups
   by a fixed table, promote one strong match as Top Hit. Predictable like
   Spotlight/Raycast; needs a small header UI.
2. Single ranked list with type bonuses. No UI change, but mixed types still
   interleave — the complaint stays.
3. Tabs/filters per type. Extra keystrokes; overkill for a launcher.

## Decisions
### Menu tree
- **Apps:** All Apps first, then categories alphabetically; empty categories
  hidden. Inside a category: submenus (Steam Library) first, then A→Z.
- **Files:** Home, Downloads, Documents, Pictures, Music, Videos, Recent Files.
- **Tools:** Screenshot ▸ (Fullscreen, Active Window, Region, Screen
  Recording), Clipboard History, Emoji Picker, Terminal, Software Center,
  Nashi ▸.
- **Appearance:** RavenSpot Themes, Color Scheme, Wallpaper, Night Light,
  Icons, Cursors, Fonts.
- **Settings:** System Settings, then Display, Sound, Network, Bluetooth,
  Keyboard, Shortcuts, Mouse, Touchpad, Power Management, Power Profile,
  Notifications, Default Apps, Autostart, Users, Screen Lock, Firewall,
  Activities, About. Duplicates of Appearance removed.
- **Power:** Lock, Screensaver, Suspend, Hibernate, Log Out, Save Session,
  Reboot, Shutdown.
- Help stays hidden from root but searchable. Browser entry removed.
- Ids: `trigger.*` → `tools.*`, `shell` → `tools.terminal`, `discover` →
  `tools.discover`, `downloads` → `files.downloads`,
  `appearance.powerprofile` → `settings.powerprofile`.
- The stale user copy is backed up to `menu.jsonc.bak-2026-11-07` and replaced
  by an empty override file.

### Search groups (fixed order)
| Key | Header | Rows |
|---|---|---|
| instant | Quick Answer | calculator, units, volume/mute, web bangs, open path |
| pc | (mode name) | `kill`/`clip`/`recent`/`:emoji` results |
| commands (strong) | Run Command | command rows when the text is clearly a command |
| top | Top Hit | best non-window row with an exact/prefix match |
| apps | Applications | app rows, app category menus |
| windows | Open Windows | KWin windows |
| system | Power & Session | Power menu entries, Restart Plasma |
| settings | Settings | settings.*, appearance.* |
| tools | Tools | tools.* |
| files | Files & Folders | files.*, file rows, Steam folders |
| help | Help | help.* |
| commands (weak) | Run Command | command rows otherwise |
| web | Search the Web | Google/DuckDuckGo fallback |

- Top Hit only when ≥2 groups are present and no instant row exists.
- Caps when searching from root: Applications 8, others 5 (instant, pc,
  commands, web uncapped). No caps inside a submenu.
- Within a group: score, then label.
- Hard-coded system actions that duplicate Power menu entries are dropped;
  Restart Plasma stays as a search-only row.

## Left to the builder
Header styling (small caps, dim colour, 26 px).

## Verification
QML unit tests for grouping/ordering/caps/top hit and menu-tree invariants
(no duplicate labels per submenu, every child's parent exists, root order),
E2E 228/228, deployed manual check.
