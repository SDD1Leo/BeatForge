#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace beatforge
{
    // The user guide (USER_GUIDE.md, built into the app) in a NEBULA window.
    // Renders the small Markdown subset the guide uses: headings, paragraphs,
    // bullet and numbered lists, **bold**, `code` and [links](...) (as text).
    class UserGuideDialog final : public juce::Component
    {
    public:
        UserGuideDialog();
        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        class Page;
        juce::Viewport viewport;
        std::unique_ptr<Page> page;
    };
}
