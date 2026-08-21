// ============================================================================
//  UnlockEngrams — one chat command, `!engrams`, that unlocks every building
//                  and crafting knowledge for whoever typed it.
//
//  WHAT "ENGRAM" MEANS HERE
//  ------------------------
//  Conan doesn't use that word. Players coming from ARK do, and they mean the
//  same thing: the knowledge that lets you build. In Conan it's split in two,
//  and unlocking only one of them leaves the player halfway:
//
//    FEATS    the knowledge tree. Buying a feat is what teaches the recipes
//             attached to it. This is the part that costs points in game.
//    RECIPES  the individual craftable entries. A character has a flag,
//             `UnlockAllRecipes`, that short-circuits the whole check.
//
//  So this plugin does both. Feats alone would leave recipes the tree doesn't
//  cover; recipes alone would leave the feat window empty and confuse anyone
//  who opens it.
//
//  WHY THE CHEAT MANAGER IS NOT USED
//  ---------------------------------
//  The obvious route is `ConanCheatManager::LearnAllFeats()`. This plugin used
//  it, and it produced the worst kind of result: the call was DISPATCHED, the
//  API correctly reported that it ran, the player was told everything was
//  unlocked — and the knowledge window still showed padlocks and "not enough
//  points".
//
//  The cheat functions return early for a player who is not an admin. Measured
//  on a live server: `cheat manager=ConanCheatManager  admin=no`, both calls
//  reported as executed, nothing learned.
//
//  THE LESSON IS ABOUT THE INSTRUMENT, NOT THE FUNCTION.
//  `UltimaChamadaExecutou()` answers "was the UFunction dispatched?" — truthfully.
//  It cannot answer "did the body do any work?", and reading it as if it could
//  is how a plugin ends up lying to a player with total confidence. Anything
//  that matters has to be verified by READING THE RESULTING STATE.
//
//  HOW IT ACTUALLY REACHES THEM
//  ----------------------------
//  Straight at the player's own progression component, which needs no admin and
//  no CheatManager at all:
//
//      ConanCharacter::GetProgressionSystem()          -> the component
//      ProgressionSystem::ServerForceLearnFeat(id,...) -> learn, FORCED
//      ProgressionSystem::IsFeatPurchased(id) -> bool  -> the proof
//
//  FORCED is the whole point. `ServerPurchaseFeat` is the shop path and checks
//  what you'd expect — `CheckFeatCost`, `CheckFeatLevel`,
//  `CheckFeatPrerequisite`. `ServerForceLearnFeat` is the game's own way of
//  granting a feat regardless: no points spent, no level requirement, no
//  prerequisite walk. That is exactly what "unlock everything" means, and it is
//  the game doing it, not us writing over its data.
//
//  The list of feats comes from the game's own DataTable — see LearnEveryFeat
//  below for why the object array is the wrong source and how that mistake
//  produced a flawless-looking result against the wrong population.
//
//  AND EVERY SINGLE ONE IS VERIFIED
//  --------------------------------
//  For each feat: ask `IsFeatPurchased` before, force it, ask again. The counts
//  the log prints — learned / already had / refused — are counts of CONFIRMED
//  state, not of calls made. If Funcom changes something, the number moves and
//  the log says so on the first use.
//
//  WHAT "REFUSED" MEANS, AND WHY IT IS NOT A BUG
//  ---------------------------------------------
//  The base FeatTable is not just the base game: it carries every DLC's feats
//  too, tagged in `FeatTableRow.DLCPackage`. The game refuses to grant a feat
//  belonging to a DLC the account does not own, and it is right to.
//
//  Proved with two accounts on one server, same code, same 2346-row table:
//
//      account A   913 learned · 101 already had · 1332 refused
//      account B   871 learned · 346 already had · 1129 refused
//
//  Identical table, different refusals. A defect in this plugin would have
//  refused the same rows for both. The game's own knowledge window agrees: the
//  remaining padlocks read "DLC missing", not "not enough points".
//
//  `ConanCheatManager::SetBypassEntitlements` exists on this build and would
//  step over that. THIS PLUGIN WILL NOT USE IT. DLC is content Funcom sells;
//  handing it out for free is not a feature, and no server owner should get it
//  by accident from a plugin that unlocks building knowledge.
//
//  THE RECIPES HALF
//  ----------------
//  `ConanCharacter.UnlockAllRecipes` is a replicated flag on the character. The
//  plugin sets the bit, calls the game's own `OnRep_UnlockAllRecipes` so the
//  reaction runs, and then READS THE BIT BACK. Same rule as the feats: the
//  report is what the state says, never what the call returned.
//
//  Three outcomes, never two: everything · half · nothing. Each one has its own
//  message and its own log line. A plugin that answers nothing is
//  indistinguishable from a broken one — this project has paid for that lesson
//  once already, in Conan Shop.
//
//  WHO CAN RUN IT
//  --------------
//  By default, anybody. That is a deliberate default for a server that installs
//  this on purpose: the whole point is handing out the knowledge. Set
//  `"permission"` in the config to a Permission node (say `engrams.use`) and it
//  becomes restricted — and if Permission isn't installed, the plugin says so
//  instead of quietly letting everyone through.
//
//  A NOTE ON LANGUAGE
//  ------------------
//  The source is English throughout, including what goes to the log. The text
//  players read is not in here at all: it lives in config.json, because it is
//  data, and a server whose players read another language changes that file
//  without touching a line of code.
//
//  BUILD
//     ./compilar.sh          (Linux/WSL)      compilar.bat  (Windows)
//  INSTALL
//     copy the folder into <server>/ConanSandbox/Binaries/Win64/Conan-Api/Plugins/
//
//  The folder has a HYPHEN: `Conan-Api`, never run together.
// ============================================================================
#include "Conan/ConanPluginApi.h"
#include "Conan/ConanBase.h"
#include "Conan/ConanPermission.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

