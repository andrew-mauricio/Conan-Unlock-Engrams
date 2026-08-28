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
//  belonging to a DLC the account does not own, and by default this plugin lets
//  it: the refusal is the game working, not this plugin failing.
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
//  THE DLC SWITCH, AND WHERE THE FLAG ACTUALLY LIVES
//  -------------------------------------------------
//  A server owner who wants that lock stepped over asks for it by name:
//  `unlock_dlc` in config.json. It is OFF by default and that is deliberate —
//  DLC is content Funcom sells, and nobody should end up handing it out because
//  they installed a plugin that unlocks building knowledge. Turning it on is a
//  decision, and it has to be written into a file to happen.
//
//  The obvious route is `ConanCheatManager::SetBypassEntitlements(true)`, and
//  it was tried first. MEASURED ON A LIVE SERVER, it changed nothing at all:
//
//      without it   2346 walked · 913 learned · 101 already had · 1332 refused
//      with it      2346 walked ·  30 learned · 984 already had · 1332 refused
//
//  Same account, same table, six days apart. 913+101 and 30+984 are both 1014
//  granted, and the refusals are the same 1332 down to the digit. Meanwhile the
//  log announced `SetBypassEntitlements(true) called on CheatManager` — a line
//  written before anything was checked, claiming an effect that never happened.
//  That is precisely the failure this file warns about forty lines above, and
//  it was committed here, in this file, by trusting a call instead of reading
//  state.
//
//  The reflection data says why it could never have worked that way:
//  `ConanCheatManager` has ZERO properties. The flag is not on it. It is on the
//  player controller, and it is replicated:
//
//      ConanPlayerController::m_CanBypassEntitlements  +0x12D8  bool, replicated
//
//  So the switch does the only honest thing available. It asks the game's own
//  setter first — that is the path that also runs whatever else the game wants
//  to run — then READS THE BIT BACK, and only writes the bit itself if the
//  setter did not take. Whichever route worked is named in the log, and when
//  neither does, that is said too instead of being left to look like success.
//
//  AND IT REPORTS WHAT THE FLAG WAS WORTH
//  --------------------------------------
//  Setting a flag is not unlocking anything. With the bypass confirmed ON, the
//  refusal count is the only thing that can say whether it mattered — so when
//  feats are still refused, this plugin asks the game directly, about one real
//  refused feat:
//
//      GameItemSpawner::SpawnFeatItem(ctx, id)                  -> the FeatItem
//      GameItemSpawner::HasDlcOrEntitlementForFeat(char, item)  -> the verdict
//
//  That turns "it didn't work" into "it didn't work, and here is the game's own
//  answer about why" inside a single run. In this project a run costs a real
//  person logging in — the most expensive thing there is here — so a diagnosis
//  that arrives one round later is a diagnosis that cost a person's evening.
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
// A tabela pode ser MENOR do que o header contra o qual este plugin foi
// compilado — e' o caso previsto pelo `tamanho`, e o motivo de ele existir. Ler
// `g_api->LerConjunto` numa API v7 le' 8 bytes DEPOIS do fim do struct: memoria
// de outra coisa, interpretada como ponteiro de funcao. Decidido uma vez, no
// carregar, enquanto o `tamanho` ainda esta' em maos.
static bool g_temV8 = false;
static std::string g_command    = "!engrams";
static std::string g_permission = "";        // empty = anybody can use it

// DLC content stays locked unless the owner asks for it BY NAME. False is not a
// shy default: it is the difference between a plugin that unlocks the knowledge
// a server already owns and one that hands out what Funcom sells. Anyone who
// wants the second has to say so in config.json.
static bool        g_unlockDlc  = false;

// Bazaar and Battle Pass content is a SEPARATE decision from DLC packages, and
// gets its own switch rather than riding along with the first. A server owner
// who is comfortable handing out the expansion packs they could have bought is
// not automatically comfortable handing out seasonal store content — and
// bundling the two would take that choice away by making it invisible.
//
// Measured: this is 1095 of the 1332 refusals, and it answers from a different
// field (`m_OwnedDurableRewards`) than the packages do.
static bool        g_unlockBazaar = false;
// `{dlc}` is in the DEFAULT, not just in the shipped config.json. Without it a
// player who had 1332 knowledges refused reads "you can build anything now" and
// finds out otherwise at a bench. Reading "still locked by DLC: 0" is the
// confirmation that it really was everything.
static std::string g_msgOk      = "Unlocked {n} knowledges. Still locked by DLC: {dlc}.";
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
    int  firstRefused = 0;   // one real id to ask the game about, 0 = none
    bool recipes = false;    // the flag, read back after writing

    // The ids the game refused, so a second pass can try them BY ID instead of
    // by package name. Lending `DLC_Special` as a package changed nothing
    // (measured 27/08 21:50 — the count stayed at 1099), which leaves
    // per-reward keying as the standing hypothesis. And the row names of the
    // FeatTable ARE the template ids, so the id is a name the game can look up.
    std::vector<int> refusedIds;
    int  bazaarTried  = 0;   // how many ids the second pass attempted
    int  bazaarWon    = 0;   // how many of those the game then granted

    // The DLC switch, in four states rather than a bool, because "we never
    // tried" and "we tried and it did not take" are different facts and only
    // one of them is a problem:
    //   -2 the owner did not ask for it   ·  -1 no such field on this build
    //    0 asked for, and it did NOT take ·   1 confirmed ON by reading it back
    int  dlc     = -2;
    std::string dlcHow;      // which route actually set it

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
        // Keep one real refused id. Asking the game "why was THIS one refused"
        // needs an actual case, and inventing a plausible id would be asking
        // about something that never happened.
        if (r.firstRefused == 0) r.firstRefused = int(id);
        r.refusedIds.push_back(int(id));
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

// ── the DLC lock, and the switch that steps over it ─────────────────────────
//
// Only runs when config.json asked for it. Returns the four states of
// Result::dlc, and NONE of them is inferred from a call returning: the bit is
// read back every time, because this exact field already produced one confident
// lie in this plugin's log.
//
// Bit 1 with a fieldmask of 255 — a native bool, one whole byte, the same shape
// as UnlockAllRecipes. `EscreverBit` is used anyway rather than a byte write,
// for the same reason it is used there: a patch that packs this next to another
// flag would turn a byte write into silent damage to the neighbour.
static const uint8_t DLC_BIT = 1;

static int UnlockDlcContent(void* controller, std::string& how)
{
    how.clear();
    if (!controller || !g_api) return 0;

    // NOT on the cheat manager. `ConanCheatManager` carries zero properties;
    // the flag lives here, on the controller, and the first version of this
    // code spent a live test proving that the hard way.
    const int32_t off = g_api->OffsetDoMembro(controller, "m_CanBypassEntitlements");
    if (off < 0)
    {
        g_api->Log("[engrams] DLC: ConanPlayerController.m_CanBypassEntitlements "
                   "does not exist on this build — there is no flag to set. The "
                   "rest of the unlock still runs.");
        return -1;
    }

    if (g_api->LerBit(controller, uint32_t(off), DLC_BIT) > 0)
    {
        how = "it was already on";
        g_api->Log("[engrams] DLC: bypass at +0x%X was ALREADY on for this player.",
                   unsigned(off));
        return 1;
    }

    // EhReplicado answers 1 · 0 · -1, and the -1 means "could not tell" — the
    // header says so in as many words. Printing it as "no" would turn a
    // non-answer into a fact, in the one line somebody will read when the
    // client's screen disagrees with the server.
    const int rep = g_api->EhReplicado(controller, uint32_t(off));
    g_api->Log("[engrams] DLC: m_CanBypassEntitlements at +0x%X (replicated=%s), "
               "currently OFF — turning it on.",
               unsigned(off),
               rep > 0 ? "yes" : (rep == 0 ? "no" : "could not tell"));

    // ROUTE 1 — the game's own setter, tried first and on purpose. Writing a
    // field directly skips whatever else the game does when that field changes;
    // calling the setter does not. Both names are tried because the controller
    // carries two cheat-manager pointers — `ConanCheatManager` of its own and
    // `CheatManager` inherited from Unreal — and on this build they point at
    // the same object, but a patch is free to disagree.
    static const char* const CM_FIELDS[] = { "ConanCheatManager", "CheatManager" };
    for (int i = 0; i < 2; ++i)
    {
        void* cm = PointerMember(controller, CM_FIELDS[i]);
        if (!cm) continue;

        ConanApi::Call<void>(cm, "SetBypassEntitlements", bool(true));
        const bool dispatched = g_api->UltimaChamadaExecutou() != 0;

        // THE LINE THAT DECIDES. `dispatched` says the UFunction ran; it cannot
        // say the body did anything, and the cheat functions on this build
        // return early for a player who is not an admin.
        const bool on = g_api->LerBit(controller, uint32_t(off), DLC_BIT) > 0;

        g_api->Log("[engrams] DLC: SetBypassEntitlements via %s — dispatched=%s, "
                   "flag now %s.", CM_FIELDS[i], dispatched ? "yes" : "NO",
                   on ? "ON" : "still off");

        if (on) { how = std::string("SetBypassEntitlements via ") + CM_FIELDS[i]; return 1; }
    }

    // ROUTE 2 — set the bit ourselves. The setter either was not reachable or
    // returned without doing its job; the field is a plain replicated bool and
    // writing it is what the setter would have done.
    if (g_api->EscreverBit(controller, uint32_t(off), DLC_BIT, 1) <= 0)
    {
        g_api->Log("[engrams] DLC: could not write m_CanBypassEntitlements at "
                   "+0x%X.", unsigned(off));
        return 0;
    }

    if (g_api->LerBit(controller, uint32_t(off), DLC_BIT) > 0)
    {
        how = "direct write to m_CanBypassEntitlements";
        g_api->Log("[engrams] DLC: bypass is ON (written directly, read back).");
        return 1;
    }

    g_api->Log("[engrams] DLC: the write reported success and the bit is STILL "
               "clear. Something on this build owns that field.");
    return 0;
}

