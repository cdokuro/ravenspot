#!/usr/bin/env bash
# ==============================================================================
# RavenSpot — Otomatik Kurulum ve Yapılandırma Betiği
# https://github.com/cdokuro/ravenspot
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AUTO_YES=false

for arg in "$@"; do
    if [[ "$arg" == "-y" || "$arg" == "--yes" ]]; then
        AUTO_YES=true
    fi
done

echo "=================================================================="
echo "🦅 RavenSpot — Modern Spotlight Search & Command Palette Kurulumu"
echo "=================================================================="
echo

# 1. Paket Yöneticisi Tespiti
PKG_MGR=""
if command -v pacman >/dev/null 2>&1; then
    PKG_MGR="pacman"
    AUR_HELPER=""
    if command -v yay >/dev/null 2>&1; then
        AUR_HELPER="yay"
    elif command -v paru >/dev/null 2>&1; then
        AUR_HELPER="paru"
    fi
fi

# 2. Temel Sistem Bağımlılıkları
echo "📦 1. Temel sistem paketleri kontrol ediliyor..."
CORE_PACKAGES=(
    "python"
    "python-pyside6"
    "qt6-declarative"
    "qt6-multimedia"
    "plasma-systemmonitor"
)

MISSING_CORE=()
if [[ "$PKG_MGR" == "pacman" ]]; then
    for pkg in "${CORE_PACKAGES[@]}"; do
        if ! pacman -Qi "$pkg" >/dev/null 2>&1; then
            MISSING_CORE+=("$pkg")
        fi
    done

    if [[ ${#MISSING_CORE[@]} -gt 0 ]]; then
        echo "Eksik temel paketler bulundu: ${MISSING_CORE[*]}"
        if [[ "$AUTO_YES" == true ]]; then
            sudo pacman -S --needed --noconfirm "${MISSING_CORE[@]}"
        else
            read -p "Bu temel paketleri şimdi yüklemek ister misiniz? [E/h]: " choice
            choice="${choice:-E}"
            if [[ "$choice" =~ ^[EeYy]$ ]]; then
                sudo pacman -S --needed "${MISSING_CORE[@]}"
            else
                echo "⚠️ Temel paketler olmadan RavenSpot düzgün çalışmayabilir."
            fi
        fi
    else
        echo "✓ Tüm temel paketler yüklü."
    fi
fi

# 3. İsteğe Bağlı Donanım İzleme ve Telemetri Paketleri
echo
echo "=================================================================="
echo "📊 2. İsteğe Bağlı Donanım İzleme ve Telemetri Paketleri"
echo "=================================================================="
echo "RavenSpot'un en altındaki GPU, CPU ve RAM göstergelerine tıkladığınızda"
echo "en detaylı ve gelişmiş pencerelerin açılması için aşağıdaki araçlar önerilir:"
echo
echo "  1) LACT (Linux AMD Controller):"
echo "     • AMD Radeon (RX 7000/9000, RX 9070 XT vb.) için saat frekansları,"
echo "       VRAM/Hotspot sıcaklıkları, fan eğrisi ve voltaj ayarları."
echo "  2) CPU-X:"
echo "     • Linux dünyasının CPU-Z'si; İşlemci saat hızları, önbellek ve"
echo "       DDR5 RAM anlık gecikme/zamanlamaları (tCL, tRCD, tRP, tRAS, SPD)."
echo "  3) Mission Center:"
echo "     • Modern Windows 11 Görev Yöneticisi tarzı akıcı CPU ve RAM grafikleri."
echo "  4) amdgpu_top:"
echo "     • AMD GPU grafik ve compute motorları için anlık CLI/GUI telemetrisi."
echo "------------------------------------------------------------------"

OPTIONAL_PACKAGES=("lact" "cpu-x" "mission-center" "amdgpu_top")
INSTALL_OPT=()

if [[ "$PKG_MGR" == "pacman" ]]; then
    if [[ "$AUTO_YES" == true ]]; then
        INSTALL_OPT=("${OPTIONAL_PACKAGES[@]}")
    else
        echo "Seçenekler:"
        echo "  [1] Önerilen tüm donanım paketlerini kur (Tümü)"
        echo "  [2] Paketleri tek tek seçerek kur"
        echo "  [3] Şimdilik atla (Yalnızca KDE yerel monitörü kullan)"
        read -p "Seçiminiz [1/2/3] (Varsayılan: 1): " opt_choice
        opt_choice="${opt_choice:-1}"

        if [[ "$opt_choice" == "1" ]]; then
            INSTALL_OPT=("${OPTIONAL_PACKAGES[@]}")
        elif [[ "$opt_choice" == "2" ]]; then
            for pkg in "${OPTIONAL_PACKAGES[@]}"; do
                read -p "$pkg kurulsun mu? [E/h]: " p_choice
                p_choice="${p_choice:-E}"
                if [[ "$p_choice" =~ ^[EeYy]$ ]]; then
                    INSTALL_OPT+=("$pkg")
                fi
            done
        fi
    fi

    if [[ ${#INSTALL_OPT[@]} -gt 0 ]]; then
        echo "Yükleniyor: ${INSTALL_OPT[*]}..."
        sudo pacman -S --needed --noconfirm "${INSTALL_OPT[@]}" || {
            if [[ -n "$AUR_HELPER" ]]; then
                "$AUR_HELPER" -S --needed "${INSTALL_OPT[@]}"
            fi
        }

        # LACT kurulduysa servisini etkinleştir
        if pacman -Qi lact >/dev/null 2>&1; then
            echo "LACT servisi (lactd) etkinleştiriliyor..."
            sudo systemctl enable --now lactd || true
        fi
    fi
fi

# 4. Dosyaların Kurulumu
echo
echo "🚀 3. RavenSpot sisteminize kuruluyor..."
TARGET_DIR="$HOME/.local/share/omarchy-menu"
mkdir -p "$TARGET_DIR"
mkdir -p "$HOME/.local/bin"
mkdir -p "$HOME/.local/share/applications"
mkdir -p "$HOME/.config/systemd/user"
mkdir -p "$HOME/.config/ravenspot"

cp -rf "$SCRIPT_DIR"/* "$TARGET_DIR/"
test -f "$TARGET_DIR/menu.jsonc" && cp -n "$TARGET_DIR/menu.jsonc" "$HOME/.config/ravenspot/menu.jsonc" || true

# Wrapper script
cat << 'EOF' > "$HOME/.local/bin/ravenspot"
#!/usr/bin/env bash
# RavenSpot launcher
exec python3 "$HOME/.local/share/omarchy-menu/bridge.py" "$@"
EOF
chmod +x "$HOME/.local/bin/ravenspot"

# Desktop entry
sed "s|/home/dokuro|$HOME|g" "$SCRIPT_DIR/ravenspot.desktop" > "$HOME/.local/share/applications/ravenspot.desktop"
chmod +x "$HOME/.local/share/applications/ravenspot.desktop"

# Systemd Service
cat << EOF > "$HOME/.config/systemd/user/ravenspot.service"
[Unit]
Description=RavenSpot - Spotlight Launcher Resident Daemon
After=graphical-session.target
PartOf=graphical-session.target

[Service]
Type=simple
ExecStart=/usr/bin/python3 $HOME/.local/share/omarchy-menu/bridge.py --daemon
Restart=on-failure
RestartSec=2s
Environment=PYTHONUNBUFFERED=1

[Install]
WantedBy=graphical-session.target
EOF

# 5. Kısayollar ve Meta Tuşu Yapılandırması
echo "⌨️ 4. Meta (Windows) tuşu yapılandırılıyor..."
if command -v kwriteconfig6 >/dev/null 2>&1; then
    # Varsayılan Kickoff Meta tuşu yakalamasını boşa çıkar
    kwriteconfig6 --file kglobalshortcutsrc --group plasmashell --key "activate application launcher" "Alt+F1,none,Uygulama Başlatıcısını Etkinleştir" || true
    # KWin ModifierOnlyShortcuts ayarla
    kwriteconfig6 --file kwinrc --group ModifierOnlyShortcuts --key Meta "org.kde.kglobalaccel,/component/ravenspot_desktop,org.kde.kglobalaccel.Component,invokeShortcut,_launch" || true
    # KWin'i yenile
    busctl --user call org.kde.KWin /KWin org.kde.KWin reconfigure >/dev/null 2>&1 || true
fi

if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 >/dev/null 2>&1 || true
fi

# 6. Servisi Başlat
echo "⚙️ 5. RavenSpot arka plan servisi başlatılıyor..."
systemctl --user daemon-reload
systemctl --user enable --now ravenspot.service
systemctl --user restart ravenspot.service

echo
echo "=================================================================="
echo "🎉 Kurulum başarıyla tamamlandı!"
echo "• Tek başına Meta (Windows) tuşuna veya Meta+Space kombinasyonuna"
echo "  basarak RavenSpot'u anında açabilirsiniz."
echo "=================================================================="
