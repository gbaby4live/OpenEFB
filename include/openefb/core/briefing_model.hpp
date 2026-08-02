#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace openefb {

enum class BriefingTab { summary, library, checklist, notes };
enum class LibraryCategory { chart, document };
enum class ChecklistPhase { preflight, takeoff_cruise, descent_landing };

struct LibraryEntry {
    LibraryCategory category{LibraryCategory::document};
    std::string name;
    std::string path;
    std::string text_content;
};

struct ChecklistItem {
    std::string label;
    bool checked{false};
};

class BriefingModel final {
public:
    BriefingModel();
    [[nodiscard]] BriefingTab active_tab() const noexcept;
    void select_tab(BriefingTab tab) noexcept;

    void update_library(std::vector<LibraryEntry> entries, std::string message = {});
    [[nodiscard]] const std::vector<LibraryEntry>& library() const noexcept;
    [[nodiscard]] const LibraryEntry* selected_entry() const noexcept;
    [[nodiscard]] int selected_entry_index() const noexcept;
    bool select_entry(int index) noexcept;
    [[nodiscard]] std::string_view library_message() const noexcept;
    void select_library_airport(std::string identifier);
    [[nodiscard]] std::string_view library_airport() const noexcept;

    [[nodiscard]] const std::vector<ChecklistItem>& checklist() const noexcept;
    [[nodiscard]] ChecklistPhase checklist_phase() const noexcept;
    void select_checklist_phase(ChecklistPhase phase) noexcept;
    void configure_checklist_for_aircraft(std::string_view aircraft_name);
    [[nodiscard]] std::string_view checklist_aircraft() const noexcept;
    bool toggle_checklist_item(std::size_t index) noexcept;
    void reset_checklist() noexcept;
    [[nodiscard]] std::size_t completed_checklist_items() const noexcept;

    void set_notes(std::string notes);
    bool append_note(char character);
    bool backspace_note() noexcept;
    void clear_notes() noexcept;
    [[nodiscard]] std::string_view notes() const noexcept;

private:
    BriefingTab active_tab_{BriefingTab::summary};
    std::vector<LibraryEntry> library_;
    int selected_entry_index_{-1};
    std::string library_message_;
    std::string library_airport_;
    std::vector<ChecklistItem> checklist_;
    ChecklistPhase checklist_phase_{ChecklistPhase::preflight};
    std::string checklist_aircraft_{"General aircraft"};
    std::string notes_;
};

} // namespace openefb
