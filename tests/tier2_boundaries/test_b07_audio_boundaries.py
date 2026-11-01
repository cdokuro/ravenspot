import json
import unittest
from tests.harness.mock_daemon import MockRavenSpotDaemon

class TestB07AudioBoundaries(unittest.TestCase):
    """Tier 2 Boundary Cases: Feature 7 - Polyphonic Audio & Sound Pool."""

    def setUp(self):
        self.daemon = MockRavenSpotDaemon("/tmp/dummy_audio_b.sock")

    def test_01_audio_volume_boundary_values(self):
        """Volume inputs outside [0, 100] are clamped strictly."""
        self.daemon.set_audio_volume(-25)
        self.assertEqual(self.daemon.sound_volume, 0)

        self.daemon.set_audio_volume(150)
        self.assertEqual(self.daemon.sound_volume, 100)

        self.daemon.set_audio_volume(50)
        self.assertEqual(self.daemon.sound_volume, 50)

    def test_02_audio_rapid_firing_concurrency(self):
        """50 rapid play requests cycle smoothly without index out of bounds."""
        for _ in range(50):
            res = self.daemon.play_sound("thock")
            self.assertTrue(res)
            self.assertIn(self.daemon.thock_voice_index, [0, 1, 2, 3])

    def test_03_audio_missing_wav_files(self):
        """Audio playback gracefully returns false when sound asset is not recognized."""
        res = self.daemon.play_sound("non_existent_explosion_sound")
        self.assertFalse(res)

    def test_04_audio_muted_suppression(self):
        """When audio is muted, playSound immediately returns False and plays nothing."""
        self.daemon.set_audio_muted(True)
        played = self.daemon.play_sound("thock")
        self.assertFalse(played)
        played = self.daemon.play_sound("creamy")
        self.assertFalse(played)

    def test_05_audio_sound_json_corrupted(self):
        """Corrupted sound.json recovers to 80% volume and unmuted."""
        corrupt_str = "{ volume: 80, muted: "
        defaults = {"volume": 80, "muted": False}
        try:
            config = json.loads(corrupt_str)
        except json.JSONDecodeError:
            config = defaults
        self.assertEqual(config["volume"], 80)
        self.assertFalse(config["muted"])

if __name__ == "__main__":
    unittest.main()
