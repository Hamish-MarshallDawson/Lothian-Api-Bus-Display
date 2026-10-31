#include "widgets.h"

#include "fonts/cond_bold_12.h"
#include "fonts/cond_bold_24.h"
#include "fonts/cond_regular_24.h"
#include "text.h"
#include "theme.h"

namespace gfx {

void drawHeader(Canvas &c, const HeaderText &h) {
    using namespace theme;
    const FontFace &title = fonts::kCondBold24;
    const FontFace &clock = fonts::kCondRegular24;
    const FontFace &caps = fonts::kCondBold12;

    c.fillRect(Rect{0, 0, SCREEN_W, HEADER_H}, HEADER_BG);
    c.hline(0, SCREEN_W - 1, HEADER_H - 1, DIVIDER);

    // Right-hand value first, so the title can take what is left
    int rightW = h.right.empty() ? 0 : textWidth(clock, h.right);
    if (!h.right.empty())
        drawText(c, clock, SCREEN_W - MARGIN - rightW, 30, h.right, TEXT);

    int titleMax = SCREEN_W - 2 * MARGIN - (rightW ? rightW + 12 : 0);
    drawText(c, title, MARGIN, 30, fitText(title, h.title, titleMax), TEXT);

    if (!h.kicker.empty())
        drawText(c, caps, MARGIN, 52, h.kicker, TEXT_MUTED);
    if (!h.hint.empty())
        drawTextIn(c, caps, Rect{0, 0, SCREEN_W - MARGIN, 0}, 52, Align::Right, h.hint, TEXT_MUTED);
}

}  // namespace gfx
