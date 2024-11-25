#include "GGrid.hpp"
#include "Logger.hpp"
#include "Renderer.hpp"
#include "IWindowHandler.hpp"
#include "GFont.hpp"
#include "GConfig.hpp"

#include "Gtk/DrawingArea.hpp"
#include "Gtk/EventController.hpp"
#include "Gtk/EventControllerKey.hpp"
#include "Gtk/Orientation.hpp"
#include "Gtk/PropagationPhase.hpp"
//#include "Gtk/StyleContext.hpp"

#include <sstream>
#include <numeric>
#include <boost/algorithm/string.hpp>

#ifdef GIR_INLINE
#include "Gtk/ApplicationWindow.ipp"
#include "Gtk/CssProvider.ipp"
#include "Gtk/DrawingArea.ipp"
#include "Gtk/EventControllerKey.ipp"
#include "Gtk/Fixed.ipp"
#include "Gtk/Label.ipp"
//#include "Gtk/StyleContext.ipp"
#endif


namespace Gdk = gir::Gdk;

GGrid::GGrid(Gtk::Fixed grid, GFont &font, Session::AtomicPtrT &session, IWindowHandler *window_handler)
    : _grid{grid}
    , _font{font}
    , _session{session}
    , _window_handler{window_handler}
    , _css_provider{Gtk::CssProvider::new_()}
{
    _grid.set_focusable(true);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(_css_provider.g_obj()), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    //_grid.get_style_context().add_provider(_css_provider.get(), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    Gtk::DrawingArea cursor = Gtk::DrawingArea::new_().g_obj();
    _cursor.reset(new GCursor{cursor, this, _session});

    Gtk::EventControllerKey controller = Gtk::EventControllerKey::new_().g_obj();
    controller.set_propagation_phase(Gtk::PropagationPhase::capture);
    controller.on_key_pressed(controller, [this](Gtk::EventControllerKey, guint keyval, guint keycode, Gdk::ModifierType state) -> gboolean {
        return _OnKeyPressed(keyval, keycode, state);
    });
    controller.on_key_released(controller, [this](Gtk::EventControllerKey, guint keyval, guint keycode, Gdk::ModifierType state) {
        _OnKeyReleased(keyval, keycode, state);
    });
    grid.add_controller(controller);
}

namespace {

std::string GetRuler()
{
    std::string str = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    for (int i = 0; i < 5; ++i)
        str += str;
    return str;
}

} // namespace;

void GGrid::MeasureCell()
{
    // Measure cell width and height
    static const std::string RULER = GetRuler();

    Gtk::Label ruler{Gtk::Label::new_("").g_obj()};
    //ruler.get_style_context().add_provider(_css_provider.get(), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    ruler.set_markup(RULER.c_str());

    int width, height;
    ruler.measure(Gtk::Orientation::horizontal, -1, &width, nullptr, nullptr, nullptr);
    ruler.measure(Gtk::Orientation::vertical, -1, &height, nullptr, nullptr, nullptr);
    _cell_width = 1.0 * width / RULER.size();
    // Adjust the cell height manually
    _cell_height = height + GConfig::GetCellHeightAdjustment();
    Logger().info("Measured cell: width={} height={}", _cell_width, _cell_height);
    ruler.ref_sink();

    _cursor->UpdateSize();
}

void GGrid::UpdateStyle(Session *session)
{
    assert(session);

    std::ostringstream oss;

    auto renderer = session->GetRenderer();
    assert(renderer);
    const auto &attr = renderer->GetDefAttr();

    oss << "* {\n";
    oss << "font-family: " << _font.GetFamily() << ";\n";
    oss << "font-size: " << _font.GetSizePt() << "pt;\n";
    if ((attr.flags & HlAttr::F_REVERSE))
    {
        if (attr.fg.has_value())
            oss << fmt::format("background-color: #{:06x};\n", attr.fg.value());
        if (attr.bg.has_value())
            oss << fmt::format("color: #{:06x};\n", attr.bg.value());
    }
    else
    {
        if (attr.bg.has_value())
            oss << fmt::format("background-color: #{:06x};\n", attr.bg.value());
        if (attr.fg.has_value())
            oss << fmt::format("color: #{:06x};\n", attr.fg.value());
    }
    oss << "}\n";

    oss << "label.status {\n";
    oss << "color: #cccccc;\n";
    oss << "}\n";

    std::string style = oss.str();
    Logger().debug("Updated CSS Style:\n{}", style);
    _css_provider.load_from_string(style.data());

    _UpdatePangoStyles(session);

    MeasureCell();
    _window_handler->CheckSizeAsync();
}

