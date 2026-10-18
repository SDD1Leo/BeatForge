#include "ui/UserGuideDialog.h"
#include "ui/Theme.h"

#include "BinaryData.h"

namespace beatforge
{
    namespace
    {
        enum class Kind { title, heading, paragraph, bullet, numbered };

        struct Block
        {
            Kind kind;
            juce::String text, marker;   // marker: "1." for numbered items
        };

        // **bold** and `code` become runs; [text](url) keeps the text.
        juce::AttributedString styled (const juce::String& source, float size, juce::Colour colour, bool bold = false)
        {
            auto text = source;
            for (int open; (open = text.indexOf ("](")) > 0;)
            {
                const int start = text.substring (0, open).lastIndexOfChar ('[');
                const int close = text.indexOfChar (open, ')');
                if (start < 0 || close < 0) break;
                text = text.substring (0, start) + text.substring (start + 1, open) + text.substring (close + 1);
            }

            juce::AttributedString as;
            as.setWordWrap (juce::AttributedString::byWord);
            as.setLineSpacing (3.0f);
            bool strong = bold, code = false;
            juce::String run;
            auto flush = [&]
            {
                if (run.isEmpty()) return;
                as.append (run, code ? theme::numericFont (size - 0.5f) : theme::font (size, strong ? juce::Font::bold : juce::Font::plain),
                           code ? theme::cyan : (strong && ! bold ? theme::textPrimary : colour));
                run.clear();
            };
            for (int i = 0; i < text.length(); ++i)
            {
                if (text.substring (i, i + 2) == "**") { flush(); strong = ! strong || bold; ++i; continue; }
                if (text[i] == '`') { flush(); code = ! code; continue; }
                run += text[i];
            }
            flush();
            return as;
        }

        std::vector<Block> parse (const juce::String& markdown)
        {
            std::vector<Block> blocks;
            juce::String paragraph;
            auto endParagraph = [&]
            {
                if (paragraph.isNotEmpty()) blocks.push_back ({ Kind::paragraph, paragraph.trim(), {} });
                paragraph.clear();
            };

            for (auto line : juce::StringArray::fromLines (markdown))
            {
                const auto t = line.trim();
                const bool continuation = line.startsWith ("  ") && ! blocks.empty() && paragraph.isEmpty()
                                          && (blocks.back().kind == Kind::bullet || blocks.back().kind == Kind::numbered);
                if (t.isEmpty())                   { endParagraph(); continue; }
                if (continuation)                  { blocks.back().text << " " << t; continue; }
                if (t.startsWith ("## "))          { endParagraph(); blocks.push_back ({ Kind::heading, t.substring (3), {} }); continue; }
                if (t.startsWith ("# "))           { endParagraph(); blocks.push_back ({ Kind::title, t.substring (2), {} }); continue; }
                if (t.startsWith ("- "))           { endParagraph(); blocks.push_back ({ Kind::bullet, t.substring (2), {} }); continue; }
                if (const auto dot = t.indexOf (". "); dot > 0 && dot <= 3 && t.substring (0, dot).containsOnly ("0123456789"))
                {
                    endParagraph();
                    blocks.push_back ({ Kind::numbered, t.substring (dot + 2), t.substring (0, dot + 1) });
                    continue;
                }
                paragraph << (paragraph.isEmpty() ? "" : " ") << t;
            }
            endParagraph();
            return blocks;
        }
    }

    //==============================================================================
    class UserGuideDialog::Page final : public juce::Component
    {
    public:
        Page() : blocks (parse (juce::String::fromUTF8 (BinaryData::USER_GUIDE_md, BinaryData::USER_GUIDE_mdSize))) {}

        // Lays the text out for `width` and returns the height it needs.
        int layout (int width)
        {
            laid.clear();
            float y = 8.0f;
            for (auto& b : blocks)
            {
                const bool listItem = b.kind == Kind::bullet || b.kind == Kind::numbered;
                const float indent = listItem ? 26.0f : 0.0f;
                const float size = b.kind == Kind::title ? 22.0f : b.kind == Kind::heading ? 16.0f : 13.5f;
                const auto colour = b.kind == Kind::paragraph || listItem ? theme::textSecondary : theme::textPrimary;
                if (b.kind == Kind::heading) y += 14.0f;

                Laid l;
                l.layout.createLayout (styled (b.text, size, colour, b.kind == Kind::title || b.kind == Kind::heading), (float) width - indent);
                l.x = indent;
                l.y = y;
                l.marker = b.kind == Kind::bullet ? juce::String::fromUTF8 ("\xe2\x80\xa2") : b.marker;
                l.rule = b.kind == Kind::heading;
                y += l.layout.getHeight() + (b.kind == Kind::title ? 14.0f : listItem ? 6.0f : 10.0f);
                laid.push_back (std::move (l));
            }
            return (int) std::ceil (y + 16.0f);
        }

        void paint (juce::Graphics& g) override
        {
            for (auto& l : laid)
            {
                if (l.rule)
                {
                    g.setColour (theme::border);
                    g.drawHorizontalLine ((int) l.y - 7, 0.0f, (float) getWidth());
                }
                if (l.marker.isNotEmpty())
                {
                    g.setColour (theme::accent);
                    const bool bullet = ! l.marker.endsWithChar ('.');
                    g.setFont (theme::font (bullet ? 18.0f : 13.5f, juce::Font::bold));
                    const auto firstLine = l.layout.getNumLines() > 0 ? l.layout.getLine (0).getLineBoundsY().getEnd() : 20.0f;
                    g.drawText (l.marker, juce::Rectangle<float> (4.0f, l.y, 22.0f, firstLine), juce::Justification::centredLeft);
                }
                l.layout.draw (g, { l.x, l.y, (float) getWidth() - l.x, l.layout.getHeight() });
            }
        }

    private:
        struct Laid
        {
            juce::TextLayout layout;
            float x = 0.0f, y = 0.0f;
            juce::String marker;
            bool rule = false;
        };

        std::vector<Block> blocks;
        std::vector<Laid> laid;
    };

    //==============================================================================
    UserGuideDialog::UserGuideDialog() : page (std::make_unique<Page>())
    {
        viewport.setViewedComponent (page.get(), false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);
        setSize (760, 640);
    }

    void UserGuideDialog::paint (juce::Graphics& g)
    {
        g.fillAll (theme::panel);
    }

    void UserGuideDialog::resized()
    {
        viewport.setBounds (getLocalBounds().reduced (28, 16));
        const auto width = viewport.getWidth() - viewport.getScrollBarThickness() - 8;
        page->setSize (width, page->layout (width));
    }
}
