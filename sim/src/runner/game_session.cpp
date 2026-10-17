#include "citysim/runner/game_session.hpp"

#include "citysim/content/era_def.hpp"
#include "citysim/time/calendar_module.hpp"

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
        session->runner_->configure([content, era_id](core::Simulation& simulation) {
            time::install_calendar(simulation, *content, era_id);
        });
        if (!request.publish_hooks.empty()) {
            session->runner_->set_publish_hook(
                [hooks = request.publish_hooks](core::Simulation& simulation, RenderSnapshot& snapshot) {
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
