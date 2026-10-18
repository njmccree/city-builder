#include "citysim/content/content_database.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace citysim::content;

namespace {

std::filesystem::path layering_fixture(const std::string& name) {
    return std::filesystem::path(CITYSIM_TEST_FIXTURES_DIR) / "layering" / name;
}

bool has_message(const std::vector<ContentIssue>& issues, const std::string& needle) {
    return std::any_of(issues.begin(), issues.end(), [&](const ContentIssue& issue) {
        return issue.message.find(needle) != std::string::npos;
    });
}

LoadResult load_fixture(ContentDatabase& database, const std::string& name,
                        const LoadOrderOptions& order = {}) {
    ModLoadRequest request;
    request.roots = {layering_fixture(name)};
    request.order = order;
    request.check_localization = false; // these fixtures use placeholder keys
    return database.load_mods(request);
}

const EraDef& era(const ContentDatabase& database, const char* id) {
    const auto& registry = database.registry<EraDef>();
    const auto handle = registry.find(std::string_view(id));
    REQUIRE(handle.has_value());
    return registry.get(*handle);
}

} // namespace

TEST_CASE("a later mod replaces, patches and adds definitions", "[content][layering]") {
    ContentDatabase database;
    const LoadResult result = load_fixture(database, "stack");
    REQUIRE(result.errors.empty());

    REQUIRE(database.mods().size() == 3);
    CHECK(database.mods()[0].id == "base");
    CHECK(database.mods()[1].id == "patcher");
    CHECK(database.mods()[2].id == "later");

    // Patched twice: the later patch wins where both set a field, and null removes an optional field.
    const EraDef& ancient = era(database, "base:era.ancient");
    CHECK(ancient.real_seconds_per_year == 175);
    CHECK(ancient.desc_key == "patcher.ancient.desc");
    CHECK(ancient.name_key == "era.x.name"); // untouched fields stay
    CHECK(ancient.default_start_year == -3000);
    CHECK_FALSE(ancient.next_era.has_value());

    // Replaced wholesale: nothing of the old definition remains.
    const EraDef& colonial = era(database, "base:era.colonial");
    CHECK(colonial.real_seconds_per_year == 360);
    CHECK(colonial.default_start_year == 1700);
    CHECK(colonial.name_key == "patcher.colonial.name");

    // Added by a mod.
    CHECK(era(database, "patcher:era.extra").order == 9);
    CHECK(database.registry<EraDef>().size() == 4);
}

TEST_CASE("origin lists every mod that touched a definition", "[content][layering]") {
    ContentDatabase database;
    REQUIRE(load_fixture(database, "stack").errors.empty());
    const auto& registry = database.registry<EraDef>();

    const DefSource& ancient = database.origin(*registry.find(std::string_view("base:era.ancient")));
    CHECK(ancient.mod_id == "later");
    CHECK(ancient.touched_by == std::vector<std::string>{"base", "patcher", "later"});

    const DefSource& colonial = database.origin(*registry.find(std::string_view("base:era.colonial")));
    CHECK(colonial.mod_id == "patcher");
    CHECK(colonial.touched_by == std::vector<std::string>{"base", "patcher"});

    const DefSource& modern = database.origin(*registry.find(std::string_view("base:era.modern")));
    CHECK(modern.touched_by == std::vector<std::string>{"base"});
}

TEST_CASE("time defines are layered field by field", "[content][layering]") {
    ContentDatabase database;
    REQUIRE(load_fixture(database, "stack").errors.empty());
    CHECK(database.time_defines().day_length_ticks == 7200);
    CHECK(database.time_defines().base_tick_rate == 20);
}

TEST_CASE("a patch of a definition that does not exist is an error", "[content][layering]") {
    ContentDatabase database;
    const LoadResult result = load_fixture(database, "patch_missing");
    REQUIRE_FALSE(result.ok());
    CHECK(has_message(result.errors, "cannot patch 'base:era.nonexistent'"));
}

TEST_CASE("the same id twice in one mod is an error", "[content][layering]") {
    ContentDatabase database;
    const LoadResult result = load_fixture(database, "same_mod_duplicate");
    REQUIRE_FALSE(result.ok());
    CHECK(has_message(result.errors, "duplicate id 'p:era.one'"));
}

TEST_CASE("a patch that breaks the definition is reported where the patch is", "[content][layering]") {
    ContentDatabase database;
    const LoadResult result = load_fixture(database, "patch_breaks_def");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.errors.size() == 1);
    // The error points at the patch's own field, in the patching mod's file.
    CHECK(result.errors[0].file.find("x.json") != std::string::npos);
    CHECK(result.errors[0].pointer == "/defs/0/real_seconds_per_year");
}

TEST_CASE("patch must be a boolean", "[content][layering]") {
    ContentDatabase database;
    const LoadResult result = load_fixture(database, "bad_patch_flag");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].pointer == "/defs/0/patch");
}

