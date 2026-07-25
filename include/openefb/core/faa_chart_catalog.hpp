#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace openefb {

struct FaaChart {
    std::string code;
    std::string name;
    std::string pdf_name;
};

[[nodiscard]] std::string faa_cycle_for_date(int year, unsigned month, unsigned day);
[[nodiscard]] std::vector<FaaChart> parse_faa_chart_catalog(std::string_view xml,
                                                            std::string_view airport_identifier);

} // namespace openefb
