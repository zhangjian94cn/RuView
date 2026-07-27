"""Tests for provision.py's additive-by-default merge behaviour (#391, #574)."""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
import unittest

# Allow `python -m unittest` from anywhere in the repo.
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import provision  # noqa: E402  — sibling import after sys.path tweak


def _mk_args(**overrides) -> argparse.Namespace:
    """Build a Namespace with every mergeable attr set to None unless overridden."""
    base = {name: None for name in provision.MERGEABLE_ATTRS}
    base.update(clear_tdm=False, clear_filter_mac=False)
    base.update(overrides)
    return argparse.Namespace(**base)


class TestStateFile(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="provision-state-")

    def tearDown(self):
        import shutil
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_load_state_empty_when_missing(self):
        self.assertEqual(provision.load_state("COM7", self.dir), {})

    def test_save_then_load_roundtrip(self):
        provision.save_state("COM7", self.dir, {"ssid": "x", "password": "y"})
        self.assertEqual(
            provision.load_state("COM7", self.dir),
            {"ssid": "x", "password": "y"},
        )

    def test_save_creates_per_port_files(self):
        provision.save_state("COM7", self.dir, {"ssid": "a"})
        provision.save_state("/dev/ttyUSB0", self.dir, {"ssid": "b"})
        self.assertEqual(provision.load_state("COM7", self.dir), {"ssid": "a"})
        self.assertEqual(provision.load_state("/dev/ttyUSB0", self.dir), {"ssid": "b"})

    def test_load_state_handles_corrupt_json(self):
        path = provision._state_path_for("COM7", self.dir)
        os.makedirs(self.dir, exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write("{not valid json")
        # Should warn but not raise.
        self.assertEqual(provision.load_state("COM7", self.dir), {})


class TestMerge(unittest.TestCase):
    def test_cli_wins_over_prior(self):
        args = _mk_args(ssid="new-ssid")
        prior = {"ssid": "old-ssid", "password": "abc"}
        merged = provision.merge_state_into_args(args, prior)
        self.assertEqual(args.ssid, "new-ssid")  # CLI value preserved
        self.assertEqual(args.password, "abc")    # filled from prior
        self.assertEqual(merged["ssid"], "new-ssid")
        self.assertEqual(merged["password"], "abc")

    def test_prior_fills_missing_cli(self):
        args = _mk_args()  # all None
        prior = {
            "ssid": "MyWiFi",
            "password": "secret",
            "target_ip": "192.168.1.20",
            "node_id": 3,
        }
        merged = provision.merge_state_into_args(args, prior)
        self.assertEqual(args.ssid, "MyWiFi")
        self.assertEqual(args.password, "secret")
        self.assertEqual(args.target_ip, "192.168.1.20")
        self.assertEqual(args.node_id, 3)
        for key, val in prior.items():
            self.assertEqual(merged[key], val)

    def test_partial_invocation_does_not_drop_unrelated_keys(self):
        # The exact #391 scenario: user previously provisioned WiFi, now adds
        # only --seed-url. Old behaviour wiped SSID. New behaviour keeps it.
        args = _mk_args(seed_url="http://10.1.10.236")
        prior = {
            "ssid": "ruv.net",
            "password": "<secret>",
            "target_ip": "192.168.1.20",
        }
        merged = provision.merge_state_into_args(args, prior)
        self.assertEqual(args.ssid, "ruv.net")
        self.assertEqual(args.password, "<secret>")
        self.assertEqual(args.target_ip, "192.168.1.20")
        self.assertEqual(args.seed_url, "http://10.1.10.236")
        # And the on-disk merged dict carries all four keys.
        self.assertEqual(set(merged.keys()),
                         {"ssid", "password", "target_ip", "seed_url"})

    def test_empty_prior_is_noop(self):
        args = _mk_args(ssid="x")
        merged = provision.merge_state_into_args(args, {})
        self.assertEqual(merged, {"ssid": "x"})

    def test_falsy_but_not_none_cli_value_overrides_prior(self):
        # node_id=0 is a legal value; must NOT be replaced by prior["node_id"]=5.
        args = _mk_args(node_id=0)
        prior = {"node_id": 5}
        merged = provision.merge_state_into_args(args, prior)
        self.assertEqual(args.node_id, 0)
        self.assertEqual(merged["node_id"], 0)

    def test_controlled_probe_settings_survive_partial_update(self):
        args = _mk_args(probe_transport="udp_broadcast")
        prior = {
            "probe_role": "rx",
            "probe_interval_ms": 50,
            "probe_transport": "raw_null",
            "filter_mac": "28:84:85:92:81:3c",
        }
        merged = provision.merge_state_into_args(args, prior)

        self.assertEqual(args.probe_role, "rx")
        self.assertEqual(args.probe_interval_ms, 50)
        self.assertEqual(args.probe_transport, "udp_broadcast")
        self.assertEqual(merged["filter_mac"], "28:84:85:92:81:3c")

    def test_clear_tdm_removes_values_from_args_and_state(self):
        args = _mk_args(clear_tdm=True)
        merged = provision.merge_state_into_args(
            args,
            {"tdm_slot": 1, "tdm_total": 3, "node_id": 2},
        )

        provision.apply_clear_flags(args, merged)

        self.assertIsNone(args.tdm_slot)
        self.assertIsNone(args.tdm_total)
        self.assertNotIn("tdm_slot", merged)
        self.assertNotIn("tdm_total", merged)
        self.assertEqual(merged["node_id"], 2)

    def test_clear_filter_removes_value_from_args_and_state(self):
        args = _mk_args(clear_filter_mac=True)
        merged = provision.merge_state_into_args(
            args,
            {"filter_mac": "28:84:85:92:81:3c", "node_id": 1},
        )

        provision.apply_clear_flags(args, merged)

        self.assertIsNone(args.filter_mac)
        self.assertNotIn("filter_mac", merged)
        self.assertEqual(merged["node_id"], 1)

    def test_state_redaction_masks_all_supported_secrets(self):
        redacted = provision.redact_state(
            {
                "ssid": "room",
                "password": "wifi-secret",
                "ota_psk": "ota-secret",
                "seed_token": "seed-secret",
                "node_id": 1,
            }
        )

        self.assertEqual(redacted["ssid"], "room")
        self.assertEqual(redacted["password"], "(set)")
        self.assertEqual(redacted["ota_psk"], "(set)")
        self.assertEqual(redacted["seed_token"], "(set)")
        self.assertEqual(redacted["node_id"], 1)

    def test_saved_state_permissions_are_owner_only(self):
        with tempfile.TemporaryDirectory() as state_dir:
            path = provision.save_state("COM7", state_dir, {"password": "secret"})
            self.assertEqual(os.stat(path).st_mode & 0o777, 0o600)


class TestStatePathSanitization(unittest.TestCase):
    def test_slashes_in_port_are_safe(self):
        path = provision._state_path_for("/dev/ttyUSB0", "/tmp/x")
        # Must not contain a raw slash in the basename
        self.assertNotIn("/", os.path.basename(path))

    def test_windows_com_port_is_safe(self):
        path = provision._state_path_for("COM7", "/tmp/x")
        self.assertTrue(path.endswith("COM7.json"))


if __name__ == "__main__":
    unittest.main()
