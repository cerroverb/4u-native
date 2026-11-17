// The settings menu's chat (S32) and room passwords (S35), included by settings_panel.cpp in its own namespace: the port's
// chat (chat/chat.h, after tabarong's designs PORT_CHAT.md and MATCHMAKING_KEY_GENERATOR.md) on two pages of their own,
// Chat and Friends (View::Chat, View::Friends), opened from Multiplayer (chat_entry_rows); the question before the relays
// connect the first time (View::ChatOn); the room's Generate a password and Share this room. The MHGU port has the same
// pages (runtime/settings_chat.inc), with the matchmaking key in place of the room.
// The chat's settings are its own (chat.txt beside mh4u-app.ini), applied at once: Save and leaving without it leave them
// (but Room messages over the game, [Network] Notices, as before). Its texts carry their Spanish beside them.

int g_chat = -1;  // the chat the page shows (FRIEND: with g_chat_peer, a friend's key); -1: not picked yet (the room's
std::string g_chat_peer;  // while in one, else Global)
const char* cs(const char* en, const char* es) {
    chat::set_spanish(spanish());
    return chat::t(en, es);
}
std::string chat_hosts() {  // (the relays without wss://, for the question and the help)
    std::string all;
    for (const std::string& r : chat::relays()) all += (all.empty() ? "" : ", ") + (r.rfind("wss://", 0) == 0 ? r.substr(6) : r);
    return all;
}
std::string chat_on_detail() {
    char b[2048];
    snprintf(b, sizeof b,
             cs("The online chat (Global, General and friends) connects this device to public Nostr relays that anyone can use: "
                "%s. As with any server on the internet, they can see your IP address, and anyone can read Global and "
                "General: never share anything private there. While it is on, your chat name, your game and whether you "
                "are in a hub or room can be seen by anyone, so your friends find you. Messages to a friend are encrypted "
                "on this device. The "
                "project runs no server and there is no account: you are a key made on this device (your friend code). "
                "Nothing is saved anywhere, and the chat works only while you are connected. You can turn it off or choose "
                "other relays at any time. The room's chat needs none of this.",
                "El chat en línea (Global, General y amigos) conecta este dispositivo a relés públicos de Nostr que cualquiera "
                "puede usar: %s. Como cualquier servidor de internet, ven tu dirección IP, y cualquiera puede leer Global y "
                "General: nunca compartas nada privado ahí. Mientras está activo, cualquiera puede ver tu nombre en el "
                "chat, tu juego y si estás en una sala, para que tus amigos te encuentren. Los mensajes a un amigo se "
                "cifran en este dispositivo. El "
                "proyecto no tiene servidores y no hay cuentas: eres una clave creada en este dispositivo (tu código de "
                "amigo). No se guarda nada en ningún lado, y el chat funciona solo mientras estás conectado. Puedes "
                "apagarlo o elegir otros relés cuando quieras. El chat de la sala no necesita nada de esto."),
             chat_hosts().c_str());
    return b;
}
void chat_turn_on() {
    if (chat::notice_seen()) {
        chat::set_enabled(true);
        return;
    }
    push(View::ChatOn, "dialog.cancel");
}
void chat_open(int channel, const std::string& peer) {
    g_chat = channel, g_chat_peer = peer;
    if (s.view == View::Chat) focus("chat.say");
    else push(View::Chat, "chat.say");
}
// A room shared in the chat: where to (the room's own players are already in it: friends online, General, Global)
void chat_share(int kind, const std::string& a, const std::string& b) {
    std::vector<std::string> names;
    std::vector<std::pair<int, std::string>> to;
    for (const chat::Person& p : chat::people())
        if (p.is_friend && p.mutual && p.online) names.push_back(p.name + " (" + cs("friend", "amigo") + ")"), to.push_back({chat::FRIEND, p.id});
    names.push_back(chat::channel_name(chat::GENERAL)), to.push_back({chat::GENERAL, ""});
    names.push_back(chat::channel_name(chat::GLOBAL)), to.push_back({chat::GLOBAL, ""});
    Row row;
    row.id = "chat.share", row.title = cs("Share it with", "Compartirla con");
    choices(row, names, -1, [to, kind, a, b](int i) {
        std::string why;
        if (i < 0 || i >= (int)to.size()) return;
        if (!chat::share(to[i].first, to[i].second, kind, a, b, &why)) s.error = chat::explain(why);
        else chat_open(to[i].first, to[i].second);
    });
}
// S35: a new password for the room (128 random bits made here, as MHGU's matchmaking key), copied for the friends
void chat_new_password() {
    const std::string key = chat::new_key();
    s.draft.password = key;
    const bool copied = SDL_SetClipboardText(key.c_str());
    s.error = copied ? cs("A new password, copied. Host or join the room with it, then share the room in the chat.",
                          "Una contraseña nueva, copiada. Crea o entra a la sala con ella, y comparte la sala en el chat.")
                     : cs("A new password (it could not be copied). Host or join the room with it, then share the room in the chat.",
                          "Una contraseña nueva (no se pudo copiar). Crea o entra a la sala con ella, y comparte la sala en el chat.");
    std::fprintf(stderr, "[Info] App: a new room password was generated (%s)\n", copied ? "copied" : "not copied");
}
// The room's rows: Generate a password (under Password), and Share this room while in one
void chat_password_row(std::vector<Row>& r) {
    action(r, "net.newpass", cs("Generate a password", "Generar una contraseña"), []() { chat_new_password(); });
    r.back().help = cs("A random password (32 letters and digits), copied: host a room with it, or give it to the room's host. "
                       "Then share the room in the chat so your friends join with one tap.",
                       "Una contraseña al azar (32 letras y números), copiada: crea una sala con ella, o dásela a quien la "
                       "crea. Después comparte la sala en el chat y tus amigos entran con un toque.");
}
void chat_share_room_row(std::vector<Row>& r, const rooms::Status& st) {
    std::string at = st.host + ":" + std::to_string(st.port);
    if (st.hosting) {  // (this phone's room: where the others find it, not the loopback)
        std::vector<std::string> mine = net_local_addresses();
        if (!mine.empty()) at = mine[0] + ":" + std::to_string(st.hosted_port);
    }
    const std::string password = s.draft.password;
    action(r, "net.share", cs("Share this room in the chat", "Compartir esta sala en el chat"), [at, password]() {
        if (!chat::enabled()) {
            chat_turn_on();
            return;
        }
        chat_share(chat::SHARE_ROOM, at, password);
    });
    r.back().value = at;
    r.back().help = cs("Sends the room's address and password to a friend online, or to General or Global, as a line they join "
                       "with one tap. A room on this phone is reached from the internet only with its port forwarded to it.",
                       "Manda la dirección y la contraseña de la sala a un amigo en línea, o a General o Global, como una línea "
                       "con la que entran con un toque. Una sala en este teléfono se alcanza desde internet solo con su puerto "
                       "redirigido a él.");
}
void join_room();
void chat_use_share(const chat::Line& l) {
    if (l.share != chat::SHARE_ROOM) return;
    s.draft.address = l.share_a, s.draft.password = l.share_b;
    first_page(3);  // (Multiplayer: page_ids)
    focus("net.join");
    join_room();
}

