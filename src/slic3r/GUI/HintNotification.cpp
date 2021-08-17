#include "HintNotification.hpp"
#include "ImGuiWrapper.hpp"
#include "format.hpp"
#include "I18N.hpp"
#include "GUI_ObjectList.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <map>

namespace Slic3r {
namespace GUI {

const std::string BOLD_MARKER_START      = "<b>"; 
const std::string BOLD_MARKER_END        = "</b>";
const std::string HYPERTEXT_MARKER_START = "<a>";
const std::string HYPERTEXT_MARKER_END   = "</a>";

namespace {
inline void push_style_color(ImGuiCol idx, const ImVec4& col, bool fading_out, float current_fade_opacity)
{
	if (fading_out)
		ImGui::PushStyleColor(idx, ImVec4(col.x, col.y, col.z, col.w * current_fade_opacity));
	else
		ImGui::PushStyleColor(idx, col);
}
enum TagCheckResult
{
	TagCheckAffirmative,
	TagCheckNegative,
	TagCheckNotCompatible
};
// returns if in mode defined by tag
TagCheckResult tag_check_mode(const std::string& tag)
{
	std::vector<std::string> allowed_tags = {"simple", "advanced", "expert"};
	if (std::find(allowed_tags.begin(), allowed_tags.end(), tag) != allowed_tags.end())
	{
		ConfigOptionMode config_mode = wxGetApp().get_mode();
		if (config_mode == ConfigOptionMode::comSimple)        return (tag == "simple"   ? TagCheckAffirmative : TagCheckNegative);
		else if (config_mode == ConfigOptionMode::comAdvanced) return (tag == "advanced" ? TagCheckAffirmative : TagCheckNegative);
		else if (config_mode == ConfigOptionMode::comExpert)   return (tag == "expert"   ? TagCheckAffirmative : TagCheckNegative);
	}
	return TagCheckNotCompatible;
}

TagCheckResult tag_check_tech(const std::string& tag)
{
	std::vector<std::string> allowed_tags = { "FFF", "MMU", "SLA" };
	if (std::find(allowed_tags.begin(), allowed_tags.end(), tag) != allowed_tags.end()) {
		const PrinterTechnology tech = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology();
		if (tech == ptFFF) {
			// MMU / FFF
			bool is_mmu = wxGetApp().extruders_edited_cnt() > 1;
			if (tag == "MMU") return (is_mmu ? TagCheckAffirmative : TagCheckNegative);
			return (tag == "FFF" ? TagCheckAffirmative : TagCheckNegative);
		} else {
			// SLA
			return (tag == "SLA" ? TagCheckAffirmative : TagCheckNegative);
		}
	}
	return TagCheckNotCompatible;
}

TagCheckResult tag_check_system(const std::string& tag)
{
	std::vector<std::string> allowed_tags = { "Windows", "Linux", "OSX" };
	if (std::find(allowed_tags.begin(), allowed_tags.end(), tag) != allowed_tags.end()) {
		if (tag =="Windows")
#ifdef WIN32
			return TagCheckAffirmative;
#else 
			return TagCheckNegative;
#endif // WIN32

		if (tag == "Linux")
#ifdef __linux__
			return TagCheckAffirmative;
#else 
			return TagCheckNegative;
#endif // __linux__

		if (tag == "OSX")
#ifdef __APPLE__
			return TagCheckAffirmative;
#else 
			return TagCheckNegative;
#endif // __apple__
	}
	return TagCheckNotCompatible;
}

// return true if NOT in disabled mode.
bool tags_check(const std::string& disabled_tags, const std::string& enabled_tags)
{
	if (disabled_tags.empty() && enabled_tags.empty())
		return true;
	// enabled tags must ALL return affirmative or check fails
	if (!enabled_tags.empty()) {
		std::string tag;
		for (size_t i = 0; i < enabled_tags.size(); i++) {
			if (enabled_tags[i] == ' ') {
				tag.erase();
				continue;
			}
			if (enabled_tags[i] != ';') {
				tag += enabled_tags[i];
			}
			if (enabled_tags[i] == ';' || i == enabled_tags.size() - 1) {
				if (!tag.empty()) {
					TagCheckResult result;
					result = tag_check_mode(tag);
					if (result == TagCheckResult::TagCheckNegative)
						return false;
					if (result == TagCheckResult::TagCheckAffirmative)
						continue;
					result = tag_check_tech(tag);
					if (result == TagCheckResult::TagCheckNegative)
						return false;
					if (result == TagCheckResult::TagCheckAffirmative)
						continue;
					result = tag_check_system(tag);
					if (result == TagCheckResult::TagCheckNegative)
						return false;
					if (result == TagCheckResult::TagCheckAffirmative)
						continue;
					BOOST_LOG_TRIVIAL(error) << "Hint Notification: Tag " << tag << " in enabled_tags not compatible.";
					// non compatible in enabled means return false since all enabled must be affirmative.
					return false;
				}
			}
		}
	}
	// disabled tags must all NOT return affirmative or check fails
	if (!disabled_tags.empty()) {
		std::string tag;
		for (size_t i = 0; i < disabled_tags.size(); i++) {
			if (disabled_tags[i] == ' ') {
				tag.erase();
				continue;
			}
			if (disabled_tags[i] != ';') {
				tag += disabled_tags[i];
			}
			if (disabled_tags[i] == ';' || i == disabled_tags.size() - 1) {
				if (!tag.empty()) {
					TagCheckResult result;
					result = tag_check_mode(tag);
					if (result == TagCheckResult::TagCheckNegative)
						continue;
					if (result == TagCheckResult::TagCheckAffirmative)
						return false;
					result = tag_check_tech(tag);
					if (result == TagCheckResult::TagCheckNegative)
						continue;
					if (result == TagCheckResult::TagCheckAffirmative)
						return false;
					result = tag_check_system(tag);
					if (result == TagCheckResult::TagCheckAffirmative)
						return false;
					if (result == TagCheckResult::TagCheckNegative)
						continue;
					BOOST_LOG_TRIVIAL(error) << "Hint Notification: Tag " << tag << " in disabled_tags not compatible.";
				}
			}
		}
	}
	return true;
}
void launch_browser_if_allowed(const std::string& url)
{
	if (wxGetApp().app_config->get("suppress_hyperlinks") != "1")
		wxLaunchDefaultBrowser(url);
}
} //namespace

void HintDatabase::init()
{
		
	load_hints_from_file(std::move(boost::filesystem::path(resources_dir()) / "data" / "hints.ini"));
		
	const AppConfig* app_config = wxGetApp().app_config;
	m_hint_id = std::atoi(app_config->get("last_hint").c_str());
    m_initialized = true;

}
void HintDatabase::load_hints_from_file(const boost::filesystem::path& path)
{
	namespace pt = boost::property_tree;
	pt::ptree tree;
 	boost::nowide::ifstream ifs(path.string());
	try {
		pt::read_ini(ifs, tree);
	}
	catch (const boost::property_tree::ini_parser::ini_parser_error& err) {
		throw Slic3r::RuntimeError(format("Failed loading hints file \"%1%\"\nError: \"%2%\" at line %3%", path, err.message(), err.line()).c_str());
	}

 	for (const auto& section : tree) {
		if (boost::starts_with(section.first, "hint:")) {
			// create std::map with tree data 
			std::map<std::string, std::string> dict;
			for (const auto& data : section.second) {
				dict.emplace(data.first, data.second.data());
			}
			
			//unescaping and translating all texts and saving all data common for all hint types 
			std::string fulltext;
			std::string text1;
			std::string hypertext_text;
			std::string follow_text;
			std::string disabled_tags;
			std::string enabled_tags;
			std::string documentation_link;
			//unescape text1
			unescape_string_cstyle(_utf8(dict["text"]), fulltext);
			// replace <b> and </b> for imgui markers
			std::string marker_s(1, ImGui::ColorMarkerStart);
			std::string marker_e(1, ImGui::ColorMarkerEnd);
			// start marker
			size_t marker_pos = fulltext.find(BOLD_MARKER_START);
			while (marker_pos != std::string::npos) {
				fulltext.replace(marker_pos, 3, marker_s);
				marker_pos = fulltext.find(BOLD_MARKER_START, marker_pos);
			}
			// end marker
			marker_pos = fulltext.find(BOLD_MARKER_END);
			while (marker_pos != std::string::npos) {
				fulltext.replace(marker_pos, 4, marker_e);
				marker_pos = fulltext.find(BOLD_MARKER_END, marker_pos);
			}
			// divide fulltext
			size_t hypertext_start = fulltext.find(HYPERTEXT_MARKER_START);
			if (hypertext_start != std::string::npos) {
				//hypertext exists
				fulltext.erase(hypertext_start, HYPERTEXT_MARKER_START.size());
				if (fulltext.find(HYPERTEXT_MARKER_START) != std::string::npos) {
					// This must not happen - only 1 hypertext allowed
					BOOST_LOG_TRIVIAL(error) << "Hint notification with multiple hypertexts: " << _utf8(dict["text"]);
					continue;
				}
				size_t hypertext_end = fulltext.find(HYPERTEXT_MARKER_END);
				if (hypertext_end == std::string::npos) {
					// hypertext was not correctly ended
					BOOST_LOG_TRIVIAL(error) << "Hint notification without hypertext end marker: " << _utf8(dict["text"]);
					continue;
				}
				fulltext.erase(hypertext_end, HYPERTEXT_MARKER_END.size());
				if (fulltext.find(HYPERTEXT_MARKER_END) != std::string::npos) {
					// This must not happen - only 1 hypertext end allowed
					BOOST_LOG_TRIVIAL(error) << "Hint notification with multiple hypertext end markers: " << _utf8(dict["text"]);
					continue;
				}
				
				text1          = fulltext.substr(0, hypertext_start);
				hypertext_text = fulltext.substr(hypertext_start, hypertext_end - hypertext_start);
				follow_text    = fulltext.substr(hypertext_end);
			} else {
				text1 = fulltext;
			}
			
			if (dict.find("disabled_tags") != dict.end()) {
				disabled_tags = dict["disabled_tags"];
			}
			if (dict.find("enabled_tags") != dict.end()) {
				enabled_tags = dict["enabled_tags"];
			}
			if (dict.find("documentation_link") != dict.end()) {
				documentation_link = dict["documentation_link"];
			}

			// create HintData
			if (dict.find("hypertext_type") != dict.end()) {
				//link to internet
				if(dict["hypertext_type"] == "link") {
					std::string	hypertext_link = dict["hypertext_link"];
					HintData	hint_data{ text1, hypertext_text, follow_text, disabled_tags, enabled_tags, false, documentation_link, [hypertext_link]() { launch_browser_if_allowed(hypertext_link); }  };
					m_loaded_hints.emplace_back(hint_data);
				// highlight settings
				} else if (dict["hypertext_type"] == "settings") {
					std::string		opt = dict["hypertext_settings_opt"];
					Preset::Type	type = static_cast<Preset::Type>(std::atoi(dict["hypertext_settings_type"].c_str()));
					std::wstring	category = boost::nowide::widen(dict["hypertext_settings_category"]);
					HintData		hint_data{ text1, hypertext_text, follow_text, disabled_tags, enabled_tags, true, documentation_link, [opt, type, category]() { GUI::wxGetApp().sidebar().jump_to_option(opt, type, category); } };
					m_loaded_hints.emplace_back(hint_data);
				// open preferences
				} else if(dict["hypertext_type"] == "preferences") {
					int			page = static_cast<Preset::Type>(std::atoi(dict["hypertext_preferences_page"].c_str()));
					HintData	hint_data{ text1, hypertext_text, follow_text, disabled_tags, enabled_tags, false, documentation_link, [page]() { wxGetApp().open_preferences(page); } };
					m_loaded_hints.emplace_back(hint_data);

				} else if (dict["hypertext_type"] == "plater") {
					std::string	item = dict["hypertext_plater_item"];
					HintData	hint_data{ text1, hypertext_text, follow_text, disabled_tags, enabled_tags, true, documentation_link, [item]() { wxGetApp().plater()->canvas3D()->highlight_toolbar_item(item); } };
					m_loaded_hints.emplace_back(hint_data);
				} else if (dict["hypertext_type"] == "gizmo") {
					std::string	item = dict["hypertext_gizmo_item"];
					HintData	hint_data{ text1, hypertext_text, follow_text, disabled_tags, enabled_tags, true, documentation_link, [item]() { wxGetApp().plater()->canvas3D()->highlight_gizmo(item); } };
					m_loaded_hints.emplace_back(hint_data);
				}
				else if (dict["hypertext_type"] == "gallery") {
					HintData	hint_data{ text1, hypertext_text, follow_text, disabled_tags, enabled_tags, false, documentation_link, []() {  wxGetApp().obj_list()->load_shape_object_from_gallery(); } };
					m_loaded_hints.emplace_back(hint_data);
				}
			} else {
				// plain text without hypertext
				HintData hint_data{ text1, hypertext_text, follow_text, disabled_tags, enabled_tags, false, documentation_link };
				m_loaded_hints.emplace_back(hint_data);
			}
		}
	}
}
HintData* HintDatabase::get_hint(bool up)
{
    if (! m_initialized) {
        init();
        //return false;
    }
	if (m_loaded_hints.empty())
	{
		BOOST_LOG_TRIVIAL(error) << "There were no hints loaded from hints.ini file.";
		return nullptr;
	}

    // shift id
    m_hint_id = (up ? m_hint_id + 1 : m_hint_id );
    m_hint_id %= m_loaded_hints.size();

	AppConfig* app_config = wxGetApp().app_config;
	app_config->set("last_hint", std::to_string(m_hint_id));

	//data = &m_loaded_hints[m_hint_id];
	/*
    data.text = m_loaded_hints[m_hint_id].text;
    data.hypertext = m_loaded_hints[m_hint_id].hypertext;
	data.follow_text = m_loaded_hints[m_hint_id].follow_text;
    data.callback = m_loaded_hints[m_hint_id].callback;
	*/
    return &m_loaded_hints[m_hint_id];
}

void NotificationManager::HintNotification::count_spaces()
{
	//determine line width 
	m_line_height = ImGui::CalcTextSize("A").y;

	
	std::string text;
	text = ImGui::WarningMarker; 
	float picture_width = ImGui::CalcTextSize(text.c_str()).x;
	m_left_indentation = picture_width * 1.5f + m_line_height / 2;
	
	// no left button picture
	//m_left_indentation = m_line_height;

	if (m_documentation_link.empty())
		m_window_width_offset = m_left_indentation + m_line_height * 3.f;
	else 
		m_window_width_offset = m_left_indentation + m_line_height * 5.5f;

	m_window_width = m_line_height * 25;
}

void NotificationManager::HintNotification::count_lines()
{
	std::string text = m_text1;
	size_t      last_end = 0;
	m_lines_count = 0;

	if (text.empty())
		return;

	m_endlines.clear();
	while (last_end < text.length() - 1)
	{
		size_t next_hard_end = text.find_first_of('\n', last_end);
		if (next_hard_end != std::string::npos && ImGui::CalcTextSize(text.substr(last_end, next_hard_end - last_end).c_str()).x < m_window_width - m_window_width_offset) {
			//next line is ended by '/n'
			m_endlines.push_back(next_hard_end);
			last_end = next_hard_end + 1;
		}
		else {
			// find next suitable endline
			if (ImGui::CalcTextSize(text.substr(last_end).c_str()).x >= m_window_width - m_window_width_offset) {
				// more than one line till end
				size_t next_space = text.find_first_of(' ', last_end);
				if (next_space > 0 && next_space < text.length()) {
					size_t next_space_candidate = text.find_first_of(' ', next_space + 1);
					while (next_space_candidate > 0 && ImGui::CalcTextSize(text.substr(last_end, next_space_candidate - last_end).c_str()).x < m_window_width - m_window_width_offset) {
						next_space = next_space_candidate;
						next_space_candidate = text.find_first_of(' ', next_space + 1);
					}
				} else {
					next_space = text.length();
				}
				// when one word longer than line.
				if (ImGui::CalcTextSize(text.substr(last_end, next_space - last_end).c_str()).x > m_window_width - m_window_width_offset ||
					ImGui::CalcTextSize(text.substr(last_end, next_space - last_end).c_str()).x < (m_window_width - m_window_width_offset) / 5 * 3
				    ) {
					float width_of_a = ImGui::CalcTextSize("a").x;
					int letter_count = (int)((m_window_width - m_window_width_offset) / width_of_a);
					while (last_end + letter_count < text.size() && ImGui::CalcTextSize(text.substr(last_end, letter_count).c_str()).x < m_window_width - m_window_width_offset) {
						letter_count++;
					}
					m_endlines.push_back(last_end + letter_count);
					last_end += letter_count;
				} else {
					m_endlines.push_back(next_space);
					last_end = next_space + 1;
				}
			}
			else {
				m_endlines.push_back(text.length());
				last_end = text.length();
			}

		}
		m_lines_count++;
	}
	int prev_end = m_endlines.size() > 1 ? m_endlines[m_endlines.size() - 2] : 0;
	int size_of_last_line = ImGui::CalcTextSize(text.substr(prev_end, last_end - prev_end).c_str()).x;
	// hypertext calculation
	if (!m_hypertext.empty()) {
		if (size_of_last_line + ImGui::CalcTextSize(m_hypertext.c_str()).x > m_window_width - m_window_width_offset) {
			// hypertext on new line
			size_of_last_line = ImGui::CalcTextSize((m_hypertext + "  ").c_str()).x;
			m_endlines.push_back(last_end);
			m_lines_count++;
		} else {
			size_of_last_line += ImGui::CalcTextSize((m_hypertext + "  ").c_str()).x;
		}
	}
	if (!m_text2.empty()) {
		text						= m_text2;
		last_end					= 0;
		m_endlines2.clear();
		// if size_of_last_line too large to fit anything
		size_t first_end = std::min(text.find_first_of('\n'), text.find_first_of(' '));
		if (size_of_last_line >= m_window_width - m_window_width_offset - ImGui::CalcTextSize(text.substr(0, first_end).c_str()).x) {
			m_endlines2.push_back(0);
			size_of_last_line = 0;
		}
		while (last_end < text.length() - 1)
		{
			size_t next_hard_end = text.find_first_of('\n', last_end);
			if (next_hard_end != std::string::npos && ImGui::CalcTextSize(text.substr(last_end, next_hard_end - last_end).c_str()).x < m_window_width - m_window_width_offset - size_of_last_line) {
				//next line is ended by '/n'
				m_endlines2.push_back(next_hard_end);
				last_end = next_hard_end + 1;
			}
			else {
				// find next suitable endline
				if (ImGui::CalcTextSize(text.substr(last_end).c_str()).x >= m_window_width - m_window_width_offset - size_of_last_line) {
					// more than one line till end
					size_t next_space = text.find_first_of(' ', last_end);
					if (next_space > 0) {
						size_t next_space_candidate = text.find_first_of(' ', next_space + 1);
						while (next_space_candidate > 0 && ImGui::CalcTextSize(text.substr(last_end, next_space_candidate - last_end).c_str()).x < m_window_width - m_window_width_offset - size_of_last_line) {
							next_space = next_space_candidate;
							next_space_candidate = text.find_first_of(' ', next_space + 1);
						}
					}
					else {
						next_space = text.length();
					}
					// when one word longer than line.
					if (ImGui::CalcTextSize(text.substr(last_end, next_space - last_end).c_str()).x > m_window_width - m_window_width_offset - size_of_last_line ||
						ImGui::CalcTextSize(text.substr(last_end, next_space - last_end).c_str()).x + size_of_last_line < (m_window_width - m_window_width_offset) / 5 * 3
						) {
						float width_of_a = ImGui::CalcTextSize("a").x;
						int letter_count = (int)((m_window_width - m_window_width_offset - size_of_last_line) / width_of_a);
						while (last_end + letter_count < text.size() && ImGui::CalcTextSize(text.substr(last_end, letter_count).c_str()).x < m_window_width - m_window_width_offset - size_of_last_line) {
							letter_count++;
						}
						m_endlines2.push_back(last_end + letter_count);
						last_end += letter_count;
					}
					else {
						m_endlines2.push_back(next_space);
						last_end = next_space + 1;
					}
				}
				else {
					m_endlines2.push_back(text.length());
					last_end = text.length();
				}

			}
			if (size_of_last_line == 0) // if first line is continuation of previous text, do not add to line count.
				m_lines_count++;
			size_of_last_line = 0; // should countain value only for first line (with hypertext) 
			
		}
	}
}

void NotificationManager::HintNotification::init()
{
	// Do not init closing notification
	if (is_finished())
		return;

	count_spaces();
	count_lines();

	m_multiline = true;

	m_notification_start = GLCanvas3D::timestamp_now();
	if (m_state == EState::Unknown)
		m_state = EState::Shown;
}

void NotificationManager::HintNotification::set_next_window_size(ImGuiWrapper& imgui)
{
	/*
	m_window_height = m_multiline ?
		(m_lines_count + 1.f) * m_line_height :
		4.f * m_line_height;
	m_window_height += 1 * m_line_height; // top and bottom
	*/

	m_window_height = std::max((m_lines_count + 1.f) * m_line_height, 5.f * m_line_height);
}

bool NotificationManager::HintNotification::on_text_click()
{
	if (m_hypertext_callback != nullptr && (!m_runtime_disable || tags_check(m_disabled_tags, m_enabled_tags)))
		m_hypertext_callback();
	return false;
}

void NotificationManager::HintNotification::render_text(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
    if (!m_has_hint_data) {
        retrieve_data();
	}

	float	x_offset = m_left_indentation;
	int		last_end = 0;
	float	starting_y = (m_lines_count < 4 ? m_line_height / 2 * (4 - m_lines_count + 1) : m_line_height / 2);
	float	shift_y = m_line_height;
	std::string line;

	for (size_t i = 0; i < (m_multiline ? /*m_lines_count*/m_endlines.size() : 2); i++) {
		line.clear();
		ImGui::SetCursorPosX(x_offset);
		ImGui::SetCursorPosY(starting_y + i * shift_y);
		if (m_endlines.size() > i && m_text1.size() >= m_endlines[i]) {
			if (i == 1 && m_endlines.size() > 2 && !m_multiline) {
				// second line with "more" hypertext
				line = m_text1.substr(m_endlines[0] + (m_text1[m_endlines[0]] == '\n' || m_text1[m_endlines[0]] == ' ' ? 1 : 0), m_endlines[1] - m_endlines[0] - (m_text1[m_endlines[0]] == '\n' || m_text1[m_endlines[0]] == ' ' ? 1 : 0));
				while (ImGui::CalcTextSize(line.c_str()).x > m_window_width - m_window_width_offset - ImGui::CalcTextSize((".." + _u8L("More")).c_str()).x) {
					line = line.substr(0, line.length() - 1);
				}
				line += "..";
			} else {
				// regural line
				line = m_text1.substr(last_end, m_endlines[i] - last_end);	
			}
			// first line is headline (for hint notification it must be divided by \n)
			if (m_text1.find('\n') >= m_endlines[i]) {
				line = ImGui::ColorMarkerStart + line + ImGui::ColorMarkerEnd;
			}
			// Add ImGui::ColorMarkerStart if there is ImGui::ColorMarkerEnd first (start was at prev line)
			if (line.find_first_of(ImGui::ColorMarkerEnd) < line.find_first_of(ImGui::ColorMarkerStart)) {
				line = ImGui::ColorMarkerStart + line;
			}

			last_end = m_endlines[i];
			if (m_text1.size() > m_endlines[i])
				last_end += (m_text1[m_endlines[i]] == '\n' || m_text1[m_endlines[i]] == ' ' ? 1 : 0);
			imgui.text(line.c_str());
		}
			
	}
	//hyperlink text
	if (!m_multiline && m_lines_count > 2) {
		render_hypertext(imgui, x_offset + ImGui::CalcTextSize((line + " ").c_str()).x, starting_y + shift_y, _u8L("More"), true);
	} else if (!m_hypertext.empty()) {
		render_hypertext(imgui, x_offset + ImGui::CalcTextSize((line + (line.empty()? "": " ")).c_str()).x, starting_y + (m_endlines.size() - 1) * shift_y, m_hypertext);
	}

	// text2
	if (!m_text2.empty() && m_multiline) {
		starting_y += (m_endlines.size() - 1) * shift_y;
		last_end = 0;
		for (size_t i = 0; i < (m_multiline ? m_endlines2.size() : 2); i++) {
			if (i == 0) //first line X is shifted by hypertext
				ImGui::SetCursorPosX(x_offset + ImGui::CalcTextSize((line + m_hypertext + (line.empty() ? " " : "  ")).c_str()).x);
			else
				ImGui::SetCursorPosX(x_offset);

			ImGui::SetCursorPosY(starting_y + i * shift_y);
			line.clear();
			if (m_endlines2.size() > i && m_text2.size() >= m_endlines2[i]) {

				// regural line
				line = m_text2.substr(last_end, m_endlines2[i] - last_end);

				// Add ImGui::ColorMarkerStart if there is ImGui::ColorMarkerEnd first (start was at prev line)
				if (line.find_first_of(ImGui::ColorMarkerEnd) < line.find_first_of(ImGui::ColorMarkerStart)) {
					line = ImGui::ColorMarkerStart + line;
				}

				last_end = m_endlines2[i];
				if (m_text2.size() > m_endlines2[i])
					last_end += (m_text2[m_endlines2[i]] == '\n' || m_text2[m_endlines2[i]] == ' ' ? 1 : 0);
				imgui.text(line.c_str());
			}

		}
	}
}

void NotificationManager::HintNotification::render_close_button(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	ImVec2 win_size(win_size_x, win_size_y);
	ImVec2 win_pos(win_pos_x, win_pos_y);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.0f, .0f, .0f, .0f));


