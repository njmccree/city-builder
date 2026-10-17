#include "citysim/content/content_database.hpp"
#include "citysim/content/content_id.hpp"
#include "citysim/content/def_registry.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace citysim::content;

namespace {
struct Loaded {
    std::unique_ptr<ContentDatabase> db = std::make_unique<ContentDatabase>();
    LoadResult result;
};

Loaded load_fixture(const std::string& name) {
    Loaded loaded;
    loaded.result = loaded.db->load({std::filesystem::path(CITYSIM_TEST_FIXTURES_DIR) / "content" / name});
    return loaded;
}

// The first issue whose formatted text contains `needle`, or nullptr.
const ContentIssue* find_issue(const std::vector<ContentIssue>& issues, const std::string& needle) {
    for (const ContentIssue& issue : issues) {
        if (issue.format().find(needle) != std::string::npos) {
            return &issue;
        }
    }
    return nullptr;
}

std::vector<std::string> era_ids(const ContentDatabase& db) {
    std::vector<std::string> ids;
    for (const ContentId& id : db.registry<EraDef>().ids()) {
        ids.push_back(id.str());
    }
    return ids;
}
} // namespace

// ---- ContentId --------------------------------------------------------------------------------------------

TEST_CASE("ContentId accepts well-formed ids", "[content][id]") {
    for (const char* text : {"base:era.ancient", "mod_1:thing", "a:b", "x9:y.z_0"}) {
        REQUIRE(ContentId::is_valid(text));
    }
    const ContentId id("base:era.ancient");
    REQUIRE(id.str() == "base:era.ancient");
    REQUIRE(id.mod() == "base");
    REQUIRE(id.name() == "era.ancient");
}

TEST_CASE("ContentId rejects malformed ids with a reason", "[content][id]") {
    for (const char* text : {"", "noprefix", "a:b:c", ":name", "mod:", "Base:thing", "base:Thing",
                             "base:thi ng", "base:thing-1", "ba\x01se:x", "base: x"}) {
        std::string reason;
        REQUIRE_FALSE(ContentId::parse(text, &reason).has_value());
        REQUIRE_FALSE(reason.empty());
        REQUIRE_THROWS_AS(ContentId(text), std::invalid_argument);
    }
}

TEST_CASE("ContentId orders and compares as text", "[content][id]") {
    REQUIRE(ContentId("a:x") < ContentId("a:y"));
    REQUIRE(ContentId("a:z") < ContentId("b:a"));
    REQUIRE(ContentId("a:x") == ContentId("a:x"));
}

// ---- DefRegistry ------------------------------------------------------------------------------------------

TEST_CASE("DefRegistry sorts by id and hands out stable handles", "[content][registry]") {
    DefRegistry<int> registry;
    REQUIRE(registry.add(ContentId("m:c"), 3));
    REQUIRE(registry.add(ContentId("m:a"), 1));
    REQUIRE(registry.add(ContentId("m:b"), 2));
    REQUIRE_FALSE(registry.add(ContentId("m:b"), 99));   // duplicate: refused, nothing stored
    REQUIRE_THROWS_AS(registry.all(), std::logic_error); // not usable before finalize()

    registry.finalize();
    REQUIRE(registry.size() == 3);
    REQUIRE(registry.all()[0] == 1);
    REQUIRE(registry.all()[1] == 2);
    REQUIRE(registry.all()[2] == 3);

    const auto handle = registry.find(ContentId("m:b"));
    REQUIRE(handle.has_value());
    REQUIRE(handle->index == 1);
    REQUIRE(registry.get(*handle) == 2);
    REQUIRE(registry.id(*handle).str() == "m:b");

    REQUIRE_FALSE(registry.find(ContentId("m:zzz")).has_value());
    REQUIRE(registry.find("m:c").has_value());
    REQUIRE_FALSE(registry.find("not a valid id").has_value());
    REQUIRE_THROWS_AS(registry.get(DefHandle<int>{}), std::out_of_range);
    REQUIRE_THROWS_AS(registry.add(ContentId("m:d"), 4), std::logic_error); // read-only now
}

