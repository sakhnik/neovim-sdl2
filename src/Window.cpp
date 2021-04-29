#include "Window.hpp"
#include "Logger.hpp"
#include <sstream>
#include <fstream>

void Window::Init()
{
    SDL_Init(SDL_INIT_VIDEO);

    const int WIN_W = 1024;
    const int WIN_H = 768;

    _window.reset(SDL_CreateWindow("nvim-ui",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        WIN_W, WIN_H,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI));
    _renderer.reset(SDL_CreateRenderer(_window.get(), -1, SDL_RENDERER_ACCELERATED));

    SDL_RendererInfo info;
    SDL_GetRendererInfo(_renderer.get(), &info);
    std::ostringstream oss;
    if ((info.flags & SDL_RENDERER_SOFTWARE))
        oss << " software";
    if ((info.flags & SDL_RENDERER_ACCELERATED))
        oss << " accelerated";
    if ((info.flags & SDL_RENDERER_PRESENTVSYNC))
        oss << " vsync";
    if ((info.flags & SDL_RENDERER_TARGETTEXTURE))
        oss << " target_texture";
    Logger().info("Using renderer {}:{}", info.name, oss.str());

    // Get the window size in pixels to cope with HiDPI
    int wp{}, hp{};
    SDL_GetRendererOutputSize(_renderer.get(), &wp, &hp);
    _scale_x = static_cast<double>(wp) / WIN_W;
    _scale_y = static_cast<double>(hp) / WIN_H;
    _painter.reset(new Painter(_scale_x, _scale_y));
}

void Window::Deinit()
{
    SDL_Quit();
}

Window::RowsColsT
Window::GetRowsCols() const
{
    int wp{}, hp{};
    SDL_GetRendererOutputSize(_renderer.get(), &wp, &hp);

    int cols = std::max(1, wp / _painter->GetCellWidth());
    int rows = std::max(1, hp / _painter->GetCellHeight());
    return {rows, cols};
}

void Window::Clear(unsigned bg)
{
    SDL_SetRenderDrawColor(_renderer.get(), bg >> 16, (bg >> 8) & 0xff, bg & 0xff, SDL_ALPHA_OPAQUE);
    SDL_SetRenderDrawBlendMode(_renderer.get(), SDL_BLENDMODE_NONE);
    SDL_RenderClear(_renderer.get());
}

namespace {

struct Texture : Window::ITexture
{
    Texture(SDL_Texture *t, int width, int height)
        : texture{t, SDL_DestroyTexture}
        , width{width}
        , height{height}
    {
    }
    ::PtrT<SDL_Texture> texture;
    int width;
    int height;
};

} //namespace;

void Window::CopyTexture(int row, int col, ITexture *texture)
{
    Texture *t = static_cast<Texture *>(texture);

    SDL_Rect srcrect = { 0, 0, t->width, t->height };
    SDL_Rect dstrect = { col * _painter->GetCellWidth(), _painter->GetCellHeight() * row, t->width, t->height };
    SDL_RenderCopy(_renderer.get(), t->texture.get(), &srcrect, &dstrect);
}

void Window::_DumpSurface(SDL_Surface *surface, const char *fname)
{
    std::ofstream ofs(fname);
    ofs << "P3 " << surface->w << " " << surface->h << " 255\n";
    for (int y = 0; y < surface->h; ++y)
    {
        for (int x = 0; x < surface->w; ++x)
        {
            uint8_t *c = static_cast<uint8_t*>(surface->pixels) + y * surface->pitch + 4 * x;
            ofs << (unsigned)c[2];
            ofs << " " << (unsigned)c[1];
            ofs << " " << (unsigned)c[0] << "\n";
        }
    }
}

