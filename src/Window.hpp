#pragma once

#include "IWindow.hpp"
#include "Utils.hpp"
#include <unordered_set>
#include <string>
#include <gtk/gtk.h>

class Renderer;
class Input;

class Window
    : public IWindow
{
public:
    Window(Renderer *, Input *);
    ~Window();

    ITexture::PtrT CreateTexture(int width, std::string_view text, const HlAttr &, const HlAttr &def_attr) override;
    void Present() override;
    void DrawCursor(cairo_t *, int row, int col, unsigned fg, std::string_view mode);

private:
    Renderer *_renderer;
    Input *_input;
    GtkWidget *_window;
    GtkWidget *_scroll;
    GtkWidget *_grid;
    GtkWidget *_cursor;

    int _cell_width = 0, _cell_height = 0;
    int _last_rows = 0, _last_cols = 0;
    std::unordered_set<GtkWidget *> _widgets;
    PtrT<GtkCssProvider> _css_provider = NullPtr<GtkCssProvider>([](auto *p) { g_object_unref(p); });
    std::string _style;

    PtrT<GdkCursor> _active_cursor = NullPtr<GdkCursor>([](auto *c) { g_object_unref(c); });
    PtrT<GdkCursor> _busy_cursor = NullPtr<GdkCursor>([](auto *c) { g_object_unref(c); });

    void _CheckSize();
    void _CheckSize2();

    gboolean _OnKeyPressed(guint keyval, guint keycode, GdkModifierType state);

    void _Present();
    void _UpdateStyle();
    void _DrawCursor(GtkDrawingArea *, cairo_t *, int width, int height);
};
