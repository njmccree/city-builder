#include "citysim/world/map_package.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <xxhash.h>

namespace citysim::world {

namespace {

using nlohmann::json;

// Reads fields of the manifest, failing with "<source>: <pointer>: <message>".
class FieldReader {
  public:
    explicit FieldReader(std::string source) : source_(std::move(source)) {}

    [[noreturn]] void fail(const std::string& pointer, const std::string& message) const {
        throw MapFormatError(source_, (pointer.empty() ? std::string("/") : pointer) + ": " + message);
    }

    const json& object_at(const json& value, const std::string& pointer) const {
        if (!value.is_object()) {
            fail(pointer, "expected an object");
        }
        return value;
    }

    const json& field(const json& object, const std::string& pointer, const char* key) const {
        const auto it = object.find(key);
        if (it == object.end()) {
            fail(pointer, std::string("missing required field '") + key + "'");
        }
        return *it;
    }

    std::string string_field(const json& object, const std::string& pointer, const char* key,
                             bool allow_empty = false) const {
        const json& value = field(object, pointer, key);
        if (!value.is_string()) {
            fail(pointer + "/" + key, "expected a string");
        }
        std::string text = value.get<std::string>();
        if (text.empty() && !allow_empty) {
            fail(pointer + "/" + key, "must not be empty");
        }
        return text;
    }

    double positive_number(const json& object, const std::string& pointer, const char* key) const {
        const json& value = field(object, pointer, key);
        if (!value.is_number()) {
            fail(pointer + "/" + key, "expected a number");
        }
        const double number = value.get<double>();
        if (!(number > 0.0) || !std::isfinite(number)) {
            fail(pointer + "/" + key, "expected a number above 0");
        }
        return number;
    }

    double number_in(const json& object, const std::string& pointer, const char* key, double low,
                     double high) const {
        const json& value = field(object, pointer, key);
        if (!value.is_number()) {
            fail(pointer + "/" + key, "expected a number");
        }
        const double number = value.get<double>();
        if (!(number >= low && number <= high)) {
            fail(pointer + "/" + key,
                 "expected a number from " + std::to_string(low) + " to " + std::to_string(high));
        }
        return number;
    }

    std::uint32_t positive_u32(const json& object, const std::string& pointer, const char* key) const {
        const json& value = field(object, pointer, key);
        if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0) ||
            value.get<std::uint64_t>() == 0 || value.get<std::uint64_t>() > UINT32_MAX) {
            fail(pointer + "/" + key, "expected a positive whole number");
        }
        return static_cast<std::uint32_t>(value.get<std::uint64_t>());
    }

    std::uint64_t hex_hash(const json& value, const std::string& pointer) const {
        std::uint64_t hash = 0;
        if (!value.is_string() || !parse_hex_hash(value.get<std::string>(), hash)) {
            fail(pointer, "expected 16 lower-case hex digits");
        }
        return hash;
    }

    // A package-relative path: '/' separators, no "..", not absolute, not empty.
    std::string relative_path(const json& value, const std::string& pointer) const {
        if (!value.is_string() || value.get<std::string>().empty()) {
            fail(pointer, "expected a file path");
        }
        std::string path = value.get<std::string>();
        const bool bad = path.front() == '/' || path.find('\\') != std::string::npos ||
                         path.find(':') != std::string::npos || path.find("//") != std::string::npos ||
                         path.back() == '/';
        std::istringstream parts(path);
        bool dotdot = false;
        for (std::string part; std::getline(parts, part, '/');) {
            dotdot = dotdot || part == ".." || part == ".";
        }
        if (bad || dotdot) {
            fail(pointer, "expected a relative path with '/' separators and no '..' ('" + path + "')");
        }
        return path;
    }

  private:
    std::string source_;
};

std::string index_pointer(const std::string& base, std::size_t index) {
    return base + "/" + std::to_string(index);
}

