import QtQuick

// Açılış/kapanış eğrisi: Behavior'ın HEDEF değerine göre seçilir.
// (Eğriyi root.opened'a bağlamak işe yaramaz: Behavior, binding'ler yeni
// durumu okumadan başlar ve bir önceki durumun eğrisini kullanırdı.)
// Kullanım: Behavior on opacity { id: b; OpenCloseAnimation { behavior: b; threshold: 0.5 } }
NumberAnimation {
    required property Behavior behavior
    // targetValue bu eşiğin üstündeyse "açılış" sayılır
    property real threshold: 0.5
    readonly property bool opening: behavior.targetValue > threshold

    duration: opening ? 180 : 120
    easing.type: opening ? Easing.OutQuint : Easing.InCubic
}
