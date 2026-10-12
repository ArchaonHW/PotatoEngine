// Story 12.4 — potato.character/1: commander identity doc
// (predefined roster + player-created share the wire format),
// CharacterLibrary dir-scan registry, and priors binding into
// FogConfig (QuantumFog surface) + IntelLedger (hearsay/RivalDeck
// surface).
#include "Campaign/Characters/Character.h"
#include "Campaign/Characters/CommanderBind.h"
#include "Campaign/Narrative/IntelLedger.h"
#include "Gameplay/Fog/QuantumFog.h"
#include "Gameplay/Json/JsonValue.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;

int failures = 0;
void Check(bool cond, const char* name) {
    if (cond) {
        std::printf("[PASS] %s\n", name);
    } else {
        std::printf("[FAIL] %s\n", name);
        ++failures;
    }
}

using Potato::Campaign::Character;
using Potato::Campaign::CharacterLibrary;
using Potato::Campaign::IntelLedger;
using Potato::Campaign::IntelKind;
using Potato::Campaign::RivalPrior;
using Potato::Gameplay::FogConfig;
using Potato::Gameplay::JsonValue;

const char* CHAR_DOC = R"json({
    "schema": "potato.character/1",
    "id": "lv_bu",
    "name": "呂布",
    "origin": "九原",
    "prior": "aggressive",
    "deck_seed": 42,
    "deck": ["press_the_advance", "feint_east"],
    "created": false
})json";

bool WriteFile(const fs::path& p, const char* text) {
    std::ofstream f(p, std::ios::binary);
    f << text;
    return f.good();
}

void TestDoc() {
    auto doc = JsonValue::Parse(CHAR_DOC);
    Check(doc.ok(), "character doc parses");
    auto c = Character::FromJson(doc.value);
    Check(c.ok(), "character decodes");
    if (!c.ok()) return;
    Check(c.value.id == "lv_bu", "id decoded");
    Check(c.value.name == "呂布", "CJK name decoded");
    Check(c.value.origin == "九原", "CJK origin decoded");
    Check(c.value.prior == RivalPrior::Aggressive,
          "prior decoded");
    Check(c.value.deckSeed == 42, "deck_seed decoded");
    Check(c.value.deck.size() == 2 &&
              c.value.deck[0] == "press_the_advance",
          "explicit deck decoded");
    Check(!c.value.created, "created flag decoded");
}

void TestRoundTrip() {
    auto doc = JsonValue::Parse(CHAR_DOC);
    auto c = Character::FromJson(doc.value);
    Check(c.ok(), "roundtrip decode");
    if (!c.ok()) return;
    const std::string emit = c.value.ToJson().Emit();
    // Canonical re-parse emits identical bytes.
    auto again = JsonValue::Parse(emit);
    Check(again.ok() && again.value.Emit() == emit,
          "serialization byte-identical");
    auto c2 = Character::FromJson(again.value);
    Check(c2.ok() && c2.value.id == c.value.id &&
              c2.value.deckSeed == c.value.deckSeed,
          "roundtrip preserves fields");
    // Optional fields absent in input stay absent on emit —
    // defaults are canonical (absent == default).
    Check(emit.find("\"created\"") == std::string::npos,
          "default 'created' not emitted");
    auto minimal = JsonValue::Parse(R"json({
        "id": "custom", "name": "自創", "origin": "行伍",
        "prior": "cunning", "deck_seed": -1
    })json");
    auto mc = Character::FromJson(minimal.value);
    Check(mc.ok() && mc.value.deckSeed ==
                            ~std::uint64_t{0},
          "negative wire seed bitcasts to u64");
    // Ungated input (no "schema") still emits canonical bytes:
    // ToJson always stamps the schema tag, and emit→parse→emit
    // is a fixed point.
    if (mc.ok()) {
        const std::string canon = mc.value.ToJson().Emit();
        auto reparse = JsonValue::Parse(canon);
        Check(reparse.ok() &&
                  reparse.value.Emit() == canon &&
                  canon.find("\"schema\":\"potato.character/1\"") !=
                      std::string::npos,
              "canonical emit is a fixed point + stamped");
    } else {
        Check(false, "minimal doc decodes");
    }
}

