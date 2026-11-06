import QtQuick
import QtTest
import "../.."

TestCase {
    name: "OutputPanel"
    when: windowShown
    width: 495; height: 500

    QtObject {
        id: fakeRunner
        property bool running: true
        property string output: "line1\nline2"
        property string command: "ls -la /etc\necho done"
        property int exitCode: 0
        property bool stopped: false
        function stop() { stopped = true }
    }

    CommandOutputPanel { id: panel; width: 495; height: 400; runner: fakeRunner }

    SignalSpy { id: copySpy; target: panel; signalName: "copyRequested" }
    SignalSpy { id: closeSpy; target: panel; signalName: "closeRequested" }

    function test_showsRunningState() {
        fakeRunner.running = true
        compare(panel.statusText, "Running…")
        compare(panel.titleText, "ls -la /etc")
        fakeRunner.running = false
        fakeRunner.exitCode = 2
        compare(panel.statusText, "Exit 2")
        fakeRunner.running = true
    }
    function test_stopCallsRunner() {
        fakeRunner.stopped = false
        panel.requestStop()
        verify(fakeRunner.stopped)
    }
    function test_copyUsesOutput() {
        copySpy.clear()
        panel.requestCopy()
        compare(copySpy.count, 1)
        compare(copySpy.signalArguments[0][0], "line1\nline2")
    }
    function test_closeEmits() {
        closeSpy.clear()
        panel.requestClose()
        compare(closeSpy.count, 1)
    }
}
