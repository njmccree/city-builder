#include "citysim/content/json_merge_patch.hpp"
#include "citysim/content/mod_discovery.hpp"
#include "citysim/content/mod_load_order.hpp"
#include "citysim/content/mod_manifest.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>

using namespace citysim::content;
using nlohmann::json;

namespace {

std::filesystem::path mods_fixture(const std::string& name) {
    return std::filesystem::path(CITYSIM_TEST_FIXTURES_DIR) / "mods" / name;
}

std::vector<std::string> ids_of(const std::vector<ModInfo>& mods) {
    std::vector<std::string> ids;
    for (const ModInfo& mod : mods) {
        ids.push_back(mod.manifest.id);
    }
    return ids;
}

const ContentIssue* find_issue(const std::vector<ContentIssue>& issues, const std::string& needle) {
    for (const ContentIssue& issue : issues) {
        if (issue.format().find(needle) != std::string::npos) {
            return &issue;
        }
    }
    return nullptr;
}

// Discover the mods in a fixture root and order them.
LoadOrderResult order_fixture(const std::string& name, const LoadOrderOptions& options = {}) {
    const DiscoveryResult found = discover_mods({mods_fixture(name)});
    REQUIRE(found.issues.empty());
    return resolve_load_order(found.mods, options);
}

} // namespace

// ---- JSON Merge Patch (RFC 7386)
// ---------------------------------------------------------------------------

TEST_CASE("merge patch passes the RFC 7386 appendix examples", "[content][mods][patch]") {
    struct Case {
        const char* original;
        const char* patch;
        const char* result;
    };
    // RFC 7386, Appendix A.
    const std::vector<Case> cases{
        {R"({"a":"b"})", R"({"a":"c"})", R"({"a":"c"})"},
        {R"({"a":"b"})", R"({"b":"c"})", R"({"a":"b","b":"c"})"},
        {R"({"a":"b"})", R"({"a":null})", R"({})"},
        {R"({"a":"b","b":"c"})", R"({"a":null})", R"({"b":"c"})"},
        {R"({"a":["b"]})", R"({"a":"c"})", R"({"a":"c"})"},
        {R"({"a":"c"})", R"({"a":["b"]})", R"({"a":["b"]})"},
        {R"({"a":{"b":"c"}})", R"({"a":{"b":"d","c":null}})", R"({"a":{"b":"d"}})"},
        {R"({"a":[{"b":"c"}]})", R"({"a":[1]})", R"({"a":[1]})"},
        {R"(["a","b"])", R"(["c","d"])", R"(["c","d"])"},
        {R"({"a":"b"})", R"(["c"])", R"(["c"])"},
        {R"({"a":"foo"})", "null", "null"},
        {R"({"a":"foo"})", R"("bar")", R"("bar")"},
        {R"({"e":null})", R"({"a":1})", R"({"e":null,"a":1})"},
        {R"([1,2])", R"({"a":"b","c":null})", R"({"a":"b"})"},
        {R"({})", R"({"a":{"bb":{"ccc":null}}})", R"({"a":{"bb":{}}})"},
    };
    for (const Case& c : cases) {
        INFO(c.original << " + " << c.patch);
        REQUIRE(merge_patch(json::parse(c.original), json::parse(c.patch)) == json::parse(c.result));
    }
}

TEST_CASE("merge patch does not modify its inputs", "[content][mods][patch]") {
    const json target = json::parse(R"({"a":{"b":1},"c":2})");
    const json patch = json::parse(R"({"a":{"b":null},"d":3})");
    const json result = merge_patch(target, patch);
    REQUIRE(target == json::parse(R"({"a":{"b":1},"c":2})"));
    REQUIRE(patch == json::parse(R"({"a":{"b":null},"d":3})"));
    REQUIRE(result == json::parse(R"({"a":{},"c":2,"d":3})"));
}

// ---- manifests
// ----------------------------------------------------------------------------------------------

TEST_CASE("mod ids and semantic versions are validated", "[content][mods][manifest]") {
    for (const char* id : {"base", "my_mod", "a.b", "mod2", "x"}) {
        REQUIRE(is_valid_mod_id(id));
    }
    for (const char* id : {"", "Base", "my mod", "a:b", "mod-1",
                           "m\xc3\xb6"
                           "d"}) {
        REQUIRE_FALSE(is_valid_mod_id(id));
    }
    for (const char* version : {"0.0.1", "1.2.3", "10.20.30", "1.0.0-beta", "1.0.0-beta.1", "1.0.0+build.5",
                                "2.0.0-rc.1+build.7"}) {
        INFO(version);
        REQUIRE(is_valid_semver(version));
    }
    for (const char* version :
         {"", "1", "1.0", "1.0.x", "v1.0.0", "1.0.0.0", "1..0", "1.0.0-", "1.0.0+", "a.b.c"}) {
        INFO(version);
        REQUIRE_FALSE(is_valid_semver(version));
    }
}