// ── answering the DLC check ourselves ───────────────────────────────────────
//
// MEASURED, WITH A REAL PLAYER, BEFORE THIS EXISTED: with
// `m_CanBypassEntitlements` confirmed ON by reading it back, the game refused
// the same 1332 feats, and `HasDlcOrEntitlementForFeat` answered FALSE for feat
// 4004. So the flag is not on this path — the ownership set is.
//
// WHY NOT WRITE THE OWNERSHIP SET. `m_OwnedDLCs` is a `TSet<FName>`, which is an
// `FScriptSet`: a sparse array with a hash index beside it. The plugin API has
// nothing for sets (only `CONAN_SAIDA_LISTA`, and that is for reading a TArray).
// Writing one by hand means reproducing the engine's hashing and free-list
// bookkeeping from outside, and a set left half-written is not a wrong answer —
// it is a crash in whatever touches it next, far from here.
//
// SO ANSWER THE QUESTION INSTEAD OF FORGING THE RECORD. The check is a UFunction
// with a bool return, which means it comes through the same funnel every hook in
// this API uses:
//
//     GameItemSpawner::HasDlcOrEntitlementForFeat(targetCharacter, FeatItem) -> bool
//     Parms +0x00  targetCharacter   +0x08  FeatItem   +0x10  ReturnValue (1 byte)
//
// The hook is registered as the AFTER callback, never the before one. Before
// would mean cancelling the call, and cancelling makes the game skip whatever
// else that function does. After lets the body run exactly as Funcom wrote it
// and changes only the answer it hands back.
//
// AND IT IS NARROW ON PURPOSE. It does nothing unless a `!engrams` is running
// AND the character being asked about is the one who typed it. Outside that
// window it costs one comparison and returns. A blanket "everyone owns
// everything" would leak into every shop, every emote and every spell check on
// the server, for players who never asked for anything.
static void*    g_grantingFor  = nullptr;   // the character mid-command, or null
static int      g_dlcAsked     = 0;         // times the game asked
static int      g_dlcForced    = 0;         // times we answered yes
static uint32_t g_dlcHookId    = 0;

static const uint32_t DLC_PARM_TARGET = 0x00;   // ObjectProperty targetCharacter
static const uint32_t DLC_PARM_RETURN = 0x10;   // BoolProperty  ReturnValue

extern "C" void OnDlcCheck(ConanChamada* c)
{
    // 0x18 is the whole block (24 bytes). A shorter one is not this function —
    // reading the return slot out of it would be reading past the end.
    if (!c || !c->Parms || c->ParmsSize < 0x18) return;

    ++g_dlcAsked;
    if (!g_grantingFor) return;             // nobody is running the command

    void* target = nullptr;
    std::memcpy(&target, static_cast<char*>(c->Parms) + DLC_PARM_TARGET, sizeof(target));
    if (target != g_grantingFor) return;    // somebody else's check: not ours to answer

    const uint8_t yes = 1;
    std::memcpy(static_cast<char*>(c->Parms) + DLC_PARM_RETURN, &yes, 1);
    ++g_dlcForced;
}

// ── measuring the DLC gate instead of guessing at it ────────────────────────
//
// Three routes have now been tried and all three failed, each for its own
// reason, all measured on a live server with a real player:
//
//   SetBypassEntitlements       dispatched, and the flag never moved
//   m_CanBypassEntitlements     written and read back ON — same 1332 refused
//   hook on the check by name   fired ZERO times: it is native, not reflected
//
// What has NOT been done is asking the game the plain questions it answers for
// free. Every one of these is a READ. Nothing here writes anything, so it can
// run on a live server with no risk, and it can eliminate whole branches of
// what to try next before a single risky line gets written.
//
//     ConanPlayerController::HasDLC(FName) -> bool     does the server think
//     ConanCharacter::HasDLC(FName)        -> bool     this account owns it?
//     GameItemSpawner::IsDLCDisabled(ctx, FName)       is the package switched
//                                          -> bool     OFF on this server?
//     GameItem::GetDLCPackage()            -> FName    what the feat says it is
//
// THE LAST TWO ENTRIES ARE THE POSITIVE CONTROL, and without them the whole
// table is worthless. `DLC_Turan` and `DLC_Nemedia` have FeatTables loaded in
// this world and appear NOWHERE in the refusal list — so this account owns
// them. If `HasDLC` answers "no" for those two as well, then it is not
// answering about ownership at all, and every "no" above it means nothing.
//
// A zero that the instrument cannot distinguish from a broken instrument is not
// a measurement. This project has paid for that lesson more than once.
static const char* const DLC_PACKAGES[] = {
    "DLC_Special",                       // 1095 of the 1332 — first because it dominates
    "DLC_Siptah",     "DLC_Aquilonia", "DLC_Yamatai", "DLC_Pict",
    "DLC_Khitai",     "DLC_Riders",    "DLC_BAS",     "DLC_Argos",
    "DLC_RPpack",     "DLC_Riddle",    "DLC_Entitlement",
    "DLC_EARhino",    "XX_DevOnly",
    "DLC_Turan",      "DLC_Nemedia",       // <- POSITIVE CONTROL: never refused
};
static const int N_DLC_PACKAGES = int(sizeof(DLC_PACKAGES) / sizeof(DLC_PACKAGES[0]));

// `DLC_Special` (1095 refusals) and `DLC_Entitlement` (4) were tried here as
// PACKAGE names against `m_OwnedDurableRewards` on 27/08 21:50 and moved
// nothing — the count stayed at 1099. The names are gone from the code rather
// than left sitting unused: what is worth keeping is the measurement, and it is
// written down in TryBazaarByRewardIds, which took the next step from it.

// yes · no · the function did not answer. Three states, because "n/a" and "no"
// being the same word is how a broken call reads as a real negative.
static const char* Tri(bool v) { return g_api->UltimaChamadaExecutou() ? (v ? "YES" : "no") : "n/a"; }

// ── the ownership set, and lending the game one of ours ─────────────────────
//
// THE LAYOUT BELOW WAS NOT DEDUCED. It was read off a live, valid specimen — a
// dump of this account's own `m_OwnedDLCs`, which is not empty because the
// account really owns `DLC_Turan` and `DLC_Nemedia`, both confirmed YES by the
// positive control. The two known FNames were found inside it, which settles
// the element size and the chaining by observation:
//
//   +0x00  void*   Data              the elements, 16 bytes each
//   +0x08  int32   ArrayNum          2
//   +0x0C  int32   ArrayMax          4
//   +0x10  uint32  AllocFlags[4]     0x3 — bits 0 and 1, one per live element
//   +0x20  void*   AllocFlagsPtr     0 -> the inline bitmap above is in use
//   +0x28  int32   NumBits           2
//   +0x2C  int32   MaxBits           128  (4 dwords inline)
//   +0x30  int32   FirstFreeIndex    -1
//   +0x34  int32   NumFreeIndices    0
//   +0x38  uint32  HashInline[1]     1 -> head of the single bucket
//   +0x40  void*   HashPtr           0 -> the inline hash above is in use
//   +0x48  int32   HashSize          1
//
//   elem[0]  FName 71437 (DLC_Turan)    HashNext = -1   HashIndex = 0
//   elem[1]  FName 71495 (DLC_Nemedia)  HashNext =  0   HashIndex = 0
//
// Walk it: Hash[0] = 1 -> elem[1] -> next 0 -> elem[0] -> next -1, end. Every
// field accounted for, nothing left over.
//
// AND `HashSize` IS ALREADY 1 IN THE GAME'S OWN SET. That matters more than it
// looks: with one bucket, `bucket = hash & (HashSize-1)` is always 0, so the
// FName hash function never has to be reproduced. That is not a trick being
// played on the engine — it is the shape the engine itself uses here.
//
// WHY LEND INSTEAD OF INSERT. There is no set primitive in the plugin API, and
// the Blueprint ones (`Set_Add`, `SetSetPropertyByName`) are CustomThunk
// wildcards that read their type off the Blueprint stack — through ProcessEvent
// they would corrupt the set. So nothing is inserted into the game's set: ours
// is put in its place for the length of the loop and the original 80 bytes go
// back, byte for byte.
//
// THE RESTORE IS THE DANGEROUS PART, NOT THE WRITE. If our pointer is left in
// that field, the controller's destructor at logout — or the next
// `ServerSendDLCs` — hands the PLUGIN's memory to the GAME's allocator. That is
// a dead server, far from the cause, with no trace. Hence the destructor: every
// exit restores, including the ones nobody thought of.
struct SetElem
{
    uint32_t nomeIdx;
    uint32_t nomeNum;
    int32_t  hashNext;
    int32_t  hashIndex;
};

// ── TWO SETS, TWO LOANS ─────────────────────────────────────────────────────
//
// The check is called `HasDlcOrEntitlementForFeat`, and the OR is literal: a
// feat carrying an ordinary DLC package is answered from `m_OwnedDLCs`, while
// Bazaar and Battle Pass content goes down a second leg that reads
// `m_OwnedDurableRewards` (+0x1330) — the same 80-byte shape, a twin reader.
//
// Measured on 27/08 at 20:57, lending only the first set: refusals went from
// 1332 to 1099, and the 1099 that stayed were exactly `DLC_Special` (1095) and
// `DLC_Entitlement` (4). Twelve purchasable packages gave way completely; these
// two did not move, because they were never being asked about that field.
//
// Each loan keeps its OWN saved copy. One shared copy across two fields would
// restore the wrong 80 bytes into one of them, which is the same catastrophe as
// not restoring at all.
// 128 IS NOT AN ARBITRARY CEILING — it is the measured one. The live specimen
// carried `MaxBits = 128`, which is the four inline dwords of the allocation
// bitmap. Past that the engine switches to a secondary allocation whose layout
// was NEVER MEASURED here, and inventing it would be exactly the mistake this
// whole feature was built to avoid. Bigger jobs go through in batches of 128,
// each one using only the path that has already worked on a live server.
static const int LOAN_MAX = 128;

// E O LOTE TEM DE SER MENOR QUE O BUFFER, senao a folga acima nao existe.
// Escrevi "ArrayMax IS THE FULL BUFFER, NOT live" como defesa contra o motor
// realocar memoria estatica nossa — e entao usei LOAN_MAX como tamanho do lote,
// o que faz ArrayNum == ArrayMax == 128 em todo lote cheio. Oito dos nove lotes
// de 1099 ficavam sem folga nenhuma: a defesa anulava a si mesma.
static const int LOAN_LOTE = 96;
static_assert(LOAN_LOTE < LOAN_MAX,
              "o lote precisa deixar folga: com ArrayNum == ArrayMax um Add do "
              "motor chama o alocador DELE sobre memoria estatica NOSSA");

struct Loan
{
    const char* campo;              // the reflected member name
    SetElem     elems[LOAN_MAX];    // OUR memory, static. The game never owns it.
    uint8_t     saved[80];
    bool        armed;
    void*       owner;
    int32_t     off;
};

static Loan g_loanDlc    = { "m_OwnedDLCs",           {}, {}, false, nullptr, -1 };
static Loan g_loanBazaar = { "m_OwnedDurableRewards", {}, {}, false, nullptr, -1 };

// One latch for BOTH. A restore that failed on either field means plugin memory
// is parked in a live controller; carrying on with the other one would only add
// a second place for the server to die.
static bool g_setBroken = false;

