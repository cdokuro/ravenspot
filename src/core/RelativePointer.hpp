#pragma once

#include <QObject>

struct wl_registry;
struct wl_pointer;
struct zwp_relative_pointer_manager_v1;
struct zwp_relative_pointer_v1;

namespace ravenspot {

// Wayland zwp_relative_pointer_v1 bağlayıcısı.
//
// Neden: Wayland'de QML MouseArea koordinatları yüzeye (surface) göreli gelir.
// Pencere layer-shell margin'leriyle taşınınca bu koordinatlar compositor commit'i
// kadar gecikmeyle kayar; local delta'lar kendi hareketimizi geri besler
// (yarı hız + titreme). Relative pointer ise yüzey konumundan BAĞIMSIZ ham fare
// delta'sı verir: sürükleme için tek doğru kaynak budur.
//
// Olaylar Qt'nin varsayılan Wayland kuyruğunda, GUI thread'inde dağıtılır.
class RelativePointer : public QObject {
    Q_OBJECT

public:
    explicit RelativePointer(QObject *parent = nullptr);
    ~RelativePointer() override;

    // Compositor protokolü destekliyor ve wl_pointer hazırsa true.
    // Tembel başlatılır: ilk çağrıda bağlanmayı dener.
    [[nodiscard]] bool ensureReady();

signals:
    // Hızlandırılmış (imleçle birebir) delta, logical pixel.
    void motion(double dx, double dy);

private:
    static void onRegistryGlobal(void *data, wl_registry *registry, uint32_t name,
                                 const char *interface, uint32_t version);
    static void onRegistryGlobalRemove(void *data, wl_registry *registry, uint32_t name);
    static void onRelativeMotion(void *data, zwp_relative_pointer_v1 *rp,
                                 uint32_t utimeHi, uint32_t utimeLo,
                                 int32_t dx, int32_t dy, int32_t dxUnaccel, int32_t dyUnaccel);

    bool bindManager();

    bool m_triedBind{false};
    zwp_relative_pointer_manager_v1 *m_manager{nullptr};
    zwp_relative_pointer_v1 *m_relPointer{nullptr};
    wl_pointer *m_boundPointer{nullptr};
};

} // namespace ravenspot
