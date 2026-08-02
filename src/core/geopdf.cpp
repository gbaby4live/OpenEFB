#include "openefb/core/geopdf.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <vector>

namespace openefb {
namespace {

std::vector<double> pdf_array(std::string_view source, std::string_view key,
                              std::size_t start = 0) {
    const auto key_position = source.find(key, start);
    const auto opening = key_position == std::string_view::npos
        ? std::string_view::npos : source.find('[', key_position + key.size());
    const auto closing = opening == std::string_view::npos
        ? std::string_view::npos : source.find(']', opening + 1);
    if (closing == std::string_view::npos) return {};
    std::vector<double> result;
    auto cursor = opening + 1;
    while (cursor < closing) {
        while (cursor < closing && (source[cursor] == ' ' || source[cursor] == '\t' ||
                                    source[cursor] == '\r' || source[cursor] == '\n')) ++cursor;
        if (cursor >= closing) break;
        double value{};
        const auto parsed = std::from_chars(source.data() + cursor,
                                             source.data() + closing, value);
        if (parsed.ec != std::errc{}) return {};
        result.push_back(value);
        cursor = static_cast<std::size_t>(parsed.ptr - source.data());
    }
    return result;
}

double interpolate(double sw, double se, double ne, double nw, double u, double v) {
    return sw * (1.0 - u) * (1.0 - v) + se * u * (1.0 - v) +
           ne * u * v + nw * (1.0 - u) * v;
}

double unwrap_longitude(double longitude, double reference) {
    while (longitude - reference > 180.0) longitude -= 360.0;
    while (longitude - reference < -180.0) longitude += 360.0;
    return longitude;
}

} // namespace

std::optional<GeoPdfReference> parse_geopdf_reference(std::string_view pdf) {
    const auto viewport = pdf.find("/VP");
    if (viewport == std::string_view::npos) return std::nullopt;
    const auto media = pdf_array(pdf, "/MediaBox");
    const auto box = pdf_array(pdf, "/BBox", viewport);
    const auto geographic = pdf_array(pdf, "/GPTS", viewport);
    const auto local = pdf_array(pdf, "/LPTS", viewport);
    if (media.size() != 4 || box.size() != 4 || geographic.size() != 8 || local.size() != 8)
        return std::nullopt;
    GeoPdfReference reference;
    std::copy_n(media.begin(), 4, reference.media_box.begin());
    std::copy_n(box.begin(), 4, reference.viewport_box.begin());
    std::copy_n(geographic.begin(), 8, reference.geographic_points.begin());
    std::copy_n(local.begin(), 8, reference.local_points.begin());
    if (reference.media_box[2] <= reference.media_box[0] ||
        reference.media_box[3] <= reference.media_box[1] ||
        reference.viewport_box[2] <= reference.viewport_box[0] ||
        reference.viewport_box[3] <= reference.viewport_box[1]) return std::nullopt;
    return reference;
}

std::optional<GeoPdfPagePoint> project_geopdf_position(
    const GeoPdfReference& reference, double latitude, double longitude) {
    if (!std::isfinite(latitude) || !std::isfinite(longitude)) return std::nullopt;
    std::array<double, 4> latitudes{};
    std::array<double, 4> longitudes{};
    for (std::size_t index = 0; index < 4; ++index) {
        latitudes[index] = reference.geographic_points[index * 2];
        longitudes[index] = reference.geographic_points[index * 2 + 1];
    }
    for (std::size_t index = 1; index < longitudes.size(); ++index)
        longitudes[index] = unwrap_longitude(longitudes[index], longitudes[0]);
    longitude = unwrap_longitude(longitude, longitudes[0]);

    const auto [minimum_latitude, maximum_latitude] = std::minmax_element(
        latitudes.begin(), latitudes.end());
    const auto [minimum_longitude, maximum_longitude] = std::minmax_element(
        longitudes.begin(), longitudes.end());
    double u = (*maximum_longitude - *minimum_longitude) > 1e-9
        ? (longitude - *minimum_longitude) / (*maximum_longitude - *minimum_longitude) : 0.5;
    double v = (*maximum_latitude - *minimum_latitude) > 1e-9
        ? (latitude - *minimum_latitude) / (*maximum_latitude - *minimum_latitude) : 0.5;
    for (int iteration = 0; iteration < 10; ++iteration) {
        const double calculated_latitude = interpolate(
            latitudes[0], latitudes[1], latitudes[2], latitudes[3], u, v);
        const double calculated_longitude = interpolate(
            longitudes[0], longitudes[1], longitudes[2], longitudes[3], u, v);
        const double latitude_u = (latitudes[1] - latitudes[0]) * (1.0 - v) +
                                  (latitudes[2] - latitudes[3]) * v;
        const double latitude_v = (latitudes[3] - latitudes[0]) * (1.0 - u) +
                                  (latitudes[2] - latitudes[1]) * u;
        const double longitude_u = (longitudes[1] - longitudes[0]) * (1.0 - v) +
                                   (longitudes[2] - longitudes[3]) * v;
        const double longitude_v = (longitudes[3] - longitudes[0]) * (1.0 - u) +
                                   (longitudes[2] - longitudes[1]) * u;
        const double determinant = latitude_u * longitude_v - latitude_v * longitude_u;
        if (std::abs(determinant) < 1e-12) break;
        const double latitude_error = latitude - calculated_latitude;
        const double longitude_error = longitude - calculated_longitude;
        u += (latitude_error * longitude_v - longitude_error * latitude_v) / determinant;
        v += (longitude_error * latitude_u - latitude_error * longitude_u) / determinant;
    }
    if (u < -0.02 || u > 1.02 || v < -0.02 || v > 1.02) return std::nullopt;
    u = std::clamp(u, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    const double local_x = interpolate(reference.local_points[0], reference.local_points[2],
                                       reference.local_points[4], reference.local_points[6], u, v);
    const double local_y = interpolate(reference.local_points[1], reference.local_points[3],
                                       reference.local_points[5], reference.local_points[7], u, v);
    return GeoPdfPagePoint{
        reference.viewport_box[0] + local_x *
            (reference.viewport_box[2] - reference.viewport_box[0]),
        reference.viewport_box[1] + local_y *
            (reference.viewport_box[3] - reference.viewport_box[1])};
}

} // namespace openefb
