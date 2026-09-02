// dllmain.cpp
//
// A Discord Rich Presence DLL. This talks to the Discord app running on
// the same PC over a local named pipe, and tells it what to show as your
// "activity" status. Designed to be as easy to use as possible. 
//
// I used a simpler version of this for DARKHAND PROLOGUE, but decided
// I might as well get this done more properly as a public service.
// Why's this version more flexible? Instead of hardcoding a fixed list
// of fields (details, state, image...), you just hand it a JSON string
// with whatever fields you want, and it forwards that straight to
// Discord. That means it automatically supports everything Discord's
// Rich Presence supports (timestamps, buttons, party size, secrets,
// clickable links, etc.) without ever needing to edit this file again.
// Unless Discord decides they want to "fix" what isn't broken again.
// Either way, this should be compatible with like, everything ever.
// GameMaker was kind of pissy with strings but this is just one DLL and 
// you pretty much just pass one string at a time really. If this doesn't
// work with whatever you're using, I'd be curious to know what you're using.
//
// Official Docs: https://docs.discord.com/developers/topics/rpc#set_activity
//
// What you get:
//  drp_init(app_id)                    - connect to Discord, do the handshake
//  drp_set_activity(json)              - set your Rich Presence to whatever JSON you pass in
//  drp_clear_activity()                - remove the Rich Presence
//  drp_respond_join(user_id, accept)   - accept or reject an "Ask to Join" request
//  drp_send_raw(json)                  - send ANY Discord RPC command, for advanced use
//  drp_poll()                          - check for messages coming back from Discord (non-blocking)
//  drp_is_connected()                  - are we still connected?
//  drp_shutdown()                      - disconnect and clean up
//
// Example of what you'd pass into drp_set_activity:
//  {   
//      "details":"Line 1","state":"Line 2",
//      "timestamps":{"start":1103700000},
//      "assets":{"large_image":"bigass_icon", "large_text" : "Hovering the bigass icon!",
//      "small_image" : "smallass_icon", "small_text" : "Hovering the smallass icon!"},
//      "party":{"id":"party1","size":[1,4]},
//      "buttons":[{"label":"Wishlist","url":"https://store.steampowered.com/app/4121110/DARKHAND/"}]
//  }
//
// drp_poll() doesn't block/freeze your software waiting for a message. It just
// checks "is anything here right now?" and returns immediately either way.
// If something did come in, you get back a single JSON string like:
//   {"opcode":1,"payload":{...whatever Discord sent...}}
// Just json_parse() that and read .opcode / .payload off it.
// Ping messages from Discord are handled automatically
// behind the scenes, so you'll never see those.

#include "pch.h"
#include <windows.h>
#include <string>
#include <cstdint>
#include <atomic>

static HANDLE g_pipe = INVALID_HANDLE_VALUE;
static std::string g_app_id;
static DWORD g_pid = 0;
static std::atomic<uint64_t> g_nonce_counter{ 0 };
static std::string g_poll_buffer; // holds whatever drp_poll() last returned

// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
// 
// You shouldn't need to touch anything in the section below.
// 
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

// Sends one message to Discord. Every message starts with a small header
// (what kind of message it is + how long it is), followed by the actual
// text.
static bool write_frame(uint32_t opcode, const std::string& payload)
{
    if (g_pipe == INVALID_HANDLE_VALUE) return false;

    uint32_t header[2] = { opcode, (uint32_t)payload.size() };
    DWORD written = 0;

    if (!WriteFile(g_pipe, header, sizeof(header), &written, NULL)) return false;
    if (written != sizeof(header)) return false;

    written = 0;

    if (!payload.empty())
    {
        if (!WriteFile(g_pipe, payload.data(), (DWORD)payload.size(), &written, NULL)) return false;
        if (written != payload.size()) return false;
    }

    return true;
}