TEST_CASE("a full manifest is read", "[content][mods][manifest]") {
    std::vector<ContentIssue> issues;
    ParseContext context(issues);
    const auto manifest = parse_mod_manifest(
        json::parse(R"({"id":"coolmod","name_key":"mod.cool","version":"1.2.3-beta","depends_on":["base"],
                        "load_after":["other"],"enabled_by_default":false})"),
        context);
    REQUIRE(manifest.has_value());
    REQUIRE(issues.empty());
    REQUIRE(manifest->id == "coolmod");
    REQUIRE(manifest->name_key == "mod.cool");
    REQUIRE(manifest->version == "1.2.3-beta");
    REQUIRE(manifest->depends_on == std::vector<std::string>{"base"});
    REQUIRE(manifest->load_after == std::vector<std::string>{"other"});
    REQUIRE_FALSE(manifest->enabled_by_default);
}

TEST_CASE("optional manifest fields have defaults", "[content][mods][manifest]") {
    std::vector<ContentIssue> issues;
    ParseContext context(issues);
    const auto manifest =
        parse_mod_manifest(json::parse(R"({"id":"m","name_key":"k","version":"0.1.0"})"), context);
    REQUIRE(manifest.has_value());
    REQUIRE(manifest->depends_on.empty());
    REQUIRE(manifest->load_after.empty());
    REQUIRE(manifest->enabled_by_default);
}

TEST_CASE("broken manifests are reported with a pointer and the folder is skipped",
          "[content][mods][manifest]") {
    const DiscoveryResult found = discover_mods({mods_fixture("bad_manifests")});
    REQUIRE(find_issue(found.issues, "mod.json:/version: '1.0' is not a semantic version") != nullptr);
    REQUIRE(find_issue(found.issues, "mod.json:/id: invalid mod id 'Bad Id'") != nullptr);
    REQUIRE(find_issue(found.issues, "mod.json:: invalid JSON") != nullptr);
    REQUIRE(find_issue(found.issues, "/name_key: expected a string") != nullptr);
    REQUIRE(find_issue(found.issues, "/depends_on: expected an array of mod ids") != nullptr);
    REQUIRE(find_issue(found.issues, "/load_after/0: expected a mod id string") != nullptr);
    REQUIRE(find_issue(found.issues, "/load_after/1: invalid mod id 'Bad Id'") != nullptr);
    REQUIRE(find_issue(found.issues, "/enabled_by_default: expected true or false") != nullptr);
    REQUIRE(find_issue(found.issues, "missing required field 'name_key'") != nullptr);
    REQUIRE(find_issue(found.issues, "missing required field 'version'") != nullptr);

    // An unknown key is only a warning, so that mod still loads.
    const ContentIssue* unknown = find_issue(found.issues, "unknown field 'colour'");
    REQUIRE(unknown != nullptr);
    REQUIRE(unknown->severity == Severity::Warning);
    REQUIRE(ids_of(found.mods) == std::vector<std::string>{"uk"});
}

// ---- discovery
// ------------------------------------------------------------------------------------------------

TEST_CASE("discovery finds each subfolder with a mod.json, by folder name", "[content][mods][discovery]") {
    const DiscoveryResult found = discover_mods({mods_fixture("order_ties")});
    REQUIRE(found.issues.empty());
    // Folder names here equal the mod ids, so folder order is id order.
    REQUIRE(ids_of(found.mods) == std::vector<std::string>{"alpha", "base", "beta", "mid", "zeta"});
    REQUIRE(found.mods[0].has_manifest_file);
}

TEST_CASE("a root that is itself a mod is that one mod", "[content][mods][discovery]") {
    const DiscoveryResult found = discover_mods({mods_fixture("order_ties") / "zeta"});
    REQUIRE(ids_of(found.mods) == std::vector<std::string>{"zeta"});
}

TEST_CASE("several roots are searched in the order given", "[content][mods][discovery]") {
    const DiscoveryResult found = discover_mods({mods_fixture("load_after"), mods_fixture("semver")});
    REQUIRE(ids_of(found.mods) == std::vector<std::string>{"a", "b", "pre"});
}

TEST_CASE("a folder with content but no manifest is accepted, named after the folder",
          "[content][mods][discovery]") {
    const DiscoveryResult found = discover_mods({mods_fixture("no_manifest")});
    REQUIRE(found.issues.empty());
    REQUIRE(ids_of(found.mods) == std::vector<std::string>{"plain"});
    REQUIRE_FALSE(found.mods[0].has_manifest_file);
    REQUIRE(found.mods[0].manifest.version == "0.0.0");
}

TEST_CASE("two mods with the same id are an error", "[content][mods][discovery]") {
    const DiscoveryResult found = discover_mods({mods_fixture("duplicate_ids")});
    REQUIRE(found.mods.size() == 1);
    REQUIRE(find_issue(found.issues, "a mod with the id 'same' was already found") != nullptr);
}

TEST_CASE("a root that is not a directory is an error", "[content][mods][discovery]") {
    const DiscoveryResult found = discover_mods({mods_fixture("does_not_exist")});
    REQUIRE(found.mods.empty());
    REQUIRE(find_issue(found.issues, "mod root is not a directory") != nullptr);
}

