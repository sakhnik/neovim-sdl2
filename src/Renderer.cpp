#include "Renderer.hpp"
#include "MsgPackRpc.hpp"
#include "IWindow.hpp"
#include "Logger.hpp"
#include <sstream>


Renderer::Renderer(uv_loop_t *loop, MsgPackRpc *rpc)
    : _rpc{rpc}
    , _timer{loop}
    , _async_exec{loop}
{
    // Default hightlight attributes
    _def_attr.fg = 0xffffff;
    _def_attr.bg = 0;

    // Prepare the initial cell grid to fill the whole window.
    // The NeoVim UI will be attached using these dimensions.
    GridResize(80, 25);
}

Renderer::~Renderer()
{
}

void Renderer::SetWindow(IWindow *window)
{
    _window = window;
}

void Renderer::Flush()
{
    // Neovim has indicated that the screen may be shown right at this time.
    _is_clean = true;

    // It's worth limiting flush rate, a user wouldn't necessarily need
    // to observe the intermediate screen states. And the CPU consumption
    // is improved dramatically when limiting the flush rate.

    // 40 ms => 25 FPS (PAL). Perhaps worth making it configurable.
    const int FLUSH_DURATION_MS = 40;

    auto now = ClockT::now();
    if (now - _last_flush_time > std::chrono::milliseconds(FLUSH_DURATION_MS))
    {
        // Do repaint the grid if enough time elapsed since last time.
        // This is useful when fast scrolling.
        _DoFlush();
    }
    else
    {
        // Make sure the final view will be presented if no more flush requests.
        _timer.Start(FLUSH_DURATION_MS, 0, [&] {
            auto lock = Lock();
            _DoFlush();
        });
    }
}

void Renderer::_AnticipateFlush()
{
    // The grid is being updated, cancel the last flush request. A new one is pending.
    _timer.Stop();
}

void Renderer::_DoFlush()
{
    // Something has changed in the screen, wait for the next occasion.
    if (!_is_clean)
        return;

    _AnticipateFlush();
    std::ostringstream oss;
    oss << "";
    _last_flush_time = ClockT::now();

    for (int row = 0, rowN = _lines.size(); row < rowN; ++row)
    {
        auto &line = _lines[row];
        auto &grid_line = line.grid_line;

        // Check if it's possible to just copy the prepared textures first
        if (!line.dirty)
            continue;

        // Mark the line clear as we're going to redraw the necessary parts
        // and update the texture cache.
        line.dirty = false;

        // Split the cells into chunks by the same hl_id
        auto chunks = _SplitChunks(line);

        auto texture_generator = [&](const GridLine::Chunk &chunk) {
            // Paint the text on the surface carefully
            if (!_window)
                return BaseTexture::PtrT{};
            auto hlit = _hl_attr.find(chunk.hl_id);
            return _window->CreateTexture(chunk.width, chunk.text,
                    hlit != _hl_attr.end() ? hlit->second : _def_attr,
                    _def_attr);
        };

        {
            auto grid_line_scanner = grid_line.GetScanner(_redraw_token);

            // Print and cache the chunks individually
            for (size_t i = 1; i < chunks.size(); ++i)
            {
                int col = chunks[i - 1];
                int end = chunks[i];
                int hl_id = line.hl_id[col];
                GridLine::Chunk chunk(col, end - col, hl_id, "");
                while (col < end)
                    chunk.text += line.text[col++];

                const auto hlit = _hl_attr.find(hl_id);
                unsigned def_bg = _def_attr.bg.value();

                if (chunk.IsSpace()
                    && (hlit == _hl_attr.end()                                   // No highlighting
                        || (hlit->second.bg.value_or(def_bg) == def_bg           // Default background
                            && 0 == (hlit->second.flags & HlAttr::F_REVERSE))))  // No reverse (foreground becomes background)
                {
                    // No need to create empty chunks coinciding with the background color
                    continue;
                }

                // Test whether the chunk should be rendered again
                if (grid_line_scanner.EnsureNext(std::move(chunk), texture_generator))
                    oss << "+";
                else
                    oss << ".";
            }
        }

        grid_line.CopyTo(_grid_lines[row]);
    }

    if (_window)
        _window->Present(_redraw_token);
    // This flush has been handled, advance the token to start accepting
    // handling new changes.
    ++_redraw_token;

    auto end_time = ClockT::now();
    oss << " " << std::chrono::duration<double>(end_time - _last_flush_time).count();
    Logger().debug("Flush {}", oss.str());
}