std::string escape_token(const std::string& token) {
    std::string out;
    for (const char c : token) {
        out += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    }
    return out;
}

} // namespace

std::string hash_to_hex(std::uint64_t hash) {
    static constexpr std::array<char, 16> k_digits{'0', '1', '2', '3', '4', '5', '6', '7',
                                                   '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string text(16, '0');
    for (std::size_t i = 0; i < 16; ++i) {
        text[15 - i] = k_digits[(hash >> (4U * i)) & 0xFU];
    }
    return text;
}

bool parse_hex_hash(std::string_view text, std::uint64_t& out) {
    if (text.size() != 16) {
        return false;
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        std::uint64_t digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<std::uint64_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<std::uint64_t>(c - 'a') + 10U;
        } else {
            return false;
        }
        value = (value << 4U) | digit;
    }
    out = value;
    return true;
}

std::uint64_t hash_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw MapFormatError(path.generic_string(), "cannot open the file");
    }
    XXH3_state_t* state = XXH3_createState();
    XXH3_64bits_reset(state);
    std::array<char, 1U << 16U> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) {
            XXH3_64bits_update(state, buffer.data(), static_cast<std::size_t>(got));
        }
    }
    const bool failed = in.bad();
    const std::uint64_t hash = XXH3_64bits_digest(state);
    XXH3_freeState(state);
    if (failed) {
        throw MapFormatError(path.generic_string(), "cannot read the file");
    }
    return hash;
}

std::uint64_t compute_package_hash(const std::map<std::string, std::uint64_t>& files) {
    std::string text;
    for (const auto& [path, hash] : files) { // std::map: sorted by path
        text += path + "=" + hash_to_hex(hash) + "\n";
    }
    return XXH3_64bits(text.data(), text.size());
}

std::string detail_tile_file(TileCoord tile) {
    return "detail/" + std::to_string(tile.tx) + "_" + std::to_string(tile.ty) + ".csr";
}