	std::string button_text;
	button_text = ImGui::CloseNotifButton;

	if (ImGui::IsMouseHoveringRect(ImVec2(win_pos.x - win_size.x / 10.f, win_pos.y),
		ImVec2(win_pos.x, win_pos.y + win_size.y - 2 * m_line_height),
		true))
	{
		button_text = ImGui::CloseNotifHoverButton;
	}
	ImVec2 button_pic_size = ImGui::CalcTextSize(button_text.c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f, button_pic_size.y * 1.25f);
	m_close_b_w = button_size.y;
	if (m_lines_count <= 3) {
		m_close_b_y = win_size.y / 2 - button_size.y * 1.25f;
		ImGui::SetCursorPosX(win_size.x - m_line_height * 2.75f);
		ImGui::SetCursorPosY(m_close_b_y);
	} else {
		ImGui::SetCursorPosX(win_size.x - m_line_height * 2.75f);
		ImGui::SetCursorPosY(win_size.y / 2 - button_size.y);
	}
	if (imgui.button(button_text.c_str(), button_size.x, button_size.y))
	{
		close();
	}
	
	//invisible large button
	ImGui::SetCursorPosX(win_size.x - m_line_height * 2.35f);
	ImGui::SetCursorPosY(0);
	if (imgui.button(" ", m_line_height * 2.125, win_size.y -  2 * m_line_height))
	{
		close();
	}
	
