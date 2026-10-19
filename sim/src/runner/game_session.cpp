#include "citysim/runner/game_session.hpp"

#include "citysim/content/era_def.hpp"
#include "citysim/debug/agent_snapshot.hpp"
#include "citysim/debug/walking_skeleton.hpp"
#include "citysim/time/calendar_module.hpp"
#include "citysim/world/terrain_publisher.hpp"
#include "citysim/world/world_module.hpp"

#include <exception>

namespace citysim::runner {

GameSession::~GameSession() {
    if (runner_) {
        runner_->stop();
    }
}

NewGameOutcome start_new_game(const NewGameRequest& request) {
    NewGameOutcome outcome;
    const auto fail = [&](std::string message) {
        outcome.errors.push_back(
            content::ContentIssue{content::Severity::Error, "", "", std::move(message), ""});
        outcome.session.reset();
    };

    std::unique_ptr<GameSession> session(new GameSession());
    session->content_ = std::make_unique<content::ContentDatabase>();

    content::ModLoadRequest load;
    load.roots = request.content_roots;
    if (!request.enabled_mods.empty()) {
        load.order.enabled = request.enabled_mods;
    }
    load.order.require_base = true;
    content::LoadResult loaded = session->content_->load_mods(load);
    outcome.warnings = std::move(loaded.warnings);
    if (!loaded.ok()) {
        outcome.errors = std::move(loaded.errors);
        return outcome;
    }

    try {
        if (!session->content_->registry<content::EraDef>().find(request.era_id)) {
            fail("unknown era '" + request.era_id + "'");
            return outcome;
        }

        // Opened once: a load builds a new Simulation, which gets the same map.
        world::WorldConfig world_config;
        if (!request.map_id.empty()) {
            world_config.map_id = request.map_id;
            world_config.map = world::open_map(*session->content_, request.map_id);
            session->map_ = world_config.map;
            world_config.cities = session->content_->city_defines();
            world_config.terrain = session->content_->terrain_defines();
        }

        if (request.clock == nullptr) {
            session->owned_clock_ = std::make_unique<SteadyClock>();
        }
        IClock& clock = request.clock != nullptr ? *request.clock : *session->owned_clock_;
        session->clock_ = &clock;

        RunnerConfig config;
        config.time = session->content_->time_defines();
        config.content = session->content_.get();
        config.autosave_directory = request.autosave_directory;
        session->runner_ =
            std::make_unique<SimulationRunner>(request.seed, clock, config, request.worker_count);

        // Repeated by the runner on the new Simulation whenever a save is loaded.
        const content::ContentDatabase* content = session->content_.get();
        const std::string era_id = request.era_id;
        const std::uint32_t test_map_size = request.test_map_size;
        // The test map wins; a world game gets agents on its terrain only when it asks for them.
        const bool world_agents = request.world_agents && !world_config.map_id.empty() && test_map_size == 0;
        session->runner_->configure(
            [content, era_id, test_map_size, world_agents, world_config](core::Simulation& simulation) {
                time::install_calendar(simulation, *content, era_id);
                if (!world_config.map_id.empty()) {
                    world::install(simulation, *content, world_config);
                }
                if (test_map_size > 0) {
                    debug::WalkingSkeletonConfig skeleton;
                    skeleton.map_width = test_map_size;
                    skeleton.map_height = test_map_size;
                    debug::install(simulation, skeleton);
                } else if (world_agents) {
                    debug::WalkingSkeletonConfig skeleton;
                    skeleton.source = debug::WalkGridSource::Terrain;
                    debug::install(simulation, skeleton);
                }
            });
        std::vector<SimulationRunner::PublishHook> all_hooks = request.publish_hooks;
        if (!world_config.map_id.empty()) {
            all_hooks.emplace_back(&world::publish_terrain_tiles);
            all_hooks.emplace_back(&world::publish_cities);
            all_hooks.emplace_back(&world::publish_flood_extents);
            all_hooks.emplace_back(&world::publish_terraform_report);
        }
        if (world_agents) {
            all_hooks.emplace_back(&debug::publish_agent_snapshot);
        }
        if (test_map_size > 0) {
            all_hooks.emplace_back(&debug::publish_agent_snapshot);
            all_hooks.emplace_back(debug::TestMapPublisher{});
        }
        if (!all_hooks.empty()) {
            session->runner_->set_publish_hook(
                [hooks = std::move(all_hooks)](core::Simulation& simulation, RenderSnapshot& snapshot) {
                    for (const SimulationRunner::PublishHook& hook : hooks) {
                        hook(simulation, snapshot);
                    }
                });
        }
        session->runner_->start();
    } catch (const std::exception& error) {
        fail(error.what());
        return outcome;
    }
    outcome.session = std::move(session);
    return outcome;
}

} // namespace citysim::runner
