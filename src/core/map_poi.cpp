#include "openefb/core/map_poi.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace openefb {
namespace {

std::vector<std::string_view> fields(std::string_view line) {
    std::vector<std::string_view> result;
    std::size_t start{};
    while (start <= line.size()) {
        const auto end = line.find('\t', start);
        result.push_back(line.substr(start, end == std::string_view::npos
                                               ? line.size() - start : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return result;
}

bool coordinate(std::string_view text, double& output) {
    std::string value(text);
    char* end{};
    output = std::strtod(value.c_str(), &end);
    return end == value.c_str() + value.size() && std::isfinite(output);
}

PoiCategory category_for(std::string_view amenity, std::string_view tourism,
                         std::string_view leisure, std::string_view golf,
                         std::string_view historic) {
    if (amenity == "restaurant" || amenity == "cafe" || amenity == "fast_food" ||
        amenity == "bar" || amenity == "pub") return PoiCategory::food;
    if (leisure == "golf_course" || leisure == "golf" || !golf.empty()) {
        return PoiCategory::golf;
    }
    if (!tourism.empty() || !historic.empty()) return PoiCategory::attraction;
    return PoiCategory::attraction;
}

std::string readable(std::string_view value) {
    std::string result(value);
    std::replace(result.begin(), result.end(), '_', ' ');
    if (!result.empty()) result.front() = static_cast<char>(std::toupper(
        static_cast<unsigned char>(result.front())));
    return result;
}

} // namespace

std::string_view poi_category_label(PoiCategory category) noexcept {
    switch (category) {
    case PoiCategory::food: return "Food & drink";
    case PoiCategory::golf: return "Golf course";
    case PoiCategory::attraction: return "Attraction";
    }
    return "Place";
}

std::vector<MapPoi> parse_overpass_pois(std::string_view input) {
    std::vector<MapPoi> result;
    std::set<long long> seen;
    std::size_t start{};
    while (start < input.size() && result.size() < 240) {
        const auto end = input.find('\n', start);
        auto line = input.substr(start, end == std::string_view::npos
                                           ? input.size() - start : end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto values = fields(line);
        if (values.size() >= 9) {
            long long id{};
            const auto [id_end, id_error] = std::from_chars(
                values[0].data(), values[0].data() + values[0].size(), id);
            double latitude{};
            double longitude{};
            if (id_error == std::errc{} && id_end == values[0].data() + values[0].size() &&
                seen.insert(id).second && coordinate(values[1], latitude) &&
                coordinate(values[2], longitude) && latitude >= -90.0 && latitude <= 90.0 &&
                longitude >= -180.0 && longitude <= 180.0) {
                const auto category = category_for(values[4], values[5], values[6],
                                                   values[7], values[8]);
                std::string detail;
                if (category == PoiCategory::food) detail = readable(values[4]);
                else if (category == PoiCategory::golf) detail = "Golf course";
                else if (!values[5].empty()) detail = readable(values[5]);
                else detail = readable(values[8]);
                std::string name(values[3]);
                if (name.empty()) name = std::string(poi_category_label(category));
                result.push_back({id, category, std::move(name), std::move(detail),
                                  latitude, longitude});
            }
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return result;
}

} // namespace openefb