// Returns true when there is nothing of ours left in the game's field.
//
// THE ORDER OF THESE LINES IS THE WHOLE SAFETY OF THE FEATURE, and the first
// version got it wrong in a way that hid itself. It cleared `g_setArmed` and
// `g_setOwner` BEFORE testing whether the write succeeded. On a failed restore
// that produced: the controller still holding plugin memory, the plugin no
// longer knowing WHICH controller, and — worst — the plugin rearmed. The next
// command would read the FORGED set as if it were the original, save that as
// its backup, hand it back at the end, and report success. The plugin's pointer
// would be reinstalled on every command, permanently, with the log saying
// everything was fine, until the server died at logout with no trace.
static bool RestoreOwnedSet(Loan& L)
{
    if (!g_api) return false;                       // unloading: nothing safe to do
    if (!L.armed || !L.owner || L.off < 0) return true;

    // IDENTITY BEFORE WRITING. `L.owner` is a UObject pointer held across calls,
    // and ConanPluginApi.h is explicit that this cannot be trusted: `Legivel`
    // proves MAPPED, not ALIVE, and the GC recycles addresses. If the field no
    // longer holds OUR memory, whatever lives there now is not ours to write 80
    // bytes into.
    uint8_t cur[80];
    std::memset(cur, 0, sizeof(cur));
    if (g_api->LerMembro(L.owner, uint32_t(L.off), cur, 80) <= 0 ||
        *(void* const*)cur != (void*)L.elems)
    {
        L.armed = false; L.owner = nullptr;
        g_api->Log("[engrams]   restore(%s): the field no longer holds our memory "
                   "— not writing over whatever is there now.", L.campo);
        return true;
    }

    if (g_api->EscreverMembro(L.owner, uint32_t(L.off), L.saved, 80) > 0)
    {
        L.armed = false; L.owner = nullptr;
        return true;
    }

    // FAILED. Keep every bit of state: it is the only record of where the damage
    // is, and clearing it is what would rearm the plugin into making it
    // permanent. The latch below stops all further lending in this process.
    g_setBroken = true;
    g_api->Log("[engrams]   !! COULD NOT RESTORE %s on controller %p at +0x%X. "
               "That controller is holding PLUGIN memory. DLC/Bazaar unlocking is "
               "DISABLED for this process. RESTART THE SERVER before anyone logs "
               "out — the game will hand our pointer to its own allocator.",
               L.campo, L.owner, unsigned(L.off));
    return false;
}

// Restores on the way out of the scope. It is a net, not a guarantee: a
// callback interrupted below C++ level never runs destructors, which is exactly
// why `LendOwnedSet` refuses to proceed when it finds our pointer still in the
// field rather than trusting that this always ran.
struct LentSetGuard
{
    Loan* a; Loan* b;
    explicit LentSetGuard(Loan* x, Loan* y = nullptr) : a(x), b(y) {}
    ~LentSetGuard() { if (a) RestoreOwnedSet(*a); if (b) RestoreOwnedSet(*b); }
};

static bool LendOwnedSet(Loan& L, void* pc, const char* const* pkgs, int n)
{
    if (!pc || !g_api || n <= 0 || n > int(sizeof(L.elems) / sizeof(L.elems[0]))) return false;

    if (g_setBroken)
    {
        g_api->Log("[engrams]   lending is DISABLED: a restore failed earlier in "
                   "this process. Restart the server. Everything else still runs.");
        return false;
    }
    if (L.armed)
    {
        g_api->Log("[engrams]   a loan on %s is still out — refusing to nest. The "
                   "saved copy holds one set, not two.", L.campo);
        return false;
    }

    const int32_t off = g_api->OffsetDoMembro(pc, L.campo);
    if (off < 0)
    {
        g_api->Log("[engrams]   %s does not exist on this build — not lending.", L.campo);
        return false;
    }

    std::memset(L.saved, 0, sizeof(L.saved));
    if (g_api->LerMembro(pc, uint32_t(off), L.saved, 80) <= 0)
    {
        g_api->Log("[engrams]   could not read the original %s — not lending. "
                   "Without a copy to give back there is no safe way in.", L.campo);
        return false;
    }

    // THE CHECK THAT CLOSES THE HOLE COMPLETELY, whatever route led here.
    // If the field ALREADY points at our memory, a previous loan never came
    // back — failed restore, destructor skipped, or a path nobody imagined.
    // Recording that as "the original" is the single step that turns an accident
    // into a permanent installation, so it is refused here instead of being
    // detected later, which never happens.
    if (*(void* const*)L.saved == (void*)L.elems)
    {
        g_setBroken = true;
        g_api->Log("[engrams]   !! %s ALREADY points at plugin memory: a previous "
                   "loan never came back. Refusing to record that as 'the "
                   "original'. Unlocking DISABLED. RESTART THE SERVER.", L.campo);
        return false;
    }

    int live = 0;
    for (int i = 0; i < n; ++i)
    {
        ConanApi::Nome nm(pkgs[i]);
        // NOT a filter for "does this build know the package". `ConanApi::Nome`
        // goes through Conv_StringToName, which CREATES the name when it is
        // absent, so a package that never existed still comes back valid — it
        // just never matches anything the game asks about, which is harmless.
        // This only catches the name machinery failing outright.
        if (!nm.valido) continue;
        L.elems[live].nomeIdx   = *(const uint32_t*)(nm.bruto);
        L.elems[live].nomeNum   = *(const uint32_t*)(nm.bruto + 4);
        L.elems[live].hashIndex = 0;                   // one bucket, so always 0
        ++live;
    }
    if (live <= 0) return false;

    // Chained exactly the way the game's own set chains: the head is the LAST
    // index and each entry points at the one before it, ending in INDEX_NONE.
    for (int i = 0; i < live; ++i) L.elems[i].hashNext = (i == 0) ? -1 : (i - 1);

    uint8_t s[80];
    std::memset(s, 0, sizeof(s));
    *(void**)   (s + 0x00) = L.elems;                   // Data
    *(int32_t*) (s + 0x08) = live;                      // ArrayNum
    // ArrayMax IS THE FULL BUFFER, NOT `live`, and that is a safety margin, not
    // tidiness. With Max == Num, one Add by the game inside the window finds the
    // array full and calls ITS allocator to grow OUR static pointer — free() on
    // plugin memory, which is the exact death this whole design is built to
    // avoid. With room to spare, such an Add lands in our own array instead.
    *(int32_t*) (s + 0x0C) = int32_t(sizeof(L.elems) / sizeof(L.elems[0]));
    // AllocFlags: one bit per live element, across the four inline dwords. The
    // specimen had 0x3 for two elements, so the bits are plain and ascending.
    // Written as a loop rather than `(1<<live)-1` because that shifts by 32 or
    // more for anything past a handful — undefined behaviour, and it would have
    // produced a plausible wrong bitmap instead of a compile error.
    for (int w = 0; w < 4; ++w)
    {
        uint32_t mask = 0;
        for (int b = 0; b < 32; ++b)
            if (w * 32 + b < live) mask |= (1u << b);
        *(uint32_t*)(s + 0x10 + w * 4) = mask;
    }
    *(void**)   (s + 0x20) = nullptr;                   // inline bitmap in use
    *(int32_t*) (s + 0x28) = live;                      // NumBits
    *(int32_t*) (s + 0x2C) = 128;                       // MaxBits, as the specimen had
    *(int32_t*) (s + 0x30) = -1;                        // FirstFreeIndex
    *(int32_t*) (s + 0x34) = 0;                         // NumFreeIndices
    *(uint32_t*)(s + 0x38) = uint32_t(live - 1);        // Hash[0] = head of the chain
    *(void**)   (s + 0x40) = nullptr;                   // inline hash in use
    *(int32_t*) (s + 0x48) = 1;                         // HashSize = 1, as the game itself uses

    if (g_api->EscreverMembro(pc, uint32_t(off), s, 80) <= 0)
    {
        g_api->Log("[engrams]   could not write the lent %s — nothing changed.", L.campo);
        return false;
    }

    L.owner = pc;
    L.off   = off;
    L.armed = true;
    return true;
}

// ── THE SHORTEST PATH: an empty package is allowed, full stop ───────────────
//
// Disassembled from HasDlcOrEntitlementForFeat (0x144E185F0):
//
//     call [vtable+0x498]   FeatItem->GetDLCPackage() -> FName
//     test rax, rax
//     je   0x144E186C6      -> mov al,1 ; ret     EMPTY MEANS ALLOWED
//
// No ownership set is consulted, and the branch that reaches the reward set is
// never taken. So blanking the package covers Bazaar content too — without ever
// needing the reward ids, which live in a global TMap this plugin cannot read.
//
// AND THE PACKAGE COMES FROM THE TABLE ROW, not from the item (0x4DE1C70):
//
//     call [vtable+0x500]   -> the FeatTableRow
//     call 0x144E13610      -> its type    (reads byte [row+8])
//     cmp  al, 4            -> confirms FeatTableRow
//     mov  rax, [rdi+0xE0]  -> +0xE0 = DLCPackage, matching golden/structs.json
//
// `[vtable+0x500]` is a native virtual — invisible to reflection, and reaching
// it by hand is what killed the server on 27/08. It is reachable now because
// the API grew `ChamarVirtual` (v7), which does that one dangerous thing once,
// centrally, behind five checks — including the one that was missing that day:
// the slot must point at EXECUTABLE memory.
//
// THE ROW IS SHARED. It belongs to the DataTable, not to a player, so blanking
// it changes the answer for EVERYONE while it is blank. That is why the window
// is one feat wide: blank, grant, restore, next. Never the whole loop.
static const uint32_t VT_GET_ROW  = 0x500 / 8;   // FeatItem -> FeatTableRow
static const uint32_t ROW_DLCPKG  = 0xE0;        // FeatTableRow.DLCPackage
static const uint32_t ROW_TIPO    = 0x08;        // byte the game checks == 4
static const uint8_t  ROW_TIPO_FEAT = 4;

// Returns the row only when the game's own type check agrees it is a
// FeatTableRow. Anything else and we are looking at a different struct, where
// +0xE0 means something else entirely.
static void* LinhaDoFeat(void* item)
{
    if (!item || !g_api || !g_api->ChamarVirtual) return nullptr;

    void* linha = nullptr;
    if (!g_api->ChamarVirtual(item, VT_GET_ROW, nullptr, &linha) || !linha)
        return nullptr;

    uint8_t tipo = 0;
    if (g_api->LerMembro(linha, ROW_TIPO, &tipo, 1) <= 0) return nullptr;
    if (tipo != ROW_TIPO_FEAT) return nullptr;
    return linha;
}

