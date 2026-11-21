// MH4U for PC: the game folder screen (pc_app.h), Android's GameFolderActivity as MHGU for PC's launcher shows it: a
// window of its own before the game's, in the settings' Hunter look (wine, bronze, gold), worked with the mouse, the
// keyboard or a controller. It says what the folder needs, lets the player choose it (the system's folder dialog), checks
// it (pc_look_at_folder: the game and its update by what they are, not by their names; the code library and its id) and
// starts the game with it. When the folder lacks only the code library (missing, or made by other tools), its builder's
// page makes it here (pc_builder.cpp; MHGU for PC's page, Android's BuilderActivity).
#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/utf8.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "pc_app.h"
#include "pc_builder.h"
#include "services/fs/dump_source.h"
#include "services/fs/positioned_file.h"

namespace fs = std::filesystem;

namespace {

// ---- what is in a folder -------------------------------------------------------------------------

constexpr unsigned long long kUsa = 0x00126300ull, kEur = 0x00126100ull, kMh4g = 0x0011D700ull;

// "game", "update" or "" for a file: by the title in it (an NCSD's partition 0, a CIA's content 0), as Dump.describe
std::string Describe(const std::string& path) {
    std::string error;
    auto file = mh4n::fs::PositionedFile::Open(path, &error);
    if (!file) return "";
    mh4n::fs::dump::Ncch n;
    if (!mh4n::fs::dump::FindNcch(*file, &n, &error)) return "";
    const unsigned long long unique = n.program_id & 0xFFFFFFFFull, high = n.program_id >> 32;
    const bool ours = unique == kUsa || unique == kEur || unique == kMh4g;
    if (high == 0x00040000ull && ours) return "game";
    if (high == 0x0004000Eull && ours) return "update";
    return "";
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string FirstWord(const fs::path& path) {
    std::ifstream in(path);
    std::string word;
    in >> word;
    return word;
}

}  // namespace

std::string pc_library_id_inside(const std::string& dll) {
    std::ifstream f(mh4n::utf8::PathFromUtf8(dll), std::ios::binary);
    if (!f) return "";
    auto read = [&f](std::uint64_t at, void* out, std::size_t n) {
        f.seekg(static_cast<std::streamoff>(at));
        f.read(static_cast<char*>(out), static_cast<std::streamsize>(n));
        return static_cast<std::size_t>(f.gcount()) == n;
    };
    auto u16 = [](const unsigned char* p) { return static_cast<unsigned>(p[0] | p[1] << 8); };
    auto u32 = [](const unsigned char* p) {
        return static_cast<std::uint32_t>(p[0] | p[1] << 8 | p[2] << 16 | static_cast<std::uint32_t>(p[3]) << 24);
    };
    unsigned char dos[64];
    if (!read(0, dos, sizeof dos) || dos[0] != 'M' || dos[1] != 'Z') return "";
    const std::uint32_t pe = u32(dos + 0x3C);
    unsigned char hdr[24];
    if (!read(pe, hdr, sizeof hdr) || std::memcmp(hdr, "PE\0\0", 4) != 0) return "";
    const unsigned sections = u16(hdr + 6), optional_size = u16(hdr + 20);
    std::vector<unsigned char> opt(optional_size);
    if (optional_size < 0x78 || !read(pe + 24, opt.data(), opt.size()) || u16(opt.data()) != 0x20B) return "";  // (PE32+)
    const std::uint32_t export_rva = u32(opt.data() + 0x70), export_size = u32(opt.data() + 0x74);
    if (export_rva == 0 || export_size == 0 || sections == 0 || sections > 96) return "";
    std::vector<unsigned char> table(static_cast<std::size_t>(sections) * 40);
    if (!read(pe + 24 + optional_size, table.data(), table.size())) return "";
    auto offset_of = [&](std::uint32_t rva, std::uint64_t* out) {
        for (unsigned i = 0; i < sections; ++i) {
            const unsigned char* s = table.data() + i * 40;
            const std::uint32_t va = u32(s + 12), vsize = std::max(u32(s + 8), u32(s + 16)), raw = u32(s + 20);
            if (rva >= va && rva < va + vsize) {
                *out = static_cast<std::uint64_t>(raw) + (rva - va);
                return true;
            }
        }
        return false;
    };
    std::uint64_t at = 0;
    unsigned char dir[40];
    if (!offset_of(export_rva, &at) || !read(at, dir, sizeof dir)) return "";
    const std::uint32_t names = u32(dir + 24), functions_rva = u32(dir + 28), names_rva = u32(dir + 32),
                        ordinals_rva = u32(dir + 36);
    std::uint64_t names_at = 0, ordinals_at = 0, functions_at = 0;
    if (names > 1000000 || !offset_of(names_rva, &names_at) || !offset_of(ordinals_rva, &ordinals_at) ||
        !offset_of(functions_rva, &functions_at))
        return "";
    static const char kWant[] = "rc_library_id";
    for (std::uint32_t i = 0; i < names; ++i) {
        unsigned char rva_bytes[4];
        std::uint64_t name_at = 0;
        char name[sizeof kWant] = {};
        if (!read(names_at + 4ull * i, rva_bytes, 4) || !offset_of(u32(rva_bytes), &name_at) ||
            !read(name_at, name, sizeof name) || std::memcmp(name, kWant, sizeof kWant) != 0)
            continue;
        unsigned char ordinal[2], function[4];
        std::uint64_t value_at = 0;
        char id[65] = {};
        if (!read(ordinals_at + 2ull * i, ordinal, 2) || !read(functions_at + 4ull * u16(ordinal), function, 4) ||
            !offset_of(u32(function), &value_at) || !read(value_at, id, 64))
            return "";
        return std::string(id, strnlen(id, 64));
    }
    return "";
}

PcGameFolder pc_look_at_folder(const std::string& folder, bool es) {
    PcGameFolder f;
    f.folder = folder;
    auto say = [&](const char* en, const char* spanish) { return std::string(es ? spanish : en); };
    std::error_code ec;
    const fs::path dir = mh4n::utf8::PathFromUtf8(folder);
    if (folder.empty() || !fs::is_directory(dir, ec)) {
        f.problem = folder.empty() ? say("Choose the folder with your game.", "Elige la carpeta con tu juego.")
                                   : say("The folder isn't there any more: choose it again.",
                                         "La carpeta ya no está: elígela de nuevo.");
        return f;
    }
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string name = Lower(mh4n::utf8::PathToUtf8(it->path().filename()));
        const std::string path = mh4n::utf8::PathToUtf8(it->path());
        if (name == kPcLibrary) {
            f.library = path;
        } else if (name.size() > 4 && (name.ends_with(".3ds") || name.ends_with(".cci") || name.ends_with(".cxi") ||
                                       name.ends_with(".cia"))) {
            const std::string kind = Describe(path);
            if (kind == "game" && f.base.empty()) f.base = path;
            else if (kind == "update" && f.update.empty()) f.update = path;
        }
    }
    // (a test: MH4U_CODE_LIBRARY names a library elsewhere, for a folder that has none, so that the player's own game
    // folder is used as it is, nothing written into it; one not made yet counts as none: the builder's test, whose
    // MH4U_BUILDER_DEST puts it there)
    if (const char* lib = SDL_getenv("MH4U_CODE_LIBRARY"); f.library.empty() && lib != nullptr && *lib != 0) {
        std::error_code ec;
        if (fs::is_regular_file(mh4n::utf8::PathFromUtf8(lib), ec)) f.library = lib;
    }
    if (f.base.empty() && f.update.empty()) {
        f.problem = say("No game here: the folder needs your game (.3ds or .cci) and its 1.3.0 update (.cia), both "
                        "decrypted.",
                        "Aquí no hay juego: la carpeta necesita tu juego (.3ds o .cci) y su actualización 1.3.0 (.cia), "
                        "los dos descifrados.");
        return f;
    }
    if (f.base.empty()) {
        f.problem = say("The game (.3ds or .cci, decrypted) is missing, beside the update.",
                        "Falta el juego (.3ds o .cci, descifrado), al lado de la actualización.");
        return f;
    }
    if (f.update.empty()) {
        f.problem = say("The 1.3.0 update (.cia, decrypted) is missing, beside the game.",
                        "Falta la actualización 1.3.0 (.cia, descifrada), al lado del juego.");
        return f;
    }
    const std::string how = say(" Build the game code on this PC makes it from your own copy.",
                                " Crear el código del juego en esta PC la hace con tu propia copia.");
    if (f.library.empty()) {
        f.library_problem = true;
        f.problem = say("The game's code library (mh4u_code_native.dll) isn't in the folder.",
                        "La librería de código del juego (mh4u_code_native.dll) no está en la carpeta.") + how;
        return f;
    }
    fs::path id_file = mh4n::utf8::PathFromUtf8(f.library);
    id_file.replace_extension(".id");
    const std::string beside = FirstWord(id_file), inside = pc_library_id_inside(f.library);
    f.library_id = inside.empty() ? beside : inside;
    if (!inside.empty() && !beside.empty() && inside != beside) {
        f.library_problem = true;
        f.problem = say("The mh4u_code_native.dll and the mh4u_code_native.id beside it come from different builds (copied "
                        "from two places). Copy both files of the same build together.",
                        "La mh4u_code_native.dll y el mh4u_code_native.id que tiene al lado son de armados distintos "
                        "(copiados de dos lugares). Copia juntos los dos archivos de un mismo armado.") + how;
        return f;
    }
    if (f.library_id != MH4U_PC_CODE_ID) {
        f.library_problem = true;
        f.problem = (f.library_id.empty()
                         ? say("This mh4u_code_native.dll has no id: it was made with an older version of the tool.",
                               "Esta mh4u_code_native.dll no tiene id: se hizo con una versión anterior de la herramienta.")
                         : say("This mh4u_code_native.dll is for another version of MH4U for PC (or for Android).",
                               "Esta mh4u_code_native.dll es de otra versión de MH4U para PC (o de Android).")) + how;
        std::fprintf(stderr, "[app] code library %s: id '%s', this program wants '%s'\n", f.library.c_str(),
                     f.library_id.c_str(), MH4U_PC_CODE_ID);
        return f;
    }
    return f;
}