	ImGui::PopStyleColor(5);


	//render_right_arrow_button(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
	render_logo(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
	render_preferences_button(imgui, win_pos_x, win_pos_y);
	if (!m_documentation_link.empty() && wxGetApp().app_config->get("suppress_hyperlinks") != "1")
	{
		render_documentation_button(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
	}
	
}

void NotificationManager::HintNotification::render_preferences_button(ImGuiWrapper& imgui, const float win_pos_x, const float win_pos_y)
{
	
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	push_style_color(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg), m_state == EState::FadingOut, m_current_fade_opacity);
	push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);

	std::string button_text;
	button_text = ImGui::PreferencesButton;
	//hover
	if (ImGui::IsMouseHoveringRect(ImVec2(win_pos_x - m_window_width / 15.f, win_pos_y + m_window_height - 1.75f * m_line_height),
		ImVec2(win_pos_x, win_pos_y + m_window_height),
		true)) {
		button_text = ImGui::PreferencesHoverButton;
		// tooltip
		long time_now = wxGetLocalTime();
		if (m_prefe_hover_time > 0 && m_prefe_hover_time < time_now) {
			ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BACKGROUND);
			ImGui::BeginTooltip();
			imgui.text(_u8L("Open Preferences."));
			ImGui::EndTooltip();
			ImGui::PopStyleColor();
		}
		if (m_prefe_hover_time == 0)
			m_prefe_hover_time = time_now;
	} else
		m_prefe_hover_time = 0;

	ImVec2 button_pic_size = ImGui::CalcTextSize(button_text.c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f, button_pic_size.y * 1.25f);
	ImGui::SetCursorPosX(m_window_width - m_line_height * 1.75f);
	if (m_lines_count <= 3) {
		ImGui::SetCursorPosY(m_close_b_y + m_close_b_w / 4.f * 7.f);
	} else {
		ImGui::SetCursorPosY(m_window_height - button_size.y - m_close_b_w / 4.f);
	}
	if (imgui.button(button_text.c_str(), button_size.x, button_size.y))
	{
		wxGetApp().open_preferences(2);
	}

	ImGui::PopStyleColor(5);
	// preferences button is in place of minimize button
	m_minimize_b_visible = true;	
}
void NotificationManager::HintNotification::render_right_arrow_button(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	// Used for debuging

	ImVec2 win_size(win_size_x, win_size_y);
	ImVec2 win_pos(win_pos_x, win_pos_y);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.0f, .0f, .0f, .0f));

	std::string button_text;
	button_text = ImGui::RightArrowButton;
	
	ImVec2 button_pic_size = ImGui::CalcTextSize(button_text.c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f, button_pic_size.y * 1.25f);

	ImGui::SetCursorPosX(m_window_width - m_line_height * 3.f);
	if (m_lines_count <= 3)
		ImGui::SetCursorPosY(m_close_b_y + m_close_b_w / 4.f * 7.f);
	else
		ImGui::SetCursorPosY(m_window_height - button_size.y - m_close_b_w / 4.f);
	if (imgui.button(button_text.c_str(), button_size.x * 0.8f, button_size.y * 1.f))
	{
		retrieve_data();
	}

	ImGui::PopStyleColor(5);
}
void NotificationManager::HintNotification::render_logo(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	ImVec2 win_size(win_size_x, win_size_y);
	ImVec2 win_pos(win_pos_x, win_pos_y);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.0f, .0f, .0f, .0f));

	std::wstring button_text;
	button_text = ImGui::ClippyMarker;//LeftArrowButton;
	std::string placeholder_text;
	placeholder_text = ImGui::EjectButton;

	ImVec2 button_pic_size = ImGui::CalcTextSize(placeholder_text.c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f * 2.f, button_pic_size.y * 1.25f * 2.f);
	ImGui::SetCursorPosY(win_size.y / 2 - button_size.y * 1.1f);
	ImGui::SetCursorPosX(0);
	// shouldnt it render as text?
	if (imgui.button(button_text.c_str(), button_size.x, button_size.y))
	{
	}
	
	ImGui::PopStyleColor(5);
}
void NotificationManager::HintNotification::render_documentation_button(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	ImVec2 win_size(win_size_x, win_size_y);
	ImVec2 win_pos(win_pos_x, win_pos_y);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.0f, .0f, .0f, .0f));

	std::wstring button_text;
	button_text = ImGui::DocumentationButton;
	std::string placeholder_text;
	placeholder_text = ImGui::EjectButton;

	if (ImGui::IsMouseHoveringRect(ImVec2(win_pos.x - m_line_height * 5.f, win_pos.y),
		ImVec2(win_pos.x - m_line_height * 2.5f, win_pos.y + win_size.y - 2 * m_line_height),
		true))
	{
		button_text = ImGui::DocumentationHoverButton;
		// tooltip
		long time_now = wxGetLocalTime();
		if (m_docu_hover_time > 0 && m_docu_hover_time < time_now) {
			ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BACKGROUND);
			ImGui::BeginTooltip();
			imgui.text(_u8L("Open Documentation in web browser."));
			ImGui::EndTooltip();
			ImGui::PopStyleColor();
		}
		if (m_docu_hover_time == 0)
			m_docu_hover_time = time_now;
	}
	else
		m_docu_hover_time = 0;

	ImVec2 button_pic_size = ImGui::CalcTextSize(placeholder_text.c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f, button_pic_size.y * 1.25f);
	ImGui::SetCursorPosX(win_size.x - m_line_height * 5.0f);
	ImGui::SetCursorPosY(win_size.y / 2 - button_size.y);
	if (imgui.button(button_text.c_str(), button_size.x, button_size.y))
	{
		open_documentation();
	}

	//invisible large button
	ImGui::SetCursorPosX(win_size.x - m_line_height * 4.625f);
	ImGui::SetCursorPosY(0);
	if (imgui.button("  ", m_line_height * 2.f, win_size.y - 2 * m_line_height))
	{
		open_documentation();
	}

	ImGui::PopStyleColor(5);
}

