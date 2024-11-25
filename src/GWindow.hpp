#pragma once

#include "IWindow.hpp"
#include "Utils.hpp"
#include "Session.hpp"
#include "GGrid.hpp"
#include "GFont.hpp"
#include "IWindowHandler.hpp"
#include "GCallbackAdaptor.hpp"

#include "Gtk/Application.hpp"
#include "Gtk/Window.hpp"
#include "Gtk/Builder.hpp"
#include "gir/Owned.hpp"

#include <string>
#include <memory>
#include <gtk/gtk.h>


namespace Gtk = gir::Gtk;

namespace gir { namespace Gtk {
class Dialog;
}}

class GWindow
    : public IWindow
    , public IWindowHandler
    , private GCallbackAdaptor<GWindow>
{
public:
    GWindow(const Gtk::Application &, Session::AtomicPtrT &session);
    ~GWindow();

    void Present() override;
    void DrawCursor(cairo_t *, int row, int col, unsigned fg, std::string_view mode);
    void SessionEnd() override;

    void SetError(const char *error) override;
    void SetGuiFont(const std::string &) override;

private:
    Gtk::Application _app;
    Session::AtomicPtrT &_session;

    gir::Owned<Gtk::Builder> _builder;
    Gtk::Window _window;
    std::string _title;
    Gtk::Widget _scroll;
    std::unique_ptr<GFont> _font;
    std::unique_ptr<GGrid> _grid;

    void _SetupWindow();
    void _SetupWindowSignals();
    void _SetupStatusLabel();
    void _UpdateTitle();

    void CheckSizeAsync() override;
    void _CheckSize();

    void _Present();
    void _SessionEnd();

    void MenuBarToggle() override;
    void MenuBarHide() override;

    void _UpdateActions();
    void _EnableAction(const char *name, bool enable);
    void _OnQuitAction(GSimpleAction *, GVariant *);
    void _OnInspectAction(GSimpleAction *, GVariant *);
    void _OnAboutAction(GSimpleAction *, GVariant *);
    void _OnSpawnAction(GSimpleAction *, GVariant *);
    void _OnConnectAction(GSimpleAction *, GVariant *);
    //void _OnConnectDlgResponse(Gtk::Dialog &, gint response, Gtk::Builder);
    void _OnDisconnectAction(GSimpleAction *, GVariant *);
    void _OnSettingsAction(GSimpleAction *, GVariant *);
    void _OnShowMarkupAction(GSimpleAction *, GVariant *);


    // A generic async pass to the Gtk thread.
    template <void (GWindow::*func)()>
    void _GtkTimer0(int ms)
    {
        auto on_timeout = [](gpointer data) -> gboolean {
            (reinterpret_cast<GWindow *>(data)->*func)();
            return FALSE;
        };
        g_timeout_add(ms, on_timeout, this);
    }

    static void _GtkTimer(int ms, std::function<void(void)> func)
    {
        using FuncT = decltype(func);
        FuncT *boxed_func = new FuncT{func};
        auto on_timeout = [](gpointer data) {
            std::unique_ptr<FuncT> f{reinterpret_cast<FuncT*>(data)};
            (*f.get())();
            return FALSE;
        };
        g_timeout_add(ms, on_timeout, boxed_func);
    }
};