namespace {

// ---- the screen ----------------------------------------------------------------------------------

// the settings menu's Hunter theme (settings_panel.cpp, MHGU for PC's launcher): wine, the game's red header, gold
const ImU32 PANEL = IM_COL32(50, 25, 24, 255), GOLD = IM_COL32(236, 212, 44, 255), GOLD_BRIGHT = IM_COL32(255, 232, 84, 255),
            TEXT = IM_COL32(242, 238, 232, 255), DIM = IM_COL32(196, 176, 166, 255), WARN = IM_COL32(255, 120, 96, 255),
            BRONZE = IM_COL32(96, 58, 34, 255), BRONZE_LIT = IM_COL32(150, 98, 46, 255), PLATE = IM_COL32(66, 34, 30, 255),
            OK = IM_COL32(150, 214, 110, 255), HEADER = IM_COL32(108, 46, 46, 255), RED_RULE = IM_COL32(176, 34, 30, 255),
            GREEN_RULE = IM_COL32(80, 142, 54, 255);
bool g_es;
const char* T(const char* en, const char* es) { return g_es ? es : en; }

struct Pick {
    std::mutex mutex;
    std::atomic<int> state{0};  // 0 idle, 1 the dialog is open, 2 a folder came back, 3 checking, 4 checked
    std::string picked;
    PcGameFolder checked;
} g_pick;

void SDLCALL Picked(void*, const char* const* list, int) {
    std::lock_guard lock(g_pick.mutex);
    if (list != nullptr && list[0] != nullptr) {
        g_pick.picked = list[0];
        g_pick.state = 2;
    } else {
        g_pick.state = 0;  // (cancelled, or the dialog failed)
    }
}

std::string Fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string Fmt(const char* f, ...) {
    char b[1600];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}
std::string Mb(int64_t bytes) { return Fmt("%lld MB", static_cast<long long>(bytes >> 20)); }
std::string Minutes(int64_t ms) {
    const long long m = ms / 60000, s = ms / 1000 % 60;
    return m > 0 ? Fmt("%lld min %02lld s", m, s) : Fmt("%lld s", s < 0 ? 0 : s);
}

bool Button(const char* label, ImVec2 size, bool primary, bool enabled = true) {
    if (!enabled) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(primary ? BRONZE_LIT : BRONZE));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(IM_COL32(176, 145, 24, 255)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(GOLD));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(GOLD));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.f);
    const bool hit = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    if (!enabled) ImGui::EndDisabled();
    return hit && enabled;
}

void Wrapped(const std::string& text, ImU32 colour) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colour));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void Line(const char* label, const std::string& value, bool ok) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(GOLD));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    Wrapped(value, ok ? TEXT : WARN);
}

void Heading(const char* title) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(GOLD));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

std::string FileName(const std::string& path) {
    return path.empty() ? std::string() : mh4n::utf8::PathToUtf8(mh4n::utf8::PathFromUtf8(path).filename());
}

// A font of the system's for the screen (Segoe UI's semibold, as Windows' own dialogs), else ImGui's
void LoadFont(float size) {
    ImGuiIO& io = ImGui::GetIO();
    const char* windir = SDL_getenv("WINDIR");
    const std::string fonts = std::string(windir != nullptr ? windir : "C:\\Windows") + "\\Fonts\\";
    for (const char* name : {"seguisb.ttf", "segoeuib.ttf", "segoeui.ttf", "arialbd.ttf"}) {
        const std::string path = fonts + name;
        std::error_code ec;
        if (fs::exists(mh4n::utf8::PathFromUtf8(path), ec) && io.Fonts->AddFontFromFileTTF(path.c_str(), size) != nullptr)
            return;
    }
    ImFontConfig cfg;
    cfg.SizePixels = size;
    io.Fonts->AddFontDefaultVector(&cfg);
}