void TestRejects() {
    const char* bad[] = {
        R"json([1,2])json",                                  // not obj
        R"json({"schema":"potato.character/2","id":"x",
                "name":"x","origin":"x","prior":"cunning",
                "deck_seed":0})json",                        // schema
        R"json({"id":"x","name":"x","origin":"x",
                "prior":"cunning","deck_seed":0,
                "bogus":1})json",                            // unknown field
        R"json({"name":"x","origin":"x","prior":"cunning",
                "deck_seed":0})json",                        // no id
        R"json({"id":"","name":"x","origin":"x",
                "prior":"cunning","deck_seed":0})json",      // empty id
        R"json({"id":"x","origin":"x","prior":"cunning",
                "deck_seed":0})json",                        // no name
        R"json({"id":"x","name":"x","prior":"cunning",
                "deck_seed":0})json",                        // no origin
        R"json({"id":"x","name":"x","origin":"x",
                "deck_seed":0})json",                        // no prior
        R"json({"id":"x","name":"x","origin":"x",
                "prior":42,"deck_seed":0})json",             // prior type
        R"json({"id":"x","name":"x","origin":"x",
                "prior":"reckless","deck_seed":0})json",     // prior vocab
        R"json({"id":"x","name":"x","origin":"x",
                "prior":"cunning"})json",                    // no seed
        R"json({"id":"x","name":"x","origin":"x",
                "prior":"cunning","deck_seed":"42"})json",   // seed type
        R"json({"id":"x","name":"x","origin":"x",
                "prior":"cunning","deck_seed":0,
                "deck":"press"})json",                       // deck type
        R"json({"id":"x","name":"x","origin":"x",
                "prior":"cunning","deck_seed":0,
                "deck":[42]})json",                          // deck entries
        R"json({"id":"x","name":"x","origin":"x",
                "prior":"cunning","deck_seed":0,
                "created":"yes"})json",                      // created type
        R"json({"id":"x","name":64,"origin":"x",
                "prior":"cunning","deck_seed":0})json",      // name type
    };
    for (const char* text : bad) {
        auto doc = JsonValue::Parse(text);
        if (!doc.ok()) {
            Check(false, "reject fixture must parse");
            continue;
        }
        auto c = Character::FromJson(doc.value);
        if (!c.ok()) {
            std::printf("[PASS] reject: %.60s\n", text);
        } else {
            std::printf("[FAIL] accepted: %.60s\n", text);
            ++failures;
        }
    }
    // Oversized field
    std::string big = "{\"id\":\"" + std::string(65, 'x') +
                      "\",\"name\":\"x\",\"origin\":\"x\","
                      "\"prior\":\"cunning\",\"deck_seed\":0}";
    auto bd = JsonValue::Parse(big);
    auto bc = Character::FromJson(bd.value);
    Check(!bc.ok(), "oversized id rejects");
}