TEST_CASE("DefRegistry ends up identical whatever order definitions arrive in", "[content][registry]") {
    DefRegistry<int> forward;
    DefRegistry<int> backward;
    for (int i = 0; i < 20; ++i) {
        forward.add(ContentId("m:" + std::string(1, static_cast<char>('a' + i))), i);
        backward.add(ContentId("m:" + std::string(1, static_cast<char>('a' + 19 - i))), 19 - i);
    }
    forward.finalize();
    backward.finalize();
    REQUIRE(std::equal(forward.all().begin(), forward.all().end(), backward.all().begin()));
    REQUIRE(forward.ids().size() == backward.ids().size());
    for (std::size_t i = 0; i < forward.ids().size(); ++i) {
        REQUIRE(forward.ids()[i] == backward.ids()[i]);
    }
}

// ---- loading ----------------------------------------------------------------------------------------------

TEST_CASE("a well-formed mod loads", "[content][loader]") {
    const Loaded loaded = load_fixture("happy");
    REQUIRE(loaded.result.errors.empty());
    REQUIRE(loaded.result.warnings.empty());
    REQUIRE(loaded.result.ok());

    const auto& eras = loaded.db->registry<EraDef>();
    REQUIRE(eras.size() == 2);
    const auto alpha = eras.find("test:era.alpha");
    REQUIRE(alpha.has_value());
    const EraDef& era = eras.get(*alpha);
    REQUIRE(era.id.str() == "test:era.alpha");
    REQUIRE(era.name_key == "era.x.name");
    REQUIRE(era.real_seconds_per_year == 120);
    REQUIRE(era.default_start_year == -3000);
    REQUIRE(era.order == 1);

    const auto& defines = loaded.db->time_defines();
    REQUIRE(defines.base_tick_rate == 30);
    REQUIRE(defines.movement_scale == 2.5F);
    REQUIRE(defines.day_length_ticks == 9000);
}

TEST_CASE("definitions are sorted by id whatever the file order", "[content][loader]") {
    // The fixture spreads ids over a_first.json, b_second.json and sub/c_nested.json, none of them in id
    // order.
    const Loaded loaded = load_fixture("sort_order");
    REQUIRE(loaded.result.ok());
    REQUIRE(era_ids(*loaded.db) ==
            std::vector<std::string>{"test:era.alpha", "test:era.beta", "test:era.mid", "test:era.zeta"});

    // Same set of definitions in a different layout: identical registry.
    const Loaded happy = load_fixture("happy");
    REQUIRE(era_ids(*happy.db) == std::vector<std::string>{"test:era.alpha", "test:era.beta"});
}

TEST_CASE("a missing time.json falls back to the built-in defaults", "[content][loader]") {
    const Loaded loaded = load_fixture("no_time_file");
    REQUIRE(loaded.result.ok());
    const citysim::time::TimeDefines defaults;
    REQUIRE(loaded.db->time_defines().base_tick_rate == defaults.base_tick_rate);
    REQUIRE(loaded.db->time_defines().day_length_ticks == defaults.day_length_ticks);
}

TEST_CASE("a duplicate id is an error naming both places", "[content][loader]") {
    const Loaded loaded = load_fixture("duplicate_id");
    REQUIRE_FALSE(loaded.result.ok());
    const ContentIssue* issue = find_issue(loaded.result.errors, "duplicate id 'test:era.dup'");
    REQUIRE(issue != nullptr);
    REQUIRE(issue->file == "defs/b.json"); // the second one is the offender
    REQUIRE(issue->pointer == "/defs/0");
    REQUIRE(issue->message.find("defs/a.json") != std::string::npos); // and the first is named
    REQUIRE(loaded.db->registry<EraDef>().size() == 1);               // the first definition is kept
}

TEST_CASE("an unknown type is an error, and the rest still loads", "[content][loader]") {
    const Loaded loaded = load_fixture("unknown_type");
    REQUIRE_FALSE(loaded.result.ok());
    const ContentIssue* issue = find_issue(loaded.result.errors, "unknown definition type 'building'");
    REQUIRE(issue != nullptr);
    REQUIRE(issue->file == "defs/x.json");
    REQUIRE(issue->pointer == "/defs/1/type");
    REQUIRE(loaded.db->registry<EraDef>().size() == 1);
}