// ── the Bazaar leg: trying the refused feats BY ID ──────────────────────────
//
// WHAT IS ALREADY KNOWN, measured, not assumed:
//   · lending the package names to `m_OwnedDLCs` freed twelve packages whole,
//     1332 -> 1099 (27/08 20:57);
//   · lending `DLC_Special` and `DLC_Entitlement` as PACKAGE names to
//     `m_OwnedDurableRewards` changed nothing at all — still 1099 (21:50).
//
// A set that ignores the package name is a set keyed by something else, and the
// obvious candidate is the reward itself. The FeatTable's row names ARE the
// template ids, so `FName("269")` is a name the game can look up. This tries
// exactly that, on the ids the game actually refused — never on invented ones.
//
// IN BATCHES OF 128, because that is the measured capacity of the inline
// allocation bitmap. Each batch is a complete lend/verify/restore cycle through
// the path that has already worked; nothing new is being risked to go wider.
//
// AND IT COUNTS CONFIRMED STATE. Not calls made — `IsFeatPurchased` afterwards,
// same rule as everywhere else in this file. If the hypothesis is wrong the
// count comes back zero, nothing was damaged, and that is a real answer.
static void TryBazaarByRewardIds(void* controller, void* prog, Result& r)
{
    if (!controller || !prog || !g_api || r.refusedIds.empty()) return;

    const size_t total = r.refusedIds.size();
    g_api->Log("[engrams]   Bazaar: trying %d refused feat(s) BY ID, in batches "
               "of %d (buffer %d, folga proposital). Package names did not work; "
               "the ids are the next thing the set could be keyed by.",
               int(total), LOAN_LOTE, LOAN_MAX);

    for (size_t base = 0; base < total; base += LOAN_LOTE)
    {
        const int n = int((total - base < size_t(LOAN_LOTE)) ? (total - base) : size_t(LOAN_LOTE));

        // The ids as text, because an FName is made from text. The storage has
        // to outlive the pointers handed to LendOwnedSet, hence two arrays.
        std::vector<std::string> textos((size_t)n);
        std::vector<const char*> nomes((size_t)n);
        char buf[24];
        for (int i = 0; i < n; ++i)
        {
            std::snprintf(buf, sizeof(buf), "%d", r.refusedIds[base + size_t(i)]);
            textos[(size_t)i] = buf;
            nomes[(size_t)i]  = textos[(size_t)i].c_str();
        }

        if (!LendOwnedSet(g_loanBazaar, controller, nomes.data(), n))
        {
            g_api->Log("[engrams]   Bazaar: batch at %d was not lent — stopping "
                       "here rather than carrying on blind.", int(base));
            return;
        }

        {
            LentSetGuard guard(&g_loanBazaar);
            for (int i = 0; i < n; ++i)
            {
                const int32_t id = int32_t(r.refusedIds[base + size_t(i)]);
                ++r.bazaarTried;

                ConanApi::Call<void>(prog, "ServerForceLearnFeat",
                                     id, bool(false), bool(true), bool(false));

                // The only line that decides, here as everywhere else.
                if (ConanApi::Call<bool>(prog, "IsFeatPurchased", id)
                    && g_api->UltimaChamadaExecutou() != 0)
                {
                    ++r.bazaarWon;
                    ++r.learned;
                    --r.refused;
                }
            }
        }   // <- this batch is back before the next one goes out
    }

    // ── SEGUNDA VOLTA: pelo PACOTE VAZIO, um feat de cada vez ───────────────
    //
    // Se emprestar os ids nao funcionou (e nao funcionou: 0 de 1098, medido),
    // resta o atalho que o desmonte mostrou — pacote vazio LIBERA, sem consultar
    // conjunto nenhum. Isso cobre Bazaar e DLC juntos, porque o desvio para o
    // conjunto de recompensas nunca chega a ser tomado.
    //
    // UM FEAT DE CADA VEZ, e a janela fecha antes do proximo. A linha pertence a`
    // DataTable, nao ao jogador: enquanto o pacote esta' em branco, a resposta
    // muda para TODO MUNDO no servidor. Uma janela por feat mede em microssegundos;
    // uma janela pelo laco inteiro mediria segundos, com outros jogadores dentro.
    if (r.bazaarWon == 0 && g_api->ChamarVirtual)
    {
        void* spawner = g_api->GetDefaultObject("GameItemSpawner");
        int limpos = 0, ganhos = 0, semLinha = 0;

        for (size_t i = 0; i < r.refusedIds.size(); ++i)
        {
            const int32_t id = int32_t(r.refusedIds[i]);

            void* item = ConanApi::Call<void*>(spawner, "SpawnFeatItem", controller, id);
            if (g_api->UltimaChamadaExecutou() == 0 || !item) continue;

            void* linha = LinhaDoFeat(item);
            if (!linha) { ++semLinha; continue; }

            uint8_t pacote[8];
            if (g_api->LerMembro(linha, ROW_DLCPKG, pacote, 8) <= 0) continue;

            const uint8_t zero[8] = {0,0,0,0,0,0,0,0};
            if (g_api->EscreverMembro(linha, ROW_DLCPKG, zero, 8) <= 0) continue;
            ++limpos;

            // A DEVOLUCAO POR DESTRUTOR, e nao por uma linha no fim do bloco.
            //
            // Entre zerar e devolver ha' DUAS chamadas ao jogo. Uma linha de
            // restauro no fim so' cobre os caminhos que alguem lembrou de
            // prever; o destrutor cobre tambem o que ninguem previu. E aqui o
            // custo de errar nao e' um jogador: a linha pertence a` DataTable,
            // entao um pacote deixado em branco muda a resposta do jogo para
            // TODO MUNDO, pelo resto da sessao.
            //
            // Mesma licao que o LentSetGuard aprendeu na revisao adversarial,
            // aplicada antes de alguem precisar apontar.
            struct Devolver
            {
                void* linha; const uint8_t* original; int id;
                ~Devolver()
                {
                    if (g_api->EscreverMembro(linha, ROW_DLCPKG, original, 8) <= 0)
                        g_api->Log("[engrams]   !! NAO devolvi o DLCPackage do feat "
                                   "%d — a linha da tabela ficou EM BRANCO para todos. "
                                   "Reinicie o servidor.", id);
                }
            } devolucao{linha, pacote, int(id)};

            ConanApi::Call<void>(prog, "ServerForceLearnFeat",
                                 id, bool(false), bool(true), bool(false));
            const bool tem = ConanApi::Call<bool>(prog, "IsFeatPurchased", id)
                             && g_api->UltimaChamadaExecutou() != 0;

            if (tem) { ++ganhos; ++r.learned; --r.refused; }
        }

        if (limpos > 0)
            g_api->Log("[engrams]   Bazaar (pacote vazio): %d linha(s) tratadas, "
                       "%d feat(s) concedidos, %d sem linha.", limpos, ganhos, semLinha);
        else
            g_api->Log("[engrams]   Bazaar (pacote vazio): nenhuma linha alcancada "
                       "(%d sem linha). ChamarVirtual respondeu? A API precisa ser v7.",
                       semLinha);
        r.bazaarWon += ganhos;
    }

    if (r.bazaarWon > 0)
        g_api->Log("[engrams]   Bazaar: %d of %d granted with the ID in the set — "
                   "the reward set IS keyed by template id.",
                   r.bazaarWon, r.bazaarTried);
    else
        g_api->Log("[engrams]   Bazaar: 0 of %d granted. The set is not keyed by "
                   "template id either. Nothing was damaged finding that out, and "
                   "the refusals below are unchanged.", r.bazaarTried);
}

// ── proving the game reads OUR set, before granting anything ────────────────
//
// Lend a set containing exactly ONE package — `DLC_RPpack`, which this account
// does NOT own — and ask the game two questions whose answers are already
// known from the baseline measured at 15:32:03:
//
//     baseline        RPpack = no    ·  Turan = YES
//     if ours is read RPpack = YES   ·  Turan = no
//
// Both answers have to FLIP. One flipping could be luck or a misread; both
// flipping in opposite directions cannot be — a blind instrument returning a
// constant fails this, and so does a set the game ignored.
//
// This runs BEFORE a single feat is granted. If it does not invert, the loop
// does not run and nothing was touched.
static bool CalibrateLentSet(void* controller)
{
    static const char* const ONE[] = { "DLC_RPpack" };

    if (!LendOwnedSet(g_loanDlc, controller, ONE, 1))
    {
        g_api->Log("[engrams]   calibration: could not lend the set — not granting.");
        return false;
    }

    bool rp, tu;
    {
        LentSetGuard guard(&g_loanDlc);   // gives the original 80 bytes back on the way out
        rp = ConanApi::Call<bool>(controller, "HasDLC", ConanApi::Nome("DLC_RPpack"));
        tu = ConanApi::Call<bool>(controller, "HasDLC", ConanApi::Nome("DLC_Turan"));
    }

    const bool inverted = rp && !tu;
    g_api->Log("[engrams]   calibration: RPpack=%s (baseline no) · Turan=%s (baseline YES) -> %s",
               rp ? "YES" : "no", tu ? "YES" : "no",
               inverted ? "INVERTED — the game is reading OUR set"
                        : "NOT inverted — the game is NOT reading it; granting nothing");
    return inverted;
}