// The table the loader hands over. Everything this plugin knows how to do with
// the game goes through it; nothing of the runtime is linked in here.
static const ConanApiTabela* g_api = nullptr;

// ── offsets inside ChatRpcData ──────────────────────────────────────────────
//
// Measured from reflection, not guessed. The struct is 128 bytes and arrives BY
// VALUE, so it starts at the beginning of the parameter block. This is the only
// raw offset in the file, and it's here because the parameter is an RPC struct
// that reflection doesn't decompose.
static const uint32_t CHAT_TEXT = 0x068;   // FString Message

// ── what the server owner can change ────────────────────────────────────────
//
// Read from config.json beside the plugin. Everything has a default that works,
// so a server that never edits the file still gets a working command.
static std::string g_command    = "!engrams";
static std::string g_permission = "";        // empty = anybody can use it
static std::string g_msgOk      = "Everything is unlocked. You can build anything now.";
static std::string g_msgDenied  = "You are not allowed to use this command.";
static std::string g_msgPartial = "Some of the knowledge was unlocked. If something still won't build, tell an admin: the reason is in ConanApi.log.";
static std::string g_msgFailed  = "I could not unlock the knowledge. Tell an admin: the reason is in ConanApi.log.";

// ── reading a pointer member BY NAME ────────────────────────────────────────
//
// The pattern every plugin in this SDK repeats, for the same reason: resolve
// the offset through reflection once, read the pointer, check it's readable
// before handing it back.
static void* PointerMember(void* obj, const char* name)
{
    if (!obj || !g_api) return nullptr;
    const int32_t off = g_api->OffsetDoMembro(obj, name);
    if (off < 0) return nullptr;                 // doesn't exist on this build
    void* p = nullptr;
    if (g_api->LerMembro(obj, uint32_t(off), &p, sizeof(p)) <= 0) return nullptr;
    // `Legivel` says the memory is MAPPED, not that the object is alive. It's
    // here to reject an obviously invalid pointer before touching it.
    if (!p || !g_api->Legivel(p, 8)) return nullptr;
    return p;
}

// ── putting the real numbers into the player's message ──────────────────────
//
// Two placeholders, and both are counts of confirmed state:
//
//   {n}    how many feats this command actually granted
//   {dlc}  how many the GAME refused — content the account does not own
//
// `{dlc}` exists because of what a live test showed. Two accounts, same server,
// same code, same 2346-row table: one was refused 1332 feats and the other
// 1129. The difference is which DLC each account owns, and a player who is told
// only "913 unlocked" while a padlock stays on their screen has been told half
// the truth. The game's own knowledge window says "DLC missing" there; the
// plugin should not say less.
//
// A plain search-and-replace, NOT a format string: the text comes from a file
// the server owner edits, and handing edited text to printf would turn a stray
// `%s` in somebody's translation into a crash on the game thread.
//
// A message without the placeholders is left exactly as written — they are an
// offer, not a requirement.
static void Trocar(std::string& texto, const char* marca, int valor)
{
    char num[16];
    std::snprintf(num, sizeof(num), "%d", valor);
    const size_t n = std::strlen(marca);
    for (size_t p = texto.find(marca); p != std::string::npos;
         p = texto.find(marca, p + std::strlen(num)))
        texto = texto.substr(0, p) + num + texto.substr(p + n);
}

