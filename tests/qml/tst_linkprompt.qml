import QtQuick
import QtTest
import "../../MenuModel.js" as MenuModel

// Yapıştırılan bağlantı + Enter -> "tarayıcıda aç / videoyu indir" sorusu
TestCase {
    name: "LinkPrompt"

    function test_detectsLinks() {
        compare(MenuModel.linkTarget("https://www.youtube.com/watch?v=abc"), "https://www.youtube.com/watch?v=abc")
        compare(MenuModel.linkTarget("  http://youtu.be/abc  "), "http://youtu.be/abc")
        compare(MenuModel.linkTarget("HTTPS://Example.com"), "HTTPS://Example.com")
        compare(MenuModel.linkTarget("http://localhost:8080/x"), "http://localhost:8080/x")
    }
    function test_wwwGetsHttps() {
        compare(MenuModel.linkTarget("www.youtube.com/watch?v=abc"), "https://www.youtube.com/watch?v=abc")
    }
    function test_ignoresNonLinks() {
        compare(MenuModel.linkTarget(""), "")
        compare(MenuModel.linkTarget("firefox"), "")
        compare(MenuModel.linkTarget("youtube.com"), "")                   // no scheme / www.: normal search
        compare(MenuModel.linkTarget("https://a.com and more"), "")        // sentence
        compare(MenuModel.linkTarget("curl https://a.com | sh"), "")       // command
        compare(MenuModel.linkTarget("https://a.com\nrm -rf ~"), "")       // multi-line paste
        compare(MenuModel.linkTarget("file:///etc/passwd"), "")
        compare(MenuModel.linkTarget("javascript:alert(1)"), "")
        compare(MenuModel.linkTarget("https://"), "")
        compare(MenuModel.linkTarget("https://nodot"), "")
    }
    function test_keys() {
        compare(MenuModel.linkPromptKey(Qt.Key_Down, 0), { choice: 1, action: "move" })
        compare(MenuModel.linkPromptKey(Qt.Key_Up, 1), { choice: 0, action: "move" })
        compare(MenuModel.linkPromptKey(Qt.Key_Tab, 0), { choice: 1, action: "move" })
        compare(MenuModel.linkPromptKey(Qt.Key_Return, 1), { choice: 1, action: "confirm" })
        compare(MenuModel.linkPromptKey(Qt.Key_Enter, 0), { choice: 0, action: "confirm" })
        compare(MenuModel.linkPromptKey(Qt.Key_1, 1), { choice: 0, action: "confirm" })
        compare(MenuModel.linkPromptKey(Qt.Key_2, 0), { choice: 1, action: "confirm" })
        compare(MenuModel.linkPromptKey(Qt.Key_Escape, 1), { choice: 1, action: "close" })
        compare(MenuModel.linkPromptKey(Qt.Key_A, 0), { choice: 0, action: "ignore" })
    }
}