void NotificationManager::HintNotification::open_documentation()
{
	if (!m_documentation_link.empty())
	{
		launch_browser_if_allowed(m_documentation_link);
	}
}
void NotificationManager::HintNotification::retrieve_data(int recursion_counter)
{
    HintData* hint_data = HintDatabase::get_instance().get_hint(recursion_counter >= 0 ? true : false);
	if (hint_data == nullptr)
		 close();

	if (hint_data != nullptr && !tags_check(hint_data->disabled_tags, hint_data->enabled_tags))
	{
		// Content for different user - retrieve another
		size_t count = HintDatabase::get_instance().get_count();
		if ((int)count < recursion_counter) {
			BOOST_LOG_TRIVIAL(error) << "Hint notification failed to load data due to recursion counter.";
		} else {
			retrieve_data(recursion_counter + 1);
		}
		return;
	}
	if(hint_data != nullptr)
    {
        NotificationData nd { NotificationType::DidYouKnowHint,
						      NotificationLevel::RegularNotification,
							  0,
						      hint_data->text,
							  hint_data->hypertext, nullptr,
							  hint_data->follow_text };
		m_hypertext_callback = hint_data->callback;
		m_disabled_tags      = hint_data->disabled_tags;
		m_enabled_tags       = hint_data->enabled_tags;
		m_runtime_disable    = hint_data->runtime_disable;
		m_documentation_link = hint_data->documentation_link;
        m_has_hint_data      = true;
		update(nd);
    }
}
} //namespace Slic3r 
} //namespace GUI 