static std::string WithCount(const std::string& text, int n, int dlc)
{
    std::string saida = text;
    Trocar(saida, "{n}", n);
    Trocar(saida, "{dlc}", dlc);
    return saida;
}

// ── answering the player ────────────────────────────────────────────────────
//
// Straight at the controller, never by looking the player up by name. Conan
// Shop learned this the expensive way: its first version searched for a
// CheatManager to deliver text to somebody it already had in hand, and answered
// nothing at all — the hook fired, the message was cancelled, and the reply
// died without a line in the log.
//
// The routes are tried in order and the log says which one took it, once. The
// day a patch moves this, it shows up in the log instead of silencing the
// plugin.
static void Reply(void* controller, const std::string& text)
{
    if (!controller || text.empty() || !g_api) return;

    static int route = 0;   // 0 = unknown · 1..2 = the route that worked · -1 = none

    auto tryRoute = [&](int which) -> bool
    {
        if (which == 1)
        {
            // The HUD's own notification: where the game already talks to the
            // player, so it appears where they are looking.
            ConanApi::Call<void>(controller, "ClientHUDShowNotification",
                                 ConanApi::TextoRico(text.c_str()),
                                 bool(true),     // positive
                                 bool(false));   // no sound: it's a command reply
            return g_api->UltimaChamadaExecutou() != 0;
        }
        if (which == 2)
        {
            // Unreal's PlayerController system message.
            ConanApi::Call<void>(controller, "ClientMessage",
                                 ConanApi::Texto(text.c_str()),
                                 ConanApi::Nome("Event"),
                                 float(8.0f));
            return g_api->UltimaChamadaExecutou() != 0;
        }
        return false;
    };

    if (route > 0 && tryRoute(route)) return;

    for (int i = 1; i <= 2; ++i)
    {
        if (i == route) continue;                // already tried above
        if (!tryRoute(i)) continue;
        if (route != i)
        {
            static const char* const NAMES[] = { "", "ClientHUDShowNotification",
                                                  "ClientMessage" };
            g_api->Log("[engrams] talking to the player through %s", NAMES[i]);
            route = i;
        }
        return;
    }

    if (route != -1)
    {
        route = -1;
        g_api->Log("[engrams] CANNOT answer the player: neither "
                   "ClientHUDShowNotification nor ClientMessage worked on this "
                   "build. The command may have worked anyway — check the lines "
                   "above.");
    }
}

// ── the work itself ─────────────────────────────────────────────────────────
//
// Knowledge in Conan is two separate things, and the plugin reports them
// separately because half of it working is a real outcome that neither "done"
// nor "failed" describes honestly:
//
//   feats    the knowledge tree, the part that costs points in game
//   recipes  the individual craftable entries (`UnlockAllRecipes`)
//
// Nothing here is counted as done unless the game CONFIRMED it ran.
// `UltimaChamadaExecutou()` is what separates "the function answered" from "the
// function doesn't exist on this build" — without it, a function renamed in a
// patch would look like a success and the player would be told they can build
// everything while nothing changed.
struct Result
{
    // Feats, counted in CONFIRMED state — every number below was checked with
    // IsFeatPurchased, not inferred from a call returning.
    int  seen    = 0;        // FeatItem objects walked
    int  learned = 0;        // were locked, are now purchased
    int  already = 0;        // the player already had them
    int  refused = 0;        // forced, and still not purchased
    bool recipes = false;    // the flag, read back after writing
    std::string why;         // why, when something didn't happen

    bool feats() const { return learned > 0 || (seen > 0 && refused == 0); }
};

// The player's own progression component. No CheatManager, no admin: this is
// the object the game itself uses when an NPC teacher grants a feat.
static void* FindProgression(void* pawn)
{
    if (!pawn) return nullptr;
    void* prog = ConanApi::Call<void*>(pawn, "GetProgressionSystem");
    if (g_api->UltimaChamadaExecutou() == 0) return nullptr;
    // `Legivel` says the memory is MAPPED, not that the object is alive — it
    // rejects an obviously invalid pointer before anything is called on it.
    if (!prog || !g_api->Legivel(prog, 8)) return nullptr;
    return prog;
}

