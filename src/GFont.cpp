#include "GFont.hpp"
#include "GConfig.hpp"
#include "Logger.hpp"

//#include <Gtk/FontChooserDialog.hpp>
//#include <Gtk/Dialog.hpp>
#include <Gtk/ResponseType.hpp>

//#ifdef GIR_INLINE
//#include <Gtk/FontChooserDialog.ipp>
//#endif


GFont::GFont(Gtk::Window parent)
    : _parent{parent}
{
    _family = GConfig::GetFontFamily();
    _size_pt = GConfig::GetFontSize();
}

void GFont::SetGuiFont(const std::string &value)
{
    SetGuiFont(value, _parent);
}

void GFont::SetGuiFont(const std::string &value, Gtk::Window parent)
{
    if (value.empty())
        return;
    if (value[0] == '*')
    {
        //using FCD = Gtk::FontChooserDialog;
        //FCD dlg = FCD::new_("Choose the font", parent).g_obj();
        //dlg.set_font(fmt::format("{} {}", _family, _size_pt).c_str());
        //dlg.set_level(Gtk::FontChooserLevel::family | Gtk::FontChooserLevel::size);
        //dlg.set_filter_func([](const PangoFontFamily *family, const PangoFontFace *, gpointer) -> gboolean {
        //        return pango_font_family_is_monospace(const_cast<PangoFontFamily *>(family));
        //    },
        //    nullptr, nullptr);
        //dlg.on_response(dlg, [this](FCD d, gint response) {
        //    d.close();
        //    if (Gtk::ResponseType::ok == response)
        //    {
        //        _family = pango_font_family_get_name(d.get_font_family());
        //        _size_pt = 1. * d.get_font_size() / PANGO_SCALE;
        //        Logger().info("Font: {}:{}", _family, _size_pt);
        //        GConfig::SetFontFamily(_family);
        //        GConfig::SetFontSize(_size_pt);
        //        OnChanged();
        //    }
        //    d.destroy();
        //});
        //dlg.show();
        return;
    }
    auto idx = value.find_first_of(":,");
    _family = value.substr(0, idx);
    Logger().info("Set guifont {}", _family);
    GConfig::SetFontFamily(_family);
    OnChanged();
}

void GFont::OnChanged()
{
    for (auto &[_, sub] : _subs)
        sub();
}

void GFont::SetSizePt(double size_pt)
{
    _size_pt = size_pt;
    GConfig::SetFontSize(size_pt);
}