// ── reading the ownership set as it really is ───────────────────────────────
//
// The next step needs the layout of `m_OwnedDLCs` — an `FScriptSet<FName>`, 80
// bytes. There are two ways to get one, and only one of them is honest here.
//
// The tempting way is to disassemble the reader and infer the field offsets
// from the instructions. That was tried, and the disassembly did not survive
// checking: the address it named turned out to be doing something else
// entirely. An offset table nobody verified, written into a pointer field on a
// live controller, is not a wrong answer — it is a dead server.
//
// The honest way is sitting right there. THIS ACCOUNT OWNS TWO PACKAGES —
// `DLC_Turan` and `DLC_Nemedia`, both confirmed YES by HasDLC against the
// positive control. So the set is NOT empty: it is a live, valid, correctly
// built specimen with two entries whose FNames this plugin can compute. Dump
// the 80 bytes, find the two known values, and the layout stops being a guess.
//
// Answers with a known value beat inference from instructions. That is the same
// rule this project applies to every other instrument.
//
// PURE READ. Nothing is written. It can run on a live server.
static void DumpOwnedSet(void* controller)
{
    if (!controller || !g_api) return;

    const int32_t off = g_api->OffsetDoMembro(controller, "m_OwnedDLCs");
    if (off < 0)
    {
        g_api->Log("[engrams]   m_OwnedDLCs does not exist on this build.");
        return;
    }

    uint8_t s[80];
    std::memset(s, 0, sizeof(s));
    if (g_api->LerMembro(controller, uint32_t(off), s, sizeof(s)) <= 0)
    {
        g_api->Log("[engrams]   could not read m_OwnedDLCs at +0x%X.", unsigned(off));
        return;
    }

    g_api->Log("[engrams]   m_OwnedDLCs at +0x%X — the 80 bytes as they are:", unsigned(off));
    for (int i = 0; i < 80; i += 8)
        g_api->Log("[engrams]     +0x%02X  %02X %02X %02X %02X %02X %02X %02X %02X   "
                   "(u32 %10u  u64 %llu)",
                   i, s[i],s[i+1],s[i+2],s[i+3],s[i+4],s[i+5],s[i+6],s[i+7],
                   unsigned(*(const uint32_t*)(s+i)),
                   (unsigned long long)(*(const uint64_t*)(s+i)));

    // The two the account owns, so the bytes can be recognised rather than
    // interpreted. If these turn up inside whatever the set points at, the
    // element layout is settled by observation.
    const char* const KNOWN[] = { "DLC_Turan", "DLC_Nemedia" };
    for (int k = 0; k < 2; ++k)
    {
        ConanApi::Nome n(KNOWN[k]);
        const uint32_t idx = *(const uint32_t*)(n.bruto);
        const uint32_t num = *(const uint32_t*)(n.bruto + 4);
        g_api->Log("[engrams]     FName(\"%s\") = index %u, number %u  (valid=%s)",
                   KNOWN[k], idx, num, n.valido ? "yes" : "NO");
    }

    // ── THE ELEMENT DUMP IS GONE, AND FOR THE SAME REASON AS THE OTHER ONE ──
    //
    // What stood here walked the 80 bytes eight at a time, treated every slot as
    // a pointer, and read 64 bytes from whatever `Legivel` accepted. It is the
    // SAME construct that killed the server at 22:50 in the FeatItem — and when
    // that one was removed, this one was left behind and shipped.
    //
    // It had run many times without crashing, which proves nothing: the set's
    // first slot really is a pointer, so it happened to be lucky. `Legivel`
    // proves a page is MAPPED, never that a value is a valid pointer, and a
    // construct that is only safe because of what the data happens to contain
    // is not safe — it is untested.
    //
    // It has also already done its job: the layout it was written to discover is
    // measured, documented above, and confirmed against the disassembly of
    // 0x1450EB950. There is nothing left to learn here and no reason to keep
    // pointing a loaded instrument at a live server.
    //
    // The header above the dump is enough for anyone reading the log: Data,
    // ArrayNum, HashSize and the rest are all in those 80 bytes, in plain sight.
}

// ── O POSITIVO DA LerConjunto, CONTRA DADO VIVO ─────────────────────────────
//
// Isto morava dentro do MeasureDlcGate, que so' e' chamado quando sobra feat
// recusado. Em 28/08/2026 o desbloqueio funcionou por inteiro — zero recusas — e
// a medicao nunca rodou. Um diagnostico que so' fala quando da' errado nao
// confirma nada quando da' certo, e "nao apareceu no log" ficou
// indistinguivel de "nao foi medido".
//
// Agora roda sempre, cedo, antes de qualquer emprestimo de conjunto: o que ela
// le' e' o estado REAL da conta, nao o nosso.
static void ProvarLerConjunto(void* controller)
{
    if (!g_temV8 || !controller) return;
    // ── O POSITIVO DA LerConjunto, com resposta conhecida ───────────────────
    //
    // Esta conta possui `DLC_Turan` e `DLC_Nemedia` — os dois FName que deram o
    // layout do FScriptSet em primeiro lugar. Ler o `m_OwnedDLCs` pela primitiva
    // nova e reencontrar exatamente esses dois é o controle positivo que os
    // negativos do arranque não conseguem dar: ele prova que a função LÊ, e não
    // apenas que recusa.
    //
    // Se um dia a conta mudar de pacotes, este teste passa a comparar contra o
    // que o `HasDLC` respondeu na tabela acima, e não contra dois nomes fixos —
    // por isso o log imprime o que achou, e não só um "ok".
    {
        const int32_t offSet = g_api->OffsetDoMembro(controller, "m_OwnedDLCs");
        if (offSet >= 0)
        {
            SetElem lidos[32];
            std::memset(lidos, 0, sizeof(lidos));
            int quantos = 0;
            const int ok = g_api->LerConjunto(
                static_cast<const uint8_t*>(controller) + offSet,
                uint32_t(sizeof(SetElem)), lidos, 32, &quantos);

            if (ok && quantos > 0)
            {
                g_api->Log("[engrams]   LerConjunto no m_OwnedDLCs: %d elemento(s) —",
                           quantos);
                char nm[128];
                for (int i = 0; i < quantos && i < 8; ++i)
                {
                    nm[0] = 0;
                    g_api->NomeDeFName(int32_t(lidos[i].nomeIdx), nm, sizeof(nm));
                    g_api->Log("[engrams]     [%d] \"%s\" (index %u, number %u)",
                               i, nm[0] ? nm : "(?)",
                               unsigned(lidos[i].nomeIdx), unsigned(lidos[i].nomeNum));
                }
            }
            else
                g_api->Log("[engrams]   LerConjunto no m_OwnedDLCs devolveu %d "
                           "elemento(s) (ok=%d). Zero e' hipotese: ou o conjunto "
                           "esta' vazio, ou a leitura recusou.", quantos, ok);
        }
    }
}