TEST_CASE("a missing required field is reported with its JSON pointer", "[content][loader]") {
    const Loaded loaded = load_fixture("missing_field");
    REQUIRE(loaded.result.errors.size() == 1);
    const ContentIssue& issue = loaded.result.errors[0];
    REQUIRE(issue.file == "defs/eras.json");
    REQUIRE(issue.pointer == "/defs/1");
    REQUIRE(issue.message == "missing required field 'real_seconds_per_year'");
    REQUIRE(issue.format() == "defs/eras.json:/defs/1: missing required field 'real_seconds_per_year'");
    // The healthy definition loads; the broken one is not stored half-parsed.
    REQUIRE(era_ids(*loaded.db) == std::vector<std::string>{"test:era.ok"});
}

TEST_CASE("wrong types and out-of-range numbers are reported per field", "[content][loader]") {
    const Loaded loaded = load_fixture("wrong_types");
    REQUIRE(loaded.result.errors.size() == 5);
    REQUIRE(find_issue(loaded.result.errors,
                       "/defs/0/real_seconds_per_year: expected a non-negative whole number") != nullptr);
    REQUIRE(find_issue(loaded.result.errors, "/defs/1/order: expected a whole number") != nullptr);
    REQUIRE(find_issue(loaded.result.errors,
                       "/defs/2/real_seconds_per_year: expected a non-negative whole number") != nullptr);
    REQUIRE(find_issue(loaded.result.errors, "/defs/3/default_start_year: number is out of range") !=
            nullptr);
    REQUIRE(find_issue(loaded.result.errors, "/defs/4/real_seconds_per_year: number is too large") !=
            nullptr);
    REQUIRE(loaded.db->registry<EraDef>().size() == 0);
}

TEST_CASE("malformed JSON is an error with the parser's message", "[content][loader]") {
    const Loaded loaded = load_fixture("bad_json");
    REQUIRE(loaded.result.errors.size() == 1);
    REQUIRE(loaded.result.errors[0].file == "defs/broken.json");
    REQUIRE(loaded.result.errors[0].message.find("invalid JSON") != std::string::npos);
}

TEST_CASE("files and definitions of the wrong shape are rejected without crashing", "[content][loader]") {
    const Loaded loaded = load_fixture("bad_shape");
    REQUIRE(find_issue(loaded.result.errors, "defs/a.json:/defs: \"defs\" must be an array") != nullptr);
    REQUIRE(find_issue(loaded.result.errors, "defs/b.json:: expected an object with a \"defs\" array") !=
            nullptr);
    REQUIRE(find_issue(loaded.result.errors, "defs/c.json:/defs/0: expected an object") != nullptr);
    REQUIRE(find_issue(loaded.result.errors, "defs/c.json:/defs/1: expected an object") != nullptr);
    REQUIRE(era_ids(*loaded.db) == std::vector<std::string>{"test:era.fine"});
}

TEST_CASE("invalid ids are rejected with a reason", "[content][loader]") {
    const Loaded loaded = load_fixture("bad_id");
    REQUIRE(loaded.result.errors.size() == 5);
    REQUIRE(find_issue(loaded.result.errors, "/defs/0/id: invalid id 'Base:Era.Ancient'") != nullptr);
    REQUIRE(find_issue(loaded.result.errors, "/defs/1/id: invalid id 'noprefix'") != nullptr);
    REQUIRE(find_issue(loaded.result.errors, "/defs/2/id: invalid id 'a:b:c'") != nullptr);
    REQUIRE(find_issue(loaded.result.errors, "/defs/3/id: invalid id ':name'") != nullptr);
    REQUIRE(find_issue(loaded.result.errors, "/defs/4/id: invalid id 'mod:'") != nullptr);
    REQUIRE(era_ids(*loaded.db) == std::vector<std::string>{"test:era.fine"});
}