std::string GGrid::_MakePangoStyle(const HlAttr &attr, const HlAttr &def_attr)
{
    std::ostringstream oss;
    if ((attr.flags & HlAttr::F_REVERSE))
    {
        auto fg = attr.fg.value_or(def_attr.fg.value());
        auto bg = attr.bg.value_or(def_attr.bg.value());
        oss << fmt::format(" background=\"#{:06x}\"", fg);
        oss << fmt::format(" color=\"#{:06x}\"", bg);
    }
    else
    {
        if (attr.bg.has_value())
            oss << fmt::format(" background=\"#{:06x}\"", attr.bg.value());
        if (attr.fg.has_value())
            oss << fmt::format(" color=\"#{:06x}\"", attr.fg.value());
    }
    if ((attr.flags & HlAttr::F_ITALIC))
        oss << " style=\"italic\"";
    if ((attr.flags & HlAttr::F_BOLD))
        oss << " weight=\"bold\"";

    if ((attr.flags & HlAttr::F_TEXT_DECORATION))
    {
        if ((attr.flags & HlAttr::F_UNDERUNDERLINE))
            oss << " underline=\"single\"";
        else if ((attr.flags & HlAttr::F_UNDERCURL))
            oss << " underline=\"error\"";
        //else if ((attr.flags & HlAttr::F_UNDERDASH))
        //    style = "dashed";
        //else if ((attr.flags & HlAttr::F_UNDERDOT))
        //    style = "dotted";
        if (attr.special.has_value())
            oss << fmt::format(" underline_color=\"#{:06x}\"", attr.special.value());
    }
    else if ((attr.flags & HlAttr::F_STRIKETHROUGH))
    {
        oss << " strikethrough=\"true\"";
    }
    return oss.str();
}

void GGrid::_UpdatePangoStyles(Session *session)
{
    auto renderer = session->GetRenderer();
    assert(renderer);

    auto def_attr = renderer->GetDefAttr();
    _default_pango_style = _MakePangoStyle(def_attr, def_attr);

    _pango_styles.clear();
    for (const auto &id_attr : renderer->GetAttrMap())
    {
        int id = id_attr.first;
        const auto &attr = id_attr.second;
        _pango_styles[id] = _MakePangoStyle(attr, def_attr);
    }
}

void GGrid::Present(int width, int height)
{
    auto session = _session.load();
    if (!session)
        return;
    auto renderer = session->GetRenderer();
    assert(renderer);
    auto guard = renderer->Lock();

    if (renderer->IsAttrMapModified())
    {
        renderer->MarkAttrMapProcessed();
        UpdateStyle(session.get());
    }

    // Create and place new labels
    _UpdateLabels(session.get());

    _cursor->Move();
    _grid.set_cursor_from_name(renderer->IsBusy() ? "progress" : "default");
    _CheckSize(width, height, session.get());
}

void GGrid::Clear()
{
    for (auto &[_, texture]: _textures)
    {
        _MoveLabel(texture.label, -1);
        _grid.remove(texture.label);
    }
    _textures.clear();
    _cursor->Hide();
}

