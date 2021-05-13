#include "HintNotification.hpp"
#include "ImGuiWrapper.hpp"
#include "libslic3r/AppConfig.hpp"

namespace Slic3r {
namespace GUI {

namespace NotificationsInternal {
static inline void push_style_color(ImGuiCol idx, const ImVec4& col, bool fading_out, float current_fade_opacity)
{
	if (fading_out)
		ImGui::PushStyleColor(idx, ImVec4(col.x, col.y, col.z, col.w * current_fade_opacity));
	else
		ImGui::PushStyleColor(idx, col);
}
} //namespace NotificationInternal 

void HintDatabase::init()
{
	const AppConfig* app_config = wxGetApp().app_config;
	m_hint_id = std::atoi(app_config->get("last_hint").c_str());
    m_initialized = true;
}
bool HintDatabase::get_hint(HintData& data, bool up)
{
    if (! m_initialized) {
        init();
        //return false;
    }
    // shift id
    m_hint_id = (up ? m_hint_id + 1 : (m_hint_id == 0 ? m_hints_collection.size() - 1 : m_hint_id - 1));
    m_hint_id %= m_hints_collection.size();

	AppConfig* app_config = wxGetApp().app_config;
	app_config->set("last_hint", std::to_string(m_hint_id));

    data.text = m_hints_collection[m_hint_id].text;
    data.hypertext = m_hints_collection[m_hint_id].hypertext;
    data.callback = m_hints_collection[m_hint_id].callback;

    return true;
}

void NotificationManager::HintNotification::count_spaces()
{
	//determine line width 
	m_line_height = ImGui::CalcTextSize("A").y;

	std::string text;
	text = ImGui::ErrorMarker; // TODO change to left arrow 
	float picture_width = ImGui::CalcTextSize(text.c_str()).x;
	m_left_indentation = picture_width + m_line_height / 2;

	m_window_width_offset = m_left_indentation + m_line_height * 5.5f;
	m_window_width = m_line_height * 25;
}

void NotificationManager::HintNotification::init()
{
	// Do not init closing notification
	if (is_finished())
		return;

	count_spaces();
	count_lines();

	m_notification_start = GLCanvas3D::timestamp_now();
	if (m_state == EState::Unknown)
		m_state = EState::Shown;
}

void NotificationManager::HintNotification::close()
{ 
	AppConfig* app_config = wxGetApp().app_config;
	app_config->set("show_hints", m_checkbox ? "0" : "1");
	PopNotification::close();
}

void NotificationManager::HintNotification::set_next_window_size(ImGuiWrapper& imgui)
{
	m_window_height = m_multiline ?
		(m_lines_count + 1.5f) * m_line_height :
		3.5f * m_line_height;
	m_window_height += 1 * m_line_height; // top and bottom
}

bool NotificationManager::HintNotification::on_text_click()
{
	if (m_hypertext_callback != nullptr)
		m_hypertext_callback();
	return false;
}

void NotificationManager::HintNotification::render_text(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
    if (!m_has_hint_data)
        retrieve_data();

	render_right_arrow_button(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
	render_left_arrow_button(imgui, win_size_x, win_size_y, win_pos_x, win_pos_y);
    //PopNotification::render_text(imgui, win_size_x, win_size_y, win_pos_x,  win_pos_y);

	float	x_offset = m_left_indentation;
	int		last_end = 0;
	float	starting_y = m_line_height / 2;
	float	shift_y = m_line_height;
	std::string line;

	for (size_t i = 0; i < (m_multiline ? m_lines_count : 2); i++) {
		line.clear();
		ImGui::SetCursorPosX(x_offset);
		ImGui::SetCursorPosY(starting_y + i * shift_y);
		if (m_endlines.size() > i && m_text1.size() >= m_endlines[i]) {
			if (i == 1 && m_lines_count > 2 && !m_multiline) {
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
		render_hypertext(imgui, x_offset + ImGui::CalcTextSize((line + " ").c_str()).x, starting_y + (m_lines_count - 1) * shift_y, m_hypertext);
	}

	ImGui::SetCursorPosX(x_offset);
	if (m_lines_count > 2 && m_multiline)
		ImGui::SetCursorPosY(starting_y + (m_lines_count + 0.25f) * shift_y);
	else
		ImGui::SetCursorPosY(starting_y + 2.25f * shift_y);
	ImGui::Checkbox("Do not show again.", &m_checkbox);
}
void NotificationManager::HintNotification::render_right_arrow_button(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	ImVec2 win_size(win_size_x, win_size_y);
	ImVec2 win_pos(win_pos_x, win_pos_y);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	NotificationsInternal::push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	NotificationsInternal::push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.0f, .0f, .0f, .0f));

	std::string button_text;
	button_text = ImGui::RightArrowButton;
	
	if (ImGui::IsMouseHoveringRect(ImVec2(win_pos.x - m_line_height * 5.f, win_pos.y),
		ImVec2(win_pos.x - m_line_height * 2.5f, win_pos.y + win_size.y),
		true))
	{
		button_text = ImGui::RightArrowHoverButton;
	}

	
	ImVec2 button_pic_size = ImGui::CalcTextSize(button_text.c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f, button_pic_size.y * 1.25f);
	ImGui::SetCursorPosX(win_size.x - m_line_height * 5.0f);
	ImGui::SetCursorPosY(win_size.y / 2 - button_size.y);
	if (imgui.button(button_text.c_str(), button_size.x, button_size.y))
	{
		retrieve_data();
	}

	//invisible large button
	ImGui::SetCursorPosX(win_size.x - m_line_height * 4.625f);
	ImGui::SetCursorPosY(0);
	if (imgui.button("  ", m_line_height * 2.f, win_size.y))
	{
		retrieve_data();
	}
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
}
void NotificationManager::HintNotification::render_left_arrow_button(ImGuiWrapper& imgui, const float win_size_x, const float win_size_y, const float win_pos_x, const float win_pos_y)
{
	ImVec2 win_size(win_size_x, win_size_y);
	ImVec2 win_pos(win_pos_x, win_pos_y);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.0f, .0f, .0f, .0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.0f, .0f, .0f, .0f));
	NotificationsInternal::push_style_color(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	NotificationsInternal::push_style_color(ImGuiCol_TextSelectedBg, ImVec4(0, .75f, .75f, 1.f), m_state == EState::FadingOut, m_current_fade_opacity);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.0f, .0f, .0f, .0f));