// Waits (freezes) until one full message arrives. We only do this once,
// right after connecting, because Discord replies to the handshake
// almost instantly. Everywhere else we use the non-blocking version
// below so we never freeze the software.
static bool read_frame_blocking(uint32_t& opcode, std::string& payload)
{
    uint32_t header[2];
    DWORD bytesRead = 0;

    if (!ReadFile(g_pipe, header, sizeof(header), &bytesRead, NULL)) return false;
    if (bytesRead != sizeof(header)) return false;

    opcode = header[0];
    uint32_t length = header[1];

    payload.resize(length);

    if (length > 0)
    {
        bytesRead = 0;
        if (!ReadFile(g_pipe, &payload[0], length, &bytesRead, NULL)) return false;
        if (bytesRead != length) return false;
    }

    return true;
}

// Checks "Has a full message arrived yet?" without waiting around.
// Returns 1 if it read a message, 0 if there's nothing to read yet,
// -1 if something went wrong (ex: Discord closed the connection).
static int try_read_frame(uint32_t& opcode, std::string& payload)
{
    if (g_pipe == INVALID_HANDLE_VALUE) return -1;

    DWORD bytesAvail = 0;
    if (!PeekNamedPipe(g_pipe, NULL, 0, NULL, &bytesAvail, NULL))
        return -1;

    if (bytesAvail < sizeof(uint32_t) * 2)
        return 0; // nothing here yet, try again later

    uint32_t header[2];
    DWORD bytesRead = 0;

    if (!ReadFile(g_pipe, header, sizeof(header), &bytesRead, NULL)) return -1;
    if (bytesRead != sizeof(header)) return -1;

    opcode = header[0];
    uint32_t length = header[1];

    // Grab the rest of the message. Usually it all shows up at once, but
    // just in case it trickles in, keep reading until we have it all.
    payload.resize(length);
    DWORD totalRead = 0;

    while (totalRead < length)
    {
        DWORD chunk = 0;
        if (!ReadFile(g_pipe, &payload[totalRead], length - totalRead, &chunk, NULL))
            return -1;
        if (chunk == 0) return -1;
        totalRead += chunk;
    }

    return 1;
}

// Discord listens on one of 10 possible pipe names. 
// We gotta try each until one works.
static bool connect_pipe()
{
    for (int i = 0; i < 10; i++)
    {
        std::string pipe_name = "\\\\?\\pipe\\discord-ipc-" + std::to_string(i);

        g_pipe = CreateFileA(
            pipe_name.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, 0, NULL
        );

        if (g_pipe != INVALID_HANDLE_VALUE)
            return true;
    }

    return false;
}

// Discord wants every command to have a unique ID attached ("nonce") so
// it can match up replies. This just counts up: n0, n1, n2...
static std::string next_nonce()
{
    uint64_t n = g_nonce_counter.fetch_add(1);
    return "n" + std::to_string(n);
}

// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
// 
// Now, the actual functions you'll wanna call from your software.
// 
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

// Connects to Discord and says hello. Call this once at startup.
// Returns 1 on success, 0 if it failed (ex: Discord isn't running)
// Remember, you need to pass your Discord Developer Portal App ID!
extern "C" __declspec(dllexport)
double __cdecl drp_init(const char* app_id)
{
    if (!app_id) return 0.0;

    g_app_id = app_id;
    g_pid = GetCurrentProcessId();

    if (!connect_pipe())
        return 0.0;

    std::string handshake = "{\"v\":1,\"client_id\":\"" + g_app_id + "\"}";

    if (!write_frame(0, handshake))
        return 0.0;

    uint32_t op = 0;
    std::string response;

    if (!read_frame_blocking(op, response))
        return 0.0;

    return 1.0;
}

// Sets what shows up as your Rich Presence. Pass in a JSON string
// describing it. I trust you to build it correctly on your end so no checks.
// If you did, it should just work with whatever fields you included.
extern "C" __declspec(dllexport)
double __cdecl drp_set_activity(const char* activity_json)
{
    if (!activity_json) return 0.0;
    if (g_pipe == INVALID_HANDLE_VALUE) return 0.0;

    std::string payload =
        "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(g_pid) +
        ",\"activity\":" + activity_json + "},\"nonce\":\"" + next_nonce() + "\"}";

    return write_frame(1, payload) ? 1.0 : 0.0;
}