gboolean GGrid::_OnKeyPressed(guint keyval, guint /*keycode*/, GdkModifierType state)
{
    //string key = Gdk.keyval_name (keyval);
    //print ("* key pressed %u (%s) %u\n", keyval, key, keycode);

    if (keyval == GDK_KEY_Alt_L)
    {
        _alt_pending = true;
        return true;
    }
    _alt_pending = false;
    _window_handler->MenuBarHide();

    auto session = _session.load();
    if (!session)
        return true;
    auto renderer = session->GetRenderer();
    if (!renderer)
        return false;

    if (0 != (GDK_CONTROL_MASK & state))
    {
        double font_size_pt = _font.GetSizePt();
        if (keyval == GDK_KEY_equal)
            font_size_pt *= 1.1;
        else if (keyval == GDK_KEY_minus)
            font_size_pt /= 1.1;
        else if (keyval == GDK_KEY_0)
            font_size_pt = 14;
        if (font_size_pt != _font.GetSizePt())
        {
            auto guard = renderer->Lock();
            _font.SetSizePt(font_size_pt);
            UpdateStyle(session.get());
            return true;
        }
    }

    gunichar uc = gdk_keyval_to_unicode(keyval);
    auto input = MkPtr(g_string_append_unichar(g_string_new(nullptr), uc),
                                               [](GString *s) { g_string_free(s, true); });
    auto start_length = input->len;

    // TODO: functional keys, shift etc
    switch (keyval)
    {
    case GDK_KEY_Escape:        input.reset(g_string_new("esc"));   break;
    case GDK_KEY_Return:        input.reset(g_string_new("cr"));    break;
    case GDK_KEY_BackSpace:     input.reset(g_string_new("bs"));    break;
    case GDK_KEY_Tab:           input.reset(g_string_new("tab"));   break;
    case GDK_KEY_less:          input.reset(g_string_new("lt"));    break;
    case GDK_KEY_Left:          input.reset(g_string_new("Left"));  break;
    case GDK_KEY_Right:         input.reset(g_string_new("Right")); break;
    case GDK_KEY_Up:            input.reset(g_string_new("Up"));    break;
    case GDK_KEY_Down:          input.reset(g_string_new("Down"));  break;
    }

    auto remapForMeta = [](decltype(input) &in) -> auto& {
        if (in->len != 1)
            return in;
        char &ch = in->str[0];
        const char SHIFTS[] = ")!@#$%^&*(";
        if (ch >= '0' && ch <= '9')
            ch = SHIFTS[ch - '0'];
        return in;
    };

    if (0 != (GDK_CONTROL_MASK & state))
    {
        input.reset(g_string_prepend(remapForMeta(input).release(), "c-"));
    }
    if (0 != (GDK_META_MASK & state) || 0 != (GDK_ALT_MASK & state))
    {
        input.reset(g_string_prepend(remapForMeta(input).release(), "m-"));
    }
    if (0 != (GDK_SUPER_MASK & state))
    {
        input.reset(g_string_prepend(remapForMeta(input).release(), "d-"));
    }

    if (input->len != start_length)
    {
        std::string raw{input->str};
        g_string_printf(input.get(), "<%s>", raw.c_str());
    }

    session->GetInput()->Accept(input->str);
    return true;
}

void GGrid::_OnKeyReleased(guint keyval, guint /*keycode*/, GdkModifierType /*state*/)
{
    if (_alt_pending && keyval == GDK_KEY_Alt_L)
    {
        _window_handler->MenuBarToggle();
    }
}

void GGrid::_CheckSize(int width, int height, Session *session)
{
    if (!session)
        return;

    int cols = std::max(1, static_cast<int>(width / CalcX(1)));
    int rows = std::max(1, static_cast<int>(height / CalcY(1)));
    // Dejitter to request resizing only once
    if (cols == _last_cols && rows == _last_rows)
        return;
    _last_cols = cols;
    _last_rows = rows;
    auto renderer = session->GetRenderer();
    if (renderer && (cols != renderer->GetWidth() || rows != renderer->GetHeight()))
    {
        Logger().info("Grid size change detected rows={} cols={}", rows, cols);
        renderer->OnResized(rows, cols);
    }
}

void GGrid::CheckSize(int width, int height)
{
    auto session = _session.load();
    if (!session)
        return;

    auto renderer = session->GetRenderer();
    if (!renderer)
        return;

    auto guard = renderer->Lock();
    _CheckSize(width, height, session.get());
}

namespace {

class DebugTimer
{
public:
    DebugTimer(const char *msg)
        : _msg{msg}
    {
    }

    void Check(int num)
    {
        auto now = ClockT::now();
        _times[num] += std::chrono::duration<double>(now - _prev_time).count();
        _prev_time = now;
    }

    ~DebugTimer()
    {
        std::ostringstream oss;
        oss << _msg;
        std::set<int> ids;
        for (auto t : _times)
            ids.insert(t.first);
        const char *comma = "";
        for (auto id : ids)
        {
            oss << comma << id << fmt::format(": {:.2f}ms", _times[id] * 1000);
            comma = ", ";
        }
        Logger().debug(oss.str());
    }

private:
    std::string _msg;
    ClockT::time_point _prev_time = ClockT::now();
    std::unordered_map<int, double> _times;
};

std::string XmlEscape(std::string s)
{
    using boost::algorithm::replace_all;
    replace_all(s, "&",  "&amp;");
    replace_all(s, "\"", "&quot;");
    replace_all(s, "\'", "&apos;");
    replace_all(s, "<",  "&lt;");
    replace_all(s, ">",  "&gt;");
    return s;
}

} //namespace