TEST_CASE("semantic versions with pre-release and build parts load", "[content][mods][discovery]") {
    const DiscoveryResult found = discover_mods({mods_fixture("semver")});
    REQUIRE(found.issues.empty());
    REQUIRE(found.mods[0].manifest.version == "2.0.0-beta.1+build.5");
}

// ---- load order
// ------------------------------------------------------------------------------------------------

TEST_CASE("load order: base first, then dependencies, with ties broken by id", "[content][mods][order]") {
    const LoadOrderResult order = order_fixture("order_ties");
    REQUIRE(order.ok());
    // base is first although "alpha" sorts before it. After base, alpha and zeta are both free: alpha goes
    // first. mid needs zeta; beta wants to come after mid.
    REQUIRE(ids_of(order.ordered) == std::vector<std::string>{"base", "alpha", "zeta", "mid", "beta"});
}

TEST_CASE("load order does not depend on discovery order", "[content][mods][order]") {
    DiscoveryResult found = discover_mods({mods_fixture("order_ties")});
    const auto expected = ids_of(resolve_load_order(found.mods).ordered);
    std::reverse(found.mods.begin(), found.mods.end());
    REQUIRE(ids_of(resolve_load_order(found.mods).ordered) == expected);
    std::rotate(found.mods.begin(), found.mods.begin() + 2, found.mods.end());
    REQUIRE(ids_of(resolve_load_order(found.mods).ordered) == expected);
}

TEST_CASE("load_after is honored when the other mod is enabled and ignored when it is not",
          "[content][mods][order]") {
    const LoadOrderResult order = order_fixture("load_after");
    REQUIRE(order.ok());
    // a wants to load after b, and after "ghost", which does not exist and is ignored.
    REQUIRE(ids_of(order.ordered) == std::vector<std::string>{"b", "a"});
}

TEST_CASE("a dependency cycle is an error that names the cycle", "[content][mods][order]") {
    const LoadOrderResult order = order_fixture("cycle");
    REQUIRE_FALSE(order.ok());
    REQUIRE(order.ordered.empty());
    REQUIRE(order.issues.size() == 1);
    REQUIRE(order.issues[0].message == "dependency cycle: x -> y -> z -> x (each mod loads after the next)");
    REQUIRE(order.issues[0].file == "mod.json");
}

TEST_CASE("a missing dependency is an error naming both mods", "[content][mods][order]") {
    const LoadOrderResult order = order_fixture("missing_dep");
    REQUIRE_FALSE(order.ok());
    REQUIRE(order.issues.size() == 1);
    REQUIRE(order.issues[0].message == "mod 'a' depends on 'ghost', which was not found");
    REQUIRE(order.issues[0].pointer == "/depends_on/0");
}

TEST_CASE("a dependency that is installed but not enabled says so", "[content][mods][order]") {
    // b is disabled by default, so a (which needs it) cannot load.
    const LoadOrderResult order = order_fixture("not_enabled");
    REQUIRE_FALSE(order.ok());
    REQUIRE(order.issues[0].message == "mod 'a' depends on 'b', which is installed but not enabled");

    // Enabling b as well fixes it, and the dependency orders them.
    LoadOrderOptions both;
    both.enabled = std::vector<std::string>{"a", "b"};
    REQUIRE(ids_of(order_fixture("not_enabled", both).ordered) == std::vector<std::string>{"b", "a"});

    // With only b enabled it loads alone.
    LoadOrderOptions just_b;
    just_b.enabled = std::vector<std::string>{"b"};
    REQUIRE(ids_of(order_fixture("not_enabled", just_b).ordered) == std::vector<std::string>{"b"});
}

TEST_CASE("the enabled list selects mods, and unknown ids are errors", "[content][mods][order]") {
    LoadOrderOptions options;
    options.enabled = std::vector<std::string>{"zeta", "alpha"};
    const LoadOrderResult order = order_fixture("order_ties", options);
    REQUIRE(ids_of(order.ordered) == std::vector<std::string>{"alpha", "zeta"});

    options.enabled = std::vector<std::string>{"alpha", "nonexistent"};
    const LoadOrderResult bad = order_fixture("order_ties", options);
    REQUIRE_FALSE(bad.ok());
    REQUIRE(find_issue(bad.issues, "enabled mod 'nonexistent' was not found") != nullptr);
}

TEST_CASE("base always loads first, so base cannot depend on another mod", "[content][mods][order]") {
    const LoadOrderResult order = order_fixture("base_conflict");
    REQUIRE_FALSE(order.ok());
    REQUIRE(find_issue(order.issues, "dependency cycle:") != nullptr);
    REQUIRE(find_issue(order.issues, "the base mod always loads first") != nullptr);
}

TEST_CASE("require_base makes a missing base an error", "[content][mods][order]") {
    LoadOrderOptions options;
    options.require_base = true;
    const LoadOrderResult order = order_fixture("load_after", options); // no base here
    REQUIRE_FALSE(order.ok());
    REQUIRE(find_issue(order.issues, "the base game (mod 'base') is required") != nullptr);

    REQUIRE(order_fixture("order_ties", options).ok());
}
