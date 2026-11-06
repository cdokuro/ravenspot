import QtQuick
import QtTest
import "../.."

// OpenCloseAnimation: Behavior'ın hedef değerine göre açılışta 180 ms OutQuint,
// kapanışta 120 ms InCubic. (root.opened'a bağlanınca Behavior bir önceki
// durumun eğrisini kullanıyordu: açılış yavaş başlayıp sonda sıçrıyordu.)
TestCase {
    name: "OpenClose"
    when: windowShown
    width: 100; height: 100

    Item {
        id: box
        opacity: 0
        Behavior on opacity { id: ob; OpenCloseAnimation { behavior: ob; threshold: 0.5 } }
    }

    function test_openIsFastStartOutQuint() {
        box.opacity = 0
        wait(250)
        box.opacity = 1
        wait(60)
        // OutQuint 180 ms: ~60 ms'de değer > 0.7 (InCubic 120 ms olsaydı ~0.12)
        verify(box.opacity > 0.7, "open should start fast, got " + box.opacity)
        wait(200)
        compare(box.opacity, 1)
    }

    function test_closeIsInCubic120() {
        box.opacity = 1
        wait(250)
        box.opacity = 0
        wait(30)
        // InCubic 120 ms: ~30 ms'de değer hâlâ > 0.9 (OutQuint 180 ms olsaydı ~0.5)
        verify(box.opacity > 0.9, "close should ease in, got " + box.opacity)
        wait(160)
        compare(box.opacity, 0)
    }
}