// The character behind a controller. Tried in the order the engine fills them.
static void* FindPawn(void* controller)
{
    if (void* p = PointerMember(controller, "Pawn"))             return p;
    if (void* p = PointerMember(controller, "Character"))        return p;
    if (void* p = PointerMember(controller, "AcknowledgedPawn")) return p;
    return nullptr;
}

// ── every feat in the build ─────────────────────────────────────────────────
//
// FROM THE TABLE, NOT FROM MEMORY. The first version of this walked
// `FindObjects("FeatItem", ...)`, which looks right and is wrong: the game only
// instantiates a `FeatItem` for a feat the character already knows. Measured on
// a live server with a level-26 character:
//
//     feats: 98 walked · 0 learned now · 98 already had · 0 refused
//
// 98 objects, 98 already owned, nothing refused — a perfect score against the
// wrong population. The knowledge window still showed padlocks, because the
// hundreds of feats the player did NOT have were never objects to begin with.
//
// The canonical source is the DataTable, the same place the game reads: its row
// names ARE the template ids. That is the whole list, whether or not anybody
// has ever learned them.
struct FNameCru   { int32_t indice; int32_t numero; };
struct FStringCru { void* dados; int32_t num; int32_t max; };

// A game FString is UTF-16 with a count; `num` includes the terminator. Only
// the ASCII part is kept here: this text is a DLC package name used for
// grouping in the log, never something a player reads.
static void TextoDeFString(const FStringCru& s, std::string& saida)
{
    saida.clear();
    if (!s.dados || s.num <= 1) return;
    if (!g_api->Legivel(s.dados, size_t(s.num) * 2)) return;
    const uint16_t* u = static_cast<const uint16_t*>(s.dados);
    for (int i = 0; i < s.num - 1; ++i)
    {
        const uint16_t c = u[i];
        if (!c) break;
        saida += (c < 0x80) ? char(c) : '?';
    }
}

static const int MAX_TABELAS = 8192;
static void* g_tabelas[MAX_TABELAS];    // static, not stack: 64 KB is not a
                                        // frame budget, and the game thread is
                                        // the only caller.

// The table is found by name, and every near miss is printed the first time.
// "I didn't find it" without the list is a dead end — a modded server can name
// it differently, and then the log is the only thing that says so.
static void* FindFeatTable()
{
    const int n = g_api->FindObjects("DataTable", g_tabelas, MAX_TABELAS, /*subclasses=*/1);
    if (n <= 0)
    {
        g_api->Log("[engrams] no DataTable in the world. Has the server finished "
                   "loading? The tables arrive with the world, not with the "
                   "process.");
        return nullptr;
    }

    char nome[256];
    void* achada = nullptr;
    static bool listou = false;

    for (int i = 0; i < n; ++i)
    {
        nome[0] = 0;
        if (!g_api->NomeDoObjeto(g_tabelas[i], nome, sizeof(nome))) continue;
        if (std::strncmp(nome, "Default__", 9) == 0) continue;   // the CDO is not a table

        if (std::strcmp(nome, "FeatTable") == 0) { achada = g_tabelas[i]; break; }
    }

    if (!listou)
    {
        listou = true;
        g_api->Log("[engrams] %d DataTable(s) in the world; FeatTable %s.",
                   n, achada ? "found" : "NOT found");
        // Everything that even mentions a feat, so a different name shows up
        // here instead of being guessed at.
        for (int i = 0; i < n; ++i)
        {
            nome[0] = 0;
            if (!g_api->NomeDoObjeto(g_tabelas[i], nome, sizeof(nome))) continue;
            if (std::strncmp(nome, "Default__", 9) == 0) continue;
            for (char* p = nome; *p; ++p)
                if ((p[0]=='F'||p[0]=='f') && (p[1]=='e'||p[1]=='E') &&
                    (p[2]=='a'||p[2]=='A') && (p[3]=='t'||p[3]=='T'))
                { g_api->Log("[engrams]    table with \"feat\" in the name: %s", nome); break; }
        }
    }
    return achada;
}

