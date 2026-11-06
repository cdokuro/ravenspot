#include "RelativePointer.hpp"

#include <QGuiApplication>
#include <QDebug>
#include <QtGui/qguiapplication_platform.h>

#include <cstring>
#include <wayland-client.h>
#include "relative-pointer-unstable-v1-client-protocol.h"

namespace ravenspot {

RelativePointer::RelativePointer(QObject *parent)
    : QObject(parent)
{
}

RelativePointer::~RelativePointer() {
    if (m_ownPointer) wl_pointer_release(m_ownPointer);
    if (m_relPointer) zwp_relative_pointer_v1_destroy(m_relPointer);
    if (m_manager) zwp_relative_pointer_manager_v1_destroy(m_manager);
}

void RelativePointer::onRegistryGlobal(void *data, wl_registry *registry, uint32_t name,
                                       const char *interface, uint32_t version) {
    Q_UNUSED(version);
    auto *self = static_cast<RelativePointer *>(data);
    if (!self->m_manager && std::strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0) {
        self->m_manager = static_cast<zwp_relative_pointer_manager_v1 *>(
            wl_registry_bind(registry, name, &zwp_relative_pointer_manager_v1_interface, 1));
    }
}

void RelativePointer::onRegistryGlobalRemove(void *, wl_registry *, uint32_t) {}

void RelativePointer::onRelativeMotion(void *data, zwp_relative_pointer_v1 *,
                                       uint32_t, uint32_t,
                                       int32_t dx, int32_t dy, int32_t, int32_t) {
    auto *self = static_cast<RelativePointer *>(data);
    emit self->motion(wl_fixed_to_double(dx), wl_fixed_to_double(dy));
}

namespace {
// wl_pointer sürümüne göre gelebilecek tüm olaylar için boş işleyiciler
// (libwayland, NULL işleyiciye olay gelirse istemciyi sonlandırır).
void noEnter(void *, wl_pointer *, uint32_t, wl_surface *, wl_fixed_t, wl_fixed_t) {}
void noLeave(void *, wl_pointer *, uint32_t, wl_surface *) {}
void noMotion(void *, wl_pointer *, uint32_t, wl_fixed_t, wl_fixed_t) {}
void noAxis(void *, wl_pointer *, uint32_t, uint32_t, wl_fixed_t) {}
void noFrame(void *, wl_pointer *) {}
void noAxisSource(void *, wl_pointer *, uint32_t) {}
void noAxisStop(void *, wl_pointer *, uint32_t, uint32_t) {}
void noAxisDiscrete(void *, wl_pointer *, uint32_t, int32_t) {}
void noAxisValue120(void *, wl_pointer *, uint32_t, int32_t) {}
void noAxisRelDir(void *, wl_pointer *, uint32_t, uint32_t) {}
void noWarp(void *, wl_pointer *, wl_fixed_t, wl_fixed_t) {}
constexpr uint32_t kBtnLeft = 0x110; // linux/input-event-codes.h BTN_LEFT
} // namespace

void RelativePointer::onPointerButton(void *data, wl_pointer *, uint32_t, uint32_t,
                                      uint32_t button, uint32_t state) {
    if (button != kBtnLeft) return;
    auto *self = static_cast<RelativePointer *>(data);
    if (state == WL_POINTER_BUTTON_STATE_RELEASED) emit self->leftButtonReleased();
    else emit self->leftButtonPressed();
}

bool RelativePointer::bindManager() {
    auto *waylandApp = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
    if (!waylandApp || !waylandApp->display()) return false;
    wl_display *display = waylandApp->display();

    // Registry'yi özel bir kuyrukta dolaş: Qt'nin varsayılan kuyruğunu
    // re-entrant roundtrip ile dağıtmamak için.
    wl_event_queue *queue = wl_display_create_queue(display);
    auto *wrapped = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapped), queue);

    wl_registry *registry = wl_display_get_registry(wrapped);
    static const wl_registry_listener listener = {
        .global = &RelativePointer::onRegistryGlobal,
        .global_remove = &RelativePointer::onRegistryGlobalRemove,
    };
    wl_registry_add_listener(registry, &listener, this);
    wl_display_roundtrip_queue(display, queue);

    // Manager'ı varsayılan kuyruğa taşı: ondan türeyen relative pointer
    // olayları Qt'nin GUI thread dağıtımıyla gelsin.
    if (m_manager) {
        wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(m_manager), nullptr);
    }

    wl_registry_destroy(registry);
    wl_proxy_wrapper_destroy(wrapped);
    wl_event_queue_destroy(queue);

    if (!m_manager) {
        qWarning() << "RelativePointer: compositor does not support zwp_relative_pointer_manager_v1";
    }
    return m_manager != nullptr;
}

bool RelativePointer::ensureReady() {
    if (!m_triedBind) {
        m_triedBind = true;
        bindManager();
    }
    if (!m_manager) return false;

    auto *waylandApp = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    wl_pointer *pointer = waylandApp ? waylandApp->pointer() : nullptr;
    if (!pointer) return false;

    // Seat yeniden yapılandırılırsa (fare çıkarılıp takılırsa) yeniden bağlan.
    if (m_relPointer && m_boundPointer != pointer) {
        zwp_relative_pointer_v1_destroy(m_relPointer);
        m_relPointer = nullptr;
    }
    if (!m_relPointer) {
        static const zwp_relative_pointer_v1_listener listener = {
            .relative_motion = &RelativePointer::onRelativeMotion,
        };
        m_relPointer = zwp_relative_pointer_manager_v1_get_relative_pointer(m_manager, pointer);
        zwp_relative_pointer_v1_add_listener(m_relPointer, &listener, this);
        m_boundPointer = pointer;
        wl_display_flush(waylandApp->display());
    }
    if (!m_ownPointer && waylandApp->seat()) {
        static const wl_pointer_listener buttonListener = {
            .enter = noEnter,
            .leave = noLeave,
            .motion = noMotion,
            .button = &RelativePointer::onPointerButton,
            .axis = noAxis,
            .frame = noFrame,
            .axis_source = noAxisSource,
            .axis_stop = noAxisStop,
            .axis_discrete = noAxisDiscrete,
            .axis_value120 = noAxisValue120,
            .axis_relative_direction = noAxisRelDir,
            .warp = noWarp,
        };
        m_ownPointer = wl_seat_get_pointer(waylandApp->seat());
        wl_pointer_add_listener(m_ownPointer, &buttonListener, this);
        wl_display_flush(waylandApp->display());
    }
    return true;
}

} // namespace ravenspot
