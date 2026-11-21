// MH4U for PC: the Multiplayer section's own rows on the PC (settings_panel.cpp includes this after the chat's pages,
// in its anonymous namespace; the Android build never sees it):
//   - Multiplayer > Discord: Discord Rich Presence and Show hunter name and HR (pc/pc_discord.h), as MHGU for PC's;
//   - Multiplayer > Chat: the chat key ([Controls] ChatKey, "/" as it comes), which opens the chat over the game with
//     its keyboard ready for a line (pc/pc_net.cpp), as the MHGU port's; the PC has no chat button.
// Their switches apply at once (no Save): they only change what the PC shows or sends to the Discord app on this PC.
}  // namespace
#include "pc/pc_discord.h"
#include "pc/pc_net.h"
namespace {

// A switch of mh4u-app.ini that applies at once
void pc_switch(std::vector<Row>& r, const char* id, const char* en, const char* es, const char* section, const char* key,
               bool fallback, bool enabled = true) {
    Row row;
    const bool on = app_setting_bool(section, key, fallback);
    row.id = id, row.title = en_es(en, es), row.value = tr(on ? "On" : "Off");
    row.toggle = on ? 1 : 0;
    row.enabled = enabled;
    const std::string sec = section, k = key;
    row.adjust = [sec, k](int d) {
        app_setting_set(sec.c_str(), k.c_str(), d > 0 ? "on" : "off");
        app_settings_save();
    };
    row.activate = [sec, k, fallback]() {
        app_setting_set(sec.c_str(), k.c_str(), app_setting_bool(sec.c_str(), k.c_str(), fallback) ? "off" : "on");
        app_settings_save();
    };
    r.push_back(std::move(row));
}

// ---- Multiplayer > Discord (1.0.2: the last page, after How to play online, as MHGU for PC's)
MENU_PAGE("multiplayer/discord", "Discord", "multiplayer", 100, {}, [](Row& r) {
    r.value = tr(app_setting_bool("Discord", "RichPresence", true) ? "On" : "Off");
    r.help = en_es("What you are doing, on your Discord profile, while the Discord app is open on this PC.",
                   "Lo que estás haciendo, en tu perfil de Discord, mientras la app de Discord está abierta en esta PC.");
});
void rows_multiplayer_discord_10(std::vector<Row>& r) {
    const bool on = app_setting_bool("Discord", "RichPresence", true);
    pc_switch(r, "discord.on", "Discord Rich Presence", "Discord Rich Presence", "Discord", "RichPresence", true);
    r.back().help = en_es("With the Discord app open on this PC, your profile there shows what you're doing: where you are, "
                          "the monsters you hunt, the map and area, how many hunters are with you, your weapon, and your "
                          "hunter's name and HR. The game only talks to the Discord app on your PC.",
                          "Con la app de Discord abierta en esta PC, tu perfil ahí muestra lo que estás haciendo: dónde estás, "
                          "los monstruos que cazas, el mapa y la zona, cuántos cazadores están contigo, tu arma, y el nombre "
                          "y el RC de tu cazador. El juego solo habla con la app de Discord de tu PC.");
    pc_switch(r, "discord.hunter", "Show hunter name and HR", "Mostrar nombre y RC del cazador", "Discord", "ShowHunter", true, on);
    r.back().help = en_es("Off: your profile shows where you are and your weapon, without your hunter's name and Hunter Rank.",
                          "Apagado: tu perfil muestra dónde estás y tu arma, sin el nombre ni el rango de cazador.");
    if (const std::string status = pc_discord_status(settings_spanish()); !status.empty())
        note(r, "discord.status", status, status.find("Discord no") == 0 || status.find("Discord isn") == 0 ? touch_look().dim : NET_GREEN);
}
void reset_multiplayer_discord_10() {
    app_setting_set("Discord", "RichPresence", "on");
    app_setting_set("Discord", "ShowHunter", "on");
    app_settings_save();
}
MENU_ENTRY("multiplayer/discord", 10, rows_multiplayer_discord_10, reset_multiplayer_discord_10);

// ---- the chat key (Keybinds' Chat, 1.0.2: MHGU for PC's place)
void rows_multiplayer_chat_key(std::vector<Row>& r) {
    const std::vector<std::string> keys = pc_chat_keys();
    const std::string now = app_setting("Controls", "ChatKey", "/");
    int at = -1;
    std::vector<std::string> names;
    for (size_t i = 0; i < keys.size(); i++) {
        names.push_back(pc_chat_key_label(keys[i], settings_spanish()));
        if (keys[i] == now) at = static_cast<int>(i);
    }
    add_choice(r, "chat.key", "Chat key", at, names, [keys](int i) {
        if (i < 0 || i >= static_cast<int>(keys.size())) return;
        app_setting_set("Controls", "ChatKey", keys[i]);
        app_settings_save();
    }, nullptr, pc_chat_key_label(now, settings_spanish()));
    r.back().title = en_es("Chat key", "Tecla del chat");
    r.back().help = en_es("The key that opens the chat while you play, ready to type a line: Enter sends it, Escape closes "
                          "it. In a room or a hub it talks to the room (with the room chat on).",
                          "La tecla que abre el chat mientras juegas, lista para escribir una línea: Enter la manda, Escape "
                          "la cierra. En una sala o un hub habla con la sala (con el chat de la sala activado).");
}
void reset_multiplayer_chat_key() {
    app_setting_set("Controls", "ChatKey", "/");
    app_settings_save();
}
// (1.0.2: shown in Controls > Keyboard and mouse > Keybinds, under Chat, as MHGU for PC's: settings_panel.cpp keybind_rows)

// The chat key pressed over the game (pc/pc_net.cpp, through settings_panel_type_chat): the chat's page, and its keyboard
// for a line when the chat shown can take one (the room's with the room chat on and a room joined, or the online chat on);
// Enter sends it and closes the menu, Escape goes back to the chat's page
void pc_chat_say_now() {
    if (g_chat < 0) g_chat = chat::hub_chat() && chat::hub_up() ? chat::HUB : chat::GLOBAL;
    const bool can = g_chat == chat::HUB ? chat::hub_chat() && chat::hub_up() : chat::enabled();
    if (!can) return;
    const int ch = g_chat;
    const std::string peer = g_chat_peer;
    open_keyboard(cs("Say something", "Decir algo"), "", KB_CHAT, chat::max_text(), "", [ch, peer](const std::string& typed, std::string& error) {
        std::string why;
        if (typed.empty() || chat::say(ch, peer, typed, &why)) {
            request_close = true;  // (sent: back to the game, as the key brought the player here from it)
            return true;
        }
        error = chat::explain(why);
        return false;
    });
}