// ---- the builder's page (Android's BuilderActivity and BuilderText, MHGU for PC's page)
const char* StageText(int s) {
    static const char* const en[PCB_STAGES] = {"Download the tools", "Read your game files", "Translate the game's code",
                                               "Compile", "Install the game code"};
    static const char* const es[PCB_STAGES] = {"Descargar las herramientas", "Leer tus archivos del juego",
                                               "Traducir el código del juego", "Compilar", "Instalar el código del juego"};
    return s >= 0 && s < PCB_STAGES ? (g_es ? es[s] : en[s]) : "";
}
// The overall percentage: the stages weigh what they take on a PC (compiling is most of it); -1 when unknown
int Percent(const PcbJob& j) {
    static const double from[PCB_STAGES] = {0, 4, 6, 25, 99}, span[PCB_STAGES] = {4, 2, 19, 74, 1};
    if (j.stage < 0 || j.stage >= PCB_STAGES) return -1;
    const double part = j.total > 0 ? std::min(1.0, static_cast<double>(j.done) / static_cast<double>(j.total)) : 0;
    return static_cast<int>(from[j.stage] + span[j.stage] * part + .5);
}
std::string ToolName(const char* detail) {
    const char* n = std::strchr(detail, ':');
    std::string name = n ? n + 1 : detail;
    if (name.find('@') != std::string::npos) name.resize(name.find('@'));
    return name == "python" ? "Python 3.14.9" : name == "zig" ? "Zig 0.13.0" : T("the builder's scripts", "los scripts del creador");
}
std::string ToolSource(const char* detail) {
    const char* at = std::strchr(detail, '@');
    return at ? at + 1 : "";
}
// One line for the page: what is going on now
std::string StatusLine(const PcbJob& j) {
    std::string s = StageText(j.stage);
    if (j.stage == 0) {
        if (!std::strncmp(j.detail, "download:", 9))
            return Fmt("%s %s (%s): %lld / %lld MB", T("Downloading", "Descargando"), ToolName(j.detail).c_str(),
                       ToolSource(j.detail).c_str(), static_cast<long long>(j.done >> 20), static_cast<long long>(j.total >> 20));
        if (!std::strncmp(j.detail, "unpack:", 7)) return Fmt("%s %s", T("Setting up", "Preparando"), ToolName(j.detail).c_str());
        return s;
    }
    if (j.stage == 3 && !std::strcmp(j.detail, "link")) return Fmt("%s: %s", s.c_str(), T("linking", "enlazando"));
    if (j.total > 0 && (j.stage == 2 || j.stage == 3))
        return Fmt("%s: %lld / %lld %s", s.c_str(), static_cast<long long>(j.done), static_cast<long long>(j.total),
                   j.stage == 2 ? T("functions", "funciones") : T("files", "archivos"));
    return s;
}
// A failure in words the player can act on (the tools' own message follows it on the page)
const char* FailureText(const char* stage) {
    const std::string s = stage;
    if (s == "space")
        return T("There isn't enough free space on the disk where MH4U is. Free some (it needs about 1.5 GB while it builds) "
                 "and try again: it goes on from where it stopped.",
                 "No hay espacio suficiente en el disco donde está MH4U. Libera espacio (hacen falta unos 1,5 GB mientras crea) "
                 "y vuelve a intentarlo: sigue desde donde quedó.");
    if (s == "download")
        return T("The tools couldn't be downloaded. Check the internet connection and try again: the download goes on from "
                 "where it stopped. When PyPI (files.pythonhosted.org) can't be reached, Zig comes from ziglang.org instead; "
                 "if both are blocked, another network or DNS (such as 1.1.1.1) helps, or make the game code by hand (Open "
                 "the guide).",
                 "No se pudieron descargar las herramientas. Revisa la conexión a internet y vuelve a intentarlo: la descarga "
                 "sigue desde donde quedó. Si PyPI (files.pythonhosted.org) no responde, Zig se descarga de ziglang.org; si "
                 "los dos están bloqueados, sirve otra red u otro DNS (como 1.1.1.1), o crear el código del juego a mano "
                 "(Abrir la guía).");
    if (s == "tools")
        return T("The builder's files that came with MH4U are missing or don't match it (builder_tools.zip, zigcc.exe). "
                 "Unzip the whole MH4U-PC zip again over this folder.",
                 "Faltan los archivos del creador que vienen con MH4U, o no coinciden con él (builder_tools.zip, zigcc.exe). "
                 "Vuelve a descomprimir todo el zip MH4U-PC sobre esta carpeta.");
    if (s == "dest")
        return T("The game folder can't be written. Choose a folder you can write in (not in Program Files).",
                 "No se puede escribir en la carpeta del juego. Elige una carpeta en la que puedas escribir (no dentro de "
                 "Archivos de programa).");
    if (s == "extract" || s == "version" || s == "mods")
        return T("Your game files aren't the ones needed: the game (.3ds or .cci) and its 1.3.0 update (.cia), both "
                 "decrypted, from your own copy. A code mod in the folder's mods may not fit this version.",
                 "Tus archivos del juego no son los que hacen falta: el juego (.3ds o .cci) y su actualización 1.3.0 (.cia), "
                 "los dos descifrados, de tu propia copia. Un mod de código en la carpeta mods puede no servir para esta "
                 "versión.");
    if (s == "memory")
        return T("The PC ran out of memory. Close other programs and try again: it goes on from where it stopped, with fewer "
                 "compilers.",
                 "La PC se quedó sin memoria. Cierra otros programas y vuelve a intentarlo: sigue desde donde quedó, con menos "
                 "compiladores.");
    if (s == "start")
        return T("The builder's Python couldn't start. Your antivirus may have blocked or removed it: allow MH4U's builder "
                 "folder, or Start over and delete the tools, and try again.",
                 "El Python del creador no pudo arrancar. Puede que tu antivirus lo haya bloqueado o borrado: permite la "
                 "carpeta builder de MH4U, o Empezar de cero y borrar las herramientas, y vuelve a intentarlo.");
    if (s == "recompile" || s == "shaders" || s == "compile" || s == "link")
        return T("The tool failed while building the code. Try again: it goes on from where it stopped. If your antivirus "
                 "removed zig.exe, allow MH4U's builder folder. If it fails again, share builder_log.txt (beside MH4U.exe).",
                 "La herramienta falló al crear el código. Vuelve a intentarlo: sigue desde donde quedó. Si tu antivirus "
                 "borró zig.exe, permite la carpeta builder de MH4U. Si vuelve a fallar, comparte builder_log.txt (junto a "
                 "MH4U.exe).");
    if (s == "install")
        return T("The game code couldn't be put in the game folder. Close the game if it is open in another window, check "
                 "the space and the access to the folder, and try again.",
                 "No se pudo poner el código del juego en la carpeta del juego. Cierra el juego si está abierto en otra "
                 "ventana, revisa el espacio y el acceso a la carpeta, y vuelve a intentarlo.");
    return T("The build stopped. Try again: it goes on from where it stopped.",
             "La creación se detuvo. Vuelve a intentarlo: sigue desde donde quedó.");
}

// A mark for a stage, drawn (the font has no check mark): 1 done, 2 now, 3 failed, 0 to come
void StageMark(ImDrawList* dl, ImVec2 c, float r, int kind) {
    if (kind == 1) {
        dl->AddLine(ImVec2(c.x - r * .8f, c.y), ImVec2(c.x - r * .2f, c.y + r * .7f), GOLD, r * .35f);
        dl->AddLine(ImVec2(c.x - r * .2f, c.y + r * .7f), ImVec2(c.x + r * .9f, c.y - r * .7f), GOLD, r * .35f);
    } else if (kind == 2) {
        dl->AddTriangleFilled(ImVec2(c.x - r * .6f, c.y - r * .8f), ImVec2(c.x - r * .6f, c.y + r * .8f), ImVec2(c.x + r * .8f, c.y),
                              GOLD);
    } else if (kind == 3) {
        dl->AddLine(ImVec2(c.x - r * .7f, c.y - r * .7f), ImVec2(c.x + r * .7f, c.y + r * .7f), WARN, r * .35f);
        dl->AddLine(ImVec2(c.x - r * .7f, c.y + r * .7f), ImVec2(c.x + r * .7f, c.y - r * .7f), WARN, r * .35f);
    } else {
        dl->AddCircle(c, r * .35f, DIM, 0, r * .2f);
    }
}

// ---- tests: MH4U_LAUNCHER_SCRIPT="open; build; wait compile 40; shot a.png; cancel; wait stopped; startover; build; wait
// done; play" works the screen as a player would (open: the folder screen's Build the game code on this PC; shot: the window
// to a PNG; wait: until the build gets there, at most 2 hours; sleep <ms>; ask cancel|close|startover|tools: that question
// shown, for a shot; the page's other buttons by their names: startover-tools, deletetools, back, exit), as MHGU's
std::vector<std::string> ScriptSteps(const char* s) {
    std::vector<std::string> out;
    std::string all = s ? s : "", step;
    for (size_t i = 0; i <= all.size(); i++) {
        if (i == all.size() || all[i] == ';') {
            while (!step.empty() && step[0] == ' ') step.erase(0, 1);
            while (!step.empty() && step.back() == ' ') step.pop_back();
            if (!step.empty()) out.push_back(step);
            step.clear();
        } else {
            step += all[i];
        }
    }
    return out;
}

}  // namespace

