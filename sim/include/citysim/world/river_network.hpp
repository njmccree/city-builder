#pragma once

#include "citysim/world/frames.hpp"
#include "citysim/world/map_format_error.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace citysim::world {

// The fixed river courses of a map (architecture §15.6), read from the package's rivers.json (schema in
// game/content/README.md). Immutable once built, so one instance is shared as part of the LoadedMap by every
// reader: terrain synthesis on worker threads, the water queries, the renderer. Not tied to a clock.
//
// A river is a set of reaches; a reach is a polyline from upstream to downstream with a width, a depth and a
// base water level (meters above sea level) at every vertex. Reaches are numbered in file order, rivers
// first: that number is the `reach index` the rest of the game uses (WaterState, the SetRiverLevel command).

struct RiverVertex {
    double x = 0.0; // world frame, meters
    double z = 0.0;
    float width_m = 0.0F;
    float depth_m = 0.0F;
    float level_m = 0.0F; // the base water surface
};

struct Reach {
    std::string river_id;
    std::string river_name;
    std::string id; // unique within the river
    int order = 1;
    double discharge_m3s = 0.0;
    std::vector<std::uint32_t> downstream; // reach indices of the reaches that start where this one ends
    std::vector<RiverVertex> vertices;     // at least 2
    // The land beside the channel that can flood (flood extents, architecture §15.6): closed outer rings in
    // the world frame, written by the pipeline. Empty when the file has none, and then the reach floods
    // nothing. A ring has at least 3 points and does not repeat its first point at the end.
    std::vector<std::vector<WorldPos>> floodplain;
};

// How a point relates to the closest part of one segment of a reach: its distance from the centerline and the
// width, depth and level interpolated at that closest point.
struct ChannelSample {
    std::uint32_t reach = 0;
    std::uint32_t segment = 0; // index of the segment's first vertex within the reach
    float distance_m = 0.0F;
    float half_width_m = 0.0F;
    float depth_m = 0.0F;
    float level_m = 0.0F;
};

class RiverNetwork {
  public:
    RiverNetwork() = default;

    // Reads rivers.json. `source` names the file in errors, which carry a JSON pointer:
    // "rivers.json: /rivers/0/reaches/1/vertices/2/width_m: must be greater than 0". Throws MapFormatError.
    static RiverNetwork parse(const nlohmann::json& json, const std::string& source);

    bool empty() const { return reaches_.empty(); }
    std::size_t reach_count() const { return reaches_.size(); }
    const std::vector<Reach>& reaches() const { return reaches_; }
    // The widest half width of any vertex: how far from a centerline a channel can reach.
    double max_half_width_m() const { return max_half_width_m_; }

    // The reach index of river `river_id`'s reach `reach_id`, or nullopt.
    std::optional<std::uint32_t> find_reach(std::string_view river_id, std::string_view reach_id) const;
    // The reach indices of a river by its name (rivers.json "name"), in file order.
    std::vector<std::uint32_t> reaches_of_river(std::string_view river_name) const;

    // The index of the first segment of a reach, for measure_segment(): the segment that starts at vertex v
    // of reach r has the index first_segment(r) + v.
    std::uint32_t first_segment(std::uint32_t reach) const { return reach_first_segment_[reach]; }

    // True if some segment could lie within `radius` meters of the box [min_x, max_x] x [min_z, max_z]: a
    // cheap test so that a tile far from every river skips the per-sample work.
    bool any_near_box(double min_x, double min_z, double max_x, double max_z, double radius) const;

    // The box of one segment, for callers that pick the segments near an area once and then measure many
    // points against only those (terrain synthesis).
    struct SegmentBox {
        std::uint32_t index = 0;
        double min_x = 0.0;
        double min_z = 0.0;
        double max_x = 0.0;
        double max_z = 0.0;
    };
    // The segments whose box lies within `radius` meters of the box [min_x, max_x] x [min_z, max_z], by
    // index.
    std::vector<SegmentBox> segments_near_box(double min_x, double min_z, double max_x, double max_z,
                                              double radius) const;
    // How the point relates to the segment with this index (an index from segments_near_box).
    ChannelSample measure_segment(std::uint32_t index, WorldPos pos) const {
        return measure(segments_[index], pos);
    }

    // Calls fn(const ChannelSample&) once for every segment that may lie within `radius` meters of `pos` (the
    // segment's box is near it; the sample's distance_m says how near). The order depends only on the data,
    // never on threads, so a caller that picks "the nearest, ties to the first" is deterministic.
    template <class Fn> void for_each_near(WorldPos pos, double radius, Fn&& fn) const {
        const std::int64_t cx0 = cell_of(pos.x - radius);
        const std::int64_t cx1 = cell_of(pos.x + radius);
        const std::int64_t cz0 = cell_of(pos.z - radius);
        const std::int64_t cz1 = cell_of(pos.z + radius);
        for (std::int64_t cz = cz0; cz <= cz1; ++cz) {
            for (std::int64_t cx = cx0; cx <= cx1; ++cx) {
                const auto it = grid_.find(cell_key(cx, cz));
                if (it == grid_.end()) {
                    continue;
                }
                for (const std::uint32_t index : it->second) {
                    const Segment& segment = segments_[index];
                    // A segment sits in every cell its box touches; visit it once, in the first of the
                    // queried cells that it touches.
                    if (cell_of(std::max(pos.x - radius, segment.min_x)) != cx ||
                        cell_of(std::max(pos.z - radius, segment.min_z)) != cz) {
                        continue;
                    }
                    fn(measure(segment, pos));
                }
            }
        }
    }

  private:
    struct Segment {
        std::uint32_t reach = 0;
        std::uint32_t first_vertex = 0;
        double min_x = 0.0;
        double min_z = 0.0;
        double max_x = 0.0;
        double max_z = 0.0;
    };

    static constexpr double k_cell_m = 512.0;
    static std::int64_t cell_of(double meters) {
        return static_cast<std::int64_t>(std::floor(meters / k_cell_m));
    }
    static std::uint64_t cell_key(std::int64_t cx, std::int64_t cz) {
        constexpr std::int64_t k_offset = std::int64_t{1} << 30;
        return (static_cast<std::uint64_t>(cx + k_offset) << 32U) | static_cast<std::uint64_t>(cz + k_offset);
    }
    ChannelSample measure(const Segment& segment, WorldPos pos) const;

    std::vector<Reach> reaches_;
    std::vector<Segment> segments_;
    std::vector<std::uint32_t> reach_first_segment_;
    // Cell -> indices into segments_, ascending. Only looked up, never iterated, so its order cannot matter.
    std::map<std::uint64_t, std::vector<std::uint32_t>> grid_;
    double max_half_width_m_ = 0.0;
};

} // namespace citysim::world
