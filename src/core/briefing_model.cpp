#include "openefb/core/briefing_model.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace openefb {
namespace { constexpr std::size_t maximum_note_length = 2000; }

BriefingModel::BriefingModel()
    : checklist_{{"Flight plan reviewed"}, {"Departure and destination weather reviewed"},
                 {"Fuel and reserve checked"}, {"Weight and balance checked"},
                 {"Required charts available"}, {"Aircraft documents reviewed"}} {}

BriefingTab BriefingModel::active_tab() const noexcept { return active_tab_; }
void BriefingModel::select_tab(BriefingTab tab) noexcept { active_tab_ = tab; }

void BriefingModel::update_library(std::vector<LibraryEntry> entries, std::string message) {
    const std::string selected_path = selected_entry() ? selected_entry()->path : std::string{};
    library_ = std::move(entries);
    library_message_ = std::move(message);
    selected_entry_index_ = -1;
    if (!selected_path.empty()) {
        const auto selected = std::find_if(library_.begin(), library_.end(), [&](const auto& entry) {
            return entry.path == selected_path;
        });
        if (selected != library_.end())
            selected_entry_index_ = static_cast<int>(std::distance(library_.begin(), selected));
    }
    if (selected_entry_index_ < 0 && !library_.empty()) selected_entry_index_ = 0;
}

const std::vector<LibraryEntry>& BriefingModel::library() const noexcept { return library_; }

const LibraryEntry* BriefingModel::selected_entry() const noexcept {
    if (selected_entry_index_ < 0 || selected_entry_index_ >= static_cast<int>(library_.size())) return nullptr;
    return &library_[static_cast<std::size_t>(selected_entry_index_)];
}

int BriefingModel::selected_entry_index() const noexcept { return selected_entry_index_; }

bool BriefingModel::select_entry(int index) noexcept {
    if (index < 0 || index >= static_cast<int>(library_.size())) return false;
    selected_entry_index_ = index;
    return true;
}

std::string_view BriefingModel::library_message() const noexcept { return library_message_; }

void BriefingModel::select_library_airport(std::string identifier) {
    std::transform(identifier.begin(), identifier.end(), identifier.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    library_airport_ = std::move(identifier);
}

std::string_view BriefingModel::library_airport() const noexcept { return library_airport_; }
const std::vector<ChecklistItem>& BriefingModel::checklist() const noexcept { return checklist_; }

bool BriefingModel::toggle_checklist_item(std::size_t index) noexcept {
    if (index >= checklist_.size()) return false;
    checklist_[index].checked = !checklist_[index].checked;
    return true;
}

void BriefingModel::reset_checklist() noexcept {
    for (auto& item : checklist_) item.checked = false;
}

std::size_t BriefingModel::completed_checklist_items() const noexcept {
    return static_cast<std::size_t>(std::count_if(checklist_.begin(), checklist_.end(),
                                                  [](const auto& item) { return item.checked; }));
}

void BriefingModel::set_notes(std::string notes) {
    if (notes.size() > maximum_note_length) notes.resize(maximum_note_length);
    notes.erase(std::remove(notes.begin(), notes.end(), '\r'), notes.end());
    notes_ = std::move(notes);
}

bool BriefingModel::append_note(char character) {
    const auto value = static_cast<unsigned char>(character);
    if (notes_.size() >= maximum_note_length || (character != '\n' && !std::isprint(value))) return false;
    notes_.push_back(character);
    return true;
}

bool BriefingModel::backspace_note() noexcept {
    if (notes_.empty()) return false;
    notes_.pop_back();
    return true;
}

void BriefingModel::clear_notes() noexcept { notes_.clear(); }
std::string_view BriefingModel::notes() const noexcept { return notes_; }

} // namespace openefb
