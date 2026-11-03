#pragma once

#include "PluginProcessor.h"
#include "dsp/synth/Mixer.h"
#include "dsp/synth/MicSim.h"

// THE MIXER page (PR7-D 0.20.0, buses and sends 0.21.0): a multi-mic kit's mic channels and the mixer's buses, as a
// sampled drum kit's mixer has them (inspired by the reference's mixer chapter; our own look and words). A strip per
// channel, then per bus, scrolling sideways (MICS / BUSES show or hide each group), top to bottom:
//   - BLEED (mic channels): on, and its level (every other drum heard through this mic, together);
//   - two SENDS: where each sends a copy (a bus, an FX bus, DELAY, REVERB); click one to choose;
//   - PAN, the FADER (with its meter), M (mute), S (solo), the polarity (flip);
//   - OUTPUT: MAIN, OUT 2 to 16, a bus (a bus: only a later one) or an FX bus; and its name (click it to choose it).
// Beside them, the chosen strip's PROPERTIES. BLEED (mic channels): BLEED FROM INSTRUMENTS (each drum that bleeds
// into it: on, level, polarity) and CLOSE MIC AUDIO (the drums it's meant for), TIME OFFSET, RELEASE. SENDS: each
// send's target, POST / PRE FADER, its DIRECT and BLEED sound's on, level and pan, LINK.
// MENU...: MUTE MODE (pre / post send), STEREO REVERSE, STEREO OUTPUTS, MULTICHANNEL OUTPUTS.
// Channels no loaded kit plays into are dimmed. Only multi-mic drums use the mixer; the rest go their own way.
class MixerPage final : public juce::Component,
                        private juce::Timer
{
public:
    explicit MixerPage (ParagonX1AudioProcessor&);
    ~MixerPage() override;

    enum class Tab { bleed, sends, effects };

    void selectChannel (int channel) { selectStrip (synth::mixer::stripOfChannel (channel)); }   // a mic channel (0.23.0: USER too)
    void selectStrip (int strip);                    // a channel, or numChannels + a bus
    int getSelectedChannel() const noexcept { return selected; }
    int getSelectedStrip() const noexcept { return selected; }
    void showTab (Tab);
    Tab getTab() const noexcept { return tab; }
    void showGroups (bool mics, bool buses);         // MICS / BUSES
    bool isStripShown (int strip) const;
    void scrollToStrip (int strip);
    juce::Array<int> getChannelsInUse() const;      // the channels a loaded kit plays into
    // For tests: a bleed cell set as its row's controls would (one undo step).
    void setBleed (int piece, float gainDb, bool invert, bool on);
    // The chosen strip's send (as its editor sets it), and a strip's send's target (as its slot's menu does); each
    // one undo step when final.
    void setSend (int slot, const synth::mixer::Send&, bool final = true);
    void setSendTarget (int strip, int slot, int target);
    juce::String describeOutput (int strip) const;   // what its OUTPUT button says
    juce::String describeSend (int strip, int slot) const;
    // THE EFFECTS (0.22.0): a strip's slot's effect (a new type: its defaults; 0 empties it), the chosen strip's slot
    // as its editor sets it (one undo step when final), the slot the EFFECTS tab edits, copy / paste.
    void setEffectType (int strip, int slot, int type);
    void setEffect (int slot, const synth::mixer::FxSlot&, bool final = true);
    void chooseEffectSlot (int slot);
    int getChosenEffectSlot() const noexcept { return chosenFx; }
    void copyEffect (int strip, int slot);
    void pasteEffect (int strip, int slot);
    juce::String describeEffect (int strip, int slot) const;
    // ROUTE INSTRUMENT MICROPHONES (0.23.0): the panel over the strips; the drum it shows; that drum's mics (as
    // channels, close first); after a route changes (the strips shown, the panel's menus).
    void openRouter (bool open);
    bool isRouterOpen() const;
    void chooseRouterPiece (int piece);
    int getRouterPiece() const;
    juce::Array<int> getRouterMics() const;
    void routingChanged();
    // SIMULATED MICS AND ROOM (0.26.0): the panel over the strips; whether a drum (all its articulations) goes
    // through them.
    void openMicSim (bool open);
    bool isMicSimOpen() const;
    bool isPieceMiked (int piece) const;
    void setPieceMiked (int piece, bool miked);
    void runMenuItem (int item);                     // MENU...'s (tests)
    enum MenuItem { preSend = 1, postSend, stereoReverse, stereoOutputs, multichannelOutputs };
    juce::String getNote() const { return note; }
    void update();   // the meters, the buttons' words, which channels are in use (the timer; snapshots)

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int bleedRowHeight = 26;   // (0.36.0) the BLEED table's rows, fitted to the panel
    void timerCallback() override;
    void refreshProperties();
    void showOutputMenu (int strip, juce::Component& from);
    void showSendMenu (int strip, int slot, juce::Component& from);
    void showEffectMenu (int strip, int slot, juce::Component& from);
    void showMixerMenu();

    class Strip;
    class PieceRow;
    class SendEditor;
    class EffectEditor;
    class MicRouter;
    std::unique_ptr<MicRouter> router;
    class MicSimPanel;
    std::unique_ptr<MicSimPanel> micSimPanel;
    juce::TextButton micSimButton { "SIM MICS..." };
    juce::Array<int> shownUsers;
    std::vector<juce::String> shownUserNames, shownBusNames;
    juce::TextButton routeButton { "ROUTE MICS..." };
    std::unique_ptr<EffectEditor> effectEditor;
    int chosenFx = 0;
    std::optional<synth::mixer::FxSlot> fxClipboard;
    std::array<synth::mixer::FxSlot, synth::mixer::numFxSlots> shownEffects {};
    ParagonX1AudioProcessor& processor;
    juce::Viewport viewport;
    juce::Component holder;   // the strips, in the viewport
    std::vector<std::unique_ptr<Strip>> strips;
    std::vector<std::unique_ptr<PieceRow>> rows;
    std::vector<std::unique_ptr<SendEditor>> sendEditors;
    juce::TextButton micsButton { "MICS" }, busesButton { "BUSES" }, menuButton { "MENU..." },
                     bleedTab { "BLEED" }, sendsTab { "SENDS" }, effectsTab { "EFFECTS" };
    juce::Slider offset { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 release { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> offsetAttachment, releaseAttachment;
    juce::Rectangle<int> stripsArea, propertiesArea, propertiesBody;
    int selected = synth::mixer::snareTop;
    Tab tab = Tab::bleed;
    juce::Array<int> inUse;
    juce::String note;
    std::array<synth::mixer::Send, synth::mixer::numSendSlots> shownSends {};   // what the send editors show
};