std::vector<size_t> Renderer::_SplitChunks(const _Line &line)
{
    // Split the line into the chunks with contiguous highlighting.
    // However, contiguous spaces should form their own chunk to avoid unnecessary text rerendering.
    const auto &hl = line.hl_id;
    const auto &text = line.text;

    std::vector<size_t> chunks;
    chunks.push_back(0);
    chunks.push_back(1);
    bool is_space = false;
    while (chunks.back() < hl.size())
    {
        size_t back = chunks.back();
        if (hl[back] != hl[chunks[chunks.size() - 2]])
        {
            chunks.push_back(back + 1);
            is_space = false;
        }
        else if (!is_space && back > 0
                 && text[back] == " " && text[back - 1] == " ")
        {
            // Make sure contiguous spaces form their own chunk
            is_space = true;
            // If the line started with spaces, no need to form a new chunk. Otherwise:
            if (back > 1)
            {
                chunks.back() = back - 1;
                // Remove empty chunks immediately
                if (chunks[chunks.size() - 2] == chunks.back())
                    chunks.resize(chunks.size() - 1);
                chunks.push_back(back + 1);
            }
        }
        else if (is_space && text[back] != " ")
        {
            // End of space chunk
            is_space = false;
            chunks.push_back(back + 1);
        }
        else
            ++chunks.back();
    }
    return chunks;
}

void Renderer::GridLine(int row, int col, std::string_view chunk, unsigned hl_id, int repeat)
{
    Logger().debug("Line row={} col={} text={} hl_id={} repeat={}", row, col, chunk, hl_id, repeat);
    _AnticipateFlush();
    _is_clean = false;

    _Line &line = _lines[row];
    line.dirty = true;

    for (int i = 0; i < repeat; ++i)
    {
        line.text[col + i] = chunk;
        line.hl_id[col + i] = hl_id;
    }
}

void Renderer::GridCursorGoto(int row, int col)
{
    Logger().debug("CursorGoto row={} col={}", row, col);
    _AnticipateFlush();
    _cursor_row = row;
    _cursor_col = col;
}

void Renderer::GridScroll(int top, int bot, int left, int right, int rows)
{
    Logger().debug("Scroll top={} bot={} left={} right={} rows={}", top, bot, left, right, rows);
    _AnticipateFlush();
    _is_clean = false;
    auto copy = [&](int row, int row_from) {
        auto &line_from = _lines[row_from];
        auto &line_to = _lines[row];
        line_to.dirty = true;
        for (int col = left; col < right; ++col)
        {
            line_to.text[col] = std::move(line_from.text[col]);
            line_to.hl_id[col] = std::move(line_from.hl_id[col]);
        }
        line_to.grid_line.MoveFrom(line_from.grid_line, left, right, _redraw_token);
    };

    if (rows > 0)
    {
        for (int row = top; row < bot - rows; ++row)
            copy(row, row + rows);
    }
    else if (rows < 0)
    {
        for (int row = bot - 1; row > top - rows - 1; --row)
            copy(row, row + rows);
    }
    else
    {
        throw std::runtime_error("Rows should not equal 0");
    }
}

void Renderer::GridClear()
{
    Logger().debug("Clear");
    _AnticipateFlush();
    _is_clean = false;
    for (auto &line : _lines)
    {
        line.dirty = true;
        for (auto &t : line.text)
            t = ' ';
        for (auto &hl : line.hl_id)
            hl = 0;
    }
}

void Renderer::HlAttrDefine(unsigned hl_id, HlAttr attr)
{
    Logger().debug("HlAttrDefine {}", hl_id);
    _hl_attr[hl_id] = attr;
    _hl_attr_modified = true;
}

void Renderer::DefaultColorSet(unsigned fg, unsigned bg)
{
    Logger().debug("DefaultColorSet fg={} bg={}", fg, bg);

    for (auto &line : _lines)
    {
        line.dirty = true;
        line.grid_line.Clear();
    }

    _def_attr.fg = fg;
    _def_attr.bg = bg;
}

void Renderer::OnResized(int rows, int cols)
{
    if (rows != static_cast<int>(_lines.size()) ||
        cols != static_cast<int>(_lines[0].text.size()))
    {
        _async_exec.Post([rows, cols, this] {
            _rpc->Request(
                [rows, cols](auto &pk) {
                    pk.pack("nvim_ui_try_resize");
                    pk.pack_array(2);
                    pk.pack(cols);
                    pk.pack(rows);
                },
                [](const msgpack::object &err, const auto &/*resp*/) {
                    if (!err.is_nil())
                    {
                        std::ostringstream oss;
                        oss << "Failed to resize UI " << err << std::endl;
                        throw std::runtime_error(oss.str());
                    }
                }
            );
        });
    }
}

void Renderer::GridResize(int width, int height)
{
    Logger().debug("GridResize width={} height={}", width, height);
    _lines.resize(height);
    for (auto &line : _lines)
    {
        line.hl_id.resize(width, 0);
        line.text.resize(width, " ");
    }

    _grid_lines.resize(height);
}

void Renderer::ModeChange(std::string_view mode)
{
    Logger().debug("ModeChange {}", mode);
    _mode = mode;
}

void Renderer::SetBusy(bool is_busy)
{
    Logger().debug("SetBusy {}", is_busy);
    _is_busy = is_busy;
}