static void LearnEveryFeat(void* prog, Result& r)
{
    void* tabela = FindFeatTable();
    if (!tabela) return;

    void* lib = g_api->GetDefaultObject("DataTableFunctionLibrary");
    if (!lib)
    {
        g_api->Log("[engrams] no DataTableFunctionLibrary on this build — no way "
                   "to read the table.");
        return;
    }

    // The row names are the template ids. 65,536 is the same ceiling the item
    // extractor uses, and the game has nothing like that many feats.
    std::vector<FNameCru> linhas(65536u);
    int n = 0;
    ConanApi::CallSaida(lib, "GetDataTableRowNames", tabela,
                        ConanApi::ParaForaLista(linhas.data(), 65536, n));
    if (n <= 0)
    {
        g_api->Log("[engrams] FeatTable exists and returned ZERO rows. Zero is a "
                   "hypothesis, not a conclusion — the table may not be "
                   "populated yet.");
        return;
    }
    g_api->Log("[engrams] FeatTable: %d row(s) — this is every feat in the build.", n);

    // ── which package each row belongs to ───────────────────────────────────
    //
    // `FeatTableRow.DLCPackage` is why this is read. The base table is NOT just
    // the base game: it carries every DLC's feats too, tagged by package. When
    // the game refuses to grant one, "how many were refused" is a number
    // nobody can act on — "how many, and from which package" is a diagnosis.
    //
    // The column is optional here on purpose. If a future build renames it, the
    // unlocking still runs and only the grouping is lost.
    std::vector<FStringCru> colDlc((size_t)n);
    std::vector<std::string> pacote((size_t)n);
    int nDlc = 0;
    ConanApi::CallSaida(lib, "GetDataTableColumnAsString",
                        tabela, ConanApi::Nome("DLCPackage"),
                        ConanApi::ParaRetornoLista(colDlc.data(), n, nDlc));
    if (g_api->UltimaChamadaExecutou() && nDlc > 0)
        for (int i = 0; i < nDlc && i < n; ++i)
            TextoDeFString(colDlc[size_t(i)], pacote[size_t(i)]);
    else
        g_api->Log("[engrams] could not read the DLCPackage column — the unlock "
                   "still runs, only the per-package breakdown is lost.");

    // Refusals grouped by package. A fixed little table rather than a std::map:
    // the packages are a handful and this runs on the game thread.
    struct Grupo { std::string nome; int quantos; };
    std::vector<Grupo> recusadosPor;

    char buf[64];
    for (int i = 0; i < n; ++i)
    {
        buf[0] = 0;
        if (g_api->NomeDeFName(linhas[size_t(i)].indice, buf, sizeof(buf)) <= 0) continue;
        const long id = std::strtol(buf, nullptr, 10);
        if (id <= 0) continue;      // a row name that isn't a number

        ++r.seen;

        // Asking first is not an optimisation. A feat the player bought with
        // their own points must not be counted as something this command gave
        // them, or the number in the log stops meaning anything.
        if (ConanApi::Call<bool>(prog, "IsFeatPurchased", int32_t(id))
            && g_api->UltimaChamadaExecutou() != 0)
        {
            ++r.already;
            continue;
        }

        // fromNPC=false        it was not a teacher NPC who taught this
        // suppressReports=true no popup per feat: there are hundreds
        // bUpdateJourneys=false the journey system is not this command's job
        ConanApi::Call<void>(prog, "ServerForceLearnFeat",
                             int32_t(id), bool(false), bool(true), bool(false));

        // THE ONLY LINE THAT DECIDES. Not whether the call returned — whether
        // the game now says the feat is purchased.
        if (ConanApi::Call<bool>(prog, "IsFeatPurchased", int32_t(id))
            && g_api->UltimaChamadaExecutou() != 0)
        {
            ++r.learned;
            continue;
        }

        ++r.refused;
        const std::string& p = (i < int(pacote.size()) && !pacote[size_t(i)].empty())
                                 ? pacote[size_t(i)]
                                 : std::string();
        const std::string chave = p.empty() ? std::string("(base game)") : p;
        bool achou = false;
        for (size_t g = 0; g < recusadosPor.size(); ++g)
            if (recusadosPor[g].nome == chave) { ++recusadosPor[g].quantos; achou = true; break; }
        if (!achou) recusadosPor.push_back(Grupo{ chave, 1 });
    }

    if (!recusadosPor.empty())
    {
        g_api->Log("[engrams] refused, by package — this is the answer to "
                   "\"why not all of them\":");
        for (size_t g = 0; g < recusadosPor.size(); ++g)
            g_api->Log("[engrams]    %6d  %s",
                       recusadosPor[g].quantos, recusadosPor[g].nome.c_str());
    }
}

