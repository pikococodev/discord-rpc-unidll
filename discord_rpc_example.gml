/// Discord Rich Presence wrapper for GameMaker.
/// Pairs with dll, drop it into your project's Included Files.
///
/// Example Usage:
///
///     drp_init("YOUR_DISCORD_APP_ID");
///
///     var _activity =
///     {
///         details:"Line 1", state:"Line 2",
///         timestamps:{start: 1103700000},
///         assets:
///         {
///             large_image:"bigass_icon", large_text:"Hovering the bigass icon!",
///             small_image:"smallass_icon", small_text:"Hovering the smallass icon!"
///         },
///         party: { id: "party_" + string(id), size: [1, 4] },
///         buttons: [ { label: "Wishlist DARKHAND", url: "https://store.steampowered.com/app/4121110/DARKHAND/"}]
///     }
///     drp_set_activity(json_stringify(_activity));
///
/// Call drp_poll() regularly (ex: in a persistent controller object) to catch any events in a timely manner.

#macro DRP_DLL "discord_rpc.dll"

function drp_init(_app_id)
{
    static _fn = external_define(DRP_DLL, "drp_init", dll_cdecl, ty_real, 1, ty_string);
    return external_call(_fn, _app_id);
}

/// _activity_json: a JSON string for the activity object, build it with json_stringify(struct)
/// Any fields Discord supports work, this wrapper doesn't restrict you to a fixed set.
function drp_set_activity(_activity_json)
{
    static _fn = external_define(DRP_DLL, "drp_set_activity", dll_cdecl, ty_real, 1, ty_string);
    return external_call(_fn, _activity_json);
}

function drp_clear_activity()
{
    static _fn = external_define(DRP_DLL, "drp_clear_activity", dll_cdecl, ty_real, 0);
    return external_call(_fn);
}

/// _accept: true to approve an "Ask to Join" request, false to reject it.
function drp_respond_join(_user_id, _accept)
{
    static _fn = external_define(DRP_DLL, "drp_respond_join", dll_cdecl, ty_real, 2, ty_string, ty_real);
    return external_call(_fn, _user_id, _accept ? 1 : 0);
}

/// Blank check for any other RPC command (SUBSCRIBE to an event, etc).
/// _command_json must include "cmd" and "args" (and ideally "nonce")
function drp_send_raw(_command_json)
{
    static _fn = external_define(DRP_DLL, "drp_send_raw", dll_cdecl, ty_real, 1, ty_string);
    return external_call(_fn, _command_json);
}

/// Returns "" if nothing arrived, otherwise a JSON string like
/// {"opcode":N,"payload":...}. Just json_parse() it.
function drp_poll()
{
    static _fn = external_define(DRP_DLL, "drp_poll", dll_cdecl, ty_string, 0);
    return external_call(_fn);
}

function drp_is_connected()
{
    static _fn = external_define(DRP_DLL, "drp_is_connected", dll_cdecl, ty_real, 0);
    return external_call(_fn);
}

function drp_shutdown()
{
    static _fn = external_define(DRP_DLL, "drp_shutdown", dll_cdecl, ty_real, 0);
    return external_call(_fn);
}
