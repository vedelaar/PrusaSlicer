#ifndef slic3r_GUI_HintNotification_hpp_
#define slic3r_GUI_HintNotification_hpp_

#include "NotificationManager.hpp"

namespace Slic3r {
namespace GUI {

// Database of hints updatable
struct HintData
{
	std::string        text;
	std::string        hypertext;
	std::function<void(void/*wxEvtHandler**/)> callback{ nullptr };
};

class HintDatabase
{
public:
    static HintDatabase& get_instance()
    {
        static HintDatabase    instance; // Guaranteed to be destroyed.
                                         // Instantiated on first use.
        return instance;
    }
private:
	HintDatabase()
		: m_hint_id(0)
	{}
public:
	HintDatabase(HintDatabase const&) = delete;
	void operator=(HintDatabase const&) = delete;

	// return true if HintData filled;
	bool get_hint(HintData& data, bool up = true);
private:
	void	init();
	size_t						m_hint_id;
	bool						m_initialized { false };
	const std::vector<HintData> m_hints_collection = {
		{ _u8L("Hint with short text.") },
		{ _u8L("Hint with quite long text. Second sentence of that long text.\n This sentence starts on new line. And this is last sentence.") },
		{ _u8L("Hint with long text. And with link to knowledge base.\nThis sentence starts on new line. And this is last sentence ending with a link: "), _u8L("Link"),
			[](/*wxEvtHandler* evnthndlr*/) { wxLaunchDefaultBrowser("https://www.help.prusa3d.com/en/article/layers-and-perimeters_1748"); } },
		{ _u8L("Hint with short text and link: "), _u8L("First print"), 
			[](/*wxEvtHandler* evnthndlr*/) { wxLaunchDefaultBrowser("https://www.help.prusa3d.com/en/article/first-print-with-prusaslicer_1753"); } },
	};
};
// Notification class - shows current Hint ("Did you know") 
class NotificationManager::HintNotification : public NotificationManager::PopNotification
{
public:
	HintNotification(const NotificationData& n, NotificationIDProvider& id_provider, wxEvtHandler* evt_handler)
		: PopNotification(n, id_provider, evt_handler)
	{
		retrieve_data();
	}
	virtual void	init() override;
	virtual void    close() override;
protected:
	virtual void	set_next_window_size(ImGuiWrapper& imgui) override;
	virtual void	count_spaces() override;
	virtual bool	on_text_click() override;
	virtual void	render_text(ImGuiWrapper& imgui,
								const float win_size_x, const float win_size_y,
								const float win_pos_x, const float win_pos_y) override;
	void			render_right_arrow_button(ImGuiWrapper& imgui,
								const float win_size_x, const float win_size_y,
								const float win_pos_x, const float win_pos_y);
	void			render_left_arrow_button(ImGuiWrapper& imgui,
								const float win_size_x, const float win_size_y,
								const float win_pos_x, const float win_pos_y);
	void			retrieve_data(bool up = true);

	bool	m_has_hint_data { false };
	bool	m_checkbox		{ false };
	std::function<void(void/*wxEvtHandler**/)> m_hypertext_callback;
};

} //namespace Slic3r 
} //namespace GUI 

#endif //slic3r_GUI_HintNotification_hpp_