void GGrid::_UpdateLabels(Session *session)
{
    auto start_time = ClockT::now();

    // Count how many new textures are going to be created
    int labels_created{};

    // Create the newly appearing labels
    auto renderer = session->GetRenderer();
    auto &grid_lines = renderer->GetGridLines();

    decltype(_textures) new_textures;
    new_textures.reserve(grid_lines.size());

    for (int row = 0, rowN = grid_lines.size(); row < rowN; ++row)
    {
        const auto &chunk = grid_lines[row];
        if (!chunk)
            continue;

        int y = row * _cell_height;

        auto it = _textures.find(chunk);
        if (it == _textures.end())
        {
            std::string text;
            for (const auto &word : chunk->words)
            {
                auto it = _pango_styles.find(word.hl_id);
                const std::string &pango_style = it == _pango_styles.end()
                    ? _default_pango_style
                    : it->second;
                text += "<span" + pango_style + ">";
                // If a chunk starts with spaces and the first non-space character is
                // has a wide glyph, spaces may be rendered too narrow.
                // To cope with that, we could span spaces with explicit font.
                auto spaces = word.text.find_first_not_of(" ");
                if (spaces)
                {
                    text += "<span font=\"" + _font.GetFamily() + "\">";
                    text += word.text.substr(0, spaces);
                    text += "</span>";
                    if (spaces != std::string::npos)
                        text += XmlEscape(word.text.substr(spaces));
                }
                else
                {
                    text += XmlEscape(word.text);
                }
                text += "</span>";
            }
            Texture t{row, Gtk::Label::new_("").g_obj()};
            t.label.set_markup(text.c_str());
            t.label.set_sensitive(false);
            t.label.set_can_focus(false);
            t.label.set_focus_on_click(false);
            //t.label.get_style_context().add_provider(_css_provider.get(), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

            _grid.put(t.label, 0, y);
            new_textures[chunk] = std::move(t);
            ++labels_created;
        }
        else
        {
            if (it->second.row != row)
            {
                _MoveLabel(it->second.label, y);
                it->second.row = row;
            }
            new_textures[chunk] = it->second;
            _textures.erase(it);
        }
    }

    for (auto &[_, texture]: _textures)
    {
        _MoveLabel(texture.label, -1);
        _grid.remove(texture.label);
    }

    _textures.swap(new_textures);

    auto finish_time = ClockT::now();
    auto duration = ToMs(finish_time - start_time).count();
    Logger().debug("GGrid::_UpdateLabels labels_created={} in {} ms", labels_created, duration);
}

void GGrid::_MoveLabel(Gtk::Label label, int new_y)
{
    int delay = GConfig::GetSmoothScrollDelay();
    if (!delay)
    {
        if (new_y != -1)
            _grid.move(label, 0, new_y);
        return;
    }
    // Check if the label is to be taken out first, no more movement.
    if (-1 == new_y)
    {
        _labels_positions.erase(label.g_obj());
        return;
    }
    // Add the label to the migration horde.
    _labels_positions[label.g_obj()] = new_y;
    // Make sure the migration is happening in the background.
    if (-1u == _scroll_timer_id)
        _scroll_timer_id = _GtkTimer0<&GGrid::_OnMoveLabels>(delay);
}

void GGrid::_OnMoveLabels()
{
    // Move the labels towards their intended positions.
    for (auto it = _labels_positions.begin(); it != _labels_positions.end(); )
    {
        Gtk::Label label{it->first};
        int new_y = it->second;
        double x{}, y{};
        _grid.get_child_position(label, &x, &y);
        // Either half the distance to the target or the final step whole.
        int dy = new_y - y;
        if (dy < -3 || dy > 3)
            dy /= 2; 
        _grid.move(label, x, y + dy);
        if (new_y == y + dy)
            it = _labels_positions.erase(it);
        else
            ++it;
    }

    // If necessary, rearm the timer.
    _scroll_timer_id = _labels_positions.empty()
        ? -1u
        : _GtkTimer0<&GGrid::_OnMoveLabels>(GConfig::GetSmoothScrollDelay());
}

std::string GGrid::DumpMarkup()
{
    std::ostringstream oss;

    auto session = _session.load();
    if (!session)
        return "??";

    auto renderer = session->GetRenderer();
    auto &grid_lines = renderer->GetGridLines();

    for (int row = 0, rowN = grid_lines.size(); row < rowN; ++row)
    {
        const auto &chunk = grid_lines[row];
        if (!chunk)
        {
            oss << "\n";
            continue;
        }

        auto it = _textures.find(chunk);
        if (it == _textures.end())
            oss << "???\n";
        else
        {
            oss << it->second.label.get_label() << "\n";
        }
    }

    return oss.str();
}