std::string pc_launcher(const PcGameFolder& current, bool es, bool open_builder) {
    g_es = es;
    pcb_init();
    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale < 1.f) scale = 1.f;
    SDL_Window* win = SDL_CreateWindow(T("MH4U - Game folder", "MH4U - Carpeta del juego"), static_cast<int>(940 * scale),
                                       static_cast<int>(660 * scale), SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
    if (win == nullptr) {
        std::fprintf(stderr, "[app] the game folder screen: no window (%s)\n", SDL_GetError());
        return "";
    }
    if (const char* at = SDL_getenv("MH4U_WINDOW_AT"); at != nullptr && *at != 0) {  // (tests: the display holding x,y)
        SDL_Point p{0, 0};
        if (SDL_sscanf(at, "%d,%d", &p.x, &p.y) == 2)
            if (SDL_DisplayID d = SDL_GetDisplayForPoint(&p); d != 0)
                SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED_DISPLAY(d), SDL_WINDOWPOS_CENTERED_DISPLAY(d));
    }
    SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
    if (ren == nullptr) {
        std::fprintf(stderr, "[app] the game folder screen: no renderer (%s)\n", SDL_GetError());
        SDL_DestroyWindow(win);
        return "";
    }
    SDL_SetRenderVSync(ren, 1);
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 0, st.FrameRounding = 3, st.WindowBorderSize = 0, st.FramePadding = ImVec2(14, 8);
    st.ChildRounding = 6, st.ChildBorderSize = 1, st.PopupRounding = 6, st.PopupBorderSize = 2;
    st.Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(TEXT);
    st.Colors[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(DIM);
    st.Colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(PANEL);
    st.Colors[ImGuiCol_ChildBg] = ImGui::ColorConvertU32ToFloat4(PLATE);
    st.Colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(PLATE);
    st.Colors[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(GOLD);
    st.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, .6f);
    st.Colors[ImGuiCol_FrameBg] = ImGui::ColorConvertU32ToFloat4(BRONZE);
    st.Colors[ImGuiCol_PlotHistogram] = ImGui::ColorConvertU32ToFloat4(GOLD);
    st.Colors[ImGuiCol_NavCursor] = ImGui::ColorConvertU32ToFloat4(GOLD_BRIGHT);
    st.ScaleAllSizes(scale);
    LoadFont(22.0f * scale);
    ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer3_Init(ren);

    PcGameFolder f = current;
    std::string result;
    bool running = true;
    std::thread checker;
    bool page = open_builder && !f.base.empty() && !f.update.empty();  // (the builder's page)
    PcbJob job;
    PcbInfo info;
    std::string confirm;  // the question open over the page: "cancel", "close", "startover", "tools" ("": none)
    bool want_close = false;
    int64_t looked_at = -1;  // (the build whose end the page has read the folder again for)
    // (tests: MH4U_LAUNCHER_PICK answers the folder dialog, MH4U_LAUNCHER_SHOT saves the screen, MH4U_LAUNCHER_PLAY plays
    // or leaves, MH4U_LAUNCHER_SCRIPT works the screen)
    const char* test_pick = SDL_getenv("MH4U_LAUNCHER_PICK");
    const char* test_shot = SDL_getenv("MH4U_LAUNCHER_SHOT");
    const char* test_play = SDL_getenv("MH4U_LAUNCHER_PLAY");
    const bool test_picks = test_pick != nullptr && *test_pick;  // (then the shot waits for the folder's check)
    std::vector<std::string> script = ScriptSteps(SDL_getenv("MH4U_LAUNCHER_SCRIPT"));
    size_t script_at = 0;
    uint64_t script_since = 0;
    std::string shot_now;
    int frame = 0, shot_at = -1;
    std::fprintf(stderr, "[app] the game folder screen%s: %s\n", page ? " (the builder's page)" : "",
                 f.problem.empty() ? "a playable folder" : f.problem.c_str());
    auto check_folder = [&](const std::string& folder) {
        g_pick.state = 3;
        if (checker.joinable()) checker.join();
        checker = std::thread([folder] {
            PcGameFolder c = pc_look_at_folder(folder, g_es);
            std::lock_guard lock(g_pick.mutex);
            g_pick.checked = c;
            g_pick.state = 4;
        });
    };
    // the code mods a build takes (Android's builder: the game folder's mods/, those on in the settings, or none), looked
    // at again when the folder changes and every few seconds while no build runs
    std::vector<PcbMods> mods;
    int mods_pick = 0;
    std::string mods_for;
    while (running) {
        ++frame;
        pcb_snapshot(&job);
        pcb_info(&info);
        const bool building = pcb_running();
        if (!building && (mods.empty() || mods_for != f.folder || frame % 180 == 0)) {
            const std::string was = mods_pick < (int)mods.size() ? mods[mods_pick].flag : "";
            const bool other = mods_for != f.folder;
            mods = pcb_mod_choices(f.folder, pc_paths().user), mods_for = f.folder, mods_pick = 0;
            for (int i = 0; !other && i < (int)mods.size(); i++)
                if (mods[i].flag == was) mods_pick = i;  // (the player's pick kept)
        }
        bool key_back = false;
        if (test_pick != nullptr && *test_pick && frame == 30) {
            std::lock_guard lock(g_pick.mutex);
            g_pick.picked = test_pick, g_pick.state = 2, test_pick = nullptr;
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            const bool close = e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED;
            const bool esc = e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_ESCAPE;
            if (close) {
                if (building) confirm = "close";  // (a build running: asked first; what it did is kept)
                else running = false;
            } else if (esc && g_pick.state != 1 && confirm.empty()) {
                if (page) key_back = true;
                else running = false;
            }
        }
        if (want_close && !building) running = false;  // (closing during a build: once it has stopped)
        if (g_pick.state == 2) {
            std::string picked;
            {
                std::lock_guard lock(g_pick.mutex);
                picked = g_pick.picked;
            }
            check_folder(picked);
        }
        if (g_pick.state == 4) {
            {
                std::lock_guard lock(g_pick.mutex);
                f = g_pick.checked;
                g_pick.state = 0;
            }
            std::fprintf(stderr, "[app] folder chosen: %s: %s\n", f.folder.c_str(), f.problem.empty() ? "playable" : f.problem.c_str());
            if (test_shot != nullptr && *test_shot) shot_at = frame + 20;
        }
        if (shot_at < 0 && test_shot != nullptr && *test_shot && !test_picks && frame == 40) shot_at = frame + 10;

        // what the buttons (or the test script) ask for this frame
        bool act_open = false, act_build = false, act_cancel = false, act_play = false, act_back = key_back,
             act_startover = false, act_tools = false, act_deltools = false;
        if (script_at < script.size() && g_pick.state == 0) {
            const std::string& stp = script[script_at];
            const uint64_t now = SDL_GetTicks();
            if (!script_since) script_since = now;
            bool next = true;
            if (stp == "open") act_open = true;
            else if (stp == "build") act_build = true;
            else if (stp == "cancel") act_cancel = true;
            else if (stp == "play") act_play = true;
            else if (stp == "back") act_back = true;
            else if (stp == "startover") act_startover = true;
            else if (stp == "startover-tools") act_tools = true;
            else if (stp == "deletetools") act_deltools = true;
            else if (stp == "mods" && !building) mods_pick = (mods_pick + 1) % (int)mods.size();
            else if (stp == "exit") running = false;
            else if (!stp.compare(0, 4, "ask ")) confirm = stp.substr(4);
            else if (!stp.compare(0, 5, "shot ")) shot_now = stp.substr(5);
            else if (!stp.compare(0, 6, "sleep ")) next = now - script_since >= static_cast<uint64_t>(SDL_atoi(stp.c_str() + 6));
            else if (!stp.compare(0, 5, "wait ")) {
                char what[32] = "";
                int n = 0;
                SDL_sscanf(stp.c_str() + 5, "%31s %d", what, &n);
                const std::string w = what;
                next = w == "done"        ? job.status == PCB_DONE
                       : w == "stopped"   ? !building && job.status != PCB_RUNNING
                       : w == "failed"    ? job.status == PCB_FAILED
                       : w == "python"    ? job.stage == 0 && !SDL_strncmp(job.detail, "download:python", 15) && job.done >= (int64_t(n) << 20)
                       : w == "zig"       ? job.stage == 0 && !SDL_strncmp(job.detail, "download:zig", 12) && job.done >= (int64_t(n) << 20)
                       : w == "recompile" ? job.stage == 2 && job.done >= n
                       : w == "compile"   ? job.stage == 3 && job.total > 0 && job.done >= n
                                          : true;
                if (!next && now - script_since > 2 * 3600 * 1000ull) next = true;  // (never forever)
            }
            if (act_build || act_cancel || act_play || act_back || act_startover || act_tools || act_deltools) confirm.clear();
            if (next) {
                std::fprintf(stderr, "[app] launcher script: %s\n", stp.c_str());
                script_at++, script_since = 0;
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        const float fsz = ImGui::GetFontSize(), band = fsz * 2.9f, pad = fsz * 1.2f, W = vp->Size.x, w = W - 2 * pad;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
        ImGui::Begin("##folder", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // the header: the game's red band and its red and green rules, a gold title
        dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + W, vp->Pos.y + band), HEADER);
        dl->AddRectFilled(ImVec2(vp->Pos.x, vp->Pos.y + band), ImVec2(vp->Pos.x + W, vp->Pos.y + band + 3 * scale), RED_RULE);
        dl->AddRectFilled(ImVec2(vp->Pos.x, vp->Pos.y + band + 3 * scale), ImVec2(vp->Pos.x + W, vp->Pos.y + band + 5 * scale),
                          GREEN_RULE);
        dl->AddText(ImVec2(vp->Pos.x + pad, vp->Pos.y + (band - fsz) * .5f), GOLD,
                    page ? T("MH4U: BUILD THE GAME CODE", "MH4U: CREAR EL CÓDIGO DEL JUEGO")
                         : T("MH4U: YOUR GAME FOLDER", "MH4U: TU CARPETA DEL JUEGO"));
        const float bh = fsz * 2.2f, by = vp->Size.y - pad - bh;
        if (!page) {
            ImGui::SetCursorPos(ImVec2(pad, band + pad));
            ImGui::PushTextWrapPos(pad + w);
            Wrapped(T("Choose the folder with your own copy of the game. It needs:",
                      "Elige la carpeta con tu propia copia del juego. Necesita:"), TEXT);
            Wrapped(T("  - the game (.3ds or .cci) and its 1.3.0 update (.cia), both decrypted;",
                      "  - el juego (.3ds o .cci) y su actualización 1.3.0 (.cia), los dos descifrados;"), DIM);
            Wrapped(T("  - the game's code library, mh4u_code_native.dll with its .id, made once on this PC from your game "
                      "(Build the game code on this PC);",
                      "  - la librería de código del juego, mh4u_code_native.dll con su .id, que se crea una vez en esta PC con "
                      "tu juego (Crear el código del juego en esta PC);"), DIM);
            Wrapped(T("  - and, if you want, mods/ and textures/.", "  - y, si quieres, mods/ y textures/."), DIM);
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::BeginChild("##status", ImVec2(w, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                                                           ImGuiChildFlags_AlwaysUseWindowPadding);
            Line(T("Folder:", "Carpeta:"), f.folder.empty() ? T("none yet", "ninguna todavía") : f.folder, !f.folder.empty());
            if (!f.folder.empty()) {
                Line(T("Game:", "Juego:"), f.base.empty() ? T("missing", "falta") : FileName(f.base), !f.base.empty());
                Line(T("Update:", "Actualización:"), f.update.empty() ? T("missing", "falta") : FileName(f.update), !f.update.empty());
                Line(T("Code library:", "Librería de código:"),
                     f.library.empty() ? T("missing", "falta")
                                       : FileName(f.library) + (f.library_id.empty() ? "" : "  (" + f.library_id + ")"),
                     !f.library.empty() && !f.library_problem);
            }
            ImGui::Spacing();
            if (g_pick.state == 3) Wrapped(T("Checking the files...", "Revisando los archivos..."), GOLD);
            else if (f.problem.empty()) Wrapped(T("Ready to play.", "Listo para jugar."), OK);
            else Wrapped(f.problem, WARN);
            ImGui::EndChild();
            ImGui::PopTextWrapPos();
            // the buttons, at the bottom (the builder's between them when the folder lacks only the game code)
            const bool can_build = f.library_problem && !f.base.empty() && !f.update.empty();
            const int nb = can_build ? 4 : 3;
            const float gap = pad * .6f;
            float bw = (w - gap * (nb - 1)) / nb, build_w = bw;
            const char* build_label = T("Build the game code on this PC", "Crear el código del juego en esta PC");
            if (can_build) {  // (the builder's button as wide as its words, which are longer in Spanish; the others share the rest)
                build_w = std::max(bw, ImGui::CalcTextSize(build_label).x + ImGui::GetStyle().FramePadding.x * 2 + pad);
                bw = (w - gap * (nb - 1) - build_w) / (nb - 1);
            }
            ImGui::SetCursorPos(ImVec2(pad, by));
            const bool busy = g_pick.state != 0;
            if (Button(T("Choose folder", "Elegir carpeta"), ImVec2(bw, bh), !f.problem.empty() && !can_build, !busy)) {
                g_pick.state = 1;
                const std::string start = f.folder;
                SDL_ShowOpenFolderDialog(Picked, nullptr, win, start.empty() ? nullptr : start.c_str(), false);
            }
            if (can_build) {
                ImGui::SameLine(0, gap);
                if (Button(build_label, ImVec2(build_w, bh), true, !busy))
                    act_open = true;
            }
            ImGui::SameLine(0, gap);
            if (Button(T("Play", "Jugar"), ImVec2(bw, bh), true, f.problem.empty() && !busy)) act_play = true;
            if (frame == 2 && f.problem.empty()) ImGui::SetKeyboardFocusHere(-1);
            ImGui::SameLine(0, gap);
            if (Button(T("Exit", "Salir"), ImVec2(bw, bh), false)) running = false;
            if (act_open && can_build) page = true;
            if (act_play && f.problem.empty()) result = f.folder, running = false;
        } else {
            // ---- the builder's page
            ImGui::SetCursorPos(ImVec2(pad, band + pad * .8f));
            ImGui::PushTextWrapPos(W - pad);
            ImGui::TextUnformatted(T("The game code is made once, on this PC, from your own copy. Nothing is sent anywhere.",
                                     "El código del juego se crea una vez, en esta PC, a partir de tu propia copia. No se envía "
                                     "nada a ningún lado."));
            ImGui::PopTextWrapPos();
            const bool ran = job.status != PCB_IDLE || info.interrupted;
            const bool active = job.status == PCB_RUNNING;
            const bool done = job.status == PCB_DONE;
            const float top = ImGui::GetCursorPosY() + fsz * .4f;
            const float tail_h = ran ? fsz * 2.6f : 0, bar_h = ran ? fsz * .7f : 0;
            const float bottom = by - pad * .4f - tail_h - bar_h - fsz * 1.7f;
            const float colw = (W - pad * 3) / 2, colh = bottom - top;
            // left: what it uses
            ImGui::SetCursorPos(ImVec2(pad, top));
            ImGui::BeginChild("##uses", ImVec2(colw, colh), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
            ImGui::PushTextWrapPos(0);
            Heading(T("WHAT IT USES", "QUÉ USA"));
            Line(T("Folder:", "Carpeta:"), f.folder, true);
            Line(T("Game:", "Juego:"), f.base.empty() ? T("missing", "falta") : FileName(f.base), !f.base.empty());
            Line(T("Update:", "Actualización:"), f.update.empty() ? T("missing", "falta") : FileName(f.update), !f.update.empty());
            Line(T("Makes:", "Crea:"), T("mh4u_code_native.dll (+ .id), in the folder", "mh4u_code_native.dll (+ .id), en la carpeta"),
                 true);
            Line(T("Now:", "Ahora:"),
                 f.library.empty()                      ? std::string(T("no game code yet", "todavía sin código del juego"))
                 : f.library_id == pcb_wanted_id()       ? Fmt("%s (%s)", T("up to date", "al día"), f.library_id.c_str())
                                                         : Fmt("%s (%s)", T("another version's", "de otra versión"),
                                                               f.library_id.empty() ? "?" : f.library_id.c_str()),
                 true);
            {  // the code mods it builds in (BuilderActivity's row and button)
                const PcbMods& m = mods[mods_pick];
                Line(T("Code mods:", "Mods de código:"),
                     mods.size() < 2 ? std::string(T("none in the folder (the single-screen mod goes in mods/)",
                                                     "ninguno en la carpeta (el de una sola pantalla va en mods/)"))
                     : m.flag == "--game-mods" ? T("the folder's mods/: ", "los de la carpeta mods/: ") + m.what
                     : m.flag == "--user-mods" ? T("the ones on in Settings: ", "los activados en Ajustes: ") + m.what
                                               : std::string(T("none", "ninguno")),
                     true);
            }
            ImGui::PopTextWrapPos();
            if (mods.size() >= 2 && !active) {
                ImGui::Spacing();
                if (Button(T("Change the code mods", "Cambiar los mods de código"), ImVec2(0, fsz * 1.9f), false))
                    mods_pick = (mods_pick + 1) % (int)mods.size();
            }
            if (!active && info.used > (1 << 20)) {
                ImGui::Spacing();
                if (Button(Fmt("%s (%s)###deltools", T("Delete the tools", "Borrar las herramientas"), Mb(info.used).c_str()).c_str(),
                           ImVec2(0, fsz * 1.9f), false))
                    confirm = "tools";
            }
            ImGui::EndChild();
            // right: what it needs (before a build), or how it goes
            ImGui::SetCursorPos(ImVec2(pad * 2 + colw, top));
            ImGui::BeginChild("##goes", ImVec2(colw, colh), ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PushTextWrapPos(0);
            if (!ran) {
                Heading(T("WHAT IT NEEDS", "QUÉ HACE FALTA"));
                if (info.to_download > 0)
                    Wrapped(Fmt(T("- The first time it downloads its tools, about %s: Python (python.org) and Zig (PyPI, or "
                                  "ziglang.org when PyPI can't be reached), free software, checked before they are used. "
                                  "Nothing of the game is ever downloaded.",
                                  "- La primera vez descarga sus herramientas, unos %s: Python (python.org) y Zig (PyPI, o "
                                  "ziglang.org si PyPI no responde), software libre, verificadas antes de usarlas. Nunca se "
                                  "descarga nada del juego."),
                                Mb(info.to_download).c_str()),
                            TEXT);
                else
                    Wrapped(T("- The tools are already on this PC.", "- Las herramientas ya están en esta PC."), TEXT);
                Wrapped(Fmt(T("- Space: %s free while it builds (there are %s). When it ends the temporary files go and the "
                              "tools stay (about 300 MB).",
                              "- Espacio: %s libres mientras crea (hay %s). Al terminar se borra lo temporal y quedan las "
                              "herramientas (unos 300 MB)."),
                            Mb(info.needed).c_str(), Mb(info.free).c_str()),
                        info.free < info.needed ? WARN : TEXT);
                Wrapped(T("- Time: about 10 to 40 minutes, depending on the PC (about 13 with 12 cores). It uses all the "
                          "processor's cores but one, at low priority: you can use the PC meanwhile.",
                          "- Tiempo: de unos 10 a 40 minutos, según la PC (unos 13 con 12 núcleos). Usa todos los núcleos del "
                          "procesador menos uno, con "
                          "prioridad baja: puedes usar la PC mientras tanto."),
                        TEXT);
                Wrapped(T("- If it is cut (Cancel, the window closed, the PC restarted), it goes on from where it stopped.",
                          "- Si se corta (Cancelar, la ventana cerrada, la PC reiniciada), sigue desde donde quedó."),
                        TEXT);
            } else {
                Heading(T("HOW IT GOES", "CÓMO VA"));
                const float r = fsz * .42f;
                for (int i = 0; i < PCB_STAGES; i++) {
                    const int kind = done || i < job.stage                              ? 1
                                     : i == job.stage && active                         ? 2
                                     : i == job.stage && job.status == PCB_FAILED       ? 3
                                                                                        : 0;
                    const ImVec2 c = ImGui::GetCursorScreenPos();
                    StageMark(ImGui::GetWindowDrawList(), ImVec2(c.x + r, c.y + fsz * .5f), r, kind);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + r * 2 + fsz * .5f);
                    std::string name = StageText(i);
                    if (i == 0 && info.to_download == 0 && kind == 1) name += T(" (already here)", " (ya están)");
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kind == 3 ? WARN : kind ? GOLD : TEXT));
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::Spacing();
                const int64_t now = static_cast<int64_t>(SDL_GetTicks());
                if (active) {
                    std::string eta;
                    if (job.stage == 3 && job.done >= 3 && job.total > job.done)
                        eta = Fmt(T(" - about %s left", " - quedan unos %s"),
                                  Minutes((now - job.stage_started_ms) * (job.total - job.done) / job.done).c_str());
                    Wrapped(Fmt("%s %s%s", T("Time:", "Tiempo:"), Minutes(now - job.started_ms).c_str(), eta.c_str()), TEXT);
                    if (job.jobs > 0)
                        Wrapped(Fmt(T("%d compilers at once, at low priority: you can use the PC meanwhile.",
                                      "%d compiladores a la vez, con prioridad baja: puedes usar la PC mientras tanto."),
                                    job.jobs),
                                DIM);
                } else if (job.status == PCB_FAILED) {
                    Wrapped(FailureText(job.error_stage), WARN);
                    if (job.error_text[0]) Wrapped(Fmt("%s %s", T("Details:", "Detalle:"), job.error_text), DIM);
                } else if (done) {
                    Wrapped(Fmt("%s %s. %s", T("Done in", "Listo en"), Minutes(job.ended_ms - job.started_ms).c_str(),
                                T("The game code is in your folder, beside the game.", "El código del juego está en tu carpeta, "
                                                                                      "junto al juego.")),
                            TEXT);
                } else if (job.status == PCB_CANCELLED) {
                    Wrapped(T("What it did is kept: Go on starts from there.", "Lo que hizo se guarda: Seguir empieza desde ahí."), TEXT);
                } else if (info.interrupted) {
                    Wrapped(T("The last build was interrupted: it goes on from where it stopped.",
                              "La última creación se interrumpió: sigue desde donde quedó."),
                            TEXT);
                }
            }
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
            // the bar, the status line and the build's last lines
            ImGui::SetCursorPos(ImVec2(pad, bottom + fsz * .5f));
            const int pc = done ? 100 : ran ? Percent(job) : 0;
            if (ran) ImGui::ProgressBar(std::max(0, pc) / 100.f, ImVec2(W - pad * 2, bar_h), "");
            const bool ready = !f.base.empty() && !f.update.empty() && info.scripts_ok;
            const std::string line =
                active                          ? Fmt("%s  (%d %%)", StatusLine(job).c_str(), std::max(pc, 0))
                : job.status == PCB_FAILED      ? Fmt("%s %s", T("Stopped at:", "Se detuvo en:"), StageText(job.stage))
                : job.status == PCB_CANCELLED   ? std::string(T("Cancelled. You can go on whenever you like.",
                                                                "Cancelada. Puedes seguir cuando quieras."))
                : done                          ? std::string(T("The game code is ready.", "El código del juego está listo."))
                : f.base.empty() || f.update.empty()
                    ? std::string(T("The game or its update is missing from the folder.", "Falta el juego o su actualización en la carpeta."))
                : !info.scripts_ok ? std::string(T("builder_tools.zip or zigcc.exe is missing beside MH4U.exe: unzip the whole "
                                                   "MH4U-PC zip again.",
                                                   "Falta builder_tools.zip o zigcc.exe junto a MH4U.exe: vuelve a descomprimir "
                                                   "todo el zip MH4U-PC."))
                : f.problem.empty() ? std::string(T("The game code here is up to date. Build again makes it anew.",
                                                    "El código del juego de esta carpeta está al día. Crear de nuevo lo vuelve "
                                                    "a crear."))
                                    : std::string(T("Press Build to start.", "Pulsa Crear para empezar."));
            const ImVec2 ls = ImGui::CalcTextSize(line.c_str());
            ImGui::SetCursorPos(ImVec2(std::max(pad, (W - ls.x) * .5f), bottom + fsz * .5f + bar_h + fsz * .35f));
            ImGui::PushTextWrapPos(W - pad);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(job.status == PCB_FAILED ? WARN : GOLD));
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            if (ran) {  // (the build's own words, its last two lines)
                char tail[2][160];
                const int n = pcb_tail(tail, 2);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(DIM));
                ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * .8f);
                for (int i = 0; i < n; i++) {
                    ImGui::SetCursorPos(ImVec2(pad, by - pad * .4f - tail_h + i * ImGui::GetFontSize() * 1.2f));
                    ImGui::TextUnformatted(tail[i]);
                }
                ImGui::PopFont();
                ImGui::PopStyleColor();
            }
            // the buttons: Build / Go on / Cancel / Play, Start over (a build half done), the guide (by hand), Back
            const bool half = info.half_done && !active && !done;
            const int nb = 3 + (half ? 1 : 0);
            const float bw = (W - pad * (nb + 1)) / nb;
            ImGui::SetCursorPos(ImVec2(pad, by));
            const char* main_label = active ? T("Cancel", "Cancelar")
                                     : done ? T("Play", "Jugar")
                                     : job.status == PCB_FAILED || job.status == PCB_CANCELLED || info.half_done
                                         ? T("Go on", "Seguir")
                                     : f.problem.empty() ? T("Build again", "Crear de nuevo")
                                                         : T("Build", "Crear");
            if (Button(Fmt("%s###main", main_label).c_str(), ImVec2(bw, bh), true, active || done || ready)) {
                if (active) confirm = "cancel";
                else if (done) act_play = true;
                else act_build = true;
            }
            int slot = 1;
            if (half) {
                ImGui::SameLine(pad * (slot + 1) + bw * slot);
                if (Button(T("Start over", "Empezar de cero"), ImVec2(bw, bh), false)) confirm = "startover";
                slot++;
            }
            ImGui::SameLine(pad * (slot + 1) + bw * slot);
            if (Button(T("Open the guide", "Abrir la guía"), ImVec2(bw, bh), false)) {
                const char* url = g_es ? "https://github.com/khorne-hub/4u-native/blob/main/README.es.md"
                                       : "https://github.com/khorne-hub/4u-native#readme";
                if (!SDL_OpenURL(url)) std::fprintf(stderr, "[app] the browser didn't open: %s\n", url);
            }
            slot++;
            ImGui::SameLine(pad * (slot + 1) + bw * slot);
            if (Button(T("Back", "Volver"), ImVec2(bw, bh), false, !active)) act_back = true;

            // the questions (Android's Look.notice): Cancel?, close while it builds, Start over?, Delete the tools?
            if (!confirm.empty() && !ImGui::IsPopupOpen("##ask")) ImGui::OpenPopup("##ask");
            ImGui::SetNextWindowSize(ImVec2(W * .7f, 0));
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + W * .5f, vp->Pos.y + vp->Size.y * .5f), ImGuiCond_Always, ImVec2(.5f, .5f));
            if (ImGui::BeginPopupModal("##ask", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
                const bool stop = confirm == "cancel" || confirm == "close";
                Heading(confirm == "cancel"  ? T("CANCEL?", "¿CANCELAR?")
                        : confirm == "close" ? T("CLOSE?", "¿CERRAR?")
                        : confirm == "tools" ? T("DELETE THE TOOLS", "BORRAR LAS HERRAMIENTAS")
                                             : T("START OVER?", "¿EMPEZAR DE CERO?"));
                const std::string body =
                    stop ? std::string(T("What is done is kept: the next time it goes on from where it stopped.",
                                         "Lo ya hecho se guarda: la próxima vez sigue desde donde quedó."))
                    : confirm == "tools"
                        ? Fmt(T("The tools and anything left from a build are deleted (%s). The game code you already made "
                                "stays in your folder. If you build it again, they are downloaded again.",
                                "Se borran las herramientas y lo que haya quedado de una creación (%s). El código del juego que "
                                "ya creaste queda en tu carpeta. Si vuelves a crearlo, se descargan otra vez."),
                              Mb(info.used).c_str())
                        : std::string(T("What this build has done so far is deleted, and the next one starts from the "
                                        "beginning. Your game folder, its files and any game code already made stay as they are. "
                                        "The downloaded tools stay too, unless you choose to delete them (a later build "
                                        "downloads them again).",
                                        "Se borra lo que esta creación lleva hecho, y la próxima empieza desde el principio. Tu "
                                        "carpeta del juego, sus archivos y el código del juego que ya hayas creado quedan como "
                                        "están. Las herramientas descargadas también quedan, salvo que elijas borrarlas (una "
                                        "creación posterior las vuelve a descargar)."));
                ImGui::PushTextWrapPos(0);
                ImGui::TextUnformatted(body.c_str());
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
                const float aw = ImGui::GetContentRegionAvail().x;
                if (confirm == "startover") {
                    if (Button(T("Start over", "Empezar de cero"), ImVec2(aw, bh), true)) act_startover = true, confirm.clear();
                    if (Button(T("Start over and delete the tools", "Empezar de cero y borrar las herramientas"), ImVec2(aw, bh), false))
                        act_tools = true, confirm.clear();
                } else if (confirm == "tools") {
                    if (Button(T("Delete", "Borrar"), ImVec2(aw, bh), true)) act_deltools = true, confirm.clear();
                } else if (confirm == "cancel") {
                    if (Button(T("Cancel the build", "Cancelar la creación"), ImVec2(aw, bh), true)) act_cancel = true, confirm.clear();
                } else if (confirm == "close") {
                    if (Button(T("Stop and close", "Detener y cerrar"), ImVec2(aw, bh), true))
                        act_cancel = true, want_close = true, confirm.clear();
                }
                if (!confirm.empty() && (Button(stop ? T("Keep building", "Seguir creando") : T("Cancel", "Cancelar"), ImVec2(aw, bh), false) ||
                                         ImGui::IsKeyPressed(ImGuiKey_Escape)))
                    confirm.clear();
                if (confirm.empty()) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            if (act_cancel && active) pcb_cancel();
            if (act_build && !active && !done && ready) pcb_start(f.folder, f.base, f.update, mods[mods_pick]);
            if (act_startover && !active) pcb_start_over(false);
            if (act_tools && !active) pcb_start_over(true);
            if (act_deltools && !active) pcb_delete_tools();
            if (done && job.ended_ms != looked_at) f = pc_look_at_folder(f.folder, g_es), looked_at = job.ended_ms;  // (once)
            if ((act_play && done) || (act_back && !active)) {
                const PcGameFolder now = pc_look_at_folder(f.folder, g_es);
                if (now.problem.empty() && (act_play || open_builder || done)) {  // (Play, or Back to the game it can play now)
                    result = now.folder, running = false;
                } else {  // (Back to the folder screen, which says what is wrong)
                    page = false, f = now;
                }
            }
        }
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 50, 25, 24, 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);
        if (!shot_now.empty()) {
            if (SDL_Surface* s = SDL_RenderReadPixels(ren, nullptr)) SDL_SavePNG(s, shot_now.c_str()), SDL_DestroySurface(s);
            std::fprintf(stderr, "[app] the game folder screen saved to %s\n", shot_now.c_str());
            shot_now.clear();
        }
        if (shot_at >= 0 && frame >= shot_at) {
            if (SDL_Surface* s = SDL_RenderReadPixels(ren, nullptr)) {
                const bool saved = SDL_SavePNG(s, test_shot);
                std::fprintf(stderr, "[app] the game folder screen saved to %s: %s\n", test_shot, saved ? "ok" : SDL_GetError());
                SDL_DestroySurface(s);
            }
            shot_at = -1, test_shot = nullptr;
            if (test_play != nullptr && *test_play != 0) {  // (1: play it if it can be played; 0: leave)
                if (*test_play == '1' && f.problem.empty()) result = f.folder;
                running = false;
            }
        }
        SDL_RenderPresent(ren);
        // (at most 60 frames a second: a hidden or minimized window's VSync doesn't wait, and the build wants the CPU)
        static uint64_t last_frame;
        const uint64_t now_ns = SDL_GetTicksNS();
        if (now_ns - last_frame < 16000000ull) SDL_DelayNS(16000000ull - (now_ns - last_frame));
        last_frame = SDL_GetTicksNS();
    }
    if (pcb_running()) {  // (closed while it builds: the build stops, what it did is kept)
        pcb_cancel();
        for (int i = 0; i < 500 && pcb_running(); i++) SDL_Delay(20);
    }
    if (checker.joinable()) checker.join();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(previous);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    std::fprintf(stderr, "[app] the game folder screen: %s\n", result.empty() ? "left" : ("play " + result).c_str());
    return result;
}