// The recipes half. The flag lives on the character, is replicated, and the
// game reacts to it through OnRep — no CheatManager anywhere in this path.
//
// The bit is written rather than the byte. On this build the property is a
// native bool (fieldmask 255), so the two are the same thing today — but a
// patch that packs it next to another flag would make a byte write clobber the
// neighbour, with no error and no way to notice.
static bool RecipesViaCharacter(void* pawn)
{
    if (!pawn) return false;

    const int32_t off = g_api->OffsetDoMembro(pawn, "UnlockAllRecipes");
    if (off < 0)
    {
        g_api->Log("[engrams] ConanCharacter.UnlockAllRecipes does not exist on "
                   "this build of the game.");
        return false;
    }

    if (g_api->EscreverBit(pawn, uint32_t(off), 1, 1) <= 0)
    {
        g_api->Log("[engrams] could not write UnlockAllRecipes at +0x%X.",
                   unsigned(off));
        return false;
    }

    // Writing the flag changes the server. The client only finds out because
    // the property replicates — and OnRep is what runs the game's own reaction
    // to the change, which on the server nobody calls for us. Without it the
    // server would believe one thing and the player's screen show another.
    //
    // This line is logged BEFORE the call, not after, and that is deliberate.
    // OnRep is a path the engine normally runs on clients; if it ever turns out
    // to touch something that only exists there, the server dies inside this
    // call and an after-the-fact log would never be written. Whoever reads the
    // log then finds the last thing attempted instead of silence.
    g_api->Log("[engrams] setting UnlockAllRecipes at +0x%X (replicated=%s), "
               "then calling OnRep_UnlockAllRecipes.",
               unsigned(off),
               g_api->EhReplicado(pawn, uint32_t(off)) > 0 ? "yes" : "NO");

    ConanApi::Call<void>(pawn, "OnRep_UnlockAllRecipes");
    const bool onRep = g_api->UltimaChamadaExecutou() != 0;

    // READ IT BACK. Writing returned success and calling returned success; both
    // are statements about the attempt, and this plugin was already burned once
    // by treating those as statements about the result. Only the bit itself
    // says whether the character carries the flag now.
    const bool set = g_api->LerBit(pawn, uint32_t(off), 1) > 0;

    g_api->Log("[engrams] recipes flag: now %s (OnRep=%s).",
               set ? "SET" : "still clear",
               onRep ? "called" : "did not answer");
    return set;
}

static Result UnlockEverything(void* controller)
{
    Result r;

    void* pawn = FindPawn(controller);
    if (!pawn)
    {
        r.why = "the player has no living character (Pawn is null)";
        return r;
    }

    void* prog = FindProgression(pawn);
    if (!prog)
    {
        r.why = "ConanCharacter::GetProgressionSystem did not answer on this "
                "build — no way to reach the feat tree";
    }
    else
    {
        LearnEveryFeat(prog, r);
        g_api->Log("[engrams] feats: %d walked · %d learned now · %d already had "
                   "· %d refused",
                   r.seen, r.learned, r.already, r.refused);

        // Every single one refused is not "some feats are special" — it is the
        // signature of a route that isn't working at all, and it deserves to be
        // named rather than left for someone to infer from an odd number.
        if (r.seen > 0 && r.learned == 0 && r.already == 0)
            g_api->Log("[engrams] EVERY feat was refused. Either "
                       "ServerForceLearnFeat or IsFeatPurchased is not behaving "
                       "as expected on this build — the route, not the feats.");
    }

    r.recipes = RecipesViaCharacter(pawn);

    const bool feats = r.feats();
    if (!feats && !r.recipes)
    {
        if (r.why.empty())
            r.why = "no feat could be forced and the recipes flag stayed clear";
    }
    else if (!feats)
    {
        if (r.why.empty())
            r.why = "the recipes are unlocked but no feat could be forced";
        g_api->Log("[engrams] PARTIAL: recipes yes, feats no — %s.", r.why.c_str());
    }
    else if (!r.recipes)
    {
        r.why = "the feats are unlocked but the recipes flag stayed clear";
        g_api->Log("[engrams] PARTIAL: feats yes, recipes no.");
    }

    return r;
}

