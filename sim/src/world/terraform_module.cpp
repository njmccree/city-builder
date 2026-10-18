#include "citysim/world/terraform_module.hpp"

#include "citysim/core/log.hpp"
#include "citysim/core/tick_context.hpp"
#include "citysim/core/ui_notice.hpp"
#include "citysim/world/city_registry.hpp"
#include "citysim/world/terraform.hpp"
#include "citysim/world/terraform_commands.hpp"
#include "citysim/world/terraform_events.hpp"
#include "citysim/world/terraform_report.hpp"
#include "citysim/world/world_terrain.hpp"

namespace citysim::world {

namespace {

void refuse(core::World& world, const Terraform& command, const char* key) {
    CS_LOG_INFO(Sim, "Terraform in city {} refused: {}", command.city_id, key);
    world.ctx().get<core::UiOutbox>().push(core::UiNotice{"terraform_failed", key, command.city_id});
}

void terraform(core::TickContext& context, const Terraform& command) {
    core::World& world = context.world;
    const CityInfo* city = world.ctx().get<CityRegistry>().find(command.city_id);
    if (city == nullptr) {
        refuse(world, command, k_error_terraform_unknown_city);
        return;
    }
    if (world.ctx().get<ActiveCity>().city_id != command.city_id) {
        refuse(world, command, k_error_terraform_not_active);
        return;
    }
    auto& terrain = world.ctx().get<WorldTerrain>();
    const TerraformResult result = apply_terraform(terrain, city->area(), command, terrain.terrain_defines());
    if (result.status == TerraformStatus::BadParameters) {
        refuse(world, command, k_error_terraform_parameters);
        return;
    }
    if (result.status == TerraformStatus::OutsideCity) {
        refuse(world, command, k_error_terraform_outside_city);
        return;
    }
    if (result.samples_changed == 0) {
        return; // the brush changed nothing (flat ground, limits): no cost, nothing to report
    }
    const double cost =
        result.volume_m3 * static_cast<double>(terrain.terrain_defines().terraform_cost_per_m3);
    context.events.emit(TerraformApplied{command.city_id, result.volume_m3, cost});
    auto& report = world.ctx().get<TerraformReport>();
    ++report.serial;
    report.city = command.city_id;
    report.volume_m3 = result.volume_m3;
    report.cost = cost;
    report.total_cost += cost;
    world.ctx().get<core::UiOutbox>().push(core::UiNotice{"terraform_applied", "", command.city_id});
}

} // namespace

void install_terraform(core::Simulation& simulation) {
    simulation.world().ctx().emplace<TerraformReport>();
    simulation.dispatcher().register_handler<Terraform>(&terraform);
}

} // namespace citysim::world
