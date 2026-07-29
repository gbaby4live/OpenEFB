#include "openefb/core/faa_chart_catalog.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>

namespace openefb {
namespace {

long long floor_div(long long value, long long divisor) {
    if (value >= 0) return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

std::string xml_value(std::string_view record, std::string_view tag) {
    const std::string opening = "<" + std::string(tag) + ">";
    const std::string closing = "</" + std::string(tag) + ">";
    const auto start = record.find(opening);
    if (start == std::string_view::npos) return {};
    const auto value_start = start + opening.size();
    const auto end = record.find(closing, value_start);
    if (end == std::string_view::npos) return {};
    std::string value = trim(std::string(record.substr(value_start, end - value_start)));
    const std::pair<std::string_view, std::string_view> entities[]{
        {"&amp;", "&"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&lt;", "<"}, {"&gt;", ">"},
    };
    for (const auto& [encoded, decoded] : entities) {
        std::size_t position = 0;
        while ((position = value.find(encoded, position)) != std::string::npos) {
            value.replace(position, encoded.size(), decoded);
            position += decoded.size();
        }
    }
    return value;
}

std::string xml_attribute(std::string_view tag, std::string_view attribute) {
    const std::string marker = std::string(attribute) + "=";
    auto position = tag.find(marker);
    if (position == std::string_view::npos) return {};
    position += marker.size();
    while (position < tag.size() && std::isspace(static_cast<unsigned char>(tag[position]))) ++position;
    if (position >= tag.size() || (tag[position] != '\"' && tag[position] != '\'')) return {};
    const char quote = tag[position++];
    const auto end = tag.find(quote, position);
    return end == std::string_view::npos ? std::string{} : trim(std::string(tag.substr(position, end - position)));
}

bool safe_pdf_name(const std::string& value) {
    if (value.size() < 5 || value.size() > 96) return false;
    if (uppercase(value.substr(value.size() - 4)) != ".PDF") return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '_' || character == '-' || character == '.';
    });
}

} // namespace

std::string faa_cycle_for_date(int year, unsigned month, unsigned day) {
    using namespace std::chrono;
    const year_month_day requested{std::chrono::year{year}, std::chrono::month{month},
                                   std::chrono::day{day}};
    if (!requested.ok()) return {};
    const sys_days anchor = std::chrono::year{2026} / January / 22;
    const auto delta = (sys_days{requested} - anchor).count();
    const long long global_cycle = floor_div(delta, 28);
    const long long year_offset = floor_div(global_cycle, 13);
    const int cycle = static_cast<int>(global_cycle - year_offset * 13 + 1);
    const int cycle_year = 26 + static_cast<int>(year_offset);
    char value[8]{};
    std::snprintf(value, sizeof(value), "%02d%02d", (cycle_year % 100 + 100) % 100, cycle);
    return value;
}

std::vector<FaaChart> parse_faa_chart_catalog(std::string_view xml,
                                              std::string_view airport_identifier) {
    std::vector<FaaChart> charts;
    const std::string wanted = uppercase(trim(std::string(airport_identifier)));
    std::size_t airport_position = 0;
    while ((airport_position = xml.find("<airport_name", airport_position)) != std::string_view::npos) {
        const auto tag_end = xml.find('>', airport_position);
        if (tag_end == std::string_view::npos) break;
        const auto tag = xml.substr(airport_position, tag_end - airport_position + 1);
        const std::string icao_identifier = uppercase(xml_attribute(tag, "icao_ident"));
        const std::string faa_identifier = uppercase(xml_attribute(tag, "apt_ident"));
        const std::string domestic_identifier = wanted.size() == 4 && wanted.front() == 'K'
            ? wanted.substr(1) : wanted;
        if (icao_identifier != wanted && faa_identifier != wanted &&
            faa_identifier != domestic_identifier) {
            airport_position = tag_end + 1;
            continue;
        }
        const auto airport_end = xml.find("</airport_name>", tag_end + 1);
        if (airport_end == std::string_view::npos) break;
        const auto airport = xml.substr(tag_end + 1, airport_end - tag_end - 1);
        std::size_t record_position = 0;
        while (charts.size() < 100 &&
               (record_position = airport.find("<record>", record_position)) != std::string_view::npos) {
            const auto record_end = airport.find("</record>", record_position);
            if (record_end == std::string_view::npos) break;
            const auto record = airport.substr(record_position, record_end - record_position);
            FaaChart chart{xml_value(record, "chart_code"), xml_value(record, "chart_name"),
                           xml_value(record, "pdf_name")};
            const std::string action = uppercase(xml_value(record, "useraction"));
            if (safe_pdf_name(chart.pdf_name) && action != "D" &&
                uppercase(chart.pdf_name) != "DELETED_JOB.PDF" &&
                uppercase(chart.pdf_name) != "DEL_APT_SERVED.PDF") {
                charts.push_back(std::move(chart));
            }
            record_position = record_end + 9;
        }
        break;
    }
    return charts;
}

} // namespace openefb