// Clears your Rich Presence, so nothing shows.
extern "C" __declspec(dllexport)
double __cdecl drp_clear_activity()
{
    if (g_pipe == INVALID_HANDLE_VALUE) return 0.0;

    std::string payload =
        "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(g_pid) +
        ",\"activity\":null},\"nonce\":\"" + next_nonce() + "\"}";

    return write_frame(1, payload) ? 1.0 : 0.0;
}

// Someone asked to join your party via "Ask to Join"
// call this to say yes (accept = 1) or no (accept = 0).
// I am not going to set up a way to test this, so...
// I hope for your sake that it works. If not, sorry.
extern "C" __declspec(dllexport)
double __cdecl drp_respond_join(const char* user_id, double accept)
{
    if (!user_id) return 0.0;
    if (g_pipe == INVALID_HANDLE_VALUE) return 0.0;

    const char* cmd = (accept != 0.0) ? "SEND_ACTIVITY_JOIN_INVITE" : "CLOSE_ACTIVITY_REQUEST";

    std::string payload =
        std::string("{\"cmd\":\"") + cmd + "\",\"args\":{\"user_id\":\"" + user_id +
        "\"},\"nonce\":\"" + next_nonce() + "\"}";

    return write_frame(1, payload) ? 1.0 : 0.0;
}

// For anything not covered above: send Discord any RPC command you want
// (like SUBSCRIBE, GET_GUILDS, and so on). Just build the whole message
// yourself, including "cmd", "args", and ideally your own "nonce".
// I haven't tested or even read about anything other than basic RP, though.
extern "C" __declspec(dllexport)
double __cdecl drp_send_raw(const char* full_command_json)
{
    if (!full_command_json) return 0.0;
    if (g_pipe == INVALID_HANDLE_VALUE) return 0.0;

    return write_frame(1, full_command_json) ? 1.0 : 0.0;
}

// Checks if Discord sent anything back. You can call this regularly with
// no interruptions, it'll either give you something or come back empty.
//
// Returns "" if nothing arrived.
// Otherwise returns one JSON string: {"opcode":N,"payload":...}
// where "payload" is whatever Discord sent, dropped in as-is, 
// wrapped together with the opcode so you only have to parse one thing).
extern "C" __declspec(dllexport)
const char* __cdecl drp_poll()
{
    g_poll_buffer.clear();

    for (;;)
    {
        uint32_t opcode = 0;
        std::string payload;

        int result = try_read_frame(opcode, payload);

        if (result <= 0)
            break; // nothing to read, or something broke; either way, stop here

        if (opcode == 3) // Discord is just checking we haven't been shot in the head; answer and keep going
        {
            write_frame(4, payload);
            continue;
        }

        if (opcode == 2) // Discord closed the connection
        {
            CloseHandle(g_pipe);
            g_pipe = INVALID_HANDLE_VALUE;
        }

        // payload is either empty (some events carry no data) or already
        // a valid JSON value, so we can just drop it straight in.
        std::string payload_json = payload.empty() ? "null" : payload;
        g_poll_buffer = "{\"opcode\":" + std::to_string(opcode) + ",\"payload\":" + payload_json + "}";
        break;
    }

    return g_poll_buffer.c_str();
}

// Quick check: are we still connected to Discord?
extern "C" __declspec(dllexport)
double __cdecl drp_is_connected()
{
    return (g_pipe != INVALID_HANDLE_VALUE) ? 1.0 : 0.0;
}

// Disconnects cleanly. Call this when your software closes.
extern "C" __declspec(dllexport)
double __cdecl drp_shutdown()
{
    if (g_pipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }

    return 1.0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    return TRUE;
}