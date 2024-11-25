#include "GWindow.hpp"
#include "config.hpp"
#include "Logger.hpp"
#include "Input.hpp"
#include "Renderer.hpp"
#include "SessionSpawn.hpp"
#include "SessionTcp.hpp"
#include "GSettingsDlg.hpp"

#include <Gtk/ApplicationWindow.hpp>
//#include <Gtk/Dialog.hpp>
//#include <Gtk/Entry.hpp>
#include <Gtk/Fixed.hpp>
#include <Gtk/IconTheme.hpp>
#include <Gtk/KeyvalTrigger.hpp>
#include <Gtk/Label.hpp>
#include <Gtk/NamedAction.hpp>
#include <Gtk/ResponseType.hpp>
#include <Gtk/Shortcut.hpp>
#include <Gtk/ShortcutController.hpp>
#include <Gtk/ShortcutScope.hpp>
//#include <Gtk/StyleContext.hpp>
#include <Gtk/StyleProvider.hpp>
#include <Gtk/Window.hpp>
//#include <Gtk/MessageDialog.hpp>
//#include <Gtk/AboutDialog.hpp>

#include <iterator>
#include <msgpack/v1/unpack.hpp>
#include <sstream>
#include <fmt/format.h>

#ifdef GIR_INLINE
#include <Gtk/Application.ipp>
#include <Gtk/ApplicationWindow.ipp>
#include <Gtk/Builder.ipp>
//#include <Gtk/Entry.ipp>
#include <Gtk/IconTheme.ipp>
#include <Gtk/KeyvalTrigger.ipp>
#include <Gtk/Label.ipp>
#include <Gtk/NamedAction.ipp>
#include <Gtk/Shortcut.ipp>
#include <Gtk/ShortcutController.ipp>
//#include <Gtk/StyleContext.ipp>
//#include <Gtk/MessageDialog.ipp>
//#include <Gtk/AboutDialog.ipp>
#endif


GWindow::GWindow(const Gtk::Application &app, Session::AtomicPtrT &session)
    : _app{app}
    , _session{session}
    , _builder{Gtk::Builder::new_from_resource("/org/sakhnik/nvim-ui/gtk/main.ui")}
{
    Gtk::IconTheme icon_theme{Gtk::IconTheme::get_for_display(gdk_display_get_default())};
    icon_theme.add_resource_path("/org/sakhnik/nvim-ui/icons");

    _SetupWindow();

    // Font handler
    _font.reset(new GFont{_window});
    _font->Subscribe([this] {
        // Update the style
        auto session = _session.load();
        if (!session)
            return;
        auto guard = session->GetRenderer()->Lock();
        _grid->UpdateStyle(session.get());
    });

    // Grid
    Gtk::Fixed grid{_builder.get_object("grid").g_obj()};
    _grid.reset(new GGrid(grid, *_font.get(), _session, this));

    // GTK wouldn't allow shrinking the window if there are widgets
    // placed in the grid. So the scroll view is required.
    _scroll = Gtk::Widget{_builder.get_object("scrolled_window").g_obj()};

    _SetupStatusLabel();

    //_window.show();
    _window.present();

    _SetupWindowSignals();

    auto sess = _session.load();
    if (sess && sess->GetRenderer())
    {
        // Initial style setup
        auto guard = sess->GetRenderer()->Lock();
        _grid->UpdateStyle(sess.get());
    }

    // Adjust the grid size to the actual window size
    CheckSizeAsync();
}

GWindow::~GWindow()
{
    _window.destroy();
}

