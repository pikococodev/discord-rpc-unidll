"""
Python (ctypes) wrapper for discord_rpc_unidll. Windows only.
I REPEAT. WINDOWS ONLY. You never know who might get here...

Place it next to this script, or pass a full path to DiscordRPC (dll_path=...)

Example usage:

    import time
    from discord_rpc import DiscordRPC

    rpc = DiscordRPC()
    rpc.init("YOUR_DISCORD_APP_ID")

    rpc.set_activity
    (
        {
            "details":"Line 1","state":"Line 2",
            "timestamps":{"start":1103700000},
            "assets":{"large_image":"bigass_icon","large_text":"Hovering the bigass icon!",
            "small_image":"smallass_icon","small_text":"Hovering the smallass icon!"},
            "timestamps": {"start": int(time.time())},
            "party":{"id":"party1","size":[1,4]},
            "buttons":[{"label":"Wishlist","url":"https://store.steampowered.com/app/4121110/DARKHAND/"}]}
        }
    )

    while True:
        event = rpc.poll()
        if event:
            print("event:", event)
        time.sleep(1)
"""

import ctypes
import json
import os


class DiscordRPC:
    def __init__(self, dll_path: str = "discord_rpc.dll"):
        if not os.path.isabs(dll_path):
            dll_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), dll_path)

        self._lib = ctypes.CDLL(dll_path)

        self._lib.drp_init.argtypes = [ctypes.c_char_p]
        self._lib.drp_init.restype = ctypes.c_double

        self._lib.drp_set_activity.argtypes = [ctypes.c_char_p]
        self._lib.drp_set_activity.restype = ctypes.c_double

        self._lib.drp_clear_activity.argtypes = []
        self._lib.drp_clear_activity.restype = ctypes.c_double

        self._lib.drp_respond_join.argtypes = [ctypes.c_char_p, ctypes.c_double]
        self._lib.drp_respond_join.restype = ctypes.c_double

        self._lib.drp_send_raw.argtypes = [ctypes.c_char_p]
        self._lib.drp_send_raw.restype = ctypes.c_double

        self._lib.drp_poll.argtypes = []
        self._lib.drp_poll.restype = ctypes.c_char_p  # important: not c_int!

        self._lib.drp_is_connected.argtypes = []
        self._lib.drp_is_connected.restype = ctypes.c_double

        self._lib.drp_shutdown.argtypes = []
        self._lib.drp_shutdown.restype = ctypes.c_double

    def init(self, app_id: str) -> bool:
        return self._lib.drp_init(app_id.encode("utf-8")) == 1.0

    def set_activity(self, activity: dict) -> bool:
        payload = json.dumps(activity).encode("utf-8")
        return self._lib.drp_set_activity(payload) == 1.0

    def clear_activity(self) -> bool:
        return self._lib.drp_clear_activity() == 1.0

    def respond_join(self, user_id: str, accept: bool) -> bool:
        return self._lib.drp_respond_join(user_id.encode("utf-8"), 1.0 if accept else 0.0) == 1.0

    def send_raw(self, command: dict) -> bool:
        payload = json.dumps(command).encode("utf-8")
        return self._lib.drp_send_raw(payload) == 1.0

    def poll(self):
        """Returns None if nothing arrived, else (opcode: int, payload: dict|None)."""
        raw = self._lib.drp_poll()
        if not raw:
            return None

        parsed = json.loads(raw.decode("utf-8"))
        return parsed["opcode"], parsed["payload"]

    def is_connected(self) -> bool:
        return self._lib.drp_is_connected() == 1.0

    def shutdown(self) -> bool:
        return self._lib.drp_shutdown() == 1.0