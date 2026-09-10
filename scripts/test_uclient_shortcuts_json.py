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

                {"type": "send_chat", "channel": "all", "message": {"mode": "text", "text": "hello"}},

                {"type": "wait", "seconds": 1},

                {"type": "switch_weapon_use", "weapon": "hammer"},

                {"type": "set_skin", "target": "player", "skin": "default"},

                {"type": "set_custom_color", "target": "player", "enabled": True},

                {"type": "set_body_color", "target": "player", "color": 13827960},

                {"type": "set_feet_color", "target": "player", "color": 255},

                {"type": "set_name", "target": "player", "name": "under"},

            ],

        },

        {

            "id": "server-connect-id",

            "name": "Join greeting",

            "enabled": True,

            "trigger": {

                "type": "server_connect",

                "targets": ["127.0.0.1:8303", "ddnet.org"],

            },

            "actions": [

                {"type": "send_chat", "channel": "all", "message": {"mode": "text", "text": "hello server"}},

            ],

        },

        {

            "id": "auto-reply-tabbed-out",

            "name": "Auto reply when tabbed out",

            "enabled": True,

            "trigger": {

                "type": "chat_received",

                "channel": "all",

                "filters": [

                    {"kind": "message", "match": "contains", "text": "ping"},

                ],

            },

            "actions": [

                {"type": "get", "property": "window_active"},

                {"type": "if", "left": "window_active", "op": "contains", "right": "Chrome"},

                {"type": "send_chat", "channel": "all", "message": {"mode": "text", "text": "I am browsing"}},

                {"type": "stop"},

                {"type": "end_if"},

            ],

        },

        {

            "id": "auto-greeting",

            "name": "Auto greeting",

            "enabled": True,

            "trigger": {

                "type": "chat_received",

                "channel": "all",

                "filters": [

                    {"kind": "message", "match": "contains", "text": "hello"},

                ],

            },

            "actions": [

                {

                    "type": "text",

                    "as": "text",

                    "parts": [

                        {"mode": "variable", "variable": "senderName"},

                        {"mode": "text", "text": ": hi"},

                    ],

                },

                {

                    "type": "send_chat",

                    "channel": "all",

                    "message": {"mode": "variable", "variable": "text"},

                },

            ],

        },

        {

            "id": "connect-leave",

            "name": "Connect and leave",

            "enabled": False,

            "trigger": {

                "type": "chat_received",

                "channel": "all",

                "filters": [

                    {"kind": "message", "match": "equals", "text": "go"},

                ],

            },

            "actions": [

                {"type": "connect_server", "address": "127.0.0.1:8303"},

                {"type": "leave_server"},

            ],

        },

    ],

}





class ShortcutsJsonTests(unittest.TestCase):

    def test_roundtrip(self):

        raw = json.dumps(SAMPLE, separators=(",", ":"))

        parsed = json.loads(raw)

        self.assertEqual(parsed["version"], 1)

        self.assertEqual(len(parsed["shortcuts"]), 5)

        sc = parsed["shortcuts"][0]

        self.assertTrue(sc["enabled"])

        self.assertEqual(sc["trigger"]["type"], "chat_received")

        self.assertEqual(len(sc["actions"]), 8)

        self.assertEqual(sc["actions"][0]["message"]["mode"], "text")

        server_sc = parsed["shortcuts"][1]

        self.assertEqual(server_sc["trigger"]["type"], "server_connect")

        self.assertEqual(server_sc["trigger"]["targets"], ["127.0.0.1:8303", "ddnet.org"])

        flow_sc = parsed["shortcuts"][2]

        self.assertEqual(flow_sc["actions"][0]["type"], "get")

        self.assertEqual(flow_sc["actions"][1]["op"], "contains")

        self.assertEqual(flow_sc["actions"][1]["right"], "Chrome")

        greet_sc = parsed["shortcuts"][3]

        self.assertEqual(greet_sc["actions"][0]["type"], "text")

        self.assertEqual(greet_sc["actions"][1]["message"]["variable"], "text")

        conn_sc = parsed["shortcuts"][4]

        self.assertEqual(conn_sc["actions"][0]["type"], "connect_server")

        self.assertEqual(conn_sc["actions"][1]["type"], "leave_server")



    def test_disabled_shortcut(self):

        doc = json.loads(json.dumps(SAMPLE))

        doc["shortcuts"][0]["enabled"] = False

        self.assertFalse(doc["shortcuts"][0]["enabled"])





if __name__ == "__main__":

    unittest.main()