TEST_CASE("unknown fields are warnings, not errors", "[content][loader]") {
    const Loaded loaded = load_fixture("unknown_field");
    REQUIRE(loaded.result.ok());
    REQUIRE(loaded.result.warnings.size() == 2);
    REQUIRE(find_issue(loaded.result.warnings, "defs/eras.json:/defs/0/colour: unknown field 'colour'") !=
            nullptr);
    REQUIRE(find_issue(loaded.result.warnings, "defines/time.json:/tick_rte: unknown field 'tick_rte'") !=
            nullptr);
}

TEST_CASE("bad time defines are reported per field", "[content][loader]") {
    const Loaded loaded = load_fixture("bad_time");
    REQUIRE(find_issue(loaded.result.errors,
                       "defines/time.json:/base_tick_rate: expected a non-negative whole number") != nullptr);
    REQUIRE(find_issue(loaded.result.errors,
                       "defines/time.json:/day_length_ticks: expected a non-negative whole number") !=
            nullptr);
    REQUIRE(find_issue(loaded.result.errors, "/speed_multipliers: expected an array of 4 whole numbers") !=
            nullptr);

    const Loaded element = load_fixture("bad_time_element");
    REQUIRE(find_issue(element.result.errors, "/speed_multipliers/2: expected a non-negative whole number") !=
            nullptr);
}

TEST_CASE("a UTF-8 byte-order mark is tolerated and non-json files are ignored", "[content][loader]") {
    const Loaded bom = load_fixture("bom");
    REQUIRE(bom.result.ok());
    REQUIRE(era_ids(*bom.db) == std::vector<std::string>{"test:era.bom"});

    const Loaded other = load_fixture("ignores_other_files");
    REQUIRE(other.result.ok());
    REQUIRE(era_ids(*other.db) == std::vector<std::string>{"test:era.real"});
}

TEST_CASE("a mod root that is not a directory is an error", "[content][loader]") {
    ContentDatabase db;
    const LoadResult result =
        db.load({std::filesystem::path(CITYSIM_TEST_FIXTURES_DIR) / "content" / "does_not_exist"});
    REQUIRE_FALSE(result.ok());
    REQUIRE(find_issue(result.errors, "mod root is not a directory") != nullptr);
}

TEST_CASE("loading twice or registering late is refused", "[content][loader]") {
    ContentDatabase db;
    (void)db.load({});
    REQUIRE_THROWS_AS(db.load({}), std::logic_error);
    REQUIRE_THROWS_AS(
        db.register_def_type<int>("thing", [](const nlohmann::json&, ParseContext&) { return 0; }),
        std::logic_error);
}

TEST_CASE("custom definition types can be registered", "[content][loader]") {
    struct Widget {
        ContentId id;
        std::string label;
    };
    ContentDatabase db;
    db.register_def_type<Widget>("widget", [](const nlohmann::json& json, ParseContext& context) {
        return Widget{context.def_id(), context.read_string(json, "label").value_or("")};
    });
    REQUIRE_THROWS_AS(db.register_def_type<Widget>(
                          "widget2", [](const nlohmann::json&, ParseContext&) { return Widget{}; }),
                      std::logic_error); // the C++ type is taken
    REQUIRE_THROWS_AS(
        db.register_def_type<int>("era", [](const nlohmann::json&, ParseContext&) { return 0; }),
        std::logic_error); // the name is taken

    const auto root = std::filesystem::temp_directory_path() / "citysim_content_widget_test";
    std::filesystem::create_directories(root / "defs");
    {
        std::ofstream(root / "defs" / "w.json")
            << R"({"defs":[{"type":"widget","id":"t:w.one","label":"hello"}]})";
    }
    const LoadResult result = db.load({root});
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);

    REQUIRE(result.ok());
    REQUIRE(db.registry<Widget>().get(*db.registry<Widget>().find("t:w.one")).label == "hello");
    REQUIRE_THROWS_AS(db.registry<std::string>(), std::logic_error);
}

// ---- references and validation
// -------------------------------------------------------------------------------

