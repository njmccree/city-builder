#include "citysim/content/content_database.hpp"
#include "citysim/content/date_text.hpp"
#include "citysim/content/localizer.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>

using namespace citysim::content;

namespace {

LoadResult load_loc_fixture(ContentDatabase& database, const std::string& name) {
    ModLoadRequest request;
    request.roots = {std::filesystem::path(CITYSIM_TEST_FIXTURES_DIR) / "loc" / name};
    return database.load_mods(request);
}

const ContentIssue* find_message(const std::vector<ContentIssue>& issues, const std::string& needle) {
    const auto it = std::find_if(issues.begin(), issues.end(), [&](const ContentIssue& issue) {
        return issue.message.find(needle) != std::string::npos;
    });
    return it == issues.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("lookup falls back from the current language to en to the key", "[content][loc]") {
    Localizer localizer;
    localizer.set("en", "hello", "Hello");
    localizer.set("en", "only.en", "English only");
    localizer.set("de", "hello", "Hallo");

    CHECK(localizer.get("hello") == "Hello"); // default language is en
    localizer.set_language("de");
    CHECK(localizer.get("hello") == "Hallo");
    CHECK(localizer.get("only.en") == "English only");
    CHECK(localizer.missing_key_count() == 0);

    CHECK(localizer.get("nowhere") == "nowhere"); // the key itself, visibly
    CHECK(localizer.missing_key_count() == 1);

    localizer.set_language("fr"); // a language with no strings falls back entirely
    CHECK(localizer.get("hello") == "Hello");
}

TEST_CASE("text() reads one language exactly, without fallback or counting", "[content][loc]") {
    Localizer localizer;
    localizer.set("en", "k", "English");
    localizer.set("de", "other", "Anderes");
    CHECK(localizer.text("en", "k") == "English");
    CHECK(localizer.text("de", "k").empty()); // no fallback to en
    CHECK(localizer.text("fr", "k").empty());
    CHECK(localizer.missing_key_count() == 0);
}

TEST_CASE("a later set replaces the text", "[content][loc]") {
    Localizer localizer;
    localizer.set("en", "k", "first");
    localizer.set("en", "k", "second");
    CHECK(localizer.get("k") == "second");
}

TEST_CASE("placeholders are substituted by name", "[content][loc]") {
    Localizer localizer;
    localizer.set("en", "greet", "{greeting}, {name}! {name} again.");
    CHECK(localizer.format("greet", {{"greeting", "Hi"}, {"name", "Ada"}}) == "Hi, Ada! Ada again.");

    // A placeholder with no argument stays visible; extra arguments are ignored.
    CHECK(localizer.format("greet", {{"greeting", "Hi"}, {"unused", "x"}}) == "Hi, {name}! {name} again.");

    CHECK(substitute_placeholders("{{literal}} {a}", {{"a", "1"}}) == "{literal} 1");
    CHECK(substitute_placeholders("broken {open", {{"open", "x"}}) == "broken {open");
    CHECK(substitute_placeholders("", {}).empty());

    // A missing key formats to the key, with no placeholder handling surprises.
    CHECK(localizer.format("nope", {{"a", "b"}}) == "nope");
}

TEST_CASE("languages and keys are listed in sorted order", "[content][loc]") {
    Localizer localizer;
    localizer.set("en", "b", "B");
    localizer.set("en", "a", "A");
    localizer.set("de", "a", "A");
    CHECK(localizer.languages() == std::vector<std::string>{"de", "en"});
    CHECK(localizer.keys("en") == std::vector<std::string>{"a", "b"});
    CHECK(localizer.keys("fr").empty());
    CHECK(localizer.has("de", "a"));
    CHECK_FALSE(localizer.has("de", "b"));
}

TEST_CASE("dates render through the localizer", "[content][loc][calendar]") {
    Localizer localizer;
    localizer.set("en", "calendar.month.3", "March");
    localizer.set("en", "calendar.date.bce", "{day} {month} {year} BCE");
    localizer.set("en", "calendar.date.ce", "{day} {month} {year} CE");
    localizer.set("de", "calendar.month.3", "Maerz");

    CHECK(format_date_text(localizer, citysim::time::Date{-2999, 3, 15}) == "15 March 3000 BCE");
    CHECK(format_date_text(localizer, citysim::time::Date{1600, 3, 1}) == "1 March 1600 CE");
    localizer.set_language("de"); // month name in German, template falls back to English
    CHECK(format_date_text(localizer, citysim::time::Date{1600, 3, 1}) == "1 Maerz 1600 CE");
}

TEST_CASE("the base game has text for every month and both date forms", "[content][loc][calendar]") {
    ContentDatabase database;
    ModLoadRequest request;
    request.roots = {std::filesystem::path(CITYSIM_GAME_CONTENT_DIR)};
    REQUIRE(database.load_mods(request).errors.empty());
    for (int month = 1; month <= 12; ++month) {
        CHECK(database.localizer().has("en", "calendar.month." + std::to_string(month)));
    }
    CHECK(format_date_text(database.localizer(), citysim::time::Date{0, 12, 31}) == "31 December 1 BCE");
    CHECK(database.localizer().missing_key_count() == 0);
}

TEST_CASE("content loading fills the localizer and later mods override keys", "[content][loc]") {
    ContentDatabase database;
    const LoadResult result = load_loc_fixture(database, "override");
    REQUIRE(result.errors.empty());
    CHECK(database.localizer().get("era.a.name") == "Alpha Prime");          // replaced by mod "over"
    CHECK(database.localizer().get("era.a.desc") == "Alpha era for {who}."); // untouched
}

TEST_CASE("every key field must exist in en", "[content][loc][validate]") {
    ContentDatabase database;
    const LoadResult result = load_loc_fixture(database, "missing_en");
    REQUIRE_FALSE(result.ok());
    const ContentIssue* issue = find_message(result.errors, "'era.a.desc' has no text in 'en'");
    REQUIRE(issue != nullptr);
    CHECK(issue->file == "defs/e.json");
    CHECK(issue->pointer == "/defs/0/desc_key");
    CHECK(result.errors.size() == 1); // having it in German does not hide it
}

TEST_CASE("keys missing in another language are warnings", "[content][loc][validate]") {
    ContentDatabase database;
    const LoadResult result = load_loc_fixture(database, "other_language");
    REQUIRE(result.ok());
    REQUIRE(result.warnings.size() == 1);
    CHECK(result.warnings[0].message == "localization key 'era.a.desc' has no text in 'de'");
}

TEST_CASE("unused keys are info, not warnings", "[content][loc][validate]") {
    ContentDatabase database;
    const LoadResult result = load_loc_fixture(database, "ok");
    REQUIRE(result.ok());
    CHECK(result.warnings.empty());
    REQUIRE(result.infos.size() == 1);
    CHECK(result.infos[0].message.find("'unused.key'") != std::string::npos);
    CHECK(result.infos[0].file == "loc/en/a.json");
}

TEST_CASE("loc text must be a string", "[content][loc][validate]") {
    ContentDatabase database;
    const LoadResult result = load_loc_fixture(database, "bad_text");
    REQUIRE_FALSE(result.ok());
    const ContentIssue* issue = find_message(result.errors, "must be a string");
    REQUIRE(issue != nullptr);
    CHECK(issue->file == "loc/en/a.json");
    CHECK(issue->pointer == "/era.a.desc");
}