void GWindow::_SetupWindow()
{
    _window = Gtk::Window{_builder.get_object("main_window").g_obj()};
    _window.set_application(_app);

    const GActionEntry actions[] = {
        { "spawn", MakeCallback<&GWindow::_OnSpawnAction>(), nullptr, nullptr, nullptr, {0, 0, 0} },
        { "connect", MakeCallback<&GWindow::_OnConnectAction>(), nullptr, nullptr, nullptr, {0, 0, 0} },
        { "disconnect", MakeCallback<&GWindow::_OnDisconnectAction>(), nullptr, nullptr, nullptr, {0, 0, 0} },
        { "settings", MakeCallback<&GWindow::_OnSettingsAction>(), nullptr, nullptr, nullptr, {0, 0, 0} },
        { "quit", MakeCallback<&GWindow::_OnQuitAction>(), nullptr, nullptr, nullptr, {0, 0, 0} },
        { "inspect", MakeCallback<&GWindow::_OnInspectAction>(), nullptr, nullptr, nullptr, {0, 0, 0} },
        { "about", MakeCallback<&GWindow::_OnAboutAction>(), nullptr, nullptr, nullptr, {0, 0, 0} },
        { "show-markup", MakeCallback<&GWindow::_OnShowMarkupAction>(), nullptr, nullptr, nullptr, {0, 0, 0} },
    };
    g_action_map_add_action_entries(G_ACTION_MAP(_window.g_obj()), actions, G_N_ELEMENTS(actions), this);

    auto controller = Gtk::ShortcutController::new_();
    auto &shortcut_controller = static_cast<Gtk::ShortcutController &>(controller);
    shortcut_controller.set_scope(Gtk::ShortcutScope::global);
    _window.add_controller(shortcut_controller);

    shortcut_controller.add_shortcut(Gtk::Shortcut::new_(
            Gtk::KeyvalTrigger::new_(GDK_KEY_n, GDK_CONTROL_MASK),
            Gtk::NamedAction::new_("win.spawn")));
    shortcut_controller.add_shortcut(Gtk::Shortcut::new_(
            Gtk::KeyvalTrigger::new_(GDK_KEY_t, GDK_CONTROL_MASK),
            Gtk::NamedAction::new_("win.connect")));
    shortcut_controller.add_shortcut(Gtk::Shortcut::new_(
            Gtk::KeyvalTrigger::new_(GDK_KEY_q, GDK_CONTROL_MASK),
            Gtk::NamedAction::new_("win.quit")));

    _UpdateActions();
}

void GWindow::_SetupWindowSignals()
{
    using SizeChangedT = gboolean (*)(GObject *, GParamSpec *, gpointer data);
    SizeChangedT sizeChanged = [](auto *, auto *, gpointer data) {
        reinterpret_cast<GWindow *>(data)->CheckSizeAsync();
        return FALSE;
    };

    g_signal_connect(_window.g_obj(), "notify::default-width", G_CALLBACK(sizeChanged), this);
    g_signal_connect(_window.g_obj(), "notify::default-height", G_CALLBACK(sizeChanged), this);

    _window.on_show(_window, [this](auto) { CheckSizeAsync(); });

    _window.on_close_request(_window, [this](auto) -> gboolean {
        Logger().info("GWindow close request");
        auto session = _session.load();
        if (session)
            session->SetWindow(nullptr);
        else
            _app.quit();
        _app.remove_window(_window);
        _window.close();
        return false;
    });
}

void GWindow::_SetupStatusLabel()
{
    Gtk::Label status_label{_builder.get_object("status").g_obj()};
    //status_label.get_style_context().add_provider(_grid->GetStyle(), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    status_label.set_visible(false);
}

void GWindow::CheckSizeAsync()
{
    _GtkTimer0<&GWindow::_CheckSize>(1);
}

void GWindow::_CheckSize()
{
    int width = _scroll.get_width();
    int height = _scroll.get_height();
    _grid->CheckSize(width, height);
}

void GWindow::Present()
{
    _GtkTimer(1, [this]() { _Present(); });
}

void GWindow::_Present()
{
    int width = _scroll.get_width();
    int height = _scroll.get_height();
    _grid->Present(width, height);
}

void GWindow::SessionEnd()
{
    _GtkTimer0<&GWindow::_SessionEnd>(1);
}

void GWindow::_SessionEnd()
{
    Logger().info("Session end");
    _grid->Clear();

    Gtk::Label status_label{_builder.get_object("status").g_obj()};
    auto session = _session.exchange({});
    if (session && !session->GetOutput().empty())
    {
        status_label.set_text(session->GetOutput().c_str());
        status_label.set_visible(true);
    }

    _UpdateActions();

    // TODO: change the style for some fancy background
}