TEST_CASE("references resolve to handles in the target registry", "[content][refs]") {
    const Loaded loaded = load_fixture("refs_ok");
    REQUIRE(loaded.result.ok());

    const auto& eras = loaded.db->registry<EraDef>();
    const EraDef& first = eras.get(*eras.find("test:era.first"));
    REQUIRE(first.next_era.has_value());
    REQUIRE(first.next_era.resolved());
    REQUIRE(eras.get(first.next_era.handle).id.str() == "test:era.second");
    REQUIRE(eras.id(first.next_era.handle).str() == "test:era.second");

    const EraDef& last = eras.get(*eras.find("test:era.third"));
    REQUIRE_FALSE(last.next_era.has_value()); // optional, left out
    REQUIRE_FALSE(last.next_era.resolved());
}

TEST_CASE("an unresolved or malformed reference is reported where it was written", "[content][refs]") {
    const Loaded loaded = load_fixture("refs_bad");
    REQUIRE(loaded.result.errors.size() == 2);

    const ContentIssue* ghost = find_issue(loaded.result.errors, "test:era.ghost");
    REQUIRE(ghost != nullptr);
    REQUIRE(ghost->file == "defs/eras.json");
    REQUIRE(ghost->pointer == "/defs/0/next_era");
    REQUIRE(ghost->message == "unresolved reference: no era has the id 'test:era.ghost'");

    const ContentIssue* malformed = find_issue(loaded.result.errors, "Not An Id");
    REQUIRE(malformed != nullptr);
    REQUIRE(malformed->pointer == "/defs/1/next_era");

    // The good reference next to the bad ones still resolved.
    const auto& eras = loaded.db->registry<EraDef>();
    REQUIRE(eras.get(*eras.find("test:era.c")).next_era.resolved());
}

TEST_CASE("validation reports value problems per definition, in id order", "[content][validation]") {
    const Loaded loaded = load_fixture("validation_fail");
    REQUIRE(loaded.result.errors.size() == 4);

    // Reported in id order, not file order, so output is stable.
    REQUIRE(loaded.result.errors[0].message.find("does not come after") !=
            std::string::npos); // test:era.backwards
    REQUIRE(loaded.result.errors[0].pointer == "/defs/3/next_era");
    REQUIRE(loaded.result.errors[1].message == "order 5 is already used by 'test:era.same_a'"); // same_b
    REQUIRE(loaded.result.errors[1].pointer == "/defs/2/order");
    REQUIRE(loaded.result.errors[2].pointer == "/defs/5/next_era");       // test:era.self
    REQUIRE(loaded.result.errors[3].message == "must be greater than 0"); // test:era.zero
    REQUIRE(loaded.result.errors[3].pointer == "/defs/0/real_seconds_per_year");

    // The clash is reported once, on the later id only.
    REQUIRE(find_issue(loaded.result.errors, "/defs/1/order") == nullptr);
}

TEST_CASE("time defines that cannot work are errors", "[content][validation]") {
    const Loaded loaded = load_fixture("time_values_bad");
    REQUIRE(loaded.result.errors.size() == 4);
    REQUIRE(find_issue(loaded.result.errors, "defines/time.json:/base_tick_rate: must be greater than 0") !=
            nullptr);
    REQUIRE(find_issue(loaded.result.errors, "defines/time.json:/day_length_ticks: must be greater than 0") !=
            nullptr);
    REQUIRE(find_issue(loaded.result.errors, "defines/time.json:/movement_scale: must be greater than 0") !=
            nullptr);
    REQUIRE(find_issue(loaded.result.errors,
                       "defines/time.json:/speed_multipliers/2: speeds 1 to 3 need a multiplier above 0") !=
            nullptr);
}

TEST_CASE("issues carry the mod root so a tool can print a full path", "[content][validation]") {
    const Loaded loaded = load_fixture("missing_field");
    REQUIRE(loaded.result.errors.size() == 1);
    const ContentIssue& issue = loaded.result.errors[0];
    REQUIRE(issue.mod_root.find("fixtures/content/missing_field") != std::string::npos);
    REQUIRE(issue.format_full() ==
            issue.mod_root + "/defs/eras.json:/defs/1: missing required field 'real_seconds_per_year'");
}