	std::string button_text;
	button_text = ImGui::LeftArrowButton;
	
	if (ImGui::IsMouseHoveringRect(ImVec2(win_pos.x - win_size.x, win_pos.y),
		ImVec2(win_pos.x - win_size.x + m_line_height * 2.75f, win_pos.y + win_size.y),
		true))
	{
		button_text = ImGui::LeftArrowHoverButton;
	}
	
	
	ImVec2 button_pic_size = ImGui::CalcTextSize(button_text.c_str());
	ImVec2 button_size(button_pic_size.x * 1.25f, button_pic_size.y * 1.25f);
	ImGui::SetCursorPosY(win_size.y / 2 - button_size.y);
	ImGui::SetCursorPosX(0);
	if (imgui.button(button_text.c_str(), button_size.x, button_size.y))
	{
		retrieve_data(false);
	}
	
	//invisible large button
	ImGui::SetCursorPosY(0);
	ImGui::SetCursorPosX(0);
	if (imgui.button("    ", m_line_height * 2.75f, win_size.y))
	{
		retrieve_data(false);
	}
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
	ImGui::PopStyleColor();
}
void NotificationManager::HintNotification::retrieve_data(bool up)
{
    HintData hint_data;
    if(HintDatabase::get_instance().get_hint(hint_data, up))
    {
        NotificationData nd { NotificationType::DidYouKnowHint, NotificationLevel::RegularNotification, 0, _u8L("DID YOU KNOW:\n") + hint_data.text, hint_data.hypertext };
        update(nd);
		m_hypertext_callback = hint_data.callback;
        m_has_hint_data = true;
		//m_multiline = false;
    }
}


} //namespace Slic3r 
} //namespace GUI 