PackageManifest parse_manifest(const json& root, const std::string& source) {
    const FieldReader read(source);
    const json& top = read.object_at(root, "");
    PackageManifest manifest;

    const json& format = read.field(top, "", "format");
    if (!format.is_number_integer() || format.get<std::int64_t>() != k_package_format_version) {
        read.fail("/format", "unsupported package format (this build reads format " +
                                 std::to_string(k_package_format_version) + ")");
    }
    manifest.format = k_package_format_version;
    manifest.map_id = read.string_field(top, "", "map_id");
    manifest.pipeline_version = read.string_field(top, "", "pipeline_version");

    const json& sources = read.field(top, "", "sources");
    if (!sources.is_array()) {
        read.fail("/sources", "expected an array");
    }
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const std::string pointer = index_pointer("/sources", i);
        const json& item = read.object_at(sources[i], pointer);
        manifest.sources.push_back(PackageSource{read.string_field(item, pointer, "name"),
                                                 read.string_field(item, pointer, "version"),
                                                 read.string_field(item, pointer, "license", true)});
    }

    const json& projection = read.object_at(read.field(top, "", "projection"), "/projection");
    manifest.projection.type = read.string_field(projection, "/projection", "type");
    if (manifest.projection.type != "laea") {
        read.fail("/projection/type", "only \"laea\" is supported, not \"" + manifest.projection.type + "\"");
    }
    manifest.projection.lat_0 = read.number_in(projection, "/projection", "lat_0", -90.0, 90.0);
    manifest.projection.lon_0 = read.number_in(projection, "/projection", "lon_0", -180.0, 180.0);

    const json& bounds = read.object_at(read.field(top, "", "bounds_m"), "/bounds_m");
    manifest.bounds_width_m = read.positive_number(bounds, "/bounds_m", "width");
    manifest.bounds_height_m = read.positive_number(bounds, "/bounds_m", "height");

    const json& region = read.object_at(read.field(top, "", "region"), "/region");
    manifest.region.cell_size_m = read.positive_number(region, "/region", "cell_size_m");
    manifest.region.width = read.positive_u32(region, "/region", "width");
    manifest.region.height = read.positive_u32(region, "/region", "height");
    const json& layers = read.object_at(read.field(region, "/region", "layers"), "/region/layers");
    if (layers.empty()) {
        read.fail("/region/layers", "needs at least one layer");
    }
    for (const auto& item : layers.items()) {
        const std::string pointer = "/region/layers/" + escape_token(item.key());
        manifest.region.layers.emplace(item.key(), read.relative_path(item.value(), pointer));
    }

    const json& detail = read.object_at(read.field(top, "", "detail"), "/detail");
    manifest.detail.tile_size_m = read.positive_number(detail, "/detail", "tile_size_m");
    manifest.detail.sample_spacing_m = read.positive_number(detail, "/detail", "sample_spacing_m");
    const double samples = manifest.detail.tile_size_m / manifest.detail.sample_spacing_m;
    if (samples != std::floor(samples)) {
        read.fail("/detail/sample_spacing_m", "the tile size must be a whole number of samples");
    }
    const json& tiles = read.field(detail, "/detail", "tiles");
    if (!tiles.is_array()) {
        read.fail("/detail/tiles", "expected an array");
    }
    std::set<TileCoord> seen;
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const std::string pointer = index_pointer("/detail/tiles", i);
        const json& pair = tiles[i];
        if (!pair.is_array() || pair.size() != 2 || !pair[0].is_number_integer() || !pair[1].is_number_integer()) {
            read.fail(pointer, "expected [tx, ty] as two whole numbers");
        }
        const auto tx = pair[0].get<std::int64_t>();
        const auto ty = pair[1].get<std::int64_t>();
        if (tx < INT32_MIN || tx > INT32_MAX || ty < INT32_MIN || ty > INT32_MAX) {
            read.fail(pointer, "tile coordinates are out of range");
        }
        const TileCoord tile{static_cast<std::int32_t>(tx), static_cast<std::int32_t>(ty)};
        if (!seen.insert(tile).second) {
            read.fail(pointer, "tile [" + std::to_string(tx) + ", " + std::to_string(ty) + "] is listed twice");
        }
        manifest.detail.tiles.push_back(tile);
    }

    manifest.rivers_file = read.relative_path(read.field(top, "", "rivers"), "/rivers");
    manifest.lakes_file = read.relative_path(read.field(top, "", "lakes"), "/lakes");

    const json& files = read.object_at(read.field(top, "", "files"), "/files");
    for (const auto& item : files.items()) {
        const std::string pointer = "/files/" + escape_token(item.key());
        read.relative_path(json(item.key()), pointer);
        manifest.files.emplace(item.key(), read.hex_hash(item.value(), pointer));
    }
    manifest.package_hash = read.hex_hash(read.field(top, "", "package_hash"), "/package_hash");
    return manifest;
}

PackageManifest read_manifest(const std::filesystem::path& dir) {
    const std::filesystem::path path = dir / k_package_file_name;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw MapFormatError(path.generic_string(), "cannot open the file");
    }
    json root;
    try {
        root = json::parse(in);
    } catch (const json::parse_error& error) {
        throw MapFormatError(path.generic_string(), std::string("not valid JSON: ") + error.what());
    }
    return parse_manifest(root, path.generic_string());
}