// ── may this player run it? ─────────────────────────────────────────────────
//
// With no `permission` configured, anybody can — the deliberate default for a
// server that installed this on purpose.
//
// With one configured and Permission NOT installed, the answer is NO. Letting
// everyone through would hand out a restricted command because a dependency is
// missing, which is the opposite of what the server owner asked for by naming
// a node.
static bool MayRun(const std::string& id, std::string& why)
{
    if (g_permission.empty()) return true;

    const ConanPermApi* perm = ConanPermObter();
    if (!perm || !perm->tem)
    {
        why = "config.json requires the permission \"" + g_permission +
              "\" but ConanPermission.dll is not installed";
        return false;
    }
    // Third argument: what to answer if Permission is absent. It cannot be
    // absent here (checked above), and 0 is the conservative choice anyway.
    if (ConanPermTem(id.c_str(), g_permission.c_str(), /*if_absent=*/0) != 1)
    {
        why = "the player does not have the node \"" + g_permission + "\"";
        return false;
    }
    return true;
}

// ── the chat hook ───────────────────────────────────────────────────────────
//
// extern "C", taking a ConanChamada*: the signature is plain C ABI, which is
// what lets the runtime live in one binary and this callback in another.
//
// THE PREFIX IS `!`, NOT `/`. Measured on a live server: Conan's client
// swallows `/command` locally and never sends it, so no plugin in any API gets
// to see it. The on-screen symptom is identical to a hook that cancelled ("it
// vanished from chat"), and only the server log tells them apart.
extern "C" ConanAcao OnChat(ConanChamada* c)
{
    if (!c || !c->Parms || c->ParmsSize < 0x80) return CONAN_CONTINUAR;

    char text[256];
    text[0] = 0;
    if (g_api->LerTextoDoJogo(c->Parms, CHAT_TEXT, text, sizeof(text)) <= 0)
        return CONAN_CONTINUAR;

    // Exactly the command, or the command followed by a space. Without this,
    // `!engramsfoo` — or another plugin's longer command starting with the same
    // letters — would be swallowed by this one, and its owner would never work
    // out why their command stopped answering.
    const size_t n = g_command.size();
    if (std::strncmp(text, g_command.c_str(), n) != 0) return CONAN_CONTINUAR;
    if (text[n] != 0 && text[n] != ' ')                return CONAN_CONTINUAR;

    void* controller = c->Obj;    // whoever typed it; no need to hunt for them

    // The player's identity, for the permission check and for the log. Without
    // Permission installed and with no node configured, an empty id is fine —
    // MayRun() never looks at it in that case.
    std::string id;
    {
        char buf[CONAN_PERM_MAX_ID] = {0};
        if (ConanPermIdDoController(controller, buf, sizeof(buf)) > 0 && buf[0])
            id = buf;
    }

    const char* who = id.empty() ? "(no id)" : id.c_str();

    std::string why;
    if (!MayRun(id, why))
    {
        g_api->Log("[engrams] refused for %s: %s", who, why.c_str());
        Reply(controller, g_msgDenied);
        return CONAN_CANCELAR;
    }

    const Result r = UnlockEverything(controller);
    const bool feats = r.feats();

    if (feats && r.recipes)
    {
        g_api->Log("[engrams] unlocked for %s (%d feats learned, %d already had, "
                   "recipes on)", who, r.learned, r.already);
        Reply(controller, WithCount(g_msgOk, r.learned, r.refused));
    }
    else if (feats || r.recipes)
    {
        // Half is a real outcome. Telling the player "done" would be a lie in
        // one direction and "failed" a lie in the other, and they'd find out
        // which by walking to a bench and not being able to build.
        g_api->Log("[engrams] PARTIAL for %s: %s", who, r.why.c_str());
        Reply(controller, WithCount(g_msgPartial, r.learned, r.refused));
    }
    else
    {
        g_api->Log("[engrams] FAILED for %s: %s", who, r.why.c_str());
        Reply(controller, WithCount(g_msgFailed, r.learned, r.refused));
    }

    // Cancelling swallows the message: that's what makes a command a command.
    // The `!engrams` never shows up in everyone's chat.
    return CONAN_CANCELAR;
}