void GWindow::SetError(const char *error)
{
    _title = "nvim-ui";
    auto session = _session.load();
    if (session)
    {
        _title += " " + session->GetDescription();
    }
    if (error)
    {
        _title += " (";
        _title += error;
        _title += ")";
    }
    _UpdateTitle();
}

void GWindow::_UpdateTitle()
{
    auto &app_window = static_cast<Gtk::ApplicationWindow &>(_window);
    bool is_menu_visible = app_window.get_show_menubar();
    if (!is_menu_visible)
    {
        std::string title{_title};
        title += " (";
        title += _("Press ALT to reach the menu");
        title += ")";
        _window.set_title(title.c_str());
    }
    else
    {
        _window.set_title(_title.c_str());
    }
}

void GWindow::MenuBarToggle()
{
    // Toggle the menubar
    auto &app_window = static_cast<Gtk::ApplicationWindow &>(_window);
    bool is_menu_visible = app_window.get_show_menubar();
    app_window.set_show_menubar(!is_menu_visible);
    _UpdateTitle();
}

void GWindow::MenuBarHide()
{
    auto &app_window = static_cast<Gtk::ApplicationWindow &>(_window);
    app_window.set_show_menubar(false);
    _UpdateTitle();
}

void GWindow::_UpdateActions()
{
    auto session = _session.load();
    bool session_active = session.get() != nullptr;
    _EnableAction("spawn", !session_active);
    _EnableAction("connect", !session_active);
    _EnableAction("quit", !session_active);
    bool is_session_tcp = dynamic_cast<SessionTcp *>(session.get()) != nullptr;
    _EnableAction("disconnect", is_session_tcp);
    _window.set_deletable(!session_active);
}

void GWindow::_EnableAction(const char *name, bool enable)
{
    GAction *a = g_action_map_lookup_action(G_ACTION_MAP(_window.g_obj()), name);
    g_simple_action_set_enabled(G_SIMPLE_ACTION(a), enable);
}

void GWindow::_OnQuitAction(GSimpleAction *, GVariant *)
{
    Logger().info("Bye!");
    auto app = _window.get_application();
    _window.destroy();
    app.quit();
}

void GWindow::_OnInspectAction(GSimpleAction *, GVariant *)
{
    Gtk::Window::set_interactive_debugging(true);
}

void GWindow::_OnAboutAction(GSimpleAction *, GVariant *)
{
    //const char *authors[] = {
    //    _("Anatolii Sakhnik https://sakhnik.com"),
    //    nullptr
    //};

    //static Gtk::AboutDialog dlg;
    //if (!dlg.g_obj())
    //{
    //    dlg = Gtk::AboutDialog::new_().g_obj();
    //    dlg.set_program_name("nvim-ui");
    //    dlg.set_title(_("About nvim-ui"));
    //    dlg.set_logo_icon_name("nvim-ui");
    //    dlg.set_authors(authors);
    //    dlg.set_license_type(Gtk::License::mit_x11);
    //    dlg.set_website("https://github.com/sakhnik/nvim-ui");
    //    dlg.set_website_label(_("GitHub: sakhnik/nvim-ui"));
    //    dlg.set_version(VERSION);
    //    std::string CHANGELOG = _("CHANGELOG");
    //    std::filesystem::path changelog_dir = ResourceDir::Get("",
    //            std::filesystem::path{PREFIX} / DATA_DIR / "doc" / "nvim-ui");
    //    auto changelog_path = changelog_dir / "CHANGELOG.html";
    //    auto uri = mk_unique_ptr(g_filename_to_uri(changelog_path.generic_string().c_str(), nullptr, nullptr), g_free);
    //    Logger().info("Changelog URI is {}", uri.get());
    //    std::string comments = _("User interface for <a href=\"https://neovim.io\">Neovim</a>");
    //    comments += "\n<a href=\"";
    //    comments += uri.get();
    //    comments += "\">" + CHANGELOG + "</a>";
    //    dlg.set_comments(comments.c_str());
    //    dlg.set_copyright("©2022 Anatolii Sakhnik");
    //    dlg.set_hide_on_close(true);

    //    // The markup isn't enabled for the comments by default, fix this.
    //    Gtk::Label comments_label{dlg.get_template_child(GTK_TYPE_ABOUT_DIALOG, "comments_label").g_obj()};
    //    comments_label.set_use_markup(true);
    //}

    //dlg.present();
}