static void MeasureDlcGate(void* controller, void* character, int featId)
{
    if (!controller || !character || !g_api) return;

    DumpOwnedSet(controller);

    void* spawner = g_api->GetDefaultObject("GameItemSpawner");

    g_api->Log("[engrams] ---- DLC, MEASURED (reads only, nothing written) ----");
    g_api->Log("[engrams]   package           ctrl.HasDLC  char.HasDLC  IsDLCDisabled");

    for (int i = 0; i < N_DLC_PACKAGES; ++i)
    {
        const char* pkg = DLC_PACKAGES[i];

        const bool b1 = ConanApi::Call<bool>(controller, "HasDLC", ConanApi::Nome(pkg));
        const char* c1 = Tri(b1);

        const bool b2 = ConanApi::Call<bool>(character, "HasDLC", ConanApi::Nome(pkg));
        const char* c2 = Tri(b2);

        const char* c3 = "n/a";
        if (spawner)
        {
            const bool b3 = ConanApi::Call<bool>(spawner, "IsDLCDisabled",
                                                 controller, ConanApi::Nome(pkg));
            c3 = g_api->UltimaChamadaExecutou() ? (b3 ? "DISABLED" : "no") : "n/a";
        }

        g_api->Log("[engrams]   %-17s %-12s %-12s %s%s", pkg, c1, c2, c3,
                   (i >= N_DLC_PACKAGES - 2) ? "   <- control: account OWNS this" : "");
    }

    // ── and the one refused feat, end to end ────────────────────────────────
    if (!spawner || featId <= 0) return;

    void* item = ConanApi::Call<void*>(spawner, "SpawnFeatItem", controller, int32_t(featId));
    if (g_api->UltimaChamadaExecutou() == 0 || !item || !g_api->Legivel(item, 8))
    {
        g_api->Log("[engrams]   SpawnFeatItem(%d) gave nothing usable — the refused "
                   "feat stays undiagnosed rather than guessed at.", featId);
        return;
    }

    // What the FEAT says it belongs to, from the object rather than from the
    // table column. If these two ever disagree, the column is not the gate.
    FNameCru pkgName{};
    pkgName = ConanApi::Call<FNameCru>(item, "GetDLCPackage");
    if (g_api->UltimaChamadaExecutou())
    {
        char buf[128]; buf[0] = 0;
        g_api->NomeDeFName(pkgName.indice, buf, sizeof(buf));
        g_api->Log("[engrams]   feat %d: GetDLCPackage() says \"%s\"", featId,
                   buf[0] ? buf : "(empty)");
    }
    else
        g_api->Log("[engrams]   feat %d: GetDLCPackage did not answer.", featId);

    const bool auto_ = ConanApi::Call<bool>(item, "GetAutoLearnDlcFeat");
    g_api->Log("[engrams]   feat %d: GetAutoLearnDlcFeat = %s", featId, Tri(auto_));

    // ── WHERE THE FEATITEM KEEPS ITS PACKAGE NAME ───────────────────────────
    //
    // Disassembled from HasDlcOrEntitlementForFeat (0x144E185F0):
    //
    //     call [vtable+0x498]     FeatItem->GetDLCPackage() -> FName
    //     mov  rax, [rsp+0x50]
    //     test rax, rax
    //     je   0x144E186C6        -> mov al,1 ; ret     EMPTY MEANS ALLOWED
    //
    // An empty package short-circuits the entire check: no ownership set is
    // consulted, and the branch that reaches the reward set is never taken. That
    // makes the package FName a smaller lever than either set — and the only one
    // that covers Bazaar content, whose reward id comes from a second virtual
    // (`[vtable+0x4a0]`) that reflection does not expose.
    //
    // `GameItem` publishes only `TemplateId`, so this field is not a UPROPERTY
    // and cannot be resolved by name. It can still be LOCATED the way the set
    // layout was: search the object for a value already known. GetDLCPackage
    // was just called above, so this feat's package FName is in hand; finding
    // those 8 bytes inside the object gives the offset by observation.
    //
    // ── REMOVED: THIS IS WHAT CRASHED THE SERVER, 27/08/2026 22:50 ─────────
    //
    // Two constructs stood here, and BOTH were wrong:
    //
    //   1. `uint8_t obj[0x400]` read from a FeatItem — golden/membros.json says
    //      FeatItem is 544 bytes. That read 480 bytes PAST THE END of the
    //      object, straight into whatever the allocator put next, and off the
    //      end of the page when the object sat near one.
    //   2. a sweep that treated every 8-byte slot as a pointer and read 0x100
    //      bytes from anything `Legivel` accepted.
    //
    // Either one alone is enough to kill the process, and together they did,
    // with a real player connected: `Exited (3)`, the log stopping dead on the
    // line above.
    //
    // WHAT IT WAS LOOKING FOR is settled anyway, and by the disassembly rather
    // than by groping at memory: `HasDlcOrEntitlementForFeat` gets the package
    // through `[vtable+0x498]`, and the FeatItem does not keep it — the row does.
    //
    // THE RULE THIS COST: never read N bytes from an object without asking the
    // reflection how big it is, and never dereference a value just because it
    // came out of an object and a page happened to be mapped. Structure
    // information comes from the structure's own API, or from disassembling the
    // function that reads it. Not from a sweep.

    // ── THE VOCABULARY THE BAZAAR LEG NEEDS ─────────────────────────────────
    //
    // From the disassembly of 0x1450EB890, the twin reader:
    //
    //     mov rdi,[rdx] · movsxd rax,[rdx+8] · lea rbp,[rdi+rax*8]
    //         ^ rdx is a TArray<FName> — the feat's REWARD IDS, not one name
    //     loop: mov rbx,[rdi]  ... search the set at +0x1330 ... cmp -> TRUE
    //
    // The check passes when ANY ONE of a feat's reward ids is in the set, and
    // the set at +0x1330 has the same layout as the one already being lent. So
    // the machinery is done; only the vocabulary is missing, because the ids
    // come from `[vtable+0x4a0]`, which reflection does not publish.
    //
    // `GameItemSpawner::m_RewardsTable` is a DataTable, and its row names are a
    // candidate for exactly those ids. Read with the SAME call the plugin
    // already uses on the FeatTable — a documented table API. That is the whole
    // difference from what crashed the server: asking the structure, instead of
    // walking memory hoping to recognise something.
    //
    // NOT NESTED. The first version of this sat inside the `if` above and was
    // deleted along with it without anyone noticing, because the indentation
    // said otherwise. It runs on its own now.
    //
    // ── NAMING [vtable+0x4a0], WITH THE OFFLINE WORK DONE FIRST ─────────────
    //
    // Five candidates for the reward vocabulary have been measured and all five
    // were wrong. The disassembly names the SLOT but not the function, so the
    // remaining question is which function sits at [vtable+0x4a0].
    //
    // THE OFFLINE PASS CAME FIRST THIS TIME, and that order is the point: the
    // value wanted is a constant of the build, and burning a player's evening to
    // read a number that lives in a file on disk is what the previous attempt
    // did wrong. Scanning the .rdata of the shipped exe for vtables (>= 0x4A8
    // bytes, ProcessEvent at slot 79, grouped by the shared GetDLCPackage at
    // +0x498) narrowed 1524 candidates to a family of 36 — and inside it, four
    // classes OVERRIDE +0x4a0:
    //
    //     RVA 0x014FA530   the base, in 31 of the 36
    //     RVA 0x0247CD80   override, 2 vtables    mov rax,[rcx+0xB8]; ret
    //     RVA 0x03DED1F0   override, 1 vtable
    //     RVA 0x03944540   override, 1 vtable
    //     RVA 0x03ED4900   override, 1 vtable
    //
    // Which of those is FeatItem cannot be settled offline: RTTI is stripped
    // from this build, so nothing in the file ties a vtable to a class name.
    // ONE 16-byte read decides it, and that read is safe for a reason that
    // survives inspection: vtable[79] was dereferenced AND CALLED by InvokeRaw
    // moments ago (that is how GetDLCPackage above ran), so `vtab` is a real
    // vtable, living in .rdata — a read-only region that is never decommitted
    // while the process lives, and therefore immune to both holes in `Legivel`
    // (the missing overflow guard, and the 250 ms region cache).
    //
    // NOT the claim the first draft made. That one said the game had just
    // called [vtable+0x498]; it had not — ConanApi::Call goes through
    // ProcessEvent, which is slot 79 (+0x278). The read was fine; the reason
    // given for it was invented, and an invented reason is what the NEXT person
    // reuses to justify the next raw read.
    //
    // THE MODULE BASE IS DERIVED, NEVER ASSUMED. A hardcoded 0x140000000 would
    // be wrong the moment ASLR moves the image — and this exe has DYNAMIC_BASE
    // and HIGH_ENTROPY_VA set, with 3.2 MB of .reloc. So the base is computed
    // FROM the +0x498 slot, whose RVA the offline pass already knows, and the
    // result is only trusted when it lands on a 64 KB boundary. If it does not,
    // the log says so and prints no RVA at all: a wrong RVA would send the next
    // session disassembling the wrong function, at the cost of another evening.
    {
        uint8_t vt[8];
        std::memset(vt, 0, sizeof(vt));
        if (g_api->LerMembro(item, 0, vt, sizeof(vt)) > 0)
        {
            void* vtab = *(void* const*)vt;
            if (vtab && g_api->Legivel(vtab, 0x4A8))
            {
                uint8_t dois[16];
                std::memset(dois, 0, sizeof(dois));
                if (g_api->LerMembro(vtab, 0x498, dois, sizeof(dois)) > 0)
                {
                    const uint64_t f498 = *(const uint64_t*)(dois);
                    const uint64_t f4a0 = *(const uint64_t*)(dois + 8);

                    // The two RVAs the offline scan says +0x498 can be. Either
                    // one anchors the base; neither matching means the vtable
                    // is not the family that was scanned, and nothing below is
                    // worth printing.
                    static const uint64_t ANCORA[] = { 0x0129DD30ull, 0x014FA530ull };
                    uint64_t base = 0;
                    for (int i = 0; i < 2 && base == 0; ++i)
                        if (f498 > ANCORA[i])
                        {
                            const uint64_t b = f498 - ANCORA[i];
                            if ((b & 0xFFFFull) == 0) base = b;   // 64 KB aligned
                        }

                    if (base == 0)
                    {
                        g_api->Log("[engrams]   vtable %p: [+0x498] = %p does not match "
                                   "either GetDLCPackage found offline. This is NOT the "
                                   "scanned family — printing no RVA rather than a wrong "
                                   "one.", vtab, (void*)f498);
                    }
                    else
                    {
                        const uint64_t r498 = f498 - base, r4a0 = f4a0 - base;
                        g_api->Log("[engrams]   vtable %p · module base %p (derived from "
                                   "the +0x498 anchor, 64KB-aligned)",
                                   vtab, (void*)base);
                        g_api->Log("[engrams]   [+0x498] GetDLCPackage = RVA 0x%llX  "
                                   "(matches the offline scan — the family is right)",
                                   (unsigned long long)r498);

                        // Positive control: the offline pass says +0x4a0 must be
                        // one of five. If it is, the method is confirmed AND the
                        // function is named. If it is not, the offline grouping
                        // was wrong, and saying so is worth more than a number.
                        static const uint64_t ESPERADOS[] = {
                            0x014FA530ull, 0x0247CD80ull, 0x03DED1F0ull,
                            0x03944540ull, 0x03ED4900ull };
                        bool bate = false;
                        for (int i = 0; i < 5; ++i) if (r4a0 == ESPERADOS[i]) bate = true;

                        g_api->Log("[engrams]   [+0x4a0] THE REWARD GETTER = RVA 0x%llX  %s",
                                   (unsigned long long)r4a0,
                                   bate ? "<<< one of the five found offline — DISASSEMBLE THIS"
                                        : "<<< NOT among the five found offline — the offline "
                                          "grouping missed it; disassemble anyway");
                    }
                }
                else
                    g_api->Log("[engrams]   could not read the vtable slots.");
            }
            else
                g_api->Log("[engrams]   the FeatItem's vtable is not readable up to "
                           "+0x4A8 — not touching it.");
        }
        else
            g_api->Log("[engrams]   could not read the FeatItem's vtable pointer — "
                       "the item was refused by the API's own guard.");
    }

    // ── THE STRONGEST CANDIDATE FOR THE REWARD IDS ──────────────────────────
    //
    // `[vtable+0x4a0]` returns a TArray<FName> and takes only `this`. Exactly
    // one reflected function on this class matches that shape:
    //
    //     GameItem::GetItemTags(parms=16) -> TArray<FName> ReturnValue
    //
    // FeatItem inherits from GameItem, and "item tags" is precisely the sort of
    // key an entitlement system indexes by. If these are the names the reward
    // set holds, the whole Bazaar half is reachable through ordinary reflection
    // — no native virtual, no raw memory, nothing like what crashed the server.
    //
    // Measured against the feat the game actually refused, so the answer is
    // about a real case rather than a plausible one.
    {
        std::vector<FNameCru> tags(256u);
        int nt = 0;
        ConanApi::CallSaida(item, "GetItemTags",
                            ConanApi::ParaRetornoLista(tags.data(), 256, nt));
        if (g_api->UltimaChamadaExecutou() && nt > 0)
        {
            g_api->Log("[engrams]   feat %d: GetItemTags returned %d tag(s) — if the "
                       "reward set is keyed by these, this is the Bazaar half.",
                       featId, nt);
            char nm[128];
            const int mostrar = (nt < 12) ? nt : 12;
            for (int i = 0; i < mostrar; ++i)
            {
                nm[0] = 0;
                g_api->NomeDeFName(tags[size_t(i)].indice, nm, sizeof(nm));
                g_api->Log("[engrams]     tag[%d] = \"%s\" (index %u, number %u)",
                           i, nm[0] ? nm : "(?)",
                           unsigned(tags[size_t(i)].indice),
                           unsigned(tags[size_t(i)].numero));
            }
        }
        else
            g_api->Log("[engrams]   feat %d: GetItemTags returned nothing (executed=%s). "
                       "Not the vocabulary either.", featId,
                       g_api->UltimaChamadaExecutou() ? "yes" : "no");
    }

    // Reporting only. Nothing is lent, nothing is written.
    if (void* rewards = PointerMember(spawner, "m_RewardsTable"))
    {
        void* dtlib = g_api->GetDefaultObject("DataTableFunctionLibrary");
        if (dtlib)
        {
            std::vector<FNameCru> rows(65536u);
            int nr = 0;
            ConanApi::CallSaida(dtlib, "GetDataTableRowNames", rewards,
                                ConanApi::ParaForaLista(rows.data(), 65536, nr));
            if (nr > 0)
            {
                g_api->Log("[engrams]   m_RewardsTable: %d row(s). If these are the "
                           "reward ids, lending them at +0x1330 is the Bazaar half.", nr);
                char nm[128];
                const int mostrar = (nr < 8) ? nr : 8;
                for (int i = 0; i < mostrar; ++i)
                {
                    nm[0] = 0;
                    g_api->NomeDeFName(rows[size_t(i)].indice, nm, sizeof(nm));
                    // The Number matters: `Foo` and `Foo_1` share an index and
                    // are different names. Printing only the index would hand
                    // back a vocabulary that silently does not match.
                    g_api->Log("[engrams]     row[%d] = \"%s\" (index %u, number %u)",
                               i, nm[0] ? nm : "(?)",
                               unsigned(rows[size_t(i)].indice),
                               unsigned(rows[size_t(i)].numero));
                }
            }
            else
                g_api->Log("[engrams]   m_RewardsTable gave ZERO rows — zero is a "
                           "hypothesis, not a conclusion; it may not be loaded here.");
        }
    }
    else
        g_api->Log("[engrams]   m_RewardsTable is not reachable from the spawner CDO.");

    const bool has = ConanApi::Call<bool>(spawner, "HasDlcOrEntitlementForFeat",
                                          character, item);
    if (g_api->UltimaChamadaExecutou() == 0)
    {
        g_api->Log("[engrams]   HasDlcOrEntitlementForFeat did not answer on this build.");
        return;
    }

    if (has)
        g_api->Log("[engrams]   VERDICT: the game says the account DOES have the "
                   "entitlement for feat %d, and it was refused anyway — the "
                   "refusal is NOT about DLC. Look at ServerForceLearnFeat.", featId);
    else
        g_api->Log("[engrams]   VERDICT: the game says the account does NOT have "
                   "the entitlement for feat %d. Read the table above to see "
                   "whether that is ownership or the package being switched off.",
                   featId);
}

