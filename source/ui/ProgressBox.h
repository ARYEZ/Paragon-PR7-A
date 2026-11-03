#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// A long job's progress (0.40.0, the owner's ask: "the progress bar that the PR7-S uses for things like rendering"):
// PR7-S 0.13's progress box, in its look (black pearl, the red glow along its edge, the PR7 mark, what's being done,
// the bar drawn as the studio's horizontal meter, lit orange to pink-red as it goes, and its percentage), with a
// CANCEL key for a job that can stop.
//
// Unlike PR7-S's, it lives inside the plugin's window (a plugin shouldn't open windows of its own over a DAW), and its
// jobs run on threads of their own (BOUNCE, loading a kit), so it doesn't need to keep a busy screen thread awake:
// MainView shows it while a job runs and its timer follows the job.
class ProgressBox final : public juce::Component,
                          private juce::Timer
{
public:
    ProgressBox();

    // What a job tells the box: its progress (0 to 1; below 0: not running any more) and what it is.
    struct Job
    {
        juce::String what;
        std::function<double()> progress;
        std::function<void()> cancel;   // none: no CANCEL key
    };
    void show (Job);
    bool isShowingJob() const noexcept { return job.progress != nullptr; }
    juce::String getWhat() const { return job.what; }
    double getProgress() const noexcept { return progress; }
    juce::Button& getCancelKey() noexcept { return cancelKey; }
    std::function<void()> onFinished;   // the job's done (or cancelled): the box has hidden itself

    void paint (juce::Graphics&) override;
    void resized() override;
    void update();   // the timer's work, now (tests)

    // The bar on its own: the studio's horizontal meter (PR7-S's LevelMeter::paintSegments, one way only).
    static void paintBar (juce::Graphics&, juce::Rectangle<float>, double progress);

private:
    void timerCallback() override { update(); }

    Job job;
    double progress = 0.0;
    juce::TextButton cancelKey { "CANCEL" };
};
