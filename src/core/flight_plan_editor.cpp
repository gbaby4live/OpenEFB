#include "openefb/core/flight_plan_editor.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace openefb {

bool FlightPlanEditor::begin(const FlightPlanSnapshot& source) {
    if (!source.available) {
        return false;
    }
    active_ = true;
    dirty_ = false;
    source_legs_ = complete_flight_plan_legs(source);
    legs_ = source_legs_;
    departure_assigned_ = !legs_.empty();
    destination_assigned_ = legs_.size() >= 2;
    selected_index_ = legs_.empty() ? -1 : 0;
    input_.clear();
    message_ = "Draft ready - enter an airport, fix, VOR, or NDB identifier";
    normalize_indices();
    return true;
}

void FlightPlanEditor::cancel() noexcept {
    active_ = false;
    dirty_ = false;
    selected_index_ = -1;
    source_legs_.clear();
    legs_.clear();
    input_.clear();
    message_.clear();
    departure_assigned_ = false;
    destination_assigned_ = false;
}

bool FlightPlanEditor::active() const noexcept { return active_; }
bool FlightPlanEditor::dirty() const noexcept { return dirty_; }
int FlightPlanEditor::selected_index() const noexcept { return selected_index_; }
const std::vector<FlightPlanLeg>& FlightPlanEditor::legs() const noexcept { return legs_; }
std::string_view FlightPlanEditor::input() const noexcept { return input_; }
std::string_view FlightPlanEditor::message() const noexcept { return message_; }

const FlightPlanLeg* FlightPlanEditor::departure() const noexcept {
    return departure_assigned_ && !legs_.empty() ? &legs_.front() : nullptr;
}

const FlightPlanLeg* FlightPlanEditor::destination() const noexcept {
    return destination_assigned_ && !legs_.empty() ? &legs_.back() : nullptr;
}

bool FlightPlanEditor::select(int index) noexcept {
    if (!active_ || index < 0 || index >= static_cast<int>(legs_.size())) {
        return false;
    }
    selected_index_ = index;
    return true;
}

bool FlightPlanEditor::append_input(char character) {
    if (!active_ || input_.size() >= maximum_identifier_length) {
        return false;
    }
    const auto value = static_cast<unsigned char>(character);
    if (!std::isalnum(value)) {
        return false;
    }
    input_.push_back(static_cast<char>(std::toupper(value)));
    return true;
}

bool FlightPlanEditor::backspace_input() noexcept {
    if (!active_ || input_.empty()) {
        return false;
    }
    input_.pop_back();
    return true;
}

void FlightPlanEditor::clear_input() noexcept { input_.clear(); }

bool FlightPlanEditor::insert_after_selection(FlightPlanLeg leg) {
    if (!active_ || legs_.size() >= maximum_legs || leg.identifier.empty()) {
        return false;
    }
    int insertion = selected_index_ < 0 ? static_cast<int>(legs_.size())
                                         : selected_index_ + 1;
    if (destination_assigned_) {
        insertion = std::min(insertion, static_cast<int>(legs_.size()) - 1);
    }
    legs_.insert(legs_.begin() + insertion, std::move(leg));
    selected_index_ = insertion;
    dirty_ = true;
    input_.clear();
    normalize_indices();
    return true;
}

bool FlightPlanEditor::set_departure(FlightPlanLeg leg) {
    if (!active_ || leg.identifier.empty() ||
        (!departure_assigned_ && legs_.size() >= maximum_legs)) {
        return false;
    }
    if (departure_assigned_ && !legs_.empty()) {
        legs_.front() = std::move(leg);
    } else {
        legs_.insert(legs_.begin(), std::move(leg));
        departure_assigned_ = true;
    }
    selected_index_ = 0;
    dirty_ = true;
    input_.clear();
    normalize_indices();
    return true;
}

