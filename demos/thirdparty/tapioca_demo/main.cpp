/// @file main.cpp
/// @brief tapioca terminal rendering demo — styled console output.

#include <tapioca/canvas.h>
#include <tapioca/console.h>
#include <tapioca/style.h>
#include <tapioca/terminal.h>

#include <cstdio>
#include <string>

int main() {
    // Query terminal capabilities
    auto size = tapioca::terminal::get_size();
    std::printf("Terminal size: %ux%u\n", size.width, size.height);

    // Create a basic console with auto-detected capabilities
    tapioca::basic_console con;

    // Plain text
    con.write("=== tapioca demo ===");
    con.newline();

    // Styled text: bold red foreground
    {
        tapioca::style bold_red;
        bold_red.fg    = tapioca::color::from_rgb(255, 60, 60);
        bold_red.attrs = tapioca::attr::bold;
        con.styled_write(bold_red, "Bold red text");
        con.newline();
    }

    // Styled text: green on dark background
    {
        tapioca::style green_on_dark;
        green_on_dark.fg = tapioca::color::from_rgb(80, 220, 100);
        green_on_dark.bg = tapioca::color::from_rgb(30, 30, 30);
        con.styled_write(green_on_dark, "Green on dark background");
        con.newline();
    }

    // Canvas: create a small character grid and fill it
    {
        constexpr uint32_t cw = 20;
        constexpr uint32_t ch = 5;
        tapioca::canvas cvs(cw, ch);

        for (uint32_t y = 0; y < ch; ++y) {
            for (uint32_t x = 0; x < cw; ++x) {
                tapioca::cell c;
                c.codepoint = (x == 0 || x == cw - 1 || y == 0 || y == ch - 1)
                                  ? U'#' : U'.';
                cvs.set(x, y, c);
            }
        }
        cvs.swap();

        con.newline();
        con.write("Canvas (20x5):");
        con.newline();
        std::string line;
        for (uint32_t y = 0; y < ch; ++y) {
            line.clear();
            for (uint32_t x = 0; x < cw; ++x) {
                line += static_cast<char>(cvs.get_current(x, y).codepoint);
            }
            con.write(line);
            con.newline();
        }
    }

    con.write("=== done ===");
    con.newline();
    return 0;
}