void TestLibrary() {
    const fs::path dir = fs::temp_directory_path() /
                         "potato_test_char_lib";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    WriteFile(dir / "01_lv_bu.json", CHAR_DOC);
    WriteFile(dir / "02_custom.json", R"json({
        "schema": "potato.character/1", "id": "my_general",
        "name": "自家統帥", "origin": "鄉里",
        "prior": "defensive", "deck_seed": 7,
        "created": true
    })json");
    WriteFile(dir / "03_bad.json", R"json({
        "schema": "potato.character/1", "id": "broken"
    })json");
    WriteFile(dir / "04_dup.json", R"json({
        "schema": "potato.character/1", "id": "lv_bu",
        "name": "呂布乙", "origin": "五原",
        "prior": "aggressive", "deck_seed": 9
    })json");
    WriteFile(dir / "05_notjson.txt", "ignored");

    CharacterLibrary lib;
    auto res = CharacterLibrary::Load(dir, lib);
    Check(res.ok, "library load ok");
    Check(res.rejected.size() == 2,
          "bad file + duplicate rejected");
    Check(lib.Size() == 2, "two characters registered");
    Check(lib.Find("lv_bu") != nullptr, "predefined found");
    Check(lib.Find("my_general") != nullptr &&
              lib.Find("my_general")->created,
          "created commander registered");
    Check(lib.Find("lv_bu")->name == "呂布",
          "first registration wins on duplicate id");

    // Per-file isolation: a bad file doesn't disturb committed
    // entries.
    CharacterLibrary lib2;
    auto res2 = CharacterLibrary::Load(dir, lib2);
    Check(res2.ok && lib2.Size() == 2,
          "reload is stable/isolated");

    // Empty dir succeeds.
    const fs::path empty = dir / "empty";
    fs::create_directories(empty);
    CharacterLibrary lib3;
    auto res3 = CharacterLibrary::Load(empty, lib3);
    Check(res3.ok && lib3.Size() == 0, "empty dir ok");

    // Unreadable dir -> io.
    CharacterLibrary lib4;
    auto res4 = CharacterLibrary::Load(dir / "nope_nope", lib4);
    Check(!res4.ok && res4.error == "io",
          "unreadable dir -> io");

    fs::remove_all(dir, ec);
}

void TestPriors() {
    // Bias table applies to a default FogConfig.
    FogConfig cfg; // defaults: probe 25, decay 10, detect 60,
                   // initialIntel 60
    Potato::Campaign::ApplyPrior(cfg, RivalPrior::Aggressive);
    Check(cfg.probeGain == 35 && cfg.initialIntel == 50 &&
              cfg.detectThreshold == 60 && cfg.decayPerMinute == 10,
          "aggressive bias applies");
    FogConfig cfg2;
    Potato::Campaign::ApplyPrior(cfg2, RivalPrior::Defensive);
    Check(cfg2.decayPerMinute == 5 && cfg2.probeGain == 25,
          "defensive bias applies");
    FogConfig cfg3;
    Potato::Campaign::ApplyPrior(cfg3, RivalPrior::Cunning);
    Check(cfg3.detectThreshold == 50, "cunning bias applies");

    // Clamps: thin intel can't go negative, decay floored at 1.
    FogConfig thin;
    thin.initialIntel = 5;
    Potato::Campaign::ApplyPrior(thin, RivalPrior::Aggressive);
    Check(thin.initialIntel == 0, "initialIntel clamps at 0");
    FogConfig lowDecay;
    lowDecay.decayPerMinute = 2;
    Potato::Campaign::ApplyPrior(lowDecay, RivalPrior::Defensive);
    Check(lowDecay.decayPerMinute == 1, "decay floors at 1");
}

void TestHearsay() {
    auto doc = JsonValue::Parse(CHAR_DOC);
    auto c = Character::FromJson(doc.value);
    Check(c.ok(), "hearsay decode");
    if (!c.ok()) return;

    IntelLedger intel;
    auto r = Potato::Campaign::RecordCommander(c.value, intel);
    Check(r.ok(), "RecordCommander posts");
    Check(intel.Size() == 1, "one intel entry");
    const auto* e = intel.Entry(r.value);
    Check(e != nullptr && e->subject == "lv_bu" &&
              e->kind == IntelKind::RivalTemperament &&
              e->chapter == 0,
          "claim keyed by commander id + temperament");
    // The hearsay refers to the commander by name AND prior.
    Check(e != nullptr &&
              e->claim.find("呂布") != std::string::npos &&
              e->claim.find("aggressive") != std::string::npos,
          "claim carries name + prior");
    // Surface reads: pending intel lists the commander.
    Check(intel.PendingFor("lv_bu").size() == 1,
          "PendingFor sees commander intel");
}

} // namespace

int main() {
    TestDoc();
    TestRoundTrip();
    TestRejects();
    TestLibrary();
    TestPriors();
    TestHearsay();
    std::printf(failures == 0 ? "ALL PASS\n"
                              : "%d FAILURES\n",
                failures);
    return failures == 0 ? 0 : 1;
}