bool FlightPlanEditor::set_destination(FlightPlanLeg leg) {
    if (!active_ || leg.identifier.empty() ||
        (!destination_assigned_ && legs_.size() >= maximum_legs)) {
        return false;
    }
    if (destination_assigned_ && !legs_.empty()) {
        legs_.back() = std::move(leg);
    } else {
        legs_.push_back(std::move(leg));
        destination_assigned_ = true;
    }
    selected_index_ = static_cast<int>(legs_.size()) - 1;
    dirty_ = true;
    input_.clear();
    normalize_indices();
    return true;
}

bool FlightPlanEditor::remove_selected() {
    if (!active_ || selected_index_ < 0 || selected_index_ >= static_cast<int>(legs_.size())) {
        return false;
    }
    const bool removed_departure = departure_assigned_ && selected_index_ == 0;
    const bool removed_destination = destination_assigned_ &&
                                     selected_index_ == static_cast<int>(legs_.size()) - 1;
    legs_.erase(legs_.begin() + selected_index_);
    departure_assigned_ = departure_assigned_ && !removed_departure;
    destination_assigned_ = destination_assigned_ && !removed_destination;
    if (legs_.empty()) {
        selected_index_ = -1;
    } else if (selected_index_ >= static_cast<int>(legs_.size())) {
        selected_index_ = static_cast<int>(legs_.size()) - 1;
    }
    dirty_ = true;
    normalize_indices();
    return true;
}

bool FlightPlanEditor::move_selected_up() {
    const int first_movable = departure_assigned_ ? 1 : 0;
    if (!active_ || selected_index_ <= first_movable ||
        selected_index_ >= static_cast<int>(legs_.size()) ||
        (destination_assigned_ && selected_index_ == static_cast<int>(legs_.size()) - 1)) {
        return false;
    }
    std::swap(legs_[selected_index_], legs_[selected_index_ - 1]);
    --selected_index_;
    dirty_ = true;
    normalize_indices();
    return true;
}

bool FlightPlanEditor::move_selected_down() {
    const int last_movable = static_cast<int>(legs_.size()) - (destination_assigned_ ? 2 : 1);
    if (!active_ || selected_index_ < 0 || selected_index_ >= last_movable ||
        (departure_assigned_ && selected_index_ == 0)) {
        return false;
    }
    std::swap(legs_[selected_index_], legs_[selected_index_ + 1]);
    ++selected_index_;
    dirty_ = true;
    normalize_indices();
    return true;
}

bool FlightPlanEditor::source_unchanged(const FlightPlanSnapshot& current) const noexcept {
    return active_ && current.available &&
           equivalent(source_legs_, complete_flight_plan_legs(current));
}

void FlightPlanEditor::set_message(std::string message) { message_ = std::move(message); }

void FlightPlanEditor::mark_applied(const FlightPlanSnapshot& current) {
    source_legs_ = complete_flight_plan_legs(current);
    legs_ = source_legs_;
    departure_assigned_ = !legs_.empty();
    destination_assigned_ = legs_.size() >= 2;
    selected_index_ = legs_.empty() ? -1 : std::min(selected_index_, static_cast<int>(legs_.size()) - 1);
    dirty_ = false;
    input_.clear();
    message_ = "Route applied to X-Plane FMS";
    normalize_indices();
}

void FlightPlanEditor::normalize_indices() noexcept {
    for (std::size_t index = 0; index < legs_.size(); ++index) {
        legs_[index].index = static_cast<int>(index);
        legs_[index].active = false;
    }
}

bool FlightPlanEditor::equivalent(const std::vector<FlightPlanLeg>& first,
                                  const std::vector<FlightPlanLeg>& second) noexcept {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.size(); ++index) {
        const auto& a = first[index];
        const auto& b = second[index];
        if (a.identifier != b.identifier || a.kind != b.kind ||
            a.altitude_feet != b.altitude_feet ||
            std::abs(a.latitude_degrees - b.latitude_degrees) > 0.000001 ||
            std::abs(a.longitude_degrees - b.longitude_degrees) > 0.000001) {
            return false;
        }
    }
    return true;
}

} // namespace openefb