TEST_CASE("a replacement is validated as a whole definition", "[content][layering]") {
    ContentDatabase database;
    const LoadResult result = load_fixture(database, "replacement_invalid");
    REQUIRE_FALSE(result.ok());
    CHECK(has_message(result.errors, "real_seconds_per_year"));
}

TEST_CASE("a patch cannot change the type of a definition", "[content][layering]") {
    ContentDatabase database;
    const LoadResult result = load_fixture(database, "patch_wrong_type");
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("mods that are off by default load only when enabled", "[content][layering]") {
    {
        ContentDatabase database;
        REQUIRE(load_fixture(database, "with_optional").errors.empty());
        CHECK(database.mods().size() == 1);
        CHECK(era(database, "base:era.ancient").order == 1);
    }
    {
        ContentDatabase database;
        LoadOrderOptions options;
        options.enabled = std::vector<std::string>{"base", "opt"};
        REQUIRE(load_fixture(database, "with_optional", options).errors.empty());
        CHECK(database.mods().size() == 2);
        CHECK(era(database, "base:era.ancient").order == 7);
    }
    {
        ContentDatabase database;
        LoadOrderOptions options;
        options.enable_all = true;
        REQUIRE(load_fixture(database, "with_optional", options).errors.empty());
        CHECK(database.mods().size() == 2);
    }
}

TEST_CASE("the content hash tracks what was loaded", "[content][layering][hash]") {
    const auto hash_of = [](const std::string& name, const LoadOrderOptions& options = {}) {
        ContentDatabase database;
        REQUIRE(load_fixture(database, name, options).errors.empty());
        return database.content_hash();
    };

    const std::uint64_t first = hash_of("stack");
    CHECK(first != 0);
    CHECK(hash_of("stack") == first); // stable across loads

    // Enabling another mod changes it, as does a different set of files.
    LoadOrderOptions with_opt;
    with_opt.enable_all = true;
    CHECK(hash_of("with_optional") != hash_of("with_optional", with_opt));
    CHECK(hash_of("stack") != hash_of("with_optional"));
}

TEST_CASE("a failed load still leaves the database consistent", "[content][layering]") {
    ContentDatabase database;
    const LoadResult result = load_fixture(database, "patch_missing");
    REQUIRE_FALSE(result.ok());
    CHECK_THROWS_AS(load_fixture(database, "stack"), std::logic_error); // loads once
}

TEST_CASE("the content hash changes when a file changes, but not for line endings", "[content][hash]") {
    namespace fs = std::filesystem;
    const fs::path work = fs::temp_directory_path() / "citysim_hash_test";
    std::error_code ignored;
    fs::remove_all(work, ignored);
    fs::create_directories(work);
    fs::copy(layering_fixture("with_optional") / "base", work / "base", fs::copy_options::recursive);

    const auto hash_now = [&]() {
        ContentDatabase database;
        ModLoadRequest request;
        request.roots = {work};
        request.check_localization = false;
        REQUIRE(database.load_mods(request).errors.empty());
        return database.content_hash();
    };
    const auto rewrite = [&](const std::function<std::string(std::string)>& edit) {
        const fs::path file = work / "base" / "defs" / "eras.json";
        std::ifstream in(file, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << edit(std::move(text));
    };

    const std::uint64_t original = hash_now();

    rewrite([](std::string text) { // LF -> CRLF
        std::string result;
        for (const char c : text) {
            if (c == '\n') {
                result += '\r';
            }
            result += c;
        }
        return result;
    });
    CHECK(hash_now() == original);

    rewrite([](std::string text) { // change a value
        const std::size_t at = text.find("120");
        text.replace(at, 3, "121");
        return text;
    });
    CHECK(hash_now() != original);

    fs::remove_all(work, ignored);
}

TEST_CASE("autosave settings come from the time defines and are validated", "[content][time]") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "citysim_autosave_defines";
    std::error_code ignored;
    fs::remove_all(root, ignored);
    fs::create_directories(root / "defines");
    const auto write = [&](const std::string& text) {
        std::ofstream(root / "defines" / "time.json", std::ios::trunc) << text;
    };

    write(R"({"autosave_interval_real_seconds": 30, "autosave_slots": 5})");
    {
        ContentDatabase database;
        REQUIRE(database.load({root}).ok());
        CHECK(database.time_defines().autosave_interval_real_seconds == 30);
        CHECK(database.time_defines().autosave_slots == 5);
    }
    write(R"({})");
    {
        ContentDatabase database;
        REQUIRE(database.load({root}).ok());
        CHECK(database.time_defines().autosave_interval_real_seconds == 600); // the built-in defaults
        CHECK(database.time_defines().autosave_slots == 3);
    }
    write(R"({"autosave_slots": 0})");
    {
        ContentDatabase database;
        const LoadResult result = database.load({root});
        REQUIRE_FALSE(result.ok());
        CHECK(has_message(result.errors, "between 1 and 99"));
    }
    fs::remove_all(root, ignored);
}