namespace {

void _SetLabelsSelectable(GtkWidget *dialog)
{
    GtkWidget *area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
    GtkWidget *widget = gtk_widget_get_first_child(area);
    if (GTK_IS_LABEL(widget))
        gtk_label_set_selectable(GTK_LABEL(widget), TRUE);
}

} //namespace;

void GWindow::_OnShowMarkupAction(GSimpleAction *, GVariant *)
{
    auto markup = _grid->DumpMarkup();

    GtkDialogFlags flags = static_cast<GtkDialogFlags>(GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL);
    auto dialog = gtk_message_dialog_new(GTK_WINDOW(_window.g_obj()),
            flags,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_CLOSE,
            "%s",
            markup.c_str());
    _SetLabelsSelectable(dialog);

    g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);
    gtk_widget_show(dialog);
}

void GWindow::_OnSpawnAction(GSimpleAction *, GVariant *)
{
    Logger().info("Spawn new");
    try
    {
        Session::PtrT session{new SessionSpawn(0, nullptr)};
        _session.store(session);
        SetError(nullptr);
        session->SetWindow(this);
        session->RunAsync();
    }
    catch (std::exception &ex)
    {
        Logger().error("Failed to spawn: {}", ex.what());
        SetError(ex.what());
    }
    _UpdateActions();
    _window.set_focus(_grid->GetFixed());
}

void GWindow::_OnConnectAction(GSimpleAction *, GVariant *)
{
    //auto builder = Gtk::Builder::new_from_resource("/org/sakhnik/nvim-ui/gtk/connect-dlg.ui");
    //Gtk::Dialog dlg{builder.get_object("connect_dlg").g_obj()};
    //dlg.set_transient_for(_window);

    //dlg.on_response(dlg, [this, builder](Gtk::Dialog dlg, gint response) {
    //    _OnConnectDlgResponse(dlg, response, builder);
    //});
    //dlg.on_destroy(dlg, [builder](Gtk::Dialog) {
    //    builder.unref();
    //});
    //dlg.show();
}

//void GWindow::_OnConnectDlgResponse(Gtk::Dialog &dlg, gint response, Gtk::Builder builder)
//{
//    dlg.destroy();
//    if (Gtk::ResponseType::ok != response)
//        return;
//    Gtk::Entry address_entry{builder.get_object("address_entry").g_obj()};
//    Gtk::Entry port_entry{builder.get_object("port_entry").g_obj()};
//
//    const char *address = address_entry.get_text();
//    int port = std::stoi(port_entry.get_text());
//    Logger().info("Connect to {}:{}", address, port);
//
//    try
//    {
//        Session::PtrT session{new SessionTcp(address, port)};
//        _session.store(session);
//        SetError(nullptr);
//        session->SetWindow(this);
//        session->RunAsync();
//    }
//    catch (std::exception &ex)
//    {
//        Logger().error("Failed to spawn: {}", ex.what());
//        SetError(ex.what());
//    }
//    _UpdateActions();
//    _window.set_focus(_grid->GetFixed());
//}

void GWindow::_OnDisconnectAction(GSimpleAction *, GVariant *)
{
    //GtkDialogFlags flags = static_cast<GtkDialogFlags>(GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL);
    //Gtk::MessageDialog dialog =
    //    G_OBJECT(gtk_message_dialog_new(GTK_WINDOW(_window.g_obj()),
    //        flags,
    //        GTK_MESSAGE_QUESTION,
    //        GTK_BUTTONS_YES_NO,
    //        _("Are you sure you want to disconnect?")));
    //dialog.on_response(dialog, [&](Gtk::Dialog dlg, gint response) {
    //    dlg.destroy();
    //    if (response == GTK_RESPONSE_YES)
    //    {
            _SessionEnd();
    //    }
    //});
    //dialog.show();
}

void GWindow::_OnSettingsAction(GSimpleAction *, GVariant *)
{
    GSettingsDlg dlg{_window, *_font.get()};
}

void GWindow::SetGuiFont(const std::string &value)
{
    _GtkTimer(1, [this, value]() {
        _font->SetGuiFont(value);
    });
}
