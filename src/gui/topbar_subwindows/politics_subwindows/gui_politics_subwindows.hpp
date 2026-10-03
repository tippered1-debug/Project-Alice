#pragma once

#include "dcon_generated_ids.hpp"
#include "system_state_forward.hpp"
#include "text.hpp"
#include "gui_element_types.hpp"

#include <string>
#include <string_view>

namespace ui {

struct release_query_wrapper {
	dcon::nation_id content;
};
struct release_emplace_wrapper {
	dcon::nation_id content;
};

void produce_decision_substitutions(sys::state& state, text::substitution_map& m, dcon::nation_id n);
class decision_window : public window_element_base {
public:
	void on_create(sys::state& state) noexcept override;
	std::unique_ptr<element_base> make_child(sys::state& state, std::string_view name, dcon::gui_def_id id) noexcept override;
};

class release_nation_window : public window_element_base {
public:
	void on_create(sys::state& state) noexcept override;
	std::unique_ptr<element_base> make_child(sys::state& state, std::string_view name, dcon::gui_def_id id) noexcept override;
};

std::unique_ptr<element_base> make_release_nation_window(sys::state& state, std::string_view name);

}