TEST_CASE("a definition type can supply its own resolve and validate steps", "[content][validation]") {
    struct Node {
        ContentId id;
        DefRef<Node> parent;
        int weight = 0;
    };
    ContentDatabase db;
    db.register_def_type<Node>(
        "node",
        [](const nlohmann::json& json, ParseContext& context) {
            Node node;
            node.id = context.def_id();
            node.parent = read_def_ref<Node>(context, json, "parent", false);
            node.weight = context.read_i32(json, "weight").value_or(0);
            return node;
        },
        [](Node& node, ParseContext& context) { resolve_def_ref(context, node.parent, "parent"); },
        [](const Node& node, const DefRegistry<Node>&, ParseContext& context) {
            if (node.weight < 0) {
                context.error(context.child_pointer("weight"), "must not be negative");
            }
        });

    const auto root = std::filesystem::temp_directory_path() / "citysim_content_node_test";
    std::filesystem::create_directories(root / "defs");
    {
        std::ofstream(root / "defs" / "n.json") << R"({"defs":[
            {"type":"node","id":"t:n.root","weight":1},
            {"type":"node","id":"t:n.child","parent":"t:n.root","weight":-3}]})";
    }
    const LoadResult result = db.load({root});
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);

    REQUIRE(result.errors.size() == 1);
    REQUIRE(result.errors[0].pointer == "/defs/1/weight");
    const auto& nodes = db.registry<Node>();
    const auto child = nodes.find("t:n.child");
    REQUIRE(child.has_value());
    REQUIRE(nodes.get(*child).parent.resolved());
}

// ---- the shipped base game
// ---------------------------------------------------------------------------------

TEST_CASE("the base game's eras match the architecture's era cadence", "[content][base]") {
    ContentDatabase db;
    const LoadResult result = db.load({std::filesystem::path(CITYSIM_GAME_CONTENT_DIR) / "base"});
    REQUIRE(result.errors.empty());
    REQUIRE(result.warnings.empty());

    const auto& eras = db.registry<EraDef>();
    REQUIRE(eras.size() == 4);

    struct Expected {
        const char* id;
        std::uint32_t seconds_per_year;
        std::int32_t start_year;
        std::int32_t order;
        const char* next;
    };
    // Architecture §6: Ancient 120, Colonial 300, Modern 720, Utopian 900 real seconds per game year.
    for (const Expected& want : {Expected{"base:era.ancient", 120, -3000, 1, "base:era.colonial"},
                                 Expected{"base:era.colonial", 300, 1600, 2, "base:era.modern"},
                                 Expected{"base:era.modern", 720, 1900, 3, "base:era.utopian"},
                                 Expected{"base:era.utopian", 900, 2025, 4, nullptr}}) {
        const auto handle = eras.find(want.id);
        REQUIRE(handle.has_value());
        const EraDef& era = eras.get(*handle);
        REQUIRE(era.real_seconds_per_year == want.seconds_per_year);
        REQUIRE(era.default_start_year == want.start_year);
        REQUIRE(era.order == want.order);
        if (want.next == nullptr) {
            REQUIRE_FALSE(era.next_era.has_value());
        } else {
            REQUIRE(era.next_era.resolved());
            REQUIRE(eras.id(era.next_era.handle).str() == want.next);
        }
        // Keys follow the era.<name>.name / .desc convention.
        REQUIRE(era.name_key.rfind("era.", 0) == 0);
        REQUIRE(era.desc_key.rfind("era.", 0) == 0);
    }
}

TEST_CASE("the base game's time defines equal the built-in defaults", "[content][base]") {
    // The defaults in code are only a fallback; the data file is the source of truth. They must agree, or a
    // missing file would silently change the pace of the game.
    ContentDatabase db;
    REQUIRE(db.load({std::filesystem::path(CITYSIM_GAME_CONTENT_DIR) / "base"}).ok());
    const citysim::time::TimeDefines defaults;
    const citysim::time::TimeDefines& loaded = db.time_defines();
    REQUIRE(loaded.base_tick_rate == defaults.base_tick_rate);
    REQUIRE(loaded.speed_multipliers == defaults.speed_multipliers);
    REQUIRE(loaded.movement_scale == defaults.movement_scale);
    REQUIRE(loaded.day_length_ticks == defaults.day_length_ticks);
}
