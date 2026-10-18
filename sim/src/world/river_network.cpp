#include "citysim/world/river_network.hpp"

#include <limits>
#include <set>

namespace citysim::world {

namespace {

using nlohmann::json;

[[noreturn]] void fail(const std::string& source, std::string_view pointer, const std::string& reason) {
    throw MapFormatError(source, (pointer.empty() ? std::string("/") : std::string(pointer)) + ": " + reason);
}

const json& field(const json& object, const std::string& source, std::string_view pointer, const char* key) {
    if (!object.is_object()) {
        fail(source, pointer, "expected an object");
    }
    const auto it = object.find(key);
    if (it == object.end()) {
        fail(source, std::string(pointer) + "/" + key, "missing field");
    }
    return *it;
}

std::string text_field(const json& object, const std::string& source, std::string_view pointer,
                       const char* key) {
    const json& value = field(object, source, pointer, key);
    if (!value.is_string() || value.get<std::string>().empty()) {
        fail(source, std::string(pointer) + "/" + key, "expected a non-empty text");
    }
    return value.get<std::string>();
}

double number_field(const json& object, const std::string& source, std::string_view pointer,
                    const char* key) {
    const json& value = field(object, source, pointer, key);
    if (!value.is_number()) {
        fail(source, std::string(pointer) + "/" + key, "expected a number");
    }
    const double number = value.get<double>();
    if (!std::isfinite(number)) {
        fail(source, std::string(pointer) + "/" + key, "must be finite");
    }
    return number;
}

float lerp(float a, float b, double t) {
    return static_cast<float>(static_cast<double>(a) + (static_cast<double>(b) - static_cast<double>(a)) * t);
}

} // namespace

RiverNetwork RiverNetwork::parse(const json& root, const std::string& source) {
    RiverNetwork network;
    const json& format = field(root, source, "", "format");
    if (!format.is_number_integer() || format.get<std::int64_t>() != 1) {
        fail(source, "/format", "unsupported rivers format (this build reads format 1)");
    }
    const json& rivers = field(root, source, "", "rivers");
    if (!rivers.is_array()) {
        fail(source, "/rivers", "expected an array");
    }

    // First pass: the reaches and their data, in file order. Downstream links are resolved in a second pass
    // because a reach may name one that comes later.
    struct Pending {
        std::size_t reach = 0;
        std::string river_pointer;
        std::string reach_pointer;
        std::vector<std::string> downstream_ids;
    };
    std::vector<Pending> pending;
    std::set<std::string> river_ids;
    for (std::size_t r = 0; r < rivers.size(); ++r) {
        const std::string river_pointer = "/rivers/" + std::to_string(r);
        const json& river = rivers[r];
        const std::string river_id = text_field(river, source, river_pointer, "id");
        const std::string river_name = text_field(river, source, river_pointer, "name");
        if (!river_ids.insert(river_id).second) {
            fail(source, river_pointer + "/id", "the river id '" + river_id + "' is used twice");
        }
        const json& reaches = field(river, source, river_pointer, "reaches");
        if (!reaches.is_array() || reaches.empty()) {
            fail(source, river_pointer + "/reaches", "expected a non-empty array");
        }
        std::set<std::string> reach_ids;
        for (std::size_t i = 0; i < reaches.size(); ++i) {
            const std::string reach_pointer = river_pointer + "/reaches/" + std::to_string(i);
            const json& entry = reaches[i];
            Reach reach;
            reach.river_id = river_id;
            reach.river_name = river_name;
            reach.id = text_field(entry, source, reach_pointer, "id");
            if (!reach_ids.insert(reach.id).second) {
                fail(source, reach_pointer + "/id",
                     "the reach id '" + reach.id + "' is used twice in this river");
            }
            reach.order = static_cast<int>(number_field(entry, source, reach_pointer, "order"));
            reach.discharge_m3s = number_field(entry, source, reach_pointer, "discharge_m3s");
            if (reach.discharge_m3s < 0.0) {
                fail(source, reach_pointer + "/discharge_m3s", "must not be negative");
            }
            Pending link{network.reaches_.size(), river_pointer, reach_pointer, {}};
            const json& downstream = field(entry, source, reach_pointer, "downstream");
            if (!downstream.is_array()) {
                fail(source, reach_pointer + "/downstream", "expected an array of reach ids");
            }
            for (std::size_t d = 0; d < downstream.size(); ++d) {
                if (!downstream[d].is_string()) {
                    fail(source, reach_pointer + "/downstream/" + std::to_string(d), "expected a reach id");
                }
                link.downstream_ids.push_back(downstream[d].get<std::string>());
            }
            const json& vertices = field(entry, source, reach_pointer, "vertices");
            if (!vertices.is_array() || vertices.size() < 2) {
                fail(source, reach_pointer + "/vertices", "a reach needs at least 2 vertices");
            }
            for (std::size_t v = 0; v < vertices.size(); ++v) {
                const std::string vertex_pointer = reach_pointer + "/vertices/" + std::to_string(v);
                const json& vertex = vertices[v];
                RiverVertex out;
                out.x = number_field(vertex, source, vertex_pointer, "x");
                out.z = number_field(vertex, source, vertex_pointer, "z");
                out.width_m = static_cast<float>(number_field(vertex, source, vertex_pointer, "width_m"));
                out.depth_m = static_cast<float>(number_field(vertex, source, vertex_pointer, "depth_m"));
                out.level_m =
                    static_cast<float>(number_field(vertex, source, vertex_pointer, "water_level_m"));
                if (!(out.width_m > 0.0F)) {
                    fail(source, vertex_pointer + "/width_m", "must be greater than 0");
                }
                if (!(out.depth_m > 0.0F)) {
                    fail(source, vertex_pointer + "/depth_m", "must be greater than 0");
                }
                network.max_half_width_m_ =
                    std::max(network.max_half_width_m_, 0.5 * static_cast<double>(out.width_m));
                reach.vertices.push_back(out);
            }
            network.reaches_.push_back(std::move(reach));
            pending.push_back(std::move(link));
        }
    }

    for (const Pending& link : pending) {
        Reach& reach = network.reaches_[link.reach];
        for (const std::string& wanted : link.downstream_ids) {
            const auto found = network.find_reach(reach.river_id, wanted);
            if (!found) {
                fail(source, link.reach_pointer + "/downstream",
                     "the reach '" + wanted + "' does not exist in this river");
            }
            if (*found == link.reach) {
                fail(source, link.reach_pointer + "/downstream", "a reach cannot flow into itself");
            }
            reach.downstream.push_back(*found);
        }
    }

    // The segment index.
    for (std::uint32_t r = 0; r < network.reaches_.size(); ++r) {
        const std::vector<RiverVertex>& vertices = network.reaches_[r].vertices;
        for (std::uint32_t v = 0; v + 1 < vertices.size(); ++v) {
            Segment segment;
            segment.reach = r;
            segment.first_vertex = v;
            segment.min_x = std::min(vertices[v].x, vertices[v + 1].x);
            segment.max_x = std::max(vertices[v].x, vertices[v + 1].x);
            segment.min_z = std::min(vertices[v].z, vertices[v + 1].z);
            segment.max_z = std::max(vertices[v].z, vertices[v + 1].z);
            const auto index = static_cast<std::uint32_t>(network.segments_.size());
            network.segments_.push_back(segment);
            for (std::int64_t cz = cell_of(segment.min_z); cz <= cell_of(segment.max_z); ++cz) {
                for (std::int64_t cx = cell_of(segment.min_x); cx <= cell_of(segment.max_x); ++cx) {
                    network.grid_[cell_key(cx, cz)].push_back(index);
                }
            }
        }
    }
    return network;
}

std::optional<std::uint32_t> RiverNetwork::find_reach(std::string_view river_id,
                                                      std::string_view reach_id) const {
    for (std::uint32_t i = 0; i < reaches_.size(); ++i) {
        if (reaches_[i].river_id == river_id && reaches_[i].id == reach_id) {
            return i;
        }
    }
    return std::nullopt;
}

std::vector<std::uint32_t> RiverNetwork::reaches_of_river(std::string_view river_name) const {
    std::vector<std::uint32_t> out;
    for (std::uint32_t i = 0; i < reaches_.size(); ++i) {
        if (reaches_[i].river_name == river_name) {
            out.push_back(i);
        }
    }
    return out;
}

bool RiverNetwork::any_near_box(double min_x, double min_z, double max_x, double max_z, double radius) const {
    for (std::int64_t cz = cell_of(min_z - radius); cz <= cell_of(max_z + radius); ++cz) {
        for (std::int64_t cx = cell_of(min_x - radius); cx <= cell_of(max_x + radius); ++cx) {
            if (grid_.contains(cell_key(cx, cz))) {
                return true;
            }
        }
    }
    return false;
}

std::vector<RiverNetwork::SegmentBox>
RiverNetwork::segments_near_box(double min_x, double min_z, double max_x, double max_z, double radius) const {
    std::vector<std::uint32_t> candidates;
    for (std::int64_t cz = cell_of(min_z - radius); cz <= cell_of(max_z + radius); ++cz) {
        for (std::int64_t cx = cell_of(min_x - radius); cx <= cell_of(max_x + radius); ++cx) {
            const auto it = grid_.find(cell_key(cx, cz));
            if (it != grid_.end()) {
                candidates.insert(candidates.end(), it->second.begin(), it->second.end());
            }
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    std::vector<SegmentBox> out;
    for (const std::uint32_t index : candidates) {
        const Segment& segment = segments_[index];
        if (segment.max_x >= min_x - radius && segment.min_x <= max_x + radius &&
            segment.max_z >= min_z - radius && segment.min_z <= max_z + radius) {
            out.push_back(SegmentBox{index, segment.min_x, segment.min_z, segment.max_x, segment.max_z});
        }
    }
    return out;
}

ChannelSample RiverNetwork::measure(const Segment& segment, WorldPos pos) const {
    const RiverVertex& a = reaches_[segment.reach].vertices[segment.first_vertex];
    const RiverVertex& b = reaches_[segment.reach].vertices[segment.first_vertex + 1];
    const double dx = b.x - a.x;
    const double dz = b.z - a.z;
    const double length_sq = dx * dx + dz * dz;
    double t = 0.0;
    if (length_sq > 0.0) {
        t = std::clamp(((pos.x - a.x) * dx + (pos.z - a.z) * dz) / length_sq, 0.0, 1.0);
    }
    const double px = a.x + t * dx - pos.x;
    const double pz = a.z + t * dz - pos.z;
    ChannelSample sample;
    sample.reach = segment.reach;
    sample.segment = segment.first_vertex;
    sample.distance_m = static_cast<float>(std::sqrt(px * px + pz * pz));
    sample.half_width_m = 0.5F * lerp(a.width_m, b.width_m, t);
    sample.depth_m = lerp(a.depth_m, b.depth_m, t);
    sample.level_m = lerp(a.level_m, b.level_m, t);
    return sample;
}

} // namespace citysim::world
