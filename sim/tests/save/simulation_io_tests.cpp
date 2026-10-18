#include "citysim/core/commands.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/save/csav_file.hpp"
#include "citysim/save/save_format_error.hpp"
#include "citysim/save/simulation_io.hpp"
#include "citysim/time/game_clock.hpp"
#include "support/calendar_game.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace citysim;
using citysim::test::CalendarGame;

namespace fs = std::filesystem;

namespace {

save::SaveOptions options_for(const CalendarGame& game) {
    save::SaveOptions options;
    options.content = game.content.get();
    options.saved_at_unix_seconds = 1'700'000'000;
    return options;
}

save::LoadOptions load_options_for(const CalendarGame& game) {
    save::LoadOptions options;
    options.content = game.content.get();
    return options;
}

bool has_warning(const save::LoadOutcome& outcome, const std::string& needle) {
    return std::any_of(outcome.warnings.begin(), outcome.warnings.end(),
                       [&](const std::string& warning) { return warning.find(needle) != std::string::npos; });
}

// Copies the calendar fixture content to a temp folder with a changed number, so the content hash differs
// while every id stays the same.
class ChangedContent {
  public:
    ChangedContent() : root_(fs::temp_directory_path() / "citysim_changed_content") {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
        fs::create_directories(root_);
        fs::copy(fs::path(CITYSIM_TEST_FIXTURES_DIR) / "content" / "calendar", root_ / "calendar",
                 fs::copy_options::recursive);
        const fs::path file = root_ / "calendar" / "defs" / "eras.json";
        std::ifstream in(file, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        const std::size_t at = text.find("\"real_seconds_per_year\": 1,");
        REQUIRE(at != std::string::npos);
        text.replace(at, 27, "\"real_seconds_per_year\": 2,");
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << text;
        out.close();
        REQUIRE(database.load({root_ / "calendar"}).ok());
    }
    ~ChangedContent() {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }
    content::ContentDatabase database;

  private:
    fs::path root_;
};

} // namespace

TEST_CASE("a saved simulation loads with the same tick, seed and state", "[save][simulation]") {
    CalendarGame original("test:era.ancient");
    original.step(250);

    const auto bytes = save::save_simulation_bytes(original.sim, options_for(original));

    CalendarGame loaded("test:era.ancient");
    const save::LoadOutcome outcome =
        save::load_simulation_bytes(loaded.sim, bytes, load_options_for(loaded));
    CHECK(outcome.warnings.empty());
    CHECK(loaded.sim.tick() == 250);
    CHECK(loaded.sim.world_seed() == original.sim.world_seed());
    CHECK(loaded.sim.world().state_hash() == original.sim.world().state_hash());

    CHECK(outcome.header.tick == 250);
    CHECK(outcome.header.saved_at_unix_seconds == 1'700'000'000);
    CHECK(outcome.header.content_hash == original.content->content_hash());
    REQUIRE(outcome.header.mods.size() == 1);
    CHECK(outcome.header.mods[0].id == "calendar");
}

TEST_CASE("run N ticks equals run k, save, load, run N-k, tick by tick", "[save][simulation]") {
    constexpr std::uint64_t k_total = 600;
    constexpr std::uint64_t k_split = 250;
    const auto script = [](CalendarGame& game, std::uint64_t tick) {
        // A scripted command set, submitted before the tick it applies to.
        if (tick % 70 == 5) {
            game.sim.enqueue({core::SequencedCommand{
                tick + 1, tick, time::SetSpeed{static_cast<std::uint8_t>(1 + tick % 3)}}});
        }
        if (tick % 150 == 100) {
            game.sim.enqueue({core::SequencedCommand{tick + 1, tick, time::TogglePause{}}});
        }
        if (tick == 300) {
            game.sim.enqueue({core::SequencedCommand{tick + 1, tick, time::SetEra{"test:era.modern"}}});
        }
    };

    CalendarGame straight("test:era.ancient");
    std::vector<std::uint64_t> expected;
    for (std::uint64_t tick = 0; tick < k_total; ++tick) {
        script(straight, tick);
        straight.step();
        expected.push_back(straight.sim.world().state_hash());
    }

    CalendarGame first("test:era.ancient");
    for (std::uint64_t tick = 0; tick < k_split; ++tick) {
        script(first, tick);
        first.step();
    }
    // A command submitted for the next tick but not yet applied must survive the save too.
    script(first, k_split);
    const auto bytes = save::save_simulation_bytes(first.sim, options_for(first));

    CalendarGame resumed("test:era.ancient");
    save::load_simulation_bytes(resumed.sim, bytes, load_options_for(resumed));
    CHECK(resumed.sim.pending_commands().size() == first.sim.pending_commands().size());
    for (std::uint64_t tick = k_split; tick < k_total; ++tick) {
        if (tick != k_split) {
            script(resumed, tick); // the command for k_split came with the save
        }
        resumed.step();
        INFO("tick " << tick + 1);
        REQUIRE(resumed.sim.world().state_hash() == expected[tick]);
    }
    CHECK(resumed.sim.tick() == k_total);
    // The input log carried over, so the resumed run recorded the same inputs as the straight one.
    CHECK(resumed.sim.input_log().entries().size() == straight.sim.input_log().entries().size());
}

TEST_CASE("the input log is optional", "[save][simulation]") {
    CalendarGame game("test:era.ancient");
    game.sim.enqueue({core::SequencedCommand{1, 0, time::TogglePause{}}});
    game.step(3);
    REQUIRE(game.sim.input_log().entries().size() == 1);

    save::SaveOptions options = options_for(game);
    options.include_input_log = false;
    const auto bytes = save::save_simulation_bytes(game.sim, options);

    CalendarGame loaded("test:era.ancient");
    save::load_simulation_bytes(loaded.sim, bytes, load_options_for(loaded));
    CHECK(loaded.sim.input_log().entries().empty());
    CHECK(loaded.sim.world().state_hash() == game.sim.world().state_hash());
}

TEST_CASE("different content is a warning, not an error", "[save][simulation]") {
    CalendarGame original("test:era.ancient");
    original.step(10);
    const auto bytes = save::save_simulation_bytes(original.sim, options_for(original));

    ChangedContent changed;
    CalendarGame loaded("test:era.ancient");
    save::LoadOptions options;
    options.content = &changed.database;
    const save::LoadOutcome outcome = save::load_simulation_bytes(loaded.sim, bytes, options);
    CHECK(has_warning(outcome, "content changed"));
    CHECK(loaded.sim.tick() == 10);
}

TEST_CASE("a different mod list is a warning naming both", "[save][simulation]") {
    CalendarGame original("test:era.ancient");
    const auto bytes = save::save_simulation_bytes(original.sim, options_for(original));

    // The same content plus one more mod.
    const fs::path extra = fs::temp_directory_path() / "citysim_extra_mod";
    std::error_code ignored;
    fs::remove_all(extra, ignored);
    fs::create_directories(extra / "defs");
    std::ofstream(extra / "defs" / "e.json")
        << R"({"defs":[{"type":"era","id":"extra:era.x","name_key":"k","desc_key":"k",)"
        << R"("real_seconds_per_year":100,"default_start_year":0,"order":99}]})";
    content::ContentDatabase other;
    REQUIRE(other.load({fs::path(CITYSIM_TEST_FIXTURES_DIR) / "content" / "calendar", extra}).ok());
    CalendarGame loaded("test:era.ancient");
    save::LoadOptions options;
    options.content = &other;
    const save::LoadOutcome outcome = save::load_simulation_bytes(loaded.sim, bytes, options);
    CHECK(has_warning(outcome, "different mods"));
    fs::remove_all(extra, ignored);
}

TEST_CASE("unknown sections are skipped with a warning", "[save][simulation]") {
    CalendarGame original("test:era.ancient");
    original.step(5);
    save::CsavContents contents =
        save::read_csav(save::save_simulation_bytes(original.sim, options_for(original)));
    contents.sections.push_back({"XTRA", 9, {std::byte{1}, std::byte{2}}});

    CalendarGame loaded("test:era.ancient");
    const save::LoadOutcome outcome =
        save::load_simulation_bytes(loaded.sim, save::write_csav(contents), load_options_for(loaded));
    CHECK(has_warning(outcome, "unknown section XTRA"));
    CHECK(loaded.sim.world().state_hash() == original.sim.world().state_hash());
}

TEST_CASE("missing or inconsistent sections are format errors", "[save][simulation]") {
    CalendarGame original("test:era.ancient");
    original.step(5);
    const save::CsavContents good =
        save::read_csav(save::save_simulation_bytes(original.sim, options_for(original)));

    SECTION("no world section") {
        save::CsavContents contents = good;
        std::erase_if(contents.sections, [](const save::RawSection& s) { return s.tag == "WRLD"; });
        CalendarGame loaded("test:era.ancient");
        CHECK_THROWS_AS(
            save::load_simulation_bytes(loaded.sim, save::write_csav(contents), load_options_for(loaded)),
            save::SaveFormatError);
    }
    SECTION("the header and META disagree") {
        save::CsavContents contents = good;
        contents.header.tick += 1;
        CalendarGame loaded("test:era.ancient");
        CHECK_THROWS_AS(
            save::load_simulation_bytes(loaded.sim, save::write_csav(contents), load_options_for(loaded)),
            save::SaveFormatError);
    }
    SECTION("a section from a newer build") {
        save::CsavContents contents = good;
        contents.sections[0].version = 9;
        CalendarGame loaded("test:era.ancient");
        CHECK_THROWS_AS(
            save::load_simulation_bytes(loaded.sim, save::write_csav(contents), load_options_for(loaded)),
            save::SaveFormatError);
    }
    SECTION("the same section twice") {
        save::CsavContents contents = good;
        contents.sections.push_back(contents.sections[0]);
        CalendarGame loaded("test:era.ancient");
        CHECK_THROWS_AS(
            save::load_simulation_bytes(loaded.sim, save::write_csav(contents), load_options_for(loaded)),
            save::SaveFormatError);
    }
}

TEST_CASE("a save can only be loaded into a simulation that has not run", "[save][simulation]") {
    CalendarGame original("test:era.ancient");
    const auto bytes = save::save_simulation_bytes(original.sim, options_for(original));
    CalendarGame busy("test:era.ancient");
    busy.step();
    CHECK_THROWS_AS(save::load_simulation_bytes(busy.sim, bytes, load_options_for(busy)), std::logic_error);
}

TEST_CASE("files are written atomically and read back", "[save][simulation]") {
    const fs::path path = fs::temp_directory_path() / "citysim_test_save.csav";
    std::error_code ignored;
    fs::remove(path, ignored);

    CalendarGame original("test:era.ancient");
    original.step(40);
    save::save_simulation(original.sim, path, options_for(original));
    CHECK(fs::exists(path));
    CHECK_FALSE(fs::exists(fs::path(path).concat(".tmp")));

    CHECK(save::read_save_header(path).tick == 40);

    CalendarGame loaded("test:era.ancient");
    save::load_simulation(loaded.sim, path, load_options_for(loaded));
    CHECK(loaded.sim.world().state_hash() == original.sim.world().state_hash());

    // Saving again over the same path replaces it.
    original.step(10);
    save::save_simulation(original.sim, path, options_for(original));
    CHECK(save::read_save_header(path).tick == 50);

    CHECK_THROWS_AS(save::read_save_header(fs::temp_directory_path() / "citysim_no_such_save.csav"),
                    std::runtime_error);
    fs::remove(path, ignored);
}