static Result UnlockEverything(void* controller)
{
    Result r;

    // Before the feats, because it is meant to change what the feat loop is
    // allowed to grant. After them it would be a flag set for next time and a
    // log line that reads like it did something.
    if (g_unlockDlc)
        r.dlc = UnlockDlcContent(controller, r.dlcHow);

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
        // The window in which OnDlcCheck answers, and it has to CLOSE — including
        // down a path I did not foresee. Left open, this plugin would start
        // answering "owns it" for every DLC check on the server, for players who
        // never typed anything. A destructor closes it; a line at the bottom of
        // the function only closes it on the paths somebody remembered.
        struct Window
        {
            void** slot;
            Window(void** s, void* v) : slot(s) { *slot = v; }
            ~Window() { *slot = nullptr; }
        };

        g_dlcAsked = g_dlcForced = 0;      // counted per command, not per session

        // ── THE WINDOW, AND IT CLOSES BEFORE ANYTHING IS MEASURED ───────────
        //
        // This inner scope exists for one reason: everything that FAKES state —
        // the lent set and the hook that answers the DLC check — has to be gone
        // before a single diagnostic runs. The first version kept both alive
        // across the measurements below, which meant `HasDLC` was answering
        // about the plugin's own forgery and the positive control (Turan and
        // Nemedia reading YES) could not fail even if everything were broken.
        //
        // An instrument that cannot return a negative is not an instrument.
        // This project has paid for that lesson more than once, and it had
        // walked straight back into it.
        //
        // The loan does NOT depend on `m_CanBypassEntitlements` any more. That
        // flag was measured useless twice; gating the one mechanism that might
        // work behind the one that provably does not was a leftover.
        {
            // ANTES do emprestimo, sempre: le' o conjunto REAL da conta. Depois
            // da janela abrir, o que esta' la' e' NOSSO, e ler isso nao provaria
            // nada sobre a primitiva.
            ProvarLerConjunto(controller);

            Window janela(&g_grantingFor,
                          (g_unlockDlc || g_unlockBazaar) ? pawn : nullptr);
            LentSetGuard emprestimo(&g_loanDlc, &g_loanBazaar);

            // `DLC_PACKAGES` carries `DLC_Turan` and `DLC_Nemedia` too — the two
            // this account genuinely owns — so the window never TAKES AWAY
            // something the player really has while it is open.
            if (g_unlockDlc)
            {
                if (!CalibrateLentSet(controller))
                    g_api->Log("[engrams]   NOT lending: the calibration did not "
                               "invert. The feats this account owns are still "
                               "granted below; the DLC half is not attempted.");
                else if (!LendOwnedSet(g_loanDlc, controller, DLC_PACKAGES, N_DLC_PACKAGES))
                    g_api->Log("[engrams]   the calibration inverted but the full "
                               "loan was refused — see the line above for why. "
                               "The DLC half is not attempted.");
                else
                    g_api->Log("[engrams]   m_OwnedDLCs: lent %d package(s), for "
                               "this command only.", N_DLC_PACKAGES);
            }

            LearnEveryFeat(prog, r);
        }   // <- the DLC loan is back and the hook is disarmed HERE

        // ── THE SECOND LEG: Bazaar and Battle Pass ──────────────────────────
        //
        // AFTER the first pass, because it needs the ids the game actually
        // refused — which only the first pass knows. Lending the package names
        // was tried on 27/08 21:50 and moved nothing; this tries the ids, which
        // is what a per-reward set would be keyed by.
        //
        // Its own scope, its own loan, its own restore. Nothing here runs while
        // the DLC loan is out.
        if (g_unlockBazaar && r.refused > 0)
            TryBazaarByRewardIds(controller, prog, r);

        g_api->Log("[engrams] feats: %d walked · %d learned now · %d already had "
                   "· %d refused",
                   r.seen, r.learned, r.already, r.refused);

        // WHETHER THE HOOK IS EVEN ON THIS PATH. If the game asked zero times,
        // the check is not reached through ProcessEvent on this build — native
        // C++ calling native C++ never passes the funnel a hook sits in. That is
        // a different problem from "asked and refused anyway", and guessing
        // between them costs another player's evening.
        if (g_unlockDlc || g_unlockBazaar)
            g_api->Log("[engrams] DLC: the game's own check ran %d time(s) and was "
                       "answered yes %d time(s).%s",
                       g_dlcAsked, g_dlcForced,
                       g_dlcAsked == 0
                         ? " ZERO means the check never came through ProcessEvent "
                           "— it is called natively, and no hook can see it."
                         : "");

        // Every single one refused is not "some feats are special" — it is the
        // signature of a route that isn't working at all, and it deserves to be
        // named rather than left for someone to infer from an odd number.
        if (r.seen > 0 && r.learned == 0 && r.already == 0)
            g_api->Log("[engrams] EVERY feat was refused. Either "
                       "ServerForceLearnFeat or IsFeatPurchased is not behaving "
                       "as expected on this build — the route, not the feats.");

        // WHAT THE ATTEMPT WAS WORTH. The refusal count is the only thing that
        // can say it, and it is measured AFTER the loan went back — so what the
        // diagnostics below read is the account's real state, not ours.
        if ((g_unlockDlc || g_unlockBazaar) && r.refused > 0)
        {
            g_api->Log("[engrams] DLC: %d feat(s) were STILL refused after the "
                       "attempt. Everything below is measured with the lent set "
                       "already returned, so it reads real state.", r.refused);
            MeasureDlcGate(controller, pawn, r.firstRefused);
        }
        else if ((g_unlockDlc || g_unlockBazaar) && r.seen > 0)
        {
            g_api->Log("[engrams] DLC: NOTHING was refused — the DLC feats came "
                       "through.");
        }
        else if (r.dlc == 0)
        {
            g_api->Log("[engrams] DLC: config.json asked for unlock_dlc, and the "
                       "flag could NOT be set. The %d refused feat(s) above are "
                       "the DLC lock still in place.", r.refused);
        }
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

    // The one setting that is not text. Same colon rule as `take` — a key named
    // in the comment block at the top of the file is a mention, not a setting —
    // and `true`, `"true"` and `1` all count, because losing an afternoon to a
    // pair of quotes is not a lesson anybody needs.
    //
    // ANYTHING ELSE LEAVES IT OFF. A config this fails to parse hands out no
    // DLC; the failure direction is the safe one, and that is not an accident.
    auto takeBool = [&](const char* key, bool& dest)
    {
        const std::string target = std::string("\"") + key + "\"";
        for (size_t p = text.find(target); p != std::string::npos;
             p = text.find(target, p + 1))
        {
            size_t c = p + target.size();
            while (c < text.size() && (text[c] == ' ' || text[c] == '\t')) ++c;
            if (c >= text.size() || text[c] != ':') continue;   // a mention, not a setting
            ++c;
            while (c < text.size() &&
                   (text[c] == ' ' || text[c] == '\t' || text[c] == '"')) ++c;
            if (c >= text.size()) return;
            dest = (text.compare(c, 4, "true") == 0) || (text[c] == '1');
            return;
        }
    };

    take("command",     g_command);
    take("permission",  g_permission);
    take("msg_ok",      g_msgOk);
    take("msg_denied",  g_msgDenied);
    take("msg_partial", g_msgPartial);
    take("msg_failed",  g_msgFailed);
    takeBool("unlock_dlc",    g_unlockDlc);
    takeBool("unlock_bazaar", g_unlockBazaar);

    // unlock_dlc is printed every start, not only when it is on. A server owner
    // who edited the file and got the syntax wrong sees "no" here instead of
    // finding out from a player asking why the DLC feats are still locked.
    g_api->Log("[engrams] config: command=\"%s\" permission=\"%s\"",
               g_command.c_str(),
               g_permission.empty() ? "(open to everyone)" : g_permission.c_str());
    g_api->Log("[engrams] config: unlock_dlc=%s · unlock_bazaar=%s",
               g_unlockDlc    ? "YES — DLC packages handed out"   : "no (DLC stays locked)",
               g_unlockBazaar ? "YES — Bazaar/Battle Pass handed out" : "no (Bazaar stays locked)");
}

// ── A BATERIA DA LerConjunto, PARAMETRIZADA PELA IMPLEMENTAÇÃO ──────────────
//
// Recebe a função em vez de chamar `g_api->LerConjunto` direto, e isso é o
// ponto: sem isso não há como rodá-la contra uma implementação errada, e sem
// rodá-la contra uma errada não se sabe se ela sabe reprovar.
typedef int (*FnLerConjunto)(const void*, uint32_t, void*, int, int*);

// O modo de falha mais provável de uma primitiva defensiva: recusar tudo. Passa
// nos quatro negativos com nota máxima e não lê nada.
static int LerConjuntoQuebrada(const void*, uint32_t, void*, int, int* fora)
{
    if (fora) *fora = 0;
    return 0;
}

