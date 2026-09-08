#!/usr/bin/env python3
"""Sanity checks for uclient_shortcuts.json schema used by launcher + client."""

import json
import unittest


SAMPLE = {
    "version": 1,
    "shortcuts": [
        {
            "id": "test-id",
            "name": "Reply hi",
            "enabled": True,
            "trigger": {
                "type": "chat_received",
                "channel": "all",
                "sender": "everyone",
                "senderName": "",
                "match": "contains",
                "text": "hi",
            },
            "actions": [
                {"type": "send_chat", "channel": "all", "message": "hello"},
                {"type": "wait", "seconds": 1},
                {"type": "switch_weapon_use", "weapon": "hammer"},
                {"type": "set_skin", "target": "player", "skin": "default"},
                {"type": "set_custom_color", "target": "player", "enabled": True},
                {"type": "set_body_color", "target": "player", "color": 13827960},
                {"type": "set_feet_color", "target": "player", "color": 255},
                {"type": "set_name", "target": "player", "name": "under"},
            ],
        }
    ],
}


class ShortcutsJsonTests(unittest.TestCase):
    def test_roundtrip(self):
        raw = json.dumps(SAMPLE, separators=(",", ":"))
        parsed = json.loads(raw)
        self.assertEqual(parsed["version"], 1)
        self.assertEqual(len(parsed["shortcuts"]), 1)
        sc = parsed["shortcuts"][0]
        self.assertTrue(sc["enabled"])
        self.assertEqual(sc["trigger"]["type"], "chat_received")
        self.assertEqual(len(sc["actions"]), 8)

    def test_disabled_shortcut(self):
        doc = json.loads(json.dumps(SAMPLE))
        doc["shortcuts"][0]["enabled"] = False
        self.assertFalse(doc["shortcuts"][0]["enabled"])


if __name__ == "__main__":
    unittest.main()