void Window::_DumpTexture(SDL_Texture *texture, const char *fname)
{
    int width, height;
    SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);
    ::PtrT<SDL_Texture> target_texture(SDL_CreateTexture(_renderer.get(), SDL_PIXELFORMAT_RGBA32,
                                                         SDL_TEXTUREACCESS_TARGET, width, height),
                                       SDL_DestroyTexture);
    SDL_Texture *target = SDL_GetRenderTarget(_renderer.get());
    SDL_SetRenderTarget(_renderer.get(), target_texture.get());
    SDL_SetRenderDrawColor(_renderer.get(), 0x00, 0x00, 0x00, 0x00);
    SDL_RenderClear(_renderer.get());
    SDL_RenderCopy(_renderer.get(), texture, NULL, NULL);
    ::PtrT<SDL_Surface> surface(SDL_CreateRGBSurface(0, width, height, 32, 0, 0, 0, 0), SDL_FreeSurface);
    SDL_RenderReadPixels(_renderer.get(), nullptr, surface->format->format, surface->pixels, surface->pitch);
    _DumpSurface(surface.get(), fname);
    SDL_SetRenderTarget(_renderer.get(), target);
}

Window::ITexture::PtrT
Window::CreateTexture(int width, std::string_view text, const HlAttr &attr, const HlAttr &def_attr)
{
    bool has_text = 0 != text.rfind("  ", 0);
    int pixel_width = (width + (has_text ? 1 : 0)) * _painter->GetCellWidth();
    int pixel_height = _painter->GetCellHeight();

    // Allocate a surface slightly wider than necessary
    auto surface = PtrT<SDL_Surface>(SDL_CreateRGBSurface(0,
        pixel_width, pixel_height,
        32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0),
        SDL_FreeSurface);

    // Paint the background color
    unsigned fg = attr.fg.value_or(def_attr.fg.value());
    unsigned bg = attr.bg.value_or(def_attr.bg.value());
    if ((attr.flags & HlAttr::F_REVERSE))
        std::swap(bg, fg);
    auto color = SDL_MapRGB(surface->format, bg >> 16, (bg >> 8) & 0xff, bg & 0xff);
    SDL_FillRect(surface.get(), nullptr, color);

    // not starts with "  "
    if (has_text)
    {
        pixel_width = _painter->Paint(surface.get(), text, attr, def_attr);
        //if (text == "Hello, world!")
        //    _DumpSurface(surface.get(), "/tmp/hello.ppm");
    }

    // Create a possibly hardware accelerated texture from the surface
    std::unique_ptr<Texture> texture{new Texture(SDL_CreateTextureFromSurface(_renderer.get(), surface.get()),
                                                 pixel_width, pixel_height)};
    SDL_SetTextureBlendMode(texture->texture.get(), SDL_BLENDMODE_NONE);
    //if (text == "Hello, world!")
    //    _DumpTexture(texture->texture.get(), "/tmp/hello2.ppm");
    return Texture::PtrT(texture.release());
}

void Window::Present()
{
    SDL_RenderPresent(_renderer.get());
}

void Window::DrawCursor(int row, int col, unsigned fg, std::string_view mode)
{
    int cell_width = _painter->GetCellWidth();
    int cell_height = _painter->GetCellHeight();

    SDL_SetRenderDrawBlendMode(_renderer.get(), SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(_renderer.get(), fg >> 16, (fg >> 8) & 0xff, fg & 0xff, 127);
    SDL_Rect rect;
    if (mode == "insert")
    {
        rect = {
            col * cell_width,
            cell_height * row,
            cell_width / 4,
            cell_height
        };
    }
    else if (mode == "replace" || mode == "operator")
    {
        rect = {
            col * cell_width,
            cell_height * row + cell_height * 3 / 4,
            cell_width,
            cell_height / 4
        };
    }
    else
    {
        rect = {
            col * cell_width,
            cell_height * row,
            cell_width,
            cell_height
        };
    }
    SDL_RenderFillRect(_renderer.get(), &rect);
}

void Window::SetBusy(bool is_busy)
{
    if (is_busy)
    {
        if (!_busy_cursor)
            _busy_cursor.reset(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAITARROW));
        SDL_SetCursor(_busy_cursor.get());
    }
    else
    {
        if (!_active_cursor)
            _active_cursor.reset(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW));
        SDL_SetCursor(_active_cursor.get());
    }
}
