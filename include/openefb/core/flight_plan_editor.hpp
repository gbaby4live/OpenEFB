#pragma once

#include "openefb/core/flight_plan_model.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace openefb {

class FlightPlanEditor final {
public:
    static constexpr std::size_t maximum_legs = 100;
    static constexpr std::size_t maximum_identifier_length = 8;

    bool begin(const FlightPlanSnapshot& source);
    void cancel() noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool dirty() const noexcept;
    [[nodiscard]] int selected_index() const noexcept;
    [[nodiscard]] const std::vector<FlightPlanLeg>& legs() const noexcept;
    [[nodiscard]] std::string_view input() const noexcept;
    [[nodiscard]] std::string_view message() const noexcept;
    [[nodiscard]] const FlightPlanLeg* departure() const noexcept;
    [[nodiscard]] const FlightPlanLeg* destination() const noexcept;

    bool select(int index) noexcept;
    bool append_input(char character);
    bool backspace_input() noexcept;
    void clear_input() noexcept;
    bool insert_after_selection(FlightPlanLeg leg);
    bool set_departure(FlightPlanLeg leg);
    bool set_destination(FlightPlanLeg leg);
    bool remove_selected();
    bool move_selected_up();
    bool move_selected_down();

    [[nodiscard]] bool source_unchanged(const FlightPlanSnapshot& current) const noexcept;
    void set_message(std::string message);
    void mark_applied(const FlightPlanSnapshot& current);

private:
    void normalize_indices() noexcept;
    [[nodiscard]] static bool equivalent(const std::vector<FlightPlanLeg>& first,
                                         const std::vector<FlightPlanLeg>& second) noexcept;

    bool active_{false};
    bool dirty_{false};
    int selected_index_{-1};
    std::vector<FlightPlanLeg> source_legs_;
    std::vector<FlightPlanLeg> legs_;
    std::string input_;
    std::string message_;
    bool departure_assigned_{false};
    bool destination_assigned_{false};
};

} // namespace openefb