int pc_message_screen(const std::string& title, const std::string& text, const std::vector<std::string>& wide,
                      const std::vector<std::string>& pair, bool es, const char* shot) {
    g_es = es;
    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale < 1.f) scale = 1.f;
    SDL_Window* win = SDL_CreateWindow("MH4U", static_cast<int>(900 * scale), static_cast<int>(560 * scale),
                                       SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (win == nullptr) return -1;
    if (const char* at = SDL_getenv("MH4U_WINDOW_AT"); at != nullptr && *at != 0) {  // (the display the game was on)
        SDL_Point p{0, 0};
        if (SDL_sscanf(at, "%d,%d", &p.x, &p.y) == 2)
            if (SDL_DisplayID d = SDL_GetDisplayForPoint(&p); d != 0)
                SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED_DISPLAY(d), SDL_WINDOWPOS_CENTERED_DISPLAY(d));
    }
    SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
    if (ren == nullptr) {
        SDL_DestroyWindow(win);
        return -1;
    }
    SDL_SetRenderVSync(ren, 1);
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGuiStyle& st = ImGui::GetStyle();  // (the game folder screen's look)
    st.WindowRounding = 0, st.FrameRounding = 3, st.WindowBorderSize = 0, st.FramePadding = ImVec2(14, 8);
    st.ChildRounding = 6, st.ChildBorderSize = 1;
    st.Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(TEXT);
    st.Colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(PANEL);
    st.Colors[ImGuiCol_ChildBg] = ImGui::ColorConvertU32ToFloat4(PLATE);
    st.Colors[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(GOLD);
    st.Colors[ImGuiCol_NavCursor] = ImGui::ColorConvertU32ToFloat4(GOLD_BRIGHT);
    st.ScaleAllSizes(scale);
    LoadFont(22.0f * scale);
    ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer3_Init(ren);

    const int n = static_cast<int>(wide.size() + pair.size());
    int answer = -1, frame = 0;
    bool running = true;
    while (running) {
        ++frame;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_ESCAPE) running = false;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        const float fsz = ImGui::GetFontSize(), band = fsz * 2.9f, pad = fsz * 1.2f, W = vp->Size.x, w = W - 2 * pad;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
        ImGui::Begin("##message", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + W, vp->Pos.y + band), HEADER);
        dl->AddRectFilled(ImVec2(vp->Pos.x, vp->Pos.y + band), ImVec2(vp->Pos.x + W, vp->Pos.y + band + 3 * scale), RED_RULE);
        dl->AddRectFilled(ImVec2(vp->Pos.x, vp->Pos.y + band + 3 * scale), ImVec2(vp->Pos.x + W, vp->Pos.y + band + 5 * scale),
                          GREEN_RULE);
        dl->AddText(ImVec2(vp->Pos.x + pad, vp->Pos.y + (band - fsz) * .5f), GOLD, title.c_str());
        // the buttons from the bottom: the pair side by side last, each wide one on a row of its own above it
        const float bh = fsz * 2.2f, gap = pad * .5f;
        const int rows = static_cast<int>(wide.size()) + (pair.empty() ? 0 : 1);
        const float top_buttons = vp->Size.y - pad - rows * bh - (rows - 1) * gap;
        if (frame == 2) {  // (the window as tall as its text and buttons need, 300 to 820 points, centred again)
            const float text_h = ImGui::CalcTextSize(text.c_str(), nullptr, false, w - st.WindowPadding.x * 2).y;
            const float want = band + pad + text_h + st.WindowPadding.y * 2 + gap + rows * bh + (rows - 1) * gap + pad;
            int ww = 0, wh = 0, pw = 0, ph = 0;
            SDL_GetWindowSize(win, &ww, &wh);
            SDL_GetWindowSizeInPixels(win, &pw, &ph);
            const float k = ph > 0 ? static_cast<float>(wh) / ph : 1.f;  // (window points a pixel)
            const int h = static_cast<int>(std::clamp(want * k, 300 * scale * k, 820 * scale * k));
            if (std::abs(h - wh) > 4) {
                SDL_DisplayID d = SDL_GetDisplayForWindow(win);
                SDL_SetWindowSize(win, ww, h);
                SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED_DISPLAY(d), SDL_WINDOWPOS_CENTERED_DISPLAY(d));
            }
        }
        ImGui::SetCursorPos(ImVec2(pad, band + pad));
        ImGui::BeginChild("##text", ImVec2(w, top_buttons - gap - band - pad), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PushTextWrapPos(0);
        Wrapped(text, TEXT);
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        float y = top_buttons;
        for (size_t i = 0; i < wide.size(); i++, y += bh + gap) {
            ImGui::SetCursorPos(ImVec2(pad, y));
            if (Button(Fmt("%s##w%zu", wide[i].c_str(), i).c_str(), ImVec2(w, bh), i == 0)) answer = static_cast<int>(i), running = false;
            if (frame == 2 && i == 0) ImGui::SetKeyboardFocusHere(-1);
        }
        if (!pair.empty()) {
            const float pw = (w - gap * (pair.size() - 1)) / pair.size();
            for (size_t i = 0; i < pair.size(); i++) {
                ImGui::SetCursorPos(ImVec2(pad + i * (pw + gap), y));
                if (Button(Fmt("%s##p%zu", pair[i].c_str(), i).c_str(), ImVec2(pw, bh), wide.empty() && i == 0))
                    answer = static_cast<int>(wide.size() + i), running = false;
                if (frame == 2 && wide.empty() && i == 0) ImGui::SetKeyboardFocusHere(-1);
            }
        }
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 50, 25, 24, 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);
        if (shot != nullptr && frame == 20) {  // (a test: the screen saved, then the answer it was given)
            if (SDL_Surface* s = SDL_RenderReadPixels(ren, nullptr)) SDL_SavePNG(s, shot), SDL_DestroySurface(s);
            running = false;
        }
        SDL_RenderPresent(ren);
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(previous);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    return answer < n ? answer : -1;
}