// The room's chat: a switch of its own, off as it comes (1.0), as MHGU's hub chat. On, the room's lines come here and over
// the game beside the chat button, which shows in a room; off, the port's chat takes no part in the room, and its lines show
// with Room messages over the game as in 0.1.9.3
void chat_hub_row(std::vector<Row>& r) {
    Row row;
    row.id = "chat.hub", row.title = chat::hub_chat_name();
    const bool on = chat::hub_chat();
    row.toggle = on, row.value = on ? cs("On", "Activo") : cs("Off", "Apagado");
    row.help = cs("Talk with everyone in the room you are in, emulator players too. On: their lines show here and over the game, "
                  "and in a room the chat button opens the chat. It travels with the room itself: no relay. Off: someone's line "
                  "shows with Room messages over the game, as before.",
                  "Habla con todos los de la sala en la que estás, también los de un emulador. Activo: sus líneas se ven acá y "
                  "sobre el juego, y en una sala el botón del chat abre el chat. Viaja con la sala misma: sin relés. Apagado: "
                  "la línea de alguien se ve con Mensajes de la sala sobre el juego, como antes.");
    row.activate = []() { chat::set_hub_chat(!chat::hub_chat()); };
    row.adjust = [](int d) { chat::set_hub_chat(d > 0); };
    r.push_back(std::move(row));
}
void chat_status_row(std::vector<Row>& r) {
    int up = 0, total = 0;
    const chat::Status st = chat::status(&up, &total);
    Row row;
    row.id = "chat.status", row.title = cs("Online chat", "Chat en línea");
    row.value = chat::status_text(st, up, total);
    if (st == chat::ONLINE) row.value += " - " + std::to_string(chat::online_count()) + " " + cs("online", "en línea");
    row.toggle = st == chat::UNAVAILABLE ? -1 : chat::enabled() ? 1 : 0;
    if (st == chat::NO_RELAY) row.ink = theme().warn;
    row.enabled = st != chat::UNAVAILABLE;
    row.help = std::string(cs("Global, General and your friends travel through public relays (", "Global, General y tus amigos viajan por relés públicos (")) +
               chat_hosts() + cs("), only while this is on. The room's chat needs no relay.", "), solo mientras esto está activo. El chat de la sala no necesita relés.");
    if (st == chat::NO_RELAY && !chat::status_reason().empty()) row.help += "\n" + chat::status_reason();
    row.activate = []() {
        if (chat::enabled()) chat::set_enabled(false);
        else chat_turn_on();
    };
    row.adjust = [](int d) {
        if (d > 0 && !chat::enabled()) chat_turn_on();
        if (d < 0) chat::set_enabled(false);
    };
    r.push_back(std::move(row));
    if (chat::enabled())  // (the switch alone shows: the relays' state under it)
        note(r, "chat.state", chat::status_text(st, up, total) + (st == chat::ONLINE ? ", " + std::to_string(chat::online_count()) + " " + cs("online", "en línea") : std::string()),
             st == chat::ONLINE ? NET_GREEN : st == chat::NO_RELAY ? theme().warn : touch_look().dim);
}
int g_bubble_pct = -1;
void chat_page_rows(std::vector<Row>& r) {
    cs("", "");  // (the language)
    std::vector<chat::Person> people = chat::people();
    if (g_chat < 0) g_chat = chat::hub_chat() && chat::hub_up() ? chat::HUB : chat::GLOBAL;
    {  // (a room joined since the page last showed and the online chat off: the room's chat, not the Global or General that
       // a page first seen before the room had picked, which only says to turn the online chat on; picking them again stays)
        static bool hub_was_up;
        const bool hub_now = chat::hub_chat() && chat::hub_up();
        if (hub_now && !hub_was_up && !chat::enabled() && (g_chat == chat::GLOBAL || g_chat == chat::GENERAL)) g_chat = chat::HUB;
        hub_was_up = hub_now;
    }
    bool found = g_chat != chat::FRIEND;
    for (const chat::Person& p : people)
        if (g_chat == chat::FRIEND && p.id == g_chat_peer && p.is_friend) found = true;
    if (!found) g_chat = chat::GLOBAL, g_chat_peer.clear();
    chat_hub_row(r);  // (the two switches, both off as they come)
    chat_status_row(r);
    {  // the chat shown: the room, General, Global, each friend
        std::vector<std::string> names;
        std::vector<std::pair<int, std::string>> to;
        int at = 0;
        for (int c : {chat::HUB, chat::GENERAL, chat::GLOBAL}) {
            int n = chat::unread(c);
            names.push_back(chat::channel_name(c) + (n ? " (" + std::to_string(n) + ")" : std::string()));
            to.push_back({c, ""});
            if (g_chat == c) at = (int)to.size() - 1;
        }
        for (const chat::Person& p : people)
            if (p.is_friend) {
                int n = chat::unread(chat::FRIEND, p.id);
                names.push_back(std::string(cs("Friend: ", "Amigo: ")) + p.name + (n ? " (" + std::to_string(n) + ")" : std::string()));
                to.push_back({chat::FRIEND, p.id});
                if (g_chat == chat::FRIEND && g_chat_peer == p.id) at = (int)to.size() - 1;
            }
        add_choice(r, "chat.channel", cs("Chat", "Chat"), at, names, [to](int i) {
            if (i >= 0 && i < (int)to.size()) g_chat = to[i].first, g_chat_peer = to[i].second;
        });
        r.back().help = chat::channel_help(g_chat);
    }
    const bool hub_on = chat::hub_chat(), hub = hub_on && chat::hub_up(), relays = chat::enabled();
    const bool can = g_chat == chat::HUB ? hub : relays;
    action(r, "chat.say", cs("Say something", "Decir algo"), []() {
        const int ch = g_chat;
        const std::string peer = g_chat_peer;
        open_keyboard(cs("Say something", "Decir algo"), "", KB_CHAT, chat::max_text(), "", [ch, peer](const std::string& typed, std::string& error) {
            std::string why;
            if (typed.empty() || chat::say(ch, peer, typed, &why)) return true;
            error = chat::explain(why);
            return false;
        });
    }, can, "Say");
    r.back().help = std::string(cs("Up to ", "Hasta ")) + std::to_string(chat::max_text()) + cs(" characters. The keyboard's Enter sends it.", " caracteres. El Enter del teclado lo manda.");
    if (!can)
        note(r, "chat.why", g_chat != chat::HUB ? cs("Turn on the online chat above to read and write here.", "Activa el chat en línea arriba para leer y escribir acá.")
                            : !hub_on       ? cs("Turn on the room chat above to read and write here.", "Activa el chat de la sala arriba para leer y escribir acá.")
                                            : cs("Not in a room: join or host one in Multiplayer.", "No estás en una sala: entra a una o crea una en Multijugador."),
             touch_look().dim);
    std::vector<chat::Line> lines = chat::lines(g_chat, g_chat_peer, 30);
    chat::mark_read(g_chat, g_chat_peer);
    if (lines.empty() && can) note(r, "chat.none", cs("Nothing has been said yet.", "Todavía no se dijo nada."), touch_look().dim);
    std::vector<std::pair<std::string, std::string>> senders;  // (the people in the lines: id, name)
    for (const chat::Line& l : lines) {
        std::string who;
        if (!l.system) {
            who = l.mine ? std::string(cs("You", "Tú")) : l.name;
            if (g_chat == chat::GLOBAL && !l.game.empty()) who += l.game == "mh4u" ? "  (MH4U)" : "  (MHGU)";
        }
        note(r, "chat.line." + std::to_string(l.serial), chat::line_text(l), l.system ? touch_look().dim : 0, who);
        if (l.share == chat::SHARE_ROOM && !l.mine) {
            chat::Line copy = l;
            action(r, "chat.use." + std::to_string(l.serial), cs("Join this room", "Entrar a esta sala"), [copy]() { chat_use_share(copy); });
        }
        if (!l.mine && !l.system && l.from != chat::my_key()) {
            bool seen = false;
            for (auto& x : senders) seen |= x.first == l.from;
            if (!seen) senders.push_back({l.from, l.name});
        }
    }
    if (!senders.empty() && g_chat != chat::FRIEND) {
        std::vector<std::string> names;
        for (auto& x : senders) names.push_back(x.second);
        Row row;
        row.id = "chat.people", row.title = cs("Someone in this chat", "Alguien de este chat");
        row.value = std::to_string(senders.size());
        row.help = cs("Add one of them as a friend, mute them (their lines no longer show here) or block them (nothing of "
                      "theirs reaches you). Only you are affected, on this device.",
                      "Agrega a alguno como amigo, sílencialo (sus líneas ya no se ven acá) o bloquéalo (no te llega nada "
                      "suyo). Solo te afecta a ti, en este dispositivo.");
        row.menu = true;
        Row copy = row;
        row.activate = [copy, names, senders]() {
            choices(copy, names, -1, [senders](int i) {
                if (i < 0 || i >= (int)senders.size()) return;
                const std::string id = senders[i].first, name = senders[i].second;
                const bool key = id.size() == 64;
                std::vector<std::string> what;
                if (key) what.push_back(cs("Add as a friend", "Agregar como amigo"));
                what.push_back(cs("Mute", "Silenciar"));
                what.push_back(cs("Block", "Bloquear"));
                Row who;
                who.id = "chat.people", who.title = name;
                choices(who, what, -1, [id, name, key](int k) {
                    if (!key) k++;
                    if (k == 0) chat::add_friend_key(id, name);
                    else if (k == 1) chat::set_muted(id, name, true);
                    else if (k == 2) chat::set_blocked(id, name, true);
                });
            });
        };
        r.push_back(std::move(row));
    }
    action(r, "chat.friends", cs("Friends", "Amigos"), []() { push(View::Friends, "friends.code"); });
    {
        int on = 0;
        for (const chat::Person& p : people) on += p.is_friend && p.online;
        r.back().value = std::to_string(on) + " " + cs("online", "en línea");
    }
    header(r, "chat.options", cs("Options", "Opciones"));
    add_choice(r, "chat.overlay", cs("Lines over the game", "Líneas sobre el juego"), chat::overlay(), {cs("Off", "Apagado"), cs("Room and friends", "Sala y amigos"), cs("All", "Todos")},
               [](int i) { chat::set_overlay(i); });
    r.back().help = cs("New lines show for a few seconds over the game, beside the chat button.",
                       "Las líneas nuevas se ven unos segundos sobre el juego, junto al botón del chat.");
    add_bool(r, "net.notices", "Room messages over the game", &s.draft.notices, "Notices");
    {
        Row row;
        row.id = "chat.bubble", row.title = cs("Chat button", "Botón del chat");
        bool on = chat::bubble_shown();
        row.toggle = on, row.value = on ? cs("On", "Activo") : cs("Off", "Apagado");
        row.help = cs("A round button over the game while the online chat is on, or the room chat is on and you are in a room: "
                      "a tap opens the chat, a drag moves it. The number on it: lines not read yet.",
                      "Un botón redondo sobre el juego mientras el chat en línea está activo, o el chat de la sala está activo "
                      "y estás en una sala: un toque abre el chat, arrastrarlo lo mueve. El número: líneas sin leer.");
        row.activate = []() { chat::set_bubble_shown(!chat::bubble_shown()); };
        row.adjust = [](int d) { chat::set_bubble_shown(d > 0); };
        r.push_back(std::move(row));
        g_bubble_pct = (int)std::lround(chat::bubble_scale() * 100);
        Row size;
        size.id = "chat.bubble.size", size.title = cs("Chat button size", "Tamaño del botón del chat");
        size.value = std::to_string(g_bubble_pct) + " %";
        size.number = true;
        size.adjust = [](int d) { chat::bubble_size(std::clamp(g_bubble_pct + d * 10, 60, 200) / 100.f); };
        r.push_back(std::move(size));
        action(r, "chat.bubble.reset", cs("Chat button back in its place", "Botón del chat a su lugar"), []() { chat::bubble_reset(); });
        r.back().arrow = false;
    }
    {
        Row row;
        row.id = "chat.name", row.title = cs("Your name in the chat", "Tu nombre en el chat");
        row.value = chat::own_name().empty() ? chat::name() + cs(" (the nickname)", " (el apodo)") : chat::own_name();
        row.help = cs("Empty: your room nickname. Up to 24 characters.", "Vacío: tu apodo de sala. Hasta 24 caracteres.");
        row.arrow = true;
        row.activate = []() {
            open_keyboard(cs("Your name in the chat", "Tu nombre en el chat"), chat::own_name(), KB_ANY, 24, "", [](const std::string& t, std::string&) {
                chat::set_name(t);
                return true;
            });
        };
        r.push_back(std::move(row));
    }
    {
        const std::vector<std::string> def = chat::default_relays(), now = chat::relays();
        const bool custom = now != def;
        std::string list;
        for (const std::string& x : now) list += (list.empty() ? "" : " ") + x;
        add_choice(r, "chat.relays", cs("Relays", "Relés"), custom ? 1 : 0, {cs("The usual ones", "Los de siempre"), custom ? list : std::string(cs("Custom...", "Otros..."))},
                   [list](int i) {
                       if (i == 0) {
                           chat::set_relays({});
                           return;
                       }
                       open_keyboard(cs("Relays", "Relés"), list, KB_ANY, 600, "wss://...", [](const std::string& t, std::string& error) {
                           std::vector<std::string> v;
                           size_t a = 0;
                           while (a < t.size()) {
                               size_t b = t.find_first_of(" ,", a);
                               std::string x = t.substr(a, b == std::string::npos ? std::string::npos : b - a);
                               if (!x.empty() && x.rfind("wss://", 0) != 0) x = "wss://" + x;
                               if (x.size() > 6) v.push_back(x);
                               if (b == std::string::npos) break;
                               a = b + 1;
                           }
                           if (v.empty()) {
                               error = cs("Type one or more relays, as wss://relay.example.org, separated by spaces.",
                                          "Escribe uno o más relés, como wss://relay.example.org, separados por espacios.");
                               return false;
                           }
                           chat::set_relays(v);
                           return true;
                       });
                   });
        r.back().help = std::string(cs("The Nostr relays the online chat goes through. The usual ones: ", "Los relés de Nostr por los que va el chat en línea. Los de siempre: ")) +
                        "relay.damus.io, nos.lol, relay.primal.net. " +
                        cs("Everyone you want to talk to needs at least one relay in common with you.",
                           "Todos con los que quieras hablar necesitan al menos un relé en común contigo.");
    }
    note(r, "chat.credit", cs("Chat and room keys after the designs of tabarong.", "Chat y claves de sala según los diseños de tabarong."), touch_look().dim);
}
void friends_page_rows(std::vector<Row>& r) {
    cs("", "");
    chat_status_row(r);
    {
        Row row;
        row.id = "friends.code", row.title = cs("Your friend code", "Tu código de amigo");
        row.value = chat::my_code();
        row.help = cs("Give it to a friend: they add you with it while you are both online. A tap copies it. It is made on "
                      "this device: deleting the app makes a new one.",
                      "Dáselo a un amigo: te agrega con él mientras los dos están en línea. Un toque lo copia. Se crea en este "
                      "dispositivo: borrar la app crea otro.");
        row.activate = []() {
            const std::string c = chat::my_code();
            if (SDL_SetClipboardText(c.c_str())) s.error = c + cs(": copied.", ": copiado.");
        };
        r.push_back(std::move(row));
    }
    action(r, "friends.add", cs("Add a friend", "Agregar un amigo"), []() {
        open_keyboard(cs("Their friend code", "Su código de amigo"), "", KB_ANY, 20, "XXXX-XXXX-XXXX", [](const std::string& typed, std::string& error) {
            std::string why;
            if (chat::add_friend(typed, &why)) return true;
            error = chat::explain(why);
            return false;
        });
    });
    r.back().help = cs("Type their code (XXXX-XXXX-XXXX). They are found as soon as both of you are online, and get your "
                       "request; you can talk privately once they add you back.",
                       "Escribe su código (XXXX-XXXX-XXXX). Se lo encuentra apenas los dos estén en línea, y le llega tu "
                       "solicitud; pueden hablar en privado cuando te agregue también.");
    std::vector<chat::Person> people = chat::people();
    bool any = false;
    for (const chat::Person& p : people) {
        if (!p.request || p.is_friend || p.blocked) continue;
        if (!any) header(r, "friends.requests", cs("Requests", "Solicitudes"));
        any = true;
        Row row;
        row.id = "friends.req." + p.id, row.title = p.name, row.value = cs("wants to be your friend", "quiere ser tu amigo");
        row.menu = true;
        Row copy = row;
        const std::string id = p.id, name = p.name;
        row.activate = [copy, id, name]() {
            choices(copy, {cs("Accept", "Aceptar"), cs("Ignore", "Ignorar"), cs("Block", "Bloquear")}, -1, [id, name](int k) {
                if (k == 0) chat::accept(id);
                else if (k == 1) chat::remove(id);
                else chat::set_blocked(id, name, true);
            });
        };
        r.push_back(std::move(row));
    }
    header(r, "friends.list", cs("Friends", "Amigos"));
    any = false;
    const rooms::Status st = rooms::GetStatus();
    for (const chat::Person& p : people) {
        if (!p.is_friend && !p.pending) continue;
        any = true;
        Row row;
        row.id = "friends.f." + (p.pending ? p.code : p.id);
        row.title = p.pending ? cs("Code ", "Código ") + chatc::show_code(p.code) : p.name.empty() ? chatc::show_code(p.code) : p.name;
        if (p.pending) row.value = cs("looking for them...", "buscándolo...");
        else if (!p.mutual) row.value = cs("waiting for them to add you", "esperando que te agregue");
        else if (p.online) row.value = p.in_hub ? cs("online, in a room", "en línea, en una sala") : cs("online", "en línea");
        else row.value = p.seen ? std::string(cs("seen ", "visto ")) + chat::ago(p.seen) : std::string(cs("offline", "desconectado"));
        if (p.online && p.mutual) row.value += p.game == "mh4u" ? " (MH4U)" : p.game == "mhgu" ? " (MHGU)" : "";
        if (p.online && p.mutual) row.ink = NET_GREEN;
        row.help = std::string(cs("Friend code: ", "Código de amigo: ")) + chatc::show_code(p.code);
        row.menu = true;
        Row copy = row;
        const chat::Person q = p;
        // (a hub's room is not sent: its address is the host's loopback; the friend finds the hub in the hub list)
        const bool room = in_room(st) && !net_in_hub() && (!st.hosting || net_room_on_lan());
        row.activate = [copy, q, room]() {
            std::vector<std::string> what;
            std::vector<int> act;
            if (q.is_friend && q.mutual) what.push_back(cs("Talk", "Hablar")), act.push_back(0);
            if (q.is_friend && q.mutual && q.online && room) what.push_back(cs("Send this room", "Mandar esta sala")), act.push_back(1);
            if (!q.pending) what.push_back(q.muted ? cs("Unmute", "Quitar silencio") : cs("Mute", "Silenciar")), act.push_back(2);
            if (!q.pending) what.push_back(cs("Block", "Bloquear")), act.push_back(3);
            what.push_back(cs("Remove", "Quitar")), act.push_back(4);
            choices(copy, what, -1, [q, act](int k) {
                if (k < 0 || k >= (int)act.size()) return;
                std::string why;
                switch (act[k]) {
                case 0: chat_open(chat::FRIEND, q.id); break;
                case 1: {
                    const rooms::Status now = rooms::GetStatus();
                    std::string at = now.host + ":" + std::to_string(now.port);
                    if (now.hosting) {
                        std::vector<std::string> mine = net_local_addresses();
                        if (!mine.empty()) at = mine[0] + ":" + std::to_string(now.hosted_port);
                    }
                    if (!chat::share(chat::FRIEND, q.id, chat::SHARE_ROOM, at, s.draft.password, &why)) s.error = chat::explain(why);
                    else chat_open(chat::FRIEND, q.id);
                    break;
                }
                case 2: chat::set_muted(q.id, q.name, !q.muted); break;
                case 3: chat::set_blocked(q.id, q.name, true); break;
                default: chat::remove(q.pending ? "code:" + q.code : q.id); break;
                }
            });
        };
        r.push_back(std::move(row));
    }
    if (!any) note(r, "friends.none", cs("No friends yet: add one with their code.", "Todavía no tienes amigos: agrega uno con su código."), touch_look().dim);
    any = false;
    for (const chat::Person& p : people) {
        if (!p.muted && !p.blocked) continue;
        if (!any) header(r, "friends.quiet", cs("Muted and blocked", "Silenciados y bloqueados"));
        any = true;
        Row row;
        row.id = "friends.q." + p.id;
        row.title = p.name.empty() ? (p.id.rfind("room:", 0) == 0 ? p.id.substr(5) : p.id.substr(0, 12)) : p.name;
        row.value = p.blocked ? cs("blocked", "bloqueado") : cs("muted", "silenciado");
        row.help = cs("A tap lets them back. Only you are affected, on this device.", "Un toque lo deja volver. Solo te afecta a ti, en este dispositivo.");
        const std::string id = p.id, name = p.name;
        const bool blocked = p.blocked;
        row.activate = [id, name, blocked]() {
            if (blocked) chat::set_blocked(id, name, false);
            chat::set_muted(id, name, false);
        };
        r.push_back(std::move(row));
    }
}
// Multiplayer's rows: the chat and the friends, each a page of its own (they take the place of the room's chat rows)
void chat_entry_rows(std::vector<Row>& r) {
    header(r, "chat.header", cs("Chat and friends", "Chat y amigos"));
    int unread = 0;
    for (int c = 0; c < chat::CHANNELS; c++) unread += chat::unread(c);
    action(r, "chat.open", cs("Chat", "Chat"), []() { push(View::Chat, "chat.say"); });
    int up = 0, total = 0;
    chat::Status st = chat::status(&up, &total);
    r.back().value = (unread ? std::to_string(unread) + " " + cs("unread", "sin leer") + " - " : std::string()) + chat::chat_state_text(st, up, total);
    r.back().help = cs("Talk with the room you are in, with every player of the ports (Global) or of this game (General), and "
                       "with your friends. Off until you turn on the room chat or the online chat there; until then nothing "
                       "of the chat shows over the game (the room's own messages show as before: Room messages over the game).",
                       "Habla con la sala en la que estás, con todos los jugadores de los ports (Global) o de este juego "
                       "(General), y con tus amigos. Apagado hasta que actives ahí el chat de la sala o el chat en línea; "
                       "hasta entonces no se ve nada del chat sobre el juego (los mensajes de la sala se ven como antes: "
                       "Mensajes de la sala sobre el juego).");
    action(r, "friends.open", cs("Friends", "Amigos"), []() { push(View::Friends, "friends.code"); });
    int on = 0, requests = 0;
    for (const chat::Person& p : chat::people()) on += p.is_friend && p.online, requests += p.request && !p.is_friend && !p.blocked;
    r.back().value = (requests ? std::to_string(requests) + " " + cs("requests", "solicitudes") + " - " : std::string()) + std::to_string(on) + " " + cs("online", "en línea");
    r.back().help = cs("Your friend code, adding friends, and who you muted or blocked.", "Tu código de amigo, agregar amigos, y a quién silenciaste o bloqueaste.");
}