static bool CalibrarLerConjunto(FnLerConjunto f, const char* qual)
{
    if (!f) return false;

    uint8_t lixo[64];
    int n = -1;

    const bool nulo = f(nullptr, 16, lixo, 4, &n) != 0;
    const bool tam  = f(lixo, 7, lixo, 4, &n) != 0;                    // 7 nao e' 16 nem 32
    const bool topo = f((const void*)0xFFFFFFFFFFFFFFFFull, 16, lixo, 4, &n) != 0;
    const bool zero = f(lixo, 16, lixo, 0, &n) != 0;                   // maxElementos 0

    // O positivo, com resposta conhecida POR CONSTRUÇÃO: um cabeçalho de
    // FScriptSet apontando para três elementos que esta função acabou de
    // escrever. Se voltarem três, e forem esses três, ela lê.
    SetElem esperado[3];
    std::memset(esperado, 0, sizeof(esperado));
    esperado[0].nomeIdx = 0x11111111; esperado[0].nomeNum = 1;
    esperado[1].nomeIdx = 0x22222222; esperado[1].nomeNum = 2;
    esperado[2].nomeIdx = 0x33333333; esperado[2].nomeNum = 3;

    uint8_t cab[0x50];
    std::memset(cab, 0, sizeof(cab));
    void* pd = esperado;
    std::memcpy(cab + 0x00, &pd, sizeof(pd));       // ponteiro dos elementos
    const int32_t tres = 3;
    std::memcpy(cab + 0x08, &tres, sizeof(tres));   // ArrayNum
    std::memcpy(cab + 0x0C, &tres, sizeof(tres));   // ArrayMax

    SetElem volta[3];
    std::memset(volta, 0, sizeof(volta));
    int lidos = -1;
    const bool fiel = f(cab, uint32_t(sizeof(SetElem)), volta, 3, &lidos) != 0
                      && lidos == 3
                      && std::memcmp(volta, esperado, sizeof(esperado)) == 0;

    // E o corte: pedir menos do que existe copia o que cabe, não o que há.
    SetElem corte[1];
    std::memset(corte, 0, sizeof(corte));
    int cortados = -1;
    const bool cortou = f(cab, uint32_t(sizeof(SetElem)), corte, 1, &cortados) != 0
                        && cortados == 1
                        && corte[0].nomeIdx == esperado[0].nomeIdx;

    const bool ok = !nulo && !tam && !topo && !zero && fiel && cortou;
    g_api->Log("[engrams]   %-9s nulo=%s tam-invalido=%s topo=%s max-zero=%s "
               "· le-3-de-3=%s corta-em-1=%s -> %s",
               qual,
               nulo ? "PASSOU" : "recusou", tam  ? "PASSOU" : "recusou",
               topo ? "PASSOU" : "recusou", zero ? "PASSOU" : "recusou",
               fiel   ? "sim" : "NAO", cortou ? "sim" : "NAO",
               ok ? "passou na bateria" : "reprovada");
    return ok;
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

    // The DLC check, hooked only when the owner asked for unlock_dlc. Registered
    // as the AFTER callback (third argument), never the before one: before would
    // mean cancelling the game's own function, and this only needs to change the
    // answer, not skip the work.
    //
    // A failure here is not fatal to the command. The feats the account owns
    // still unlock; what is lost is the DLC half, and the log says so rather
    // than leaving somebody to wonder why the number did not move.
    if (g_unlockDlc || g_unlockBazaar)
    {
        g_dlcHookId = g_api->HookProcessEvent("HasDlcOrEntitlementForFeat",
                                              nullptr, OnDlcCheck, 100);
        if (!g_dlcHookId)
            g_api->Log("[engrams] DLC: could not hook HasDlcOrEntitlementForFeat "
                       "— the reason is above, from the API. unlock_dlc will have "
                       "no effect; everything else still works.");
        else
            g_api->Log("[engrams] DLC: hooked HasDlcOrEntitlementForFeat (id %u). "
                       "It answers ONLY during a %s, and ONLY about the character "
                       "who typed it.", g_dlcHookId, g_command.c_str());
    }

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

    // ── CALIBRAÇÃO DA GUARDA DE ESTOURO DO `Legivel` ────────────────────────
    //
    // Roda em todo arranque, e mede a guarda que a API ganhou depois de este
    // plugin derrubar o servidor em 27/08 às 22:50.
    //
    // O DEFEITO: `CacheConsultar` testava `a + n <= r.fim` sem guarda de
    // estouro. Com `a = 0xFFFF...FF` e `n = 0x100`, `a + n` dá a volta e vale
    // 0xFF, que cabe em qualquer região — então `Legivel` respondia SIM para o
    // topo do espaço de endereços, e o `memcpy` seguinte matava o processo.
    // E `0xFFFF...FF` é o lixo mais comum que existe: dois `INDEX_NONE` (-1)
    // vizinhos lidos como um `uint64`.
    //
    // TRÊS CASOS DE RESPOSTA CONHECIDA, porque guarda que nunca foi vista
    // reprovando não é guarda — é esperança:
    //
    //   negativo   0xFFFFFFFFFFFFFFFF + 0x100  ESTOURA        -> tem de dar NÃO
    //   fronteira  0xFFFFFFFFFFFFFEFF + 0x100  não estoura,
    //                                          mas é inválido -> tem de dar NÃO
    //   positivo   a própria tabela da API, 0x20 bytes        -> tem de dar SIM
    //
    // O positivo é o que separa "a guarda funciona" de "a guarda passou a
    // recusar tudo" — este projeto já teve duas guardas que reprovavam código
    // correto, e o estrago foi maior que o do defeito original.
    //
    // Chamar `Legivel` com endereço inválido é SEGURO: ela consulta, não lê.
    // Quem morre é o `memcpy` de quem acreditou nela.
    {
        // ── O CACHE PRECISA ESTAR QUENTE, OU O TESTE NÃO TESTA NADA ─────────
        //
        // A primeira versão disto rodava direto e devolveu 3/3 com a API AINDA
        // FURADA. Não porque o furo não existisse: porque no arranque o cache
        // `thread_local` está vazio, `CacheConsultar` devolve 0, e `LegivelImpl`
        // cai no `VirtualQuery` — que recusa corretamente. O teste passava sem
        // exercitar o caminho defeituoso uma única vez.
        //
        // O furo mora no ATALHO do cache, e o atalho só existe depois que uma
        // região >= 64 KB foi guardada (CACHE_MIN_TAM). Então o teste aquece
        // primeiro, com memória nossa, grande e comprometida.
        //
        // Isto é o controle positivo do próprio instrumento: sem o aquecimento,
        // "recusou (certo)" significa apenas "o atalho não foi tomado".
        static uint8_t aquecedor[192u * 1024u];   // > 64 KB, para o cache guardar
        aquecedor[0] = 1;                          // toca a página: MEM_COMMIT
        const bool quente = g_api->Legivel(aquecedor, 0x20) != 0;

        const bool neg  = g_api->Legivel((const void*)0xFFFFFFFFFFFFFFFFull, 0x100) != 0;
        const bool fron = g_api->Legivel((const void*)0xFFFFFFFFFFFFFEFFull, 0x100) != 0;
        const bool pos  = g_api->Legivel((const void*)g_api, 0x20) != 0;
        const bool ok   = (!neg && !fron && pos && quente);

        if (!quente)
            g_api->Log("[engrams] guarda de estouro: NAO CONSEGUI AQUECER o cache — "
                       "o resultado abaixo nao prova nada sobre o atalho.");

        g_api->Log("[engrams] guarda de estouro do Legivel: topo=%s · fronteira=%s "
                   "· valido=%s -> %s",
                   neg  ? "PASSOU (DEFEITO)" : "recusou (certo)",
                   fron ? "PASSOU (DEFEITO)" : "recusou (certo)",
                   pos  ? "aceitou (certo)"  : "RECUSOU (a guarda cegou)",
                   ok ? "CALIBRADA 3/3"
                      : "REPROVADA — ou a API deste servidor ainda tem o furo de "
                        "22:50, ou a correcao cegou a guarda");
    }

    // ── CALIBRAÇÃO DA ChamarVirtual (v7) ────────────────────────────────────
    //
    // Cinco casos de resposta conhecida. Sem os negativos, "funcionou" e "aceita
    // qualquer coisa" são indistinguíveis — e esta primitiva existe justamente
    // porque aceitar qualquer coisa matou o servidor em 27/08.
    //
    // O positivo usa a tabela da própria API como objeto: ela NÃO é UObject e
    // não tem vtable de verdade, então serve como negativo de "objeto errado".
    // O positivo de verdade só existe com um objeto do jogo em mãos, e ele é
    // medido no comando, não aqui.
    if (g_api->tamanho >= sizeof(ConanApiTabela) && g_api->ChamarVirtual)
    {
        void* r = nullptr;
        const bool nulo   = g_api->ChamarVirtual(nullptr, 0, nullptr, &r) != 0;
        const bool baixo  = g_api->ChamarVirtual((void*)0x100, 0, nullptr, &r) != 0;
        const bool alto   = g_api->ChamarVirtual((void*)g_api, 99999, nullptr, &r) != 0;
        const bool topo   = g_api->ChamarVirtual((void*)0xFFFFFFFFFFFFFFFFull, 1, nullptr, &r) != 0;
        // a tabela da API é memória legível cujo primeiro qword é um ponteiro de
        // função — mas para DADOS, não para uma vtable. O slot 0 dela não aponta
        // para código executável, então tem de ser recusado.
        const bool naoVt  = g_api->ChamarVirtual((void*)g_api, 300, nullptr, &r) != 0;

        const bool ok = (!nulo && !baixo && !alto && !topo && !naoVt);
        g_api->Log("[engrams] ChamarVirtual: nulo=%s pagina-baixa=%s indice-alto=%s "
                   "topo=%s nao-vtable=%s -> %s",
                   nulo  ? "PASSOU(DEFEITO)" : "recusou",
                   baixo ? "PASSOU(DEFEITO)" : "recusou",
                   alto  ? "PASSOU(DEFEITO)" : "recusou",
                   topo  ? "PASSOU(DEFEITO)" : "recusou",
                   naoVt ? "PASSOU(DEFEITO)" : "recusou",
                   ok ? "CALIBRADA 5/5 nos negativos"
                      : "REPROVADA — ou aceita o que devia recusar, ou nao le' o que devia ler");
    }
    else
        g_api->Log("[engrams] ChamarVirtual ausente: a API deste servidor e' anterior "
                   "a v7. O caminho do pacote vazio nao roda; o resto roda.");

    // O `tamanho` é o que separa "a tabela tem o campo" de "leio 8 bytes depois
    // do fim do struct". Decidido UMA vez, aqui, enquanto ele está em mãos.
    g_temV8 = (g_api->tamanho >= sizeof(ConanApiTabela)) && (g_api->LerConjunto != nullptr);

    // ── CALIBRAÇÃO DA LerConjunto (v8) — E DA PRÓPRIA CALIBRAÇÃO ────────────
    //
    // Quatro recusas provam que a primitiva diz não. Não provam que ela LÊ. Uma
    // implementação que devolvesse 0 sempre passaria nos quatro com nota máxima
    // — que é, letra por letra, como uma guarda desta casa passou verde tendo
    // conferido zero arquivos.
    //
    // Então a bateria roda DUAS vezes: contra a primitiva de verdade, e contra
    // uma sabidamente quebrada. Se a quebrada também passar, quem está com
    // defeito é o teste, e o log diz isso em vez de anunciar aprovação.
    if (g_temV8)
    {
        g_api->Log("[engrams] LerConjunto — a bateria roda 2x: na primitiva, e "
                   "numa quebrada de proposito (que TEM de reprovar)");
        const bool real  = CalibrarLerConjunto(g_api->LerConjunto, "primitiva");
        const bool falsa = CalibrarLerConjunto(LerConjuntoQuebrada, "quebrada");

        if (real && !falsa)
            g_api->Log("[engrams] LerConjunto: CALIBRADA — aprovou a certa e "
                       "reprovou a quebrada");
        else if (falsa)
            g_api->Log("[engrams] LerConjunto: TESTE INVALIDO — a bateria "
                       "aprovou uma implementacao que devolve zero sempre. "
                       "Nao confie no resultado da primitiva.");
        else
            g_api->Log("[engrams] LerConjunto: REPROVADA — a primitiva nao "
                       "passou na propria bateria");
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
