import json
import os
import tempfile
import unittest
from tests.harness.mock_daemon import MockRavenSpotDaemon

class TestF07AudioEngine(unittest.TestCase):
    """Feature 7: Polyphonic Audio & Sound Pool (R2, AC)."""

    def setUp(self):
        self.sounds_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../sounds"))
        self.thock_path = os.path.join(self.sounds_dir, "thock.wav")
        self.creamy_path = os.path.join(self.sounds_dir, "creamy.wav")
        self.daemon = MockRavenSpotDaemon("/tmp/dummy_audio.sock")

    def test_01_sound_files_exist(self):
        """Verifies both sound assets (thock.wav and creamy.wav) exist."""
        self.assertTrue(os.path.isfile(self.thock_path), f"thock.wav missing at {self.thock_path}")
        self.assertTrue(os.path.isfile(self.creamy_path), f"creamy.wav missing at {self.creamy_path}")

    def test_02_sound_files_wav_header_valid(self):
        """Verifies sound files contain valid RIFF / WAVE format headers."""
        for path in [self.thock_path, self.creamy_path]:
            with open(path, "rb") as f:
                header = f.read(12)
                self.assertEqual(header[0:4], b"RIFF", f"{path} is missing RIFF magic bytes")
                self.assertEqual(header[8:12], b"WAVE", f"{path} is missing WAVE format identifier")

    def test_03_audio_engine_4voice_round_robin(self):
        """Verifies 4-voice round-robin cycle for thock navigation sound."""
        voices_played = []
        for _ in range(8):
            self.daemon.play_sound("thock")
            voices_played.append(self.daemon.thock_voice_index)

        expected_sequence = [1, 2, 3, 0, 1, 2, 3, 0]
        self.assertEqual(voices_played, expected_sequence)

    def test_04_audio_engine_creamy_click(self):
        """Verifies creamy.wav is triggered on click actions."""
        res = self.daemon.play_sound("creamy")
        self.assertTrue(res)
        self.assertEqual(self.daemon.last_played_sound, "creamy_voice_0")

    def test_05_audio_config_persistence(self):
        """Verifies sound volume and mute state can be serialized to sound.json format."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            config_path = os.path.join(tmp_dir, "sound.json")
            config_data = {"volume": 75, "muted": False}
            with open(config_path, "w", encoding="utf-8") as f:
                json.dump(config_data, f)

            with open(config_path, "r", encoding="utf-8") as f:
                loaded = json.load(f)
            self.assertEqual(loaded["volume"], 75)
            self.assertFalse(loaded["muted"])

if __name__ == "__main__":
    unittest.main()