json manifest_to_json(const PackageManifest& manifest) {
    json sources = json::array();
    for (const PackageSource& source : manifest.sources) {
        sources.push_back({{"name", source.name}, {"version", source.version}, {"license", source.license}});
    }
    json tiles = json::array();
    for (const TileCoord& tile : manifest.detail.tiles) {
        tiles.push_back(json::array({tile.tx, tile.ty}));
    }
    json layers = json::object();
    for (const auto& [name, file] : manifest.region.layers) {
        layers[name] = file;
    }
    json files = json::object();
    for (const auto& [path, hash] : manifest.files) {
        files[path] = hash_to_hex(hash);
    }
    return json{{"format", manifest.format},
                {"map_id", manifest.map_id},
                {"pipeline_version", manifest.pipeline_version},
                {"sources", sources},
                {"projection",
                 {{"type", manifest.projection.type},
                  {"lat_0", manifest.projection.lat_0},
                  {"lon_0", manifest.projection.lon_0}}},
                {"bounds_m", {{"width", manifest.bounds_width_m}, {"height", manifest.bounds_height_m}}},
                {"region",
                 {{"cell_size_m", manifest.region.cell_size_m},
                  {"width", manifest.region.width},
                  {"height", manifest.region.height},
                  {"layers", layers}}},
                {"detail",
                 {{"tile_size_m", manifest.detail.tile_size_m},
                  {"sample_spacing_m", manifest.detail.sample_spacing_m},
                  {"tiles", tiles}}},
                {"rivers", manifest.rivers_file},
                {"lakes", manifest.lakes_file},
                {"files", files},
                {"package_hash", hash_to_hex(manifest.package_hash)}};
}

std::string manifest_to_text(const PackageManifest& manifest) {
    return manifest_to_json(manifest).dump(2) + "\n";
}

PackageManifest verify_package(const std::filesystem::path& dir) {
    PackageManifest manifest = read_manifest(dir);
    const std::string where = (dir / k_package_file_name).generic_string();
    std::vector<std::string> problems;

    // Every file the manifest refers to must be in the list.
    const auto require_listed = [&](const std::string& what, const std::string& file) {
        if (!manifest.files.contains(file)) {
            problems.push_back(what + " '" + file + "' is not in \"files\"");
        }
    };
    for (const auto& [name, file] : manifest.region.layers) {
        require_listed("region layer '" + name + "'", file);
    }
    for (const TileCoord& tile : manifest.detail.tiles) {
        require_listed("detail tile", detail_tile_file(tile));
    }
    require_listed("rivers file", manifest.rivers_file);
    require_listed("lakes file", manifest.lakes_file);

    // Every listed file exists and matches its checksum.
    for (const auto& [file, expected] : manifest.files) {
        const std::filesystem::path path = dir / std::filesystem::path(file);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            problems.push_back("'" + file + "' is missing");
            continue;
        }
        const std::uint64_t actual = hash_file(path);
        if (actual != expected) {
            problems.push_back("'" + file + "' has checksum " + hash_to_hex(actual) + " but the manifest says " +
                               hash_to_hex(expected));
        }
    }

    // No detail tile on disk that the manifest does not know about.
    std::error_code ec;
    if (std::filesystem::is_directory(dir / "detail", ec)) {
        std::set<std::string> on_disk;
        for (const auto& entry : std::filesystem::directory_iterator(dir / "detail", ec)) {
            if (entry.is_regular_file()) {
                on_disk.insert("detail/" + entry.path().filename().generic_string());
            }
        }
        for (const std::string& file : on_disk) {
            if (!manifest.files.contains(file)) {
                problems.push_back("'" + file + "' is in the package folder but not in \"files\"");
            }
        }
    }

    if (compute_package_hash(manifest.files) != manifest.package_hash) {
        problems.push_back("package_hash " + hash_to_hex(manifest.package_hash) + " does not match the file list (" +
                           hash_to_hex(compute_package_hash(manifest.files)) + ")");
    }

    if (!problems.empty()) {
        constexpr std::size_t k_shown = 5;
        std::string message = std::to_string(problems.size()) + " problem(s): ";
        for (std::size_t i = 0; i < std::min(problems.size(), k_shown); ++i) {
            message += (i == 0 ? "" : "; ") + problems[i];
        }
        if (problems.size() > k_shown) {
            message += "; and " + std::to_string(problems.size() - k_shown) + " more";
        }
        throw MapFormatError(where, message);
    }
    return manifest;
}

} // namespace citysim::world