// ── config.json ─────────────────────────────────────────────────────────────
//
// Read with the API's own path helpers, never a relative one: the game
// process's working directory isn't guaranteed and can change. A missing file
// is NOT an error — the defaults above already work, and a plugin that refuses
// to start for want of an optional file is a plugin that fails on half the
// installations.
//
// The parser is deliberately tiny: this file has six string keys and no
// nesting. Pulling in a JSON library, or embedding SQLite for its json1 the way
// Conan Shop does, would be a lot of new code on the most exposed path of a
// plugin whose whole job is one command.
static void ReadConfig()
{
    if (!g_api || !g_api->CaminhoConfig) return;
    const char* path = g_api->CaminhoConfig("UnlockEngrams");
    if (!path) return;

    FILE* f = nullptr;
    CONAN_FOPEN(f, path, "rb");
    if (!f) { g_api->Log("[engrams] no config.json — using the defaults"); return; }

    std::string text;
    {
        char buf[4096]; size_t r;
        while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, r);
    }
    std::fclose(f);

    // "key": "value" — does nothing when the key isn't there, which leaves the
    // default in place instead of blanking it.
    //
    // A COLON MUST FOLLOW THE KEY. The shipped config.json opens with a comment
    // block that names every key, so "the first place this word appears" is the
    // wrong rule: it would land in the prose, then run off looking for the next
    // colon and the next quote, and load a sentence out of the documentation as
    // if the server owner had configured it. Requiring the colon is what tells
    // a real setting apart from a mention of one, and the search moves on
    // instead of giving up, because the real setting is further down the file.
    auto take = [&](const char* key, std::string& dest)
    {
        const std::string target = std::string("\"") + key + "\"";
        for (size_t p = text.find(target);
             p != std::string::npos;
             p = text.find(target, p + 1))
        {
            size_t c = p + target.size();
            while (c < text.size() && (text[c] == ' ' || text[c] == '\t')) ++c;
            if (c >= text.size() || text[c] != ':') continue;   // a mention, not a setting

            size_t a = text.find('"', c);
            if (a == std::string::npos) return;
            size_t b = a + 1;
            std::string v;
            while (b < text.size() && text[b] != '"')
            {
                if (text[b] == '\\' && b + 1 < text.size()) ++b;   // \" stays
                v += text[b++];
            }
            if (b < text.size()) dest = v;
            return;
        }
    };

    take("command",     g_command);
    take("permission",  g_permission);
    take("msg_ok",      g_msgOk);
    take("msg_denied",  g_msgDenied);
    take("msg_partial", g_msgPartial);
    take("msg_failed",  g_msgFailed);

    g_api->Log("[engrams] config: command=\"%s\" permission=\"%s\"",
               g_command.c_str(),
               g_permission.empty() ? "(open to everyone)" : g_permission.c_str());
}

extern "C" __declspec(dllexport)
void ConanPluginCarregar(const ConanApiTabela* api)
{
    // This check is not a formality. A plugin compiled against a table BIGGER
    // than the server's (an API older than the plugin) would read a pointer
    // past the end of the struct and call junk — on the game's thread. And
    // there's no logging the reason: `Log` is one of the very fields you can't
    // trust here. Leaving quietly is the right move.
    if (!api || api->tamanho < sizeof(ConanApiTabela)) return;
    g_api = api;

    // Everything ConanBase.h calls goes through the table, and this is the line
    // that hands it over. Without it, every ConanApi::Call in this file becomes
    // a silent no-op.
    ConanApi::UsarTabela(api);

    g_api->Log("");
    g_api->Log("=== UnlockEngrams ===");

    if (!g_api->Pronta())
    {
        // No reflection means no way to resolve a member or call a function.
        // Fail loud and stop, rather than register a hook that can never work.
        g_api->Log("[engrams] ABORTED: reflection unavailable. Did the game "
                   "update? Regenerate the API.");
        return;
    }

    ReadConfig();

    const uint32_t id = g_api->HookProcessEvent("ServerSendChatMessage",
                                                OnChat, nullptr, 100);
    if (!id)
    {
        // 0 is the FAILURE, not the id. The reason is already in the log, put
        // there by the API itself.
        g_api->Log("[engrams] could not hook the chat; the command \"%s\" will "
                   "not work.", g_command.c_str());
        return;
    }

    g_api->Log("[engrams] ready. Type %s in the game chat.", g_command.c_str());
}

extern "C" __declspec(dllexport)
void ConanPluginDescarregar(void)
{
    // Honest about today's state: ConanLoader does NOT call this (it unloads no
    // plugin; the server process dies whole). It's here because the contract in
    // ConanPluginApi.h provides for it and it costs nothing. What it is NOT is
    // a cleanup guarantee — don't put anything here that genuinely has to
    // happen.
    if (g_api) g_api->Log("[engrams] unloading.");
    g_api = nullptr;
}
