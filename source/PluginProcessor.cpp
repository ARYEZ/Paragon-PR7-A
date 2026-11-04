#include "PluginProcessor.h"
#include "dsp/synth/DrumGizmo.h"
#include "dsp/synth/SamplePack.h"
#include "dsp/synth/Instrument.h"

#include <map>

#include "Parameters.h"
#include "PluginEditor.h"

namespace
{
    constexpr double gainRampSeconds = 0.02;

    // Extra settings kept in the saved state (DAW project).
    const juce::Identifier rigNameKey { "rigName" };
    const juce::Identifier rigFileKey { "rigFile" };
    const juce::Identifier rigFactoryKey { "rigFactory" };
    const juce::Identifier rigModifiedKey { "rigModified" };
    // Which version's parameters a project was saved with, as for presets (see Rig.cpp).
    const juce::Identifier parameterVersionKey { "parameterVersion" };
    // The kit's samples, kept in the project as presets keep them.
    const juce::Identifier drumSamplesKey { "DrumSamples" }, drumSampleKey { "DrumSample" };
    constexpr double retiredTableMs = 2000.0;   // how long a replaced sample or pattern is kept, in case a block is still playing it
    const juce::Identifier modulationKey { "Modulation" };
    const juce::Identifier midiLinksKey { "MidiLinks" };
    const juce::Identifier noteMapKey { "NoteMap" };
    constexpr int parameterVersion = 5;   // 2: 0.17.0 (the articulations: older projects load with drums 21 to 23 off)
                                          // 3: 0.33.0 (drums 24 to 29 off; layers' instrument numbers moved)
                                          // 4: 0.36.0 (drums 30 to 36 likewise)
                                          // 5: 0.39.0 (the drum buses: older projects keep their channels' routes)
}

//==============================================================================
// THE OUTPUTS (0.3.0): MAIN, on from the start; OUT 2 to 16 off until the DAW switches them on (as hosts expect of a
// multi-output instrument: Cubase, for one, makes a mixer channel for each output that starts on).
juce::AudioProcessor::BusesProperties ParagonX1AudioProcessor::makeOutputBuses()
{
    static_assert (synth::numOutputs == ParamIDs::numOutputs);
    auto buses = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
    for (int o = 1; o < synth::numOutputs; ++o)
        buses = buses.withOutput (ParamIDs::outputNames[o], juce::AudioChannelSet::stereo(), false);
    return buses;
}

ParagonX1AudioProcessor::ParagonX1AudioProcessor()
    : AudioProcessor (makeOutputBuses()),
      parameters (*this, nullptr, "ParagonX1", createParameterLayout())
{
    for (int b = 0; b < ParamIDs::numInsertBuses; ++b)
    {
        auto& bus = rack->buses[(size_t) b];
        const auto find = [this, b] (const char* what)
        {
            auto* value = parameters.getRawParameterValue (ParamIDs::ins (b, what));
            jassert (value != nullptr);
            return value;
        };
        for (size_t s = 0; s < bus.drive_.size(); ++s) bus.drive_[s] = find (ParamIDs::insDriveSettings[s]);
        for (size_t s = 0; s < bus.glitch_.size(); ++s) bus.glitch_[s] = find (ParamIDs::insGlitchSettings[s]);
        for (size_t s = 0; s < bus.lofi_.size(); ++s) bus.lofi_[s] = find (ParamIDs::insLofiSettings[s]);
        bus.sends_ = { find ("SendDelay"), find ("SendReverb") };
        bus.output_ = find ("Output");
    }
    for (int k = 0; k < ParamIDs::numDrums; ++k)
        for (size_t s = 0; s < std::size (ParamIDs::drumSettings); ++s)
        {
            drumParameters[(size_t) k].values[s] = parameters.getRawParameterValue (ParamIDs::drum (k, ParamIDs::drumSettings[s]));
            jassert (drumParameters[(size_t) k].values[s] != nullptr);
        }
    for (int c = 0; c < synth::mixer::numMicChannels; ++c)
        for (size_t s = 0; s < std::size (ParamIDs::mixSettings); ++s)
        {
            mixParameters[(size_t) c][s] = parameters.getRawParameterValue (ParamIDs::mix (c, ParamIDs::mixSettings[s]));
            jassert (mixParameters[(size_t) c][s] != nullptr);
        }
    for (int b = 0; b < synth::mixer::numBuses; ++b)
        for (size_t s = 0; s < std::size (ParamIDs::busSettings); ++s)
        {
            busParameters[(size_t) b][s] = parameters.getRawParameterValue (ParamIDs::mixBus (b, ParamIDs::busSettings[s]));
            jassert (busParameters[(size_t) b][s] != nullptr);
        }
    muteModeParameter = parameters.getRawParameterValue (ParamIDs::mixMuteMode);
    compileMatrix();   // the patterns and envelopes, ready from the start
    for (auto* list : { &samplesForAudio, &reversesForAudio })
        for (auto& sample : *list)
            sample = nullptr;

    // THE RACK: each band's parameters, read every block.
    for (int band = 0; band < ParamIDs::eqNumBands; ++band)
        for (size_t i = 0; i < std::size (ParamIDs::eqBandSettings); ++i)
            rack->eqBandParameters[(size_t) band].values[i] = parameters.getRawParameterValue (ParamIDs::eqBand (band, ParamIDs::eqBandSettings[i]));
    for (int band = 0; band < ParamIDs::domNumBands; ++band)
        for (size_t i = 0; i < std::size (ParamIDs::domBandSettings); ++i)
            rack->dominionBandParameters[(size_t) band].values[i] = parameters.getRawParameterValue (ParamIDs::domBand (band, ParamIDs::domBandSettings[i]));
    for (int band = 0; band < ParamIDs::mbNumBands; ++band)
        for (size_t i = 0; i < std::size (ParamIDs::mbBandSettings); ++i)
            rack->multibandBandParameters[(size_t) band].values[i] = parameters.getRawParameterValue (ParamIDs::mbBand (band, ParamIDs::mbBandSettings[i]));

    // 0.49.0: every parameter's value by its ID in a hash table, for `param` (read some hundred times a block): APVTS's
    // own lookup walks a sorted map with a string compare per step, about 3% of the audio work on a busy preset.
    for (auto* p : juce::AudioProcessor::getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (auto* value = parameters.getRawParameterValue (ranged->getParameterID()))
                rawValues.emplace (std::string (ranged->getParameterID().toRawUTF8()), value);

    rigLibrary = std::make_unique<RigLibrary>();
    rack->setFolder (rigLibrary->getFolder());
    // THE GROOVE PLAYER: its folders and what was read from them (the GROOVES page reads them again when it opens).
    grooveFolders = std::make_unique<grooves::Library> (rigLibrary->getFolder().getChildFile ("Grooves.xml"),
                                                        rigLibrary->getFolder().getChildFile ("Groove Index.xml"));
    grooveFolders->setUserFolder (rigLibrary->getFolder().getChildFile ("User MIDI"));   // 0.16.0: always there, last
    applyGroovePreview();
    // THE NOTE MAP: a new instance starts with the saved CUSTOM map, if one was saved (else the groove libraries' layout).
    {
        auto custom = synth::drums::notemap::grooveLibrary();
        if (const auto xml = juce::XmlDocument::parse (noteMapDefaultFile()); xml != nullptr && xml->hasTagName (noteMapKey))
            custom = synth::drums::notemap::decode (xml->getStringAttribute ("keys"));
        setCustomMap (custom);
    }
    // MIDI LEARN: a new instance starts with the default links, if some were saved.
    if (midiLinksDefaultFile().existsAsFile())
        importMidiLinks (midiLinksDefaultFile());
    // A new PR7-D is the Init preset (every parameter at its default), so the
    // header names it and < > step on from there.
    currentRig = { "Init", {}, "Init" };

    // Any change to a parameter that presets store marks the preset as
    // modified, and finishing an edit to one records an undo step.
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            if (! ParamIDs::notInRigs.contains (withId->paramID))
            {
                parameters.addParameterListener (withId->paramID, this);
                parameter->addListener (this);
            }

    // Housekeeping off the audio thread: record undo steps, render reversed hits, let go of old samples.
    startTimerHz (30);

    // The starting point, so the first edit can be undone.
    history.record (takeSnapshot());
    checkpointWanted = false;
}

ParagonX1AudioProcessor::~ParagonX1AudioProcessor()
{
    // THE SAMPLE ENGINE's loads: none may install after this.
    lifeToken.reset();
    if (instrumentPool != nullptr)
        instrumentPool->removeAllJobs (true, 30000);
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
        {
            parameters.removeParameterListener (withId->paramID, this);
            parameter->removeListener (this);
        }

    stopTimer();
}

void ParagonX1AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedBlockSize = juce::jmax (1, samplesPerBlock);
    keyboardMidi.ensureSize (2048);
    drumMidi.ensureSize (2048);
    keyboardState.reset();
    drumKit.prepare (sampleRate, 512);   // the pieces processChunk renders in
    mixerEffects.prepare (sampleRate, 512);   // the kit runs them in its pieces

    outputGain.reset (sampleRate, gainRampSeconds);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (ParamIDs::outputGain) + param (ParamIDs::patchLevel)));
    bypassAmount.reset (sampleRate, gainRampSeconds);
    bypassAmount.setCurrentAndTargetValue (isOn (ParamIDs::bypass) ? 1.0f : 0.0f);

    // THE RACK.
    rack->sampleRate = sampleRate;
    rack->parametricEq.prepare (sampleRate, preparedBlockSize);
    rack->dominionProcessor.prepare (sampleRate, preparedBlockSize);
    rack->compressor.prepare (sampleRate, preparedBlockSize);
    rack->multiband.prepare (sampleRate, preparedBlockSize);
    rack->compSidechain.setSize (2, preparedBlockSize);
    rack->limiter.prepare (sampleRate, preparedBlockSize);
    rack->eqInputTap.setSampleRate (sampleRate);
    rack->delay.prepare (sampleRate, preparedBlockSize);
    rack->reverb.prepare (sampleRate, preparedBlockSize);
    for (auto& block : rack->modulation)
        block.prepare (sampleRate, preparedBlockSize);
    for (auto& bus : rack->buses)
    {
        for (int c = 0; c < 2; ++c)
        {
            bus.drive[(size_t) c].prepare (sampleRate, preparedBlockSize);
            bus.glitch[(size_t) c].prepare (sampleRate, preparedBlockSize);
        }
        bus.modulation.prepare (sampleRate, preparedBlockSize);
        bus.loFi.prepare (sampleRate, preparedBlockSize);
        bus.buffer.setSize (2, preparedBlockSize);
    }
    rack->loFi.prepare (sampleRate, preparedBlockSize);
    for (auto& send : rack->sends)
        send.setSize (2, preparedBlockSize);
    rack->eqOutputTap.setSampleRate (sampleRate);
    // OUT 2 to 16's hold-back: room for the most MAIN's chain can add (CONSTRICTOR's 20 ms look-ahead, the LIMITER's
    // longest, its oversampling filters and last stage), with time to spare.
    const auto mostLatency = (int) std::ceil ((effects::multiband::lookaheadSeconds + effects::limiter::maxLookaheadMs * 0.001 + 0.02) * sampleRate);
    rack->auxDelay.setSize (2 * (synth::numOutputs - 1), mostLatency + 1);
    rack->auxDelay.clear();
    rack->auxDelayWrite = 0;
    updateLatency();
}

effects::MasterLimiter::Settings ParagonX1AudioProcessor::getLimiterSettings() const
{
    effects::MasterLimiter::Settings s;
    s.on = isOn (ParamIDs::limitOn);
    s.style = (effects::limiter::Style) juce::jlimit (0, ParamRanges::limitModes.size() - 1, juce::roundToInt (param (ParamIDs::limitMode)));
    s.gainDb = param (ParamIDs::limitGain);
    s.ceilingDb = param (ParamIDs::limitCeiling);
    s.lookaheadMs = param (ParamIDs::limitLookahead);
    s.attackMs = param (ParamIDs::limitAttack);
    s.releaseMs = param (ParamIDs::limitRelease);
    s.transientLink = param (ParamIDs::limitLinkTransient) / 100.0f;
    s.releaseLink = param (ParamIDs::limitLinkRelease) / 100.0f;
    s.truePeak = isOn (ParamIDs::limitTruePeak);
    s.oversampling = juce::roundToInt (param (ParamIDs::limitOversampling));
    s.dither = juce::roundToInt (param (ParamIDs::limitDither));
    s.shaping = juce::roundToInt (param (ParamIDs::limitShaping));
    s.unity = isOn (ParamIDs::limitUnity);
    s.delta = isOn (ParamIDs::limitDelta);
    s.dcFilter = isOn (ParamIDs::limitDcFilter);
    s.trimDb = param (ParamIDs::limitTrim);
    s.mix = param (ParamIDs::limitMix) / 100.0f;
    s.link = isOn (ParamIDs::limitLink);
    return s;
}

void ParagonX1AudioProcessor::updateLatency()
{
    const auto latency = mainLatencySamples();
    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

effects::multiband::Settings ParagonX1AudioProcessor::getMultibandSettings() const noexcept
{
    namespace mb = effects::multiband;
    mb::Settings settings;
    settings.lookahead = isOn (ParamIDs::mbLookahead);
    settings.mix = param (ParamIDs::mbMix) / 100.0f;
    settings.soloBand = rack->multibandSolo.load();
    for (size_t i = 0; i < settings.bands.size(); ++i)
    {
        const auto& v = rack->multibandBandParameters[i].values;   // in mbBandSettings' order
        auto& band = settings.bands[i];
        band.used = v[0]->load() > 0.5f;
        band.on = v[1]->load() > 0.5f;
        band.lowHz = v[2]->load();
        band.highHz = v[3]->load();
        band.slope = juce::jlimit (0, mb::numSlopes - 1, juce::roundToInt (v[4]->load()));
        band.thresholdDb = v[5]->load();
        band.rangeDb = v[6]->load();
        band.mode = (mb::Mode) juce::jlimit (0, 1, juce::roundToInt (v[7]->load()));
        band.ratio = v[8]->load();
        band.kneeDb = v[9]->load();
        band.attackMs = v[10]->load();
        band.releaseMs = v[11]->load();
        band.character = (effects::Compressor::Character) juce::jlimit (0, 2, juce::roundToInt (v[12]->load()));
        band.stereo = (mb::Stereo) juce::jlimit (0, 2, juce::roundToInt (v[13]->load()));
        band.link = v[14]->load() / 100.0f;
        band.outputDb = v[15]->load();
        band.lookaheadMs = v[16]->load();
    }
    return settings;
}

eq::ParametricEq::Settings ParagonX1AudioProcessor::getEqSettings() const noexcept
{
    eq::ParametricEq::Settings settings;
    settings.on = isOn (ParamIDs::eqOn);
    settings.outputDb = param (ParamIDs::eqOutput);
    settings.gainScale = param (ParamIDs::eqGainScale);
    settings.autoGain = isOn (ParamIDs::eqAutoGain);
    settings.soloBand = rack->eqSoloBand.load();
    for (size_t i = 0; i < settings.bands.size(); ++i)
    {
        const auto& values = rack->eqBandParameters[i].values;   // in eqBandSettings' order
        auto& band = settings.bands[i];
        band.used = values[0]->load() > 0.5f;
        band.on = values[1]->load() > 0.5f;
        band.shape = (eq::Shape) juce::roundToInt (values[2]->load());
        band.frequency = values[3]->load();
        band.gainDb = values[4]->load();
        band.q = values[5]->load();
        band.slope = juce::roundToInt (values[6]->load());
        band.placement = (eq::Placement) juce::roundToInt (values[7]->load());
        band.dynamicRangeDb = values[8]->load();
        band.autoThreshold = values[9]->load() > 0.5f;
        band.thresholdDb = values[10]->load();
        band.attack = values[11]->load();
        band.release = values[12]->load();
    }
    return settings;
}

dominion::Settings ParagonX1AudioProcessor::getDominionSettings() const noexcept
{
    dominion::Settings s;
    s.on = isOn (ParamIDs::domOn);
    s.mode = (dominion::Mode) juce::jlimit (0, 1, juce::roundToInt (param (ParamIDs::domMode)));
    s.depth = param (ParamIDs::domDepth);
    s.detail = param (ParamIDs::domDetail);
    s.attack = param (ParamIDs::domAttack);
    s.release = param (ParamIDs::domRelease);
    s.stereo = (dominion::StereoMode) juce::jlimit (0, 1, juce::roundToInt (param (ParamIDs::domStereo)));
    s.link = param (ParamIDs::domLink);
    s.focus = param (ParamIDs::domFocus);
    s.detailTiltLow = param (ParamIDs::domDetailLow);
    s.detailTiltHigh = param (ParamIDs::domDetailHigh);
    s.attackTiltLow = param (ParamIDs::domAttackLow);
    s.attackTiltHigh = param (ParamIDs::domAttackHigh);
    s.releaseTiltLow = param (ParamIDs::domReleaseLow);
    s.releaseTiltHigh = param (ParamIDs::domReleaseHigh);
    s.maxCutDb = param (ParamIDs::domMaxCut);
    s.wetTrimDb = param (ParamIDs::domWetTrim);
    s.mix = param (ParamIDs::domMix);
    s.outDb = param (ParamIDs::domOut);
    s.delta = isOn (ParamIDs::domDelta);
    for (size_t i = 0; i < s.bands.size(); ++i)
    {
        const auto& values = rack->dominionBandParameters[i].values;   // in domBandSettings' order
        auto& band = s.bands[i];
        band.used = values[0]->load() > 0.5f;
        band.on = values[1]->load() > 0.5f;
        band.shape = (dominion::Shape) juce::jlimit (0, dominion::shapeNames.size() - 1, juce::roundToInt (values[2]->load()));
        band.frequency = values[3]->load();
        band.depthDb = values[4]->load();
        band.q = values[5]->load();
        band.slope = juce::roundToInt (values[6]->load());
        band.focus = values[7]->load();
    }
    s.listenBand = rack->dominionListen.load();
    return s;
}

effects::Delay::Settings ParagonX1AudioProcessor::getDelaySettings() const noexcept
{
    effects::Delay::Settings s;
    s.on = isOn (ParamIDs::delayOn);
    s.timeMs = param (ParamIDs::delayTime);
    s.syncIndex = juce::jlimit (0, effects::Delay::syncNames.size() - 1, juce::roundToInt (param (ParamIDs::delaySync)));
    s.feedback = param (ParamIDs::delayFeedback);
    s.tone = param (ParamIDs::delayTone);
    s.mix = param (ParamIDs::delayMix);
    s.pingPong = isOn (ParamIDs::delayPingPong);
    s.type = (effects::Delay::Type) juce::jlimit (0, effects::Delay::typeNames.size() - 1, juce::roundToInt (param (ParamIDs::delayType)));
    s.duck = param (ParamIDs::delayDuck);
    s.returnOnly = true;
    return s;
}

effects::Reverb::Settings ParagonX1AudioProcessor::getReverbSettings() const noexcept
{
    effects::Reverb::Settings s;
    s.on = isOn (ParamIDs::reverbOn);
    s.type = (effects::Reverb::Type) juce::jlimit (0, effects::Reverb::typeNames.size() - 1, juce::roundToInt (param (ParamIDs::reverbType)));
    s.size = param (ParamIDs::reverbSize);
    s.tone = param (ParamIDs::reverbTone);
    s.mix = param (ParamIDs::reverbMix);
    s.preDelayMs = param (ParamIDs::reverbPreDelay);
    s.duck = param (ParamIDs::reverbDuck);
    s.returnOnly = true;
    return s;
}

void ParagonX1AudioProcessor::processSends (juce::dsp::AudioBlock<float> block, const AuxOutputs& aux)
{
    // DELAY on its send (giving back just its echoes), then REVERB on its own send plus DELAY > REVERB's share of
    // those echoes; both returns join the kit before ECHELON. Each does nothing once it's off and its tail is gone.
    auto& r = *rack;
    const auto numSamples = (int) block.getNumSamples();
    juce::dsp::AudioBlock<float> delayReturn (r.sends[0].getArrayOfWritePointers(), 2, (size_t) numSamples);
    juce::dsp::AudioBlock<float> reverbReturn (r.sends[1].getArrayOfWritePointers(), 2, (size_t) numSamples);
    r.delay.process (delayReturn, getDelaySettings(), juce::jmax (1.0, hostBpm.load()));
    if (const auto feed = param (ParamIDs::delayToReverb); feed > 0.0f)
        for (int c = 0; c < 2; ++c)
            r.sends[1].addFrom (c, 0, r.sends[0], c, 0, numSamples, feed);
    r.reverb.process (reverbReturn, getReverbSettings());
    for (int s = 0; s < synth::numSends; ++s)
        r.returnPeak[(size_t) s] = juce::jmax (r.sends[(size_t) s].getMagnitude (0, 0, numSamples), r.sends[(size_t) s].getMagnitude (1, 0, numSamples));
    // Each return into its output: MAIN, or another the DAW has on.
    const auto numChannels = (int) block.getNumChannels();
    const auto outputs = std::array { ParamIDs::delayOutput, ParamIDs::reverbOutput };
    for (int s = 0; s < synth::numSends; ++s)
    {
        const auto& send = r.sends[(size_t) s];
        if (const auto [auxLeft, auxRight] = aux.at (juce::roundToInt (param (outputs[(size_t) s]))); auxLeft != nullptr)
        {
            juce::FloatVectorOperations::add (auxLeft, send.getReadPointer (0), numSamples);
            juce::FloatVectorOperations::add (auxRight, send.getReadPointer (1), numSamples);
        }
        else
            for (int c = 0; c < numChannels; ++c)
            {
                auto* out = block.getChannelPointer ((size_t) c);
                for (int i = 0; i < numSamples; ++i)
                    out[i] += numChannels == 1 ? 0.5f * (send.getSample (0, i) + send.getSample (1, i)) : send.getSample (c, i);
            }
    }
}

effects::Modulation::Settings ParagonX1AudioProcessor::getModSettings (int which) const noexcept
{
    return modSettingsFor (which == 0 ? ParamIDs::modIds : ParamIDs::mod2Ids);
}

effects::Modulation::Settings ParagonX1AudioProcessor::modSettingsFor (const ParamIDs::ModIds& ids) const noexcept
{
    // As PR7-G's modSettingsFor, without its expression pedal and POSITION.
    effects::Modulation::Settings s;
    s.on = isOn (ids.on);
    s.type = (effects::Modulation::Type) juce::jlimit (0, effects::Modulation::typeNames.size() - 1, juce::roundToInt (param (ids.type)));
    // SYNC: one LFO cycle per note length at the song's tempo.
    s.rateHz = param (ids.rate);
    if (const auto sync = juce::roundToInt (param (ids.sync)); sync > 0 && sync < (int) std::size (effects::Delay::syncBeats))
        s.rateHz = juce::jlimit (0.02f, 40.0f, (float) (juce::jmax (1.0, hostBpm.load()) / 60.0) / effects::Delay::syncBeats[sync]);   // 1/32 at 300 BPM
    s.depth = param (ids.depth);
    s.mix = param (ids.mix);
    s.eq = param (ids.eq);
    s.stereoOutputs = isOn (ids.stereo);
    s.manual = param (ids.manual);
    s.resonance = param (ids.res);
    s.hadronBlock = isOn (ids.hadron);
    s.wave = param (ids.wave);
    return s;
}

effects::LoFi::Settings ParagonX1AudioProcessor::getLoFiSettings() const noexcept { return loFiSettingsFor (-1); }

effects::LoFi::Settings ParagonX1AudioProcessor::getInsertLoFiSettings (int bus) const noexcept { return loFiSettingsFor (bus); }

effects::LoFi::Settings ParagonX1AudioProcessor::loFiSettingsFor (int bus) const noexcept
{
    // The whole sound's lofi* (bus -1), or a bus's "ins<n>Lofi*": the same settings in the same order.
    const auto value = [this, bus] (const char* whole, int index)
    {
        return bus < 0 ? param (whole) : rack->buses[(size_t) bus].lofi_[(size_t) index]->load();
    };
    effects::LoFi::Settings s;
    s.on = value (ParamIDs::lofiOn, 0) > 0.5f;
    s.type = (effects::LoFi::Type) juce::jlimit (0, effects::LoFi::typeNames.size() - 1, juce::roundToInt (value (ParamIDs::lofiType, 1)));
    s.amount = value (ParamIDs::lofiAmount, 2);
    s.noise = value (ParamIDs::lofiNoise, 3);
    s.mix = value (ParamIDs::lofiMix, 4);
    s.levelDb = value (ParamIDs::lofiLevel, 5);
    return s;
}

effects::Drive::Settings ParagonX1AudioProcessor::getInsertDriveSettings (int bus) const noexcept
{
    const auto& v = rack->buses[(size_t) juce::jlimit (0, synth::numInsertBuses - 1, bus)].drive_;   // insDriveSettings' order
    const auto value = [&v] (int index) { return v[(size_t) index]->load(); };
    effects::Drive::Settings s;
    s.on = value (0) > 0.5f;
    s.model = (effects::Drive::Model) juce::jlimit (0, effects::Drive::modelNames.size() - 1, juce::roundToInt (value (1)));
    s.drive = value (2);
    s.tone = value (3);
    s.levelDb = value (4);
    s.attack = juce::jlimit (0, effects::Drive::skylineAttackNames.size() - 1, juce::roundToInt (value (5)));
    s.gate = value (6);
    s.high = value (7);
    s.low = value (8);
    s.middle = value (9);
    s.midFreq = value (10);
    return s;
}

effects::Glitch::Settings ParagonX1AudioProcessor::getInsertGlitchSettings (int bus) const noexcept
{
    const auto& v = rack->buses[(size_t) juce::jlimit (0, synth::numInsertBuses - 1, bus)].glitch_;   // insGlitchSettings' order
    const auto value = [&v] (int index) { return v[(size_t) index]->load(); };
    effects::Glitch::Settings s;
    s.on = value (0) > 0.5f;
    s.mode = (effects::Glitch::Mode) juce::jlimit (0, effects::Glitch::modeNames.size() - 1, juce::roundToInt (value (1)));
    s.sync = value (2) > 0.5f;
    s.lengthMs = value (3);
    s.division = juce::jlimit (0, effects::Glitch::syncNames.size() - 1, juce::roundToInt (value (4)));
    s.density = value (5);
    s.pitch = value (6);
    s.chaos = value (7);
    s.mix = value (8);
    return s;
}

std::array<float, synth::numSends> ParagonX1AudioProcessor::getInsertSends (int bus) const noexcept
{
    const auto& v = rack->buses[(size_t) juce::jlimit (0, synth::numInsertBuses - 1, bus)].sends_;
    return { v[0]->load(), v[1]->load() };
}

void ParagonX1AudioProcessor::processInsertBuses (juce::dsp::AudioBlock<float> block, bool sendsReady, const AuxOutputs& aux)
{
    // Each bus a drum plays into: DRIVE and GLITCH (one a side: PR7-G's are mono), MOD, LO-FI, as on PR7-G's
    // chain (GLITCH POST, after the drive); then into the mix, and into the sends at the bus's amounts.
    const auto numSamples = (int) block.getNumSamples();
    const auto bpm = juce::jmax (1.0, hostBpm.load());
    for (int b = 0; b < synth::numInsertBuses; ++b)
    {
        auto& bus = rack->buses[(size_t) b];
        if (! bus.used)
        {
            bus.peak = 0.0f;
            continue;
        }
        auto busBlock = juce::dsp::AudioBlock<float> (bus.buffer).getSubBlock (0, (size_t) numSamples);
        const auto drive = getInsertDriveSettings (b);
        const auto glitch = getInsertGlitchSettings (b);
        // After the DRIVE, a DC blocker (5 Hz): RABID's circuit can leave its coupling capacitor charged, a steady
        // offset after loud playing. On PR7-G the amp after the pedal blocks it; here nothing would. Only while the
        // drive is on; switched off, what it was taking off fades out over about 50 ms, then the bus is untouched.
        const auto dcCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 5.0f / (float) getSampleRate());
        const auto dcFade = std::exp (-1.0f / (0.01f * (float) getSampleRate()));
        for (size_t c = 0; c < 2; ++c)
        {
            bus.drive[c].process (busBlock.getSingleChannelBlock (c), drive);
            auto& dc = bus.driveDc[c];
            auto* x = busBlock.getChannelPointer (c);
            if (drive.on)
                for (int i = 0; i < numSamples; ++i)
                {
                    dc += (x[i] - dc) * dcCoefficient;
                    x[i] -= dc;
                }
            else if (dc != 0.0f)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    dc *= dcFade;
                    x[i] -= dc;
                }
                if (std::abs (dc) < 1.0e-9f)
                    dc = 0.0f;
            }
            bus.glitch[c].process (busBlock.getSingleChannelBlock (c), glitch, bpm, hostPpq);
        }
        bus.modulation.process (busBlock, getInsertModSettings (b));
        bus.loFi.process (busBlock, getInsertLoFiSettings (b));
        bus.peak = juce::jmax (bus.buffer.getMagnitude (0, 0, numSamples), bus.buffer.getMagnitude (1, 0, numSamples));

        // Into its output: MAIN, or another the DAW has on.
        const auto numChannels = (int) block.getNumChannels();
        if (const auto [auxLeft, auxRight] = aux.at (juce::roundToInt (bus.output_->load())); auxLeft != nullptr)
        {
            juce::FloatVectorOperations::add (auxLeft, bus.buffer.getReadPointer (0), numSamples);
            juce::FloatVectorOperations::add (auxRight, bus.buffer.getReadPointer (1), numSamples);
        }
        else
            for (int c = 0; c < numChannels; ++c)
            {
                auto* out = block.getChannelPointer ((size_t) c);
                if (numChannels == 1)
                    for (int i = 0; i < numSamples; ++i)
                        out[i] += 0.5f * (bus.buffer.getSample (0, i) + bus.buffer.getSample (1, i));
                else
                    juce::FloatVectorOperations::add (out, bus.buffer.getReadPointer (c), numSamples);
            }
        if (sendsReady)
        {
            const auto sends = getInsertSends (b);
            const auto on = std::array<bool, synth::numSends> { isOn (ParamIDs::delayOn), isOn (ParamIDs::reverbOn) };
            for (int s = 0; s < synth::numSends; ++s)
                if (on[(size_t) s] && sends[(size_t) s] > 0.0f)
                    for (int c = 0; c < 2; ++c)
                        rack->sends[(size_t) s].addFrom (c, 0, bus.buffer, c, 0, numSamples, sends[(size_t) s]);
        }
    }
}

void ParagonX1AudioProcessor::processModFx (juce::dsp::AudioBlock<float> block)
{
    // MOD, then MOD 2, then LO-FI, as on PR7-G (where LO-FI comes after the reverb too, so the echoes go "through
    // the phone" with the rest). Each passes the sound untouched while off.
    rack->modulation[0].process (block, getModSettings (0));
    rack->modulation[1].process (block, getModSettings (1));
    const auto loFi = getLoFiSettings();
    rack->loFi.process (block, loFi);
    auto peak = 0.0f;
    if (loFi.on)
        for (size_t c = 0; c < block.getNumChannels(); ++c)
            peak = juce::jmax (peak, juce::FloatVectorOperations::findMaximum (block.getChannelPointer (c), (int) block.getNumSamples()),
                               -juce::FloatVectorOperations::findMinimum (block.getChannelPointer (c), (int) block.getNumSamples()));
    rack->loFiPeak = peak;
}

void ParagonX1AudioProcessor::processRack (juce::dsp::AudioBlock<float> block)
{
    // As PR7-E 0.18.1's processChunk, from its EQ to its COMP: ECHELON, with what goes in and comes out kept for its
    // analyser (and EQ Match); DOMINION after it (so it hears what the EQ made); CONSTRICTOR's bands, then its full band
    // (PR7-C's compressor, which takes whole buffers and the same audio to listen to). Each does nothing while off.
    auto& r = *rack;
    const auto numChannels = (int) block.getNumChannels();
    const auto numSamples = (int) block.getNumSamples();
    if (r.compSidechain.getNumSamples() < numSamples)
        return;   // not prepared (prepareToPlay sizes everything): pass the kit through rather than read past a buffer
    const auto writeTap = [&] (InputTap& tap)
    {
        if (numChannels > 1)
            tap.write (block.getChannelPointer (0), block.getChannelPointer (1), numSamples);
        else
            tap.write (block.getChannelPointer (0), numSamples);
    };
    writeTap (r.eqInputTap);
    r.parametricEq.process (block, getEqSettings());
    writeTap (r.eqOutputTap);

    r.dominionProcessor.process (block, getDominionSettings());

    r.multiband.process (block, getMultibandSettings(), isOn (ParamIDs::compOn));
    float* channels[effects::maxChannels] {};
    for (int channel = 0; channel < numChannels; ++channel)
    {
        channels[channel] = block.getChannelPointer ((size_t) channel);
        r.compSidechain.copyFrom (channel, 0, channels[channel], numSamples);
    }
    juce::AudioBuffer<float> audio (channels, numChannels, numSamples);
    const juce::AudioBuffer<float> detector (r.compSidechain.getArrayOfWritePointers(), numChannels, numSamples);
    r.compressor.process (audio, detector, numSamples,
                          { isOn (ParamIDs::compOn) && isOn (ParamIDs::compWide), param (ParamIDs::compThreshold), param (ParamIDs::compRatio),
                            param (ParamIDs::compAttack), param (ParamIDs::compRelease), isOn (ParamIDs::compAuto),
                            param (ParamIDs::compMakeUp),
                            (effects::Compressor::Character) juce::jlimit (0, 2, juce::roundToInt (param (ParamIDs::compCharacter))),
                            isOn (ParamIDs::compLink) });
}

juce::AudioProcessorParameter* ParagonX1AudioProcessor::getBypassParameter() const
{
    return parameters.getParameter (ParamIDs::bypass);
}

float ParagonX1AudioProcessor::param (const char* id) const
{
    // Looked up by ID each block: 0.49.0 through a hash table made in the constructor (no allocation: the key is
    // only viewed); APVTS's own (sorted map) lookup for any ID not in it.
    if (const auto it = rawValues.find (std::string_view (id)); it != rawValues.end())
        return it->second->load();
    return parameters.getRawParameterValue (id)->load();
}

int ParagonX1AudioProcessor::mainLatencySamples() const noexcept
{
    // CONSTRICTOR's LOOK-AHEAD, and the LIMITER's (with its oversampling and true peak): the kit itself adds none.
    return (isOn (ParamIDs::mbLookahead) ? (int) std::lround (effects::multiband::lookaheadSeconds * rack->sampleRate) : 0)
           + rack->limiter.getLatencySamples (getLimiterSettings());
}

bool ParagonX1AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // An instrument: no audio in; MAIN mono or stereo; OUT 2 to 16 each stereo or off.
    const auto output = layouts.getMainOutputChannelSet();
    if (! layouts.getMainInputChannelSet().isDisabled()
        || ! (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo()))
        return false;
    for (int o = 1; o < layouts.outputBuses.size(); ++o)
        if (const auto& set = layouts.outputBuses.getReference (o); ! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    return true;
}

void ParagonX1AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0 || numSamples == 0)
        return;

    if (auto* host = getPlayHead())
        if (const auto position = host->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                hostBpm = *bpm;
            const auto ppq = position->getPpqPosition();
            hostPpq = position->getIsPlaying() && ppq.hasValue() ? *ppq : -1.0;
            if (const auto signature = position->getTimeSignature(); signature.hasValue() && signature->numerator > 0 && signature->denominator > 0)
            {
                hostNumerator = signature->numerator;
                hostDenominator = signature->denominator;
            }
        }

    // THE DRUM MACHINE (0.38.0): the sequencer follows the DAW while it plays; else PLAY's own clock, from its start.
    dawPlaying = hostPpq >= 0.0;
    if (machineRestart.exchange (false))
        machineBeat = 0.0;
    if (hostPpq >= 0.0)
    {
        seqPpq = hostPpq;
        seqBpm = juce::jmax (1.0, hostBpm.load());
    }
    else if (machinePlaying.load())
    {
        seqBpm = juce::jlimit (1.0, 1000.0, (double) param (ParamIDs::machineTempo));
        seqPpq = machineBeat;
        machineBeat += numSamples / getSampleRate() * seqBpm / 60.0;
    }
    else
    {
        seqPpq = -1.0;
        seqBpm = juce::jmax (1.0, hostBpm.load());
    }

    // MIDI LEARN: the last controller moved (for the strip and for learning), and the knobs linked to controllers.
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (! message.isController())
            continue;
        const auto cc = message.getControllerNumber();
        if (cc < 0 || cc >= 120)
            continue;   // 120-127: channel mode messages (all notes off...)
        const auto value = (float) message.getControllerValue() / 127.0f;
        lastController = cc;
        lastControllerValue = value;
        ++controllerCount;
        if (auto* target = controllerTargets[(size_t) cc].load())
            target->setValueNotifyingHost (value);
    }

    // The on-screen keyboard's notes join the DAW's: all of it goes to the kit (it plays the notes in its 12 keys, and
    // with the DRUM SEQUENCER on KEY, the four under them start and stop its patterns; it ignores everything else).
    keyboardMidi.clear();
    keyboardMidi.addEvents (midi, 0, numSamples, 0);
    keyboardState.processNextMidiBuffer (keyboardMidi, 0, numSamples, true);
    const auto kit = getKitSettings();
    const auto padSequencer = machineRecording.load() ? getDrumSeqSettings() : synth::drumseq::Settings {};
    for (int slot = 0; slot < ParamIDs::numDrums; ++slot)
        if (pendingPreview[(size_t) slot].exchange (false))
        {
            drumKit.trigger (slot, 0.8f, kit);
            recordMachineNote (seqPpq, seqBpm, slot, 0.8f, padSequencer);   // REC: a pad played on the face
        }

    // OUT 2 to 16: the ones the DAW has switched on (the others' drums play in MAIN).
    AuxOutputs aux;
    for (int o = 1; o < synth::numOutputs; ++o)
        if (const auto* bus = getBus (false, o); bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() == 2)
            if (const auto first = bus->getChannelIndexInProcessBlockBuffer (0); first >= 0 && first + 1 < buffer.getNumChannels())
            {
                aux.left[(size_t) o - 1] = buffer.getWritePointer (first);
                aux.right[(size_t) o - 1] = buffer.getWritePointer (first + 1);
            }

    // Some hosts occasionally send more samples than they promised in
    // prepareToPlay; work through an oversized block in promised-size pieces.
    riffFirstChunk = true;
    riffFrom = -1.0;
    for (int start = 0; start < numSamples; start += preparedBlockSize)
        processChunk (buffer, keyboardMidi, kit, start, juce::jmin (preparedBlockSize, numSamples - start), aux.from (start));
    runRiff (buffer, numSamples);   // RIFF MATCH: the riff with the groove preview (MAIN, after everything, as the click)

    runSongClick (buffer, buffer.getNumSamples());   // THE CLICK (0.44.0): the song's metronome, after everything
        runTapping (buffer, keyboardMidi, kit);   // TAP TO FIND: its taps and its metronome (after everything: the click on top)
    activeVoices = drumKit.getNumActiveVoices();
    midi.clear();   // an instrument: nothing goes on down the MIDI chain
}

void ParagonX1AudioProcessor::processChunk (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& drums,
                                            const synth::drums::KitSettings& kit, int start, int numSamples, const AuxOutputs& aux)
{
    // MAIN's channels (the buffer's first; OUT 2 to 16's follow, while on).
    const auto numChannels = juce::jmin (buffer.getNumChannels(), getMainBusNumOutputChannels());
    // The kit adds into its outputs: the mix, the other outputs, the sends (while their effect is on) and the insert
    // buses a drum is routed to, so each starts the piece silent. Mono out: the right side goes into a scratch buffer,
    // folded in after.
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, start, numSamples);
    float scratchRight[512];
    const auto sendsReady = rack->sends[0].getNumSamples() >= numSamples;
    for (auto& bus : rack->buses)
        bus.used = false;
    for (const auto& slot : kit.slots)
        if (slot.insertBus >= 1 && slot.insertBus <= synth::numInsertBuses)
            rack->buses[(size_t) slot.insertBus - 1].used = true;
    // THE MIXER's routes and sends into an FX BUS (0.21.0) need it ready too.
    {
        const auto useFx = [this] (int target)
        {
            if (const auto f = synth::mixer::fxBusOfTarget (target); f >= 0 && f < (int) rack->buses.size())
                rack->buses[(size_t) f].used = true;
        };
        for (const auto& ch : kit.channels)
            useFx (ch.route);
        for (const auto& bus : kit.buses)
            useFx (bus.route);
        if (kit.sends != nullptr)
            for (const auto& strip : *kit.sends)
                for (const auto& send : strip)
                    useFx (send.target);
    }
    if (sendsReady)
    {
        for (auto& send : rack->sends)
            send.clear (0, numSamples);
        for (auto& bus : rack->buses)
            if (bus.used)
                bus.buffer.clear (0, numSamples);
    }

    auto sequencer = getDrumSeqSettings();
    if (const auto* compiled = matrixForAudio.load())
    {
        sequencer.patterns = &compiled->drumPatterns;
        mixerEffects.setTable (&compiled->effects);   // the slots' settings this block (0.22.0)
    }
    const auto bpm = juce::jmax (1.0, hostBpm.load());
    auto* left = buffer.getWritePointer (0, start);
    for (int done = 0; done < numSamples; done += (int) std::size (scratchRight))
    {
        const auto n = juce::jmin ((int) std::size (scratchRight), numSamples - done);
        auto* right = numChannels > 1 ? buffer.getWritePointer (1, start + done) : scratchRight;
        if (numChannels == 1)
            std::fill (scratchRight, scratchRight + n, 0.0f);
        synth::drums::Outputs out;
        out.left = left + done;
        out.right = right;
        const auto auxHere = aux.from (done);
        out.auxLeft = auxHere.left;
        out.auxRight = auxHere.right;
        if (sendsReady)
        {
            for (size_t s = 0; s < out.sendLeft.size(); ++s)
            {
                out.sendLeft[s] = rack->sends[s].getWritePointer (0, done);
                out.sendRight[s] = rack->sends[s].getWritePointer (1, done);
            }
            for (size_t b = 0; b < rack->buses.size(); ++b)
                if (rack->buses[b].used)
                {
                    out.busLeft[b] = rack->buses[b].buffer.getWritePointer (0, done);
                    out.busRight[b] = rack->buses[b].buffer.getWritePointer (1, done);
                }
        }
        const auto ppq = hostPpq >= 0.0 ? hostPpq + (start + done) / getSampleRate() * bpm / 60.0 : -1.0;
        // The sequencer's own position: the DAW's, or THE DRUM MACHINE's PLAY (0.38.0).
        const auto seqHere = seqPpq >= 0.0 ? seqPpq + (start + done) / getSampleRate() * seqBpm / 60.0 : -1.0;
        if (machineRecording.load() && seqHere >= 0.0)
            for (const auto metadata : drums)
                if (const auto pos = metadata.samplePosition - (start + done); pos >= 0 && pos < n)
                    if (const auto& m = metadata.getMessage(); m.isNoteOn())
                        if (const auto slot = kit.slotFor (m.getNoteNumber()); slot >= 0)
                            recordMachineNote (seqHere + pos / getSampleRate() * seqBpm / 60.0, seqBpm, slot, m.getFloatVelocity(), sequencer);
        // The preview's hits and the song's, in sample order (the kit plays them in turn).
        const auto numGrooveHits = collectGrooveHits (n);
        const auto numHits = numGrooveHits + collectSongHits (n, ppq, bpm, grooveHits.data() + numGrooveHits, (int) grooveHits.size() - numGrooveHits);
        recordSongNotes (drums, start + done, n, kit);   // RECORD: this piece's notes at their song beats
        noteSongClicks (start + done, n, bpm);             // THE CLICK (0.44.0): this piece's beats
        if (numGrooveHits > 0 && numHits > numGrooveHits)
            std::sort (grooveHits.begin(), grooveHits.begin() + numHits, [] (const auto& a, const auto& b) { return a.offset < b.offset; });
        drumKit.process (out, n, drums, start + done, kit, sequencer, seqHere, seqBpm, grooveHits.data(), numHits);
        if (numChannels == 1)
            for (int i = 0; i < n; ++i)
                left[done + i] = 0.5f * (left[done + i] + scratchRight[i]);
    }

    // The insert buses into the mix and the sends; the sends' returns; MOD, MOD 2 and LO-FI; THE RACK: ECHELON,
    // DOMINION, CONSTRICTOR; then the preset's own level and OUTPUT; then the LIMITER, last, so nothing after it can
    // push a peak past its ceiling (OUTPUT drives into it, as into a mastering limiter); then BYPASS (to silence:
    // there's nothing "clean" to pass through on an instrument).
    const auto block = juce::dsp::AudioBlock<float> (buffer).getSubBlock ((size_t) start, (size_t) numSamples)
                                                            .getSubsetChannelBlock (0, (size_t) juce::jmin (numChannels, 2));
    if (sendsReady)
    {
        processInsertBuses (block, true, aux);
        processSends (block, aux);
        processModFx (block);   // after the returns, so the echoes and reverb are part of what they shape
    }
    processRack (block);
    // The preset's level and OUTPUT: the whole plugin's volume, so on OUT 2 to 16 too (a drum moved off MAIN keeps its
    // level). MAIN's effects and the LIMITER are MAIN's alone: the other outputs are for the DAW to mix.
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (param (ParamIDs::outputGain) + param (ParamIDs::patchLevel)));
    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = outputGain.getNextValue();
        for (int channel = 0; channel < juce::jmin (numChannels, 2); ++channel)
            buffer.getWritePointer (channel, start)[i] *= gain;
        for (size_t o = 0; o < aux.left.size(); ++o)
            if (aux.left[o] != nullptr)
            {
                aux.left[o][i] *= gain;
                aux.right[o][i] *= gain;
            }
    }
    if (rack->compSidechain.getNumSamples() >= numSamples)
        rack->limiter.process (block, getLimiterSettings());
    delayAuxOutputs (aux, numSamples);   // OUT 2 to 16 in time with MAIN
    // BYPASS silences every output.
    bypassAmount.setTargetValue (isOn (ParamIDs::bypass) ? 1.0f : 0.0f);
    if (bypassAmount.isSmoothing() || bypassAmount.getTargetValue() > 0.5f)
        for (int i = 0; i < numSamples; ++i)
        {
            const auto gain = 1.0f - bypassAmount.getNextValue();
            for (int channel = 0; channel < juce::jmin (numChannels, 2); ++channel)
                buffer.getWritePointer (channel, start)[i] *= gain;
            for (size_t o = 0; o < aux.left.size(); ++o)
                if (aux.left[o] != nullptr)
                {
                    aux.left[o][i] *= gain;
                    aux.right[o][i] *= gain;
                }
        }

    for (int channel = 0; channel < juce::jmin (numChannels, 2); ++channel)
        outputMeter.pushSamples (buffer.getReadPointer (channel, start), numSamples);
}

void ParagonX1AudioProcessor::delayAuxOutputs (const AuxOutputs& aux, int numSamples) noexcept
{
    // Each of OUT 2 to 16 through its ring, read MAIN's latency behind (the rings run for the outputs that are off too,
    // with silence, so one switched on mid-song doesn't first play what was left in its ring).
    auto& ring = rack->auxDelay;
    const auto size = ring.getNumSamples();
    if (size == 0)
        return;
    const auto latency = juce::jlimit (0, size - 1, mainLatencySamples());
    auto write = rack->auxDelayWrite;
    for (size_t o = 0; o < aux.left.size(); ++o)
    {
        auto* ringLeft = ring.getWritePointer (2 * (int) o);
        auto* ringRight = ring.getWritePointer (2 * (int) o + 1);
        auto* left = aux.left[o];
        auto* right = aux.right[o];
        auto w = write;
        for (int i = 0; i < numSamples; ++i)
        {
            ringLeft[w] = left != nullptr ? left[i] : 0.0f;
            ringRight[w] = right != nullptr ? right[i] : 0.0f;
            const auto r = w >= latency ? w - latency : w - latency + size;
            if (left != nullptr)
            {
                left[i] = ringLeft[r];
                right[i] = ringRight[r];
            }
            if (++w == size)
                w = 0;
        }
    }
    rack->auxDelayWrite = (write + numSamples) % size;
}

//==============================================================================
void ParagonX1AudioProcessor::timerCallback()
{
    finishMidiLearn();
    updateDrumReverses();
    retireOldData();
    updateLatency();
    rack->eqMatch.update (rack->eqInputTap);   // EQ Match listens with its window closed too
    recordCheckpointIfReady();
}

//==============================================================================
void ParagonX1AudioProcessor::retireOldData()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    std::erase_if (retiredMatrices, [now] (const auto& retired) { return now - retired.second > retiredTableMs; });
    std::erase_if (retiredMidiIns, [now] (const auto& retired) { return now - retired.second > retiredTableMs; });
    mixerEffects.freeRetired();
    std::erase_if (retiredSamples, [now] (const auto& retired) { return now - retired.second > retiredTableMs; });
    std::erase_if (retiredInstruments, [now] (const auto& retired) { return now - retired.second > retiredTableMs; });
    std::erase_if (retiredGrooves, [now] (const auto& retired) { return now - retired.second > retiredTableMs; });
    std::erase_if (retiredRiffs, [now] (const auto& retired) { return now - retired.second > retiredTableMs; });
    std::erase_if (retiredSongs, [now] (const auto& retired) { return now - retired.second > retiredTableMs; });
}

void ParagonX1AudioProcessor::compileMatrix()
{
    // What the audio thread plays: the patterns as they are, and the envelopes as plain shapes.
    auto compiled = std::make_shared<CompiledMatrix>();
    for (size_t p = 0; p < compiled->drumPatterns.size() && p < matrix.drumPatterns.size(); ++p)
        compiled->drumPatterns[p] = matrix.drumPatterns[p];
    static_assert (ModMatrix::numDrumSlots == synth::drums::numSlots);
    for (size_t k = 0; k < compiled->drumEnvelopes.size() && k < matrix.drumEnvelopes.size(); ++k)
        compiled->drumEnvelopes[k] = matrix.drumEnvelopes[k].toShape();
    for (size_t k = 0; k < compiled->velocityCurves.size() && k < matrix.drumVelocityCurves.size(); ++k)
        compiled->velocityCurves[k] = matrix.drumVelocityCurves[k];
    for (int c = 0; c < synth::mixer::numChannels; ++c)
        for (int p = 0; p < synth::mixer::numPieces; ++p)
            compiled->bleed[(size_t) c][(size_t) p] = matrix.bleed (c, p);
    for (int strip = 0; strip < synth::mixer::numStrips; ++strip)
        for (int slot = 0; slot < synth::mixer::numSendSlots; ++slot)
            compiled->sends[(size_t) strip][(size_t) slot] = matrix.send (strip, slot);
    for (int piece = 0; piece < synth::mixer::numPieces; ++piece)
        for (int mic = 0; mic < synth::mixer::numChannels; ++mic)
            compiled->micRoutes[(size_t) piece][(size_t) mic] = matrix.micRoute (piece, mic);
    // THE MIXER's EFFECTS: an unknown type (a later version's) left empty; each value within its range.
    for (int strip = 0; strip < synth::mixer::numStrips; ++strip)
        for (int slot = 0; slot < synth::mixer::numFxSlots; ++slot)
        {
            auto fx = matrix.effect (strip, slot);
            if (fx.type < 0 || fx.type >= mixerfx::typeNames.size())
                fx = {};
            const auto& params = mixerfx::paramsOf (fx.type);
            for (size_t i = 0; i < params.size() && i < fx.values.size(); ++i)
                fx.values[i] = juce::jlimit (params[i].min, params[i].max, fx.values[i]);
            compiled->effects[(size_t) strip][(size_t) slot] = fx;
        }
    mixerEffects.sync (compiled->effects);   // the effects made before the audio thread sees their slots
    if (compiledMatrix != nullptr)
        retiredMatrices.emplace_back (compiledMatrix, juce::Time::getMillisecondCounterHiRes());
    compiledMatrix = compiled;
    matrixForAudio = compiledMatrix.get();
}

void ParagonX1AudioProcessor::setModMatrix (const ModMatrix& newMatrix)
{
    if (matrix == newMatrix)
        return;
    matrix = newMatrix;
    compileMatrix();
    markModified();
}

//==============================================================================
// THE DRUMS.
void ParagonX1AudioProcessor::setDrumSample (int slot, std::shared_ptr<const synth::drums::Sample> sample)
{
    slot = juce::jlimit (0, ParamIDs::numDrums - 1, slot);
    if (drumSamples[(size_t) slot] == sample)
        return;
    if (drumSamples[(size_t) slot] != nullptr)
        retiredSamples.emplace_back (drumSamples[(size_t) slot], juce::Time::getMillisecondCounterHiRes());
    drumSamples[(size_t) slot] = std::move (sample);
    samplesForAudio[(size_t) slot] = drumSamples[(size_t) slot].get();
}

bool ParagonX1AudioProcessor::loadDrumSample (int slot, const juce::File& file)
{
    auto sample = synth::drums::sampleFiles::load (file);
    if (sample == nullptr)
        return false;
    slot = juce::jlimit (0, ParamIDs::numDrums - 1, slot);
    setDrumSample (slot, std::move (sample));
    setDrumInstrument (slot, nullptr);   // one file in place of an instrument
    instrumentRefs[(size_t) slot] = {};
    // A new file plays whole: START and END go back to its ends.
    for (const auto& [what, value] : { std::pair { "SampleStart", 0.0f }, std::pair { "SampleEnd", 1.0f } })
        if (auto* parameter = parameters.getParameter (ParamIDs::drum (slot, what)))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    makeSlotPlaySample (slot);
    markModified();
    requestCheckpoint();
    return true;
}

void ParagonX1AudioProcessor::makeSlotPlaySample (int slot)
{
    // A slot that becomes SAMPLE plays it as recorded: its shaping goes to neutral (a drum synth's DECAY, TONE and SNAP
    // would cut or tilt it). A slot already on SAMPLE keeps the shaping the player gave it.
    auto* model = parameters.getParameter (ParamIDs::drum (slot, "Model"));
    if (model != nullptr && juce::roundToInt (model->convertFrom0to1 (model->getValue())) != (int) synth::drums::Model::sample)
    {
        for (const auto& [what, value] : { std::pair { "Pitch", 0.0f }, std::pair { "Decay", 0.5f }, std::pair { "Tone", 0.5f }, std::pair { "Snap", 0.5f } })
            if (auto* parameter = parameters.getParameter (ParamIDs::drum (slot, what)))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        model->setValueNotifyingHost (model->convertTo0to1 ((float) synth::drums::Model::sample));
    }
}

void ParagonX1AudioProcessor::clearDrumSample (int slot)
{
    setDrumSample (slot, nullptr);
    setDrumInstrument (slot, nullptr);
    instrumentRefs[(size_t) juce::jlimit (0, ParamIDs::numDrums - 1, slot)] = {};
    markModified();
    requestCheckpoint();
}

//==============================================================================
// THE SAMPLE ENGINE (0.19.0)
std::shared_ptr<const synth::drums::Instrument> ParagonX1AudioProcessor::getDrumInstrument (int slot) const
{
    return drumInstruments[(size_t) juce::jlimit (0, 2 * ParamIDs::numDrums - 1, slot)];
}

void ParagonX1AudioProcessor::setDrumInstrument (int slot, std::shared_ptr<const synth::drums::Instrument> instrument)
{
    slot = juce::jlimit (0, 2 * ParamIDs::numDrums - 1, slot);   // 0.29.0: a layer's too
    if (drumInstruments[(size_t) slot] == instrument)
        return;
    // A replaced instrument stays until its longest hit can't still be playing (0.28.0: a streamed cymbal rings for
    // half a minute), then 2 s more, as other retired tables do.
    if (const auto& old = drumInstruments[(size_t) slot]; old != nullptr)
        retiredInstruments.emplace_back (old, juce::Time::getMillisecondCounterHiRes() + 1000.0 * old->longestSeconds());
    if (instrument != nullptr && instrument->streamed() && diskStreamer == nullptr)
    {
        diskStreamer = std::make_unique<synth::drums::Streamer>();
        streamerForAudio = diskStreamer.get();
    }
    drumInstruments[(size_t) slot] = std::move (instrument);
    instrumentsForAudio[(size_t) slot] = drumInstruments[(size_t) slot].get();
}

juce::String ParagonX1AudioProcessor::refFor (const synth::drums::Instrument& instrument)
{
    return (instrument.kind == synth::drums::Instrument::Kind::sfz ? "sfz|" + juce::String (instrument.note) : juce::String ("folder|"))
           + "|" + instrument.source.getFullPathName();
}

void ParagonX1AudioProcessor::startLoading (std::vector<std::pair<int, juce::String>> refs)
{
    const auto generation = ++loadGeneration;
    for (const auto& [slot, ref] : refs)
        instrumentRefs[(size_t) slot] = ref;
    if (refs.empty())
        return;
    ++pendingLoads;
    std::weak_ptr<int> alive = lifeToken;
    if (instrumentPool == nullptr)
        instrumentPool = std::make_unique<juce::ThreadPool> (1);
    instrumentPool->addJob ([this, refs, generation, alive]
    {
        // Each SFZ file read once, whatever number of its notes the slots take (and each sample pack's names).
        std::map<juce::String, synth::drums::instruments::Sfz> files;
        std::map<juce::String, synth::drums::samplepack::PackInfo> packs;
        std::vector<std::pair<int, std::shared_ptr<const synth::drums::Instrument>>> done;
        juce::StringArray problems;
        loadsTotal = (int) refs.size();   // (0.40.0: for the progress box)
        loadsDone = 0;
        for (const auto& [slot, ref] : refs)
        {
            if (alive.expired() || loadGeneration.load() != generation)
                break;
            if (loadGeneration.load() == generation)
                loadsDone = (int) done.size();
            const auto parts = juce::StringArray::fromTokens (ref, "|", {});
            if (parts.size() < 3)
                continue;
            // 0.27.0: a kit's place may be "{kit:<name>}/..." (a factory preset's), where this computer keeps that kit.
            auto path = ref.fromFirstOccurrenceOf ("|", false, false).fromFirstOccurrenceOf ("|", false, false);
            if (path.startsWith ("{kit:"))
            {
                const auto kitName = path.fromFirstOccurrenceOf ("{kit:", false, false).upToFirstOccurrenceOf ("}", false, false);
                const auto known = knownKitFolder (kitName);
                if (known == juce::File())
                {
                    problems.add (parts[0] == "pack" ? "the " + kitName + " sample pack isn't on this computer yet (or was moved): DRUMS > LOAD... > A SAMPLE PACK..."
                                                     : "the " + kitName + " kit isn't on this computer yet: DRUMS > LOAD... > GET FREE KITS..., then LOAD... > A DRUMGIZMO KIT...");
                    done.emplace_back (slot, nullptr);
                    continue;
                }
                path = known.getChildFile (path.fromFirstOccurrenceOf ("}/", false, false)).getFullPathName();
            }
            const juce::File source (path);
            std::shared_ptr<const synth::drums::Instrument> instrument;
            if (parts[0] == "dg")
            {
                // A DrumGizmo kit's instrument(s): "dg|<names, by commas>;<light | full>|<kit file>".
                const auto names = juce::StringArray::fromTokens (parts[1].upToFirstOccurrenceOf (";", false, false), ",", {});
                const auto mode = parts[1].fromFirstOccurrenceOf (";", false, false);
                using synth::drums::drumgizmo::Trim;
                instrument = synth::drums::drumgizmo::load (source, names, mode == "full" ? Trim::everything()
                                                                         : mode == "stream" ? Trim::streamed() : Trim::light(), problems);
            }
            else if (parts[0] == "file")
                instrument = synth::drums::instruments::fromFile (source);   // 0.29.0: one hit (a layer's)
            else if (parts[0] == "pack" || parts[0] == "pack2")
            {
                // 0.33.0: a sample pack's slot: "pack|<slot>|<folder>" (its names read once a load). 0.34.0's imports are
                // "pack2|...": each cymbal's spot mic on its own USER channel.
                const auto cymbalChannels = parts[0] == "pack2";
                const auto key = source.getFullPathName() + (cymbalChannels ? "#2" : "");
                auto it = packs.find (key);
                if (it == packs.end())
                    it = packs.emplace (key, synth::drums::samplepack::scan (source, cymbalChannels)).first;
                instrument = synth::drums::samplepack::load (it->second, parts[1].getIntValue(), problems);
            }
            else if (parts[0] == "folder")
                instrument = synth::drums::instruments::fromFolder (source);
            else
            {
                const auto note = parts[1].getIntValue();
                auto key = source.getFullPathName() + (note < 0 ? "#all" : "");
                auto it = files.find (key);
                if (it == files.end())
                    it = files.emplace (key, synth::drums::instruments::fromSfz (source, note < 0 ? synth::drums::instruments::wholeFile : -1)).first;
                problems.addArray (it->second.problems);
                if (note >= 0)
                {
                    if (const auto found = it->second.notes.find (note); found != it->second.notes.end())
                        instrument = found->second;
                }
                else if (! it->second.notes.empty())
                    instrument = it->second.notes.begin()->second;
            }
            if (instrument == nullptr)
                problems.add ("couldn't load " + source.getFullPathName() + (parts[1].isNotEmpty() ? " (note " + parts[1] + ")" : juce::String()));
            done.emplace_back (slot, instrument);
        }
        {
            const std::scoped_lock lock (loadedLock);
            if (loadGeneration.load() == generation)
            {
                loadedInstruments.insert (loadedInstruments.end(), done.begin(), done.end());
                problems.removeDuplicates (false);
                loadProblems = problems;
            }
        }
        --pendingLoads;
        juce::MessageManager::callAsync ([this, alive]
        {
            if (! alive.expired())
                installLoadedInstruments();
        });
    });
}

void ParagonX1AudioProcessor::installLoadedInstruments()
{
    std::vector<std::pair<int, std::shared_ptr<const synth::drums::Instrument>>> ready;
    {
        const std::scoped_lock lock (loadedLock);
        ready.swap (loadedInstruments);
    }
    for (const auto& [slot, instrument] : ready)
        setDrumInstrument (slot, instrument);
}

juce::StringArray ParagonX1AudioProcessor::getInstrumentProblems() const
{
    const std::scoped_lock lock (loadedLock);
    return loadProblems;
}

bool ParagonX1AudioProcessor::waitForInstruments (int timeoutMs)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (pendingLoads.load() > 0 && juce::Time::getMillisecondCounter() < until)
        juce::Thread::sleep (5);
    installLoadedInstruments();
    return pendingLoads.load() == 0;
}

void ParagonX1AudioProcessor::loadInstrumentFolder (int slot, const juce::File& folder)
{
    slot = juce::jlimit (0, ParamIDs::numDrums - 1, slot);
    setDrumSample (slot, nullptr);
    makeSlotPlaySample (slot);
    startLoading ({ { slot, "folder||" + folder.getFullPathName() } });
    markModified();
    requestCheckpoint();
}

void ParagonX1AudioProcessor::loadInstrumentSfz (int slot, const juce::File& sfz, int note)
{
    slot = juce::jlimit (0, ParamIDs::numDrums - 1, slot);
    setDrumSample (slot, nullptr);
    makeSlotPlaySample (slot);
    startLoading ({ { slot, "sfz|" + juce::String (note) + "|" + sfz.getFullPathName() } });
    markModified();
    requestCheckpoint();
}

ParagonX1AudioProcessor::KitImport ParagonX1AudioProcessor::importSfzKit (const juce::File& sfz, synth::drums::notemap::Layout layout)
{
    using namespace synth::drums::notemap;
    KitImport result;
    const auto notes = synth::drums::instruments::sfzNotes (sfz);
    const auto map = layout == Layout::grooveLibrary ? grooveLibrary() : layout == Layout::generalMidi ? generalMidi()
                   : layout == Layout::custom ? customMap : rootLayout (juce::roundToInt (param (ParamIDs::drumsRoot)));
    // Each drum: its home note in that layout if the kit plays it, else the note with the most regions.
    std::map<int, int> chosen;
    for (const auto& [note, regions] : notes)
    {
        const auto slot = slotOf (map, note);
        if (slot < 0)
        {
            result.unused.add (note);
            continue;
        }
        const auto home = synth::drums::notemap::homeNote (map, layout, slot, juce::roundToInt (param (ParamIDs::drumsRoot)));
        const auto it = chosen.find (slot);
        if (it == chosen.end() || note == home || (it->second != home && regions > notes.at (it->second)))
        {
            if (it != chosen.end())
                result.unused.add (it->second);
            chosen[slot] = note;
        }
        else
            result.unused.add (note);
    }
    std::vector<std::pair<int, juce::String>> refs;
    for (const auto& [slot, note] : chosen)
    {
        result.slotNotes.emplace_back (slot, note);
        setDrumSample (slot, nullptr);
        makeSlotPlaySample (slot);
        refs.emplace_back (slot, "sfz|" + juce::String (note) + "|" + sfz.getFullPathName());
    }
    result.unused.sort();
    startLoading (refs);
    markModified();
    requestCheckpoint();
    return result;
}

ParagonX1AudioProcessor::DrumGizmoImport ParagonX1AudioProcessor::importDrumGizmoKit (const juce::File& kitFile, KitLoad load)
{
    // 0.27.0: a whole DrumGizmo kit onto the slots (our rules: drumgizmo::assign), its mics into the mixer's channels.
    DrumGizmoImport result;
    const auto info = synth::drums::drumgizmo::read (kitFile);
    if (! info.valid())
        return result;
    result.kitName = info.name;
    const auto assigned = synth::drums::drumgizmo::assign (info);
    result.unused = assigned.unused;
    std::vector<std::pair<int, juce::String>> refs;
    for (const auto& [slot, names] : assigned.slots)
    {
        result.slots.emplace_back (slot, names);
        setDrumSample (slot, nullptr);
        makeSlotPlaySample (slot);
        setParameterNow (ParamIDs::drum (slot, "On"), 1.0f);
        refs.emplace_back (slot, "dg|" + names.joinIntoString (",") + (load == KitLoad::full ? ";full" : load == KitLoad::streamed ? ";stream" : ";light")
                                 + "|" + kitFile.getFullPathName());
    }
    // The hi-hat's and ride's articulations the kit has no hits for: off, so they play the nearest it has (their
    // stand-ins), not a synth drum among recorded ones.
    // (0.36.0: the extra drums too: tom 4 and 5, the shoulder, the chokes, crashes 3 to 6, the FX crash, china 2.)
    using namespace synth::drums::notemap;
    for (const auto slot : { HO1, HO3, RE, HH, T4, HSH, C1C, C2C, CHC, SPC, C3, C4, C5, C6, CFX, CH2, T5 })
        if (assigned.slots.count (slot) == 0)
            setParameterNow (ParamIDs::drum (slot, "On"), 0.0f);
    rememberKit (info.name, kitFile.getParentDirectory());
    startLoading (refs);
    markModified();
    requestCheckpoint();
    return result;
}

ParagonX1AudioProcessor::SamplePackImport ParagonX1AudioProcessor::importSamplePack (const juce::File& folder)
{
    SamplePackImport result;
    const auto info = synth::drums::samplepack::scan (folder, true);
    result.name = info.name;
    result.unused = info.unused;
    result.userChannels = info.userChannels;
    if (! info.valid())
        return result;
    // Every drum it has: SAMPLE, on, its sounds (streamed); the hat's steps and the rimshot it has none of: off (they
    // play the nearest it has: a pack's rimshot is often its snare's hardest layer).
    std::vector<std::pair<int, juce::String>> refs;
    for (const auto& [slot, files] : info.slots)
    {
        result.slots.emplace_back (slot, (int) files.size());
        setDrumSample (slot, nullptr);
        makeSlotPlaySample (slot);
        setParameterNow (ParamIDs::drum (slot, "On"), 1.0f);
        setParameterNow (ParamIDs::drum (slot, "LayerOut"), 0.0f);   // (a layer's USER channel may be one of the pack's now)
        refs.emplace_back (slot, "pack2|" + juce::String (slot) + "|{kit:" + info.name + "}/");
    }
    using namespace synth::drums::notemap;
    for (const auto slot : { HO1, HO3, SD2, RE, HH, HSH, C1C, C2C, CHC, SPC, T4, C3, C4, C5, C6, CFX, CH2, T5 })
        if (info.slots.count (slot) == 0)
            setParameterNow (ParamIDs::drum (slot, "On"), 0.0f);
    // Its USER channels: fresh, named; a trigger or effect mic starts 12 dB down, a sub kick 6 (our guesses: they're
    // blended in to taste), the rest at 0 dB.
    auto m = matrix;
    for (int u = 0; u < info.userChannels.size() && u < synth::mixer::numUserChannels; ++u)
    {
        const auto channel = synth::mixer::numChannels + u;
        for (const auto* what : ParamIDs::mixSettings)
            if (auto* parameter = parameters.getParameter (ParamIDs::mix (channel, what)))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (parameter->getDefaultValue());
                parameter->endChangeGesture();
            }
        const auto& name = info.userChannels[u];
        const auto level = name.contains ("TRIGGER") || name.contains ("FX") ? -12.0f : name.contains ("SUB") ? -6.0f : 0.0f;
        setParameterNow (ParamIDs::mix (channel, "Level"), level);
        // 0.39.0: into the bus (or output) of the drum's own channel ("KICK SUB" with KICK IN onto the KICK bus).
        if (const auto home = synth::mixer::channelForName (name); home >= 0 && home < synth::mixer::numChannels)
        {
            setParameterNow (ParamIDs::mix (channel, "Output"), (float) getMixerOutput (home));
            setParameterNow (ParamIDs::mix (channel, "Route"), (float) getMixerRoute (home));
        }
        m.userChannelNames[(size_t) u] = name.substring (0, 24);
    }
    setModMatrix (m);
    rememberKit (info.name, folder);
    startLoading (refs);
    markModified();
    requestCheckpoint();
    return result;
}

void ParagonX1AudioProcessor::loadInstrumentRef (int slot, const juce::String& ref)
{
    slot = juce::jlimit (0, ParamIDs::numDrums - 1, slot);
    setDrumSample (slot, nullptr);
    makeSlotPlaySample (slot);
    startLoading ({ { slot, ref } });
    markModified();
    requestCheckpoint();
}

void ParagonX1AudioProcessor::loadLayer (int slot, const juce::File& fileOrFolder)
{
    slot = juce::jlimit (0, ParamIDs::numDrums - 1, slot);
    startLoading ({ { layerOf (slot), (fileOrFolder.isDirectory() ? "folder||" : "file||") + fileOrFolder.getFullPathName() } });
    // Through the mixer (a multi-mic drum, or one heard through SIMULATED MICS): a channel of its own, the drum's own
    // close mics 10 dB down so the layer leads (its room, overheads and bleed stay as they are).
    const auto instrument = drumInstruments[(size_t) slot];
    const auto micSim = param (ParamIDs::drumsMicSim) > 0.5f;
    if ((instrument != nullptr && instrument->hasMics()) || micSim)
        if (createLayerChannel (slot) >= 0)
        {
            const auto piece = synth::drums::pieceOfSlot (slot);
            for (int c = 0; c < synth::mixer::numChannels && piece >= 0; ++c)
                if (synth::drums::isDirect (slot, c) && ! synth::mixer::hearsWholeKit (c))
                    if (auto* level = parameters.getParameter (ParamIDs::mix (c, "Level")))
                    {
                        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (level);
                        setParameterNow (ParamIDs::mix (c, "Level"), juce::jmax (-60.0f, ranged->convertFrom0to1 (ranged->getValue()) - 10.0f));
                    }
        }
    markModified();
    requestCheckpoint();
}

void ParagonX1AudioProcessor::clearLayer (int slot)
{
    slot = juce::jlimit (0, ParamIDs::numDrums - 1, slot);
    setDrumInstrument (layerOf (slot), nullptr);
    instrumentRefs[(size_t) layerOf (slot)] = {};
    markModified();
    requestCheckpoint();
}

int ParagonX1AudioProcessor::createLayerChannel (int slot)
{
    slot = juce::jlimit (0, ParamIDs::numDrums - 1, slot);
    // The first USER channel no mic route and no other layer uses: settings back to new, named, the layer into it.
    auto used = getUserChannelsInUse();
    for (int u = 0; u < synth::mixer::numUserChannels; ++u)
    {
        const auto channel = synth::mixer::numChannels + u;
        if (used.contains (channel))
            continue;
        for (const auto* what : ParamIDs::mixSettings)
            if (auto* parameter = parameters.getParameter (ParamIDs::mix (channel, what)))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (parameter->getDefaultValue());
                parameter->endChangeGesture();
            }
        auto m = matrix;
        const auto piece = synth::drums::pieceOfSlot (slot);
        m.userChannelNames[(size_t) u] = (juce::String (piece >= 0 ? synth::drums::pieces[(size_t) piece].name : synth::drums::slotDefaults[(size_t) slot].key) + " LAYER").substring (0, 24);
        setModMatrix (m);
        // 0.39.0: into the bus (or output) its drum's own channel goes to (the kick's layer onto the KICK bus).
        if (const auto home = synth::drums::homeChannel (slot); home >= 0 && home < synth::mixer::numChannels)
        {
            setParameterNow (ParamIDs::mix (channel, "Output"), (float) getMixerOutput (home));
            setParameterNow (ParamIDs::mix (channel, "Route"), (float) getMixerRoute (home));
        }
        setParameterNow (ParamIDs::drum (slot, "LayerOut"), (float) (channel + 1));
        requestCheckpoint();
        return channel;
    }
    return -1;
}

juce::File ParagonX1AudioProcessor::knownKitsFile() const { return rigLibrary->getFolder().getChildFile ("Sample Kits.xml"); }

void ParagonX1AudioProcessor::rememberKit (const juce::String& name, const juce::File& folder)
{
    // "Sample Kits.xml" in the presets folder: where each kit was last loaded from on this computer.
    const std::scoped_lock lock (kitsLock);
    auto xml = juce::XmlDocument::parse (knownKitsFile());
    if (xml == nullptr || ! xml->hasTagName ("SampleKits"))
        xml = std::make_unique<juce::XmlElement> ("SampleKits");
    for (auto* k : xml->getChildWithTagNameIterator ("Kit"))
        if (k->getStringAttribute ("name") == name)
        {
            k->setAttribute ("folder", folder.getFullPathName());
            xml->writeTo (knownKitsFile());
            return;
        }
    auto* k = xml->createNewChildElement ("Kit");
    k->setAttribute ("name", name);
    k->setAttribute ("folder", folder.getFullPathName());
    knownKitsFile().getParentDirectory().createDirectory();
    xml->writeTo (knownKitsFile());
}

juce::File ParagonX1AudioProcessor::knownKitFolder (const juce::String& name) const
{
    const std::scoped_lock lock (kitsLock);
    if (const auto xml = juce::XmlDocument::parse (knownKitsFile()))
        for (const auto* k : xml->getChildWithTagNameIterator ("Kit"))
            if (k->getStringAttribute ("name") == name)
                if (const juce::File folder (k->getStringAttribute ("folder")); folder.isDirectory())
                    return folder;
    return {};
}

std::shared_ptr<const synth::drums::Sample> ParagonX1AudioProcessor::getDrumSample (int slot) const
{
    return drumSamples[(size_t) juce::jlimit (0, ParamIDs::numDrums - 1, slot)];
}

void ParagonX1AudioProcessor::setParameterNow (const juce::String& id, float value)
{
    if (auto* parameter = parameters.getParameter (id))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }
}

namespace
{
    juce::String mixerStripId (int strip, const char* what)
    {
        // A mic channel's (USER 1-8 after the buses) or a bus's.
        const auto channel = synth::mixer::channelOfStrip (strip);
        return channel >= 0 ? ParamIDs::mix (channel, what) : ParamIDs::mixBus (strip - synth::mixer::numChannels, what);
    }
}

int ParagonX1AudioProcessor::getMixerRoute (int strip) const
{
    const auto* value = parameters.getRawParameterValue (mixerStripId (juce::jlimit (0, synth::mixer::numStrips - 1, strip), "Route"));
    return value != nullptr ? juce::roundToInt (value->load()) : 0;
}

int ParagonX1AudioProcessor::getMixerOutput (int strip) const
{
    const auto* value = parameters.getRawParameterValue (mixerStripId (juce::jlimit (0, synth::mixer::numStrips - 1, strip), "Output"));
    return value != nullptr ? juce::roundToInt (value->load()) : 0;
}

void ParagonX1AudioProcessor::setMixerRoute (int strip, int route)
{
    strip = juce::jlimit (0, synth::mixer::numStrips - 1, strip);
    // A bus only into a later one.
    if (const auto b = synth::mixer::busOfTarget (route); b >= 0 && ! synth::mixer::canFeedBus (strip, b))
        return;
    setParameterNow (mixerStripId (strip, "Route"), (float) juce::jlimit (0, synth::mixer::routeNames.size() - 1, route));
}

void ParagonX1AudioProcessor::setMixerOutput (int strip, int output)
{
    strip = juce::jlimit (0, synth::mixer::numStrips - 1, strip);
    setParameterNow (mixerStripId (strip, "Route"), 0.0f);
    setParameterNow (mixerStripId (strip, "Output"), (float) juce::jlimit (0, synth::numOutputs - 1, output));
}

void ParagonX1AudioProcessor::mixerStereoReverse()
{
    for (int strip = 0; strip < synth::mixer::numStrips; ++strip)
        if (const auto* value = parameters.getRawParameterValue (mixerStripId (strip, "Pan")); value != nullptr && std::abs (value->load()) > 1.0e-6f)
            setParameterNow (mixerStripId (strip, "Pan"), -value->load());
    auto m = matrix;
    for (auto& send : m.mixerSends)
    {
        send.directPan = -send.directPan;
        send.bleedPan = -send.bleedPan;
    }
    setModMatrix (m);
    requestCheckpoint();
}

int ParagonX1AudioProcessor::multichannelOutputOf (int channel) noexcept
{
    using namespace synth::mixer;
    switch (channel)
    {
        case kickIn: case kickOut:         return 1;   // OUT 2
        case snareTop: case snareBottom:   return 2;
        case hiHat:                        return 3;
        case tom1: case tom2: case tom3:   return 4;
        case cymbals:                      return 5;
        case overheads:                    return 6;
        case room: case ambience:          return 7;
        default:                           return 8;   // OTHER: OUT 9
    }
}

int ParagonX1AudioProcessor::getMicRoute (int piece, int mic) const
{
    if (piece < 0 || piece >= synth::mixer::numPieces || mic < 0 || mic >= synth::mixer::numChannels)
        return synth::mixer::routeAuto;
    return matrix.micRoute (piece, mic);
}

void ParagonX1AudioProcessor::setMicRoute (int piece, int mic, int to)
{
    if (piece < 0 || piece >= synth::mixer::numPieces || mic < 0 || mic >= synth::mixer::numChannels)
        return;
    if (to != synth::mixer::routeAuto && to != synth::mixer::routeNone && (to < 0 || to >= synth::mixer::numMicChannels))
        return;
    auto m = matrix;
    m.micRoute (piece, mic) = (std::int8_t) to;
    setModMatrix (m);
    requestCheckpoint();
}

juce::Array<int> ParagonX1AudioProcessor::getUserChannelsInUse() const
{
    juce::Array<int> used;
    for (const auto to : matrix.micRoutes)
        if (synth::mixer::isUserChannel (to))
            used.addIfNotAlreadyThere (to);
    // 0.33.0: and the USER channels a drum's own sounds use (a sample pack's extra mics).
    for (int k = 0; k < ParamIDs::numDrums; ++k)
        if (const auto& instrument = drumInstruments[(size_t) k]; instrument != nullptr)
            for (const auto c : instrument->micChannels())
                if (synth::mixer::isUserChannel (c))
                    used.addIfNotAlreadyThere (c);
    // 0.29.0: and the drums' layers' channels.
    for (int k = 0; k < ParamIDs::numDrums; ++k)
        if (auto* out = parameters.getRawParameterValue (ParamIDs::drum (k, "LayerOut")))
            if (const auto channel = juce::roundToInt (out->load()) - 1; synth::mixer::isUserChannel (channel))
                used.addIfNotAlreadyThere (channel);
    used.sort();
    return used;
}

int ParagonX1AudioProcessor::createUserChannel (int piece, int mic)
{
    if (piece < 0 || piece >= synth::mixer::numPieces || mic < 0 || mic >= synth::mixer::numChannels)
        return -1;
    const auto used = getUserChannelsInUse();
    for (int u = 0; u < synth::mixer::numUserChannels; ++u)
    {
        const auto channel = synth::mixer::numChannels + u;
        if (used.contains (channel))
            continue;
        // A fresh channel: its settings back to new, named after the piece and the mic (as the reference's "X ..."),
        // into the same output and bus as the mic's own channel.
        for (const auto* what : ParamIDs::mixSettings)
            if (auto* parameter = parameters.getParameter (ParamIDs::mix (channel, what)))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (parameter->getDefaultValue());
                parameter->endChangeGesture();
            }
        setParameterNow (ParamIDs::mix (channel, "Output"), (float) getMixerOutput (mic));
        setParameterNow (ParamIDs::mix (channel, "Route"), (float) getMixerRoute (mic));
        auto m = matrix;
        m.userChannelNames[(size_t) u] = (juce::String (synth::drums::pieces[(size_t) piece].name) + " " + synth::mixer::channelNames[mic]).substring (0, 24);
        m.micRoute (piece, mic) = (std::int8_t) channel;
        setModMatrix (m);
        requestCheckpoint();
        return channel;
    }
    return -1;
}

juce::String ParagonX1AudioProcessor::getMixerChannelName (int channel) const
{
    if (channel >= 0 && channel < synth::mixer::numChannels)
        return synth::mixer::channelNames[channel];
    if (synth::mixer::isUserChannel (channel))
    {
        const auto& name = matrix.userChannelNames[(size_t) (channel - synth::mixer::numChannels)];
        return name.isNotEmpty() ? name : "USER " + juce::String (channel - synth::mixer::numChannels + 1);
    }
    return {};
}

juce::String ParagonX1AudioProcessor::getMixerBusName (int bus) const
{
    if (bus < 0 || bus >= synth::mixer::numBuses)
        return {};
    const auto& name = matrix.busNames[(size_t) bus];
    return name.isNotEmpty() ? name : synth::mixer::busNames[bus];
}

void ParagonX1AudioProcessor::renameBus (int bus, const juce::String& name)
{
    if (bus < 0 || bus >= synth::mixer::numBuses)
        return;
    auto m = matrix;
    m.busNames[(size_t) bus] = name.trim().toUpperCase().substring (0, 24);
    setModMatrix (m);
    requestCheckpoint();
}

int ParagonX1AudioProcessor::getMachinePad (int place) const noexcept
{
    return place >= 0 && place < ParamIDs::numDrums ? machinePads[(size_t) place] : 0;
}

void ParagonX1AudioProcessor::setMachinePad (int place, int slot)
{
    if (place < 0 || place >= ParamIDs::numDrums || slot < 0 || slot >= ParamIDs::numDrums)
        return;
    // Swapped with the place that had it: every drum stays on one place.
    const auto other = (int) (std::find (machinePads.begin(), machinePads.end(), slot) - machinePads.begin());
    if (other < ParamIDs::numDrums)
        machinePads[(size_t) other] = machinePads[(size_t) place];
    machinePads[(size_t) place] = slot;
    markModified();
}

void ParagonX1AudioProcessor::resetMachinePads()
{
    for (int i = 0; i < ParamIDs::numDrums; ++i)
        machinePads[(size_t) i] = i;
    markModified();
}

void ParagonX1AudioProcessor::renameUserChannel (int channel, const juce::String& name)
{
    if (! synth::mixer::isUserChannel (channel))
        return;
    auto m = matrix;
    m.userChannelNames[(size_t) (channel - synth::mixer::numChannels)] = name.trim().toUpperCase().substring (0, 24);
    setModMatrix (m);
    requestCheckpoint();
}

void ParagonX1AudioProcessor::applyMixerOutputs (bool multichannel)
{
    for (int strip = 0; strip < synth::mixer::numStrips; ++strip)
        setMixerOutput (strip, multichannel && strip < synth::mixer::numChannels ? multichannelOutputOf (strip) : 0);
    requestCheckpoint();
}

void ParagonX1AudioProcessor::previewDrum (int slot) noexcept
{
    if (slot >= 0 && slot < ParamIDs::numDrums)
        pendingPreview[(size_t) slot] = true;
}

//==============================================================================
// TAP TO FIND (0.6.0)
void ParagonX1AudioProcessor::startTapping (double bpm, double loopBeats, double beatsPerBar)
{
    tapBpm = juce::jlimit (30.0, 300.0, bpm);
    tapLoopBeats = juce::jmax (1.0, loopBeats);
    tapBeatsPerBar = juce::jmax (0.5, beatsPerBar);
    ++tapRestart;   // from the loop's start
    tapping = true;
}

void ParagonX1AudioProcessor::stopTapping()
{
    tapping = false;
    tapPosition = -1.0;
}

void ParagonX1AudioProcessor::tapPad (int lane) noexcept
{
    if (lane >= 0 && lane < grooves::numLanes)
        tapsWaiting.fetch_or (1 << lane);
}

int ParagonX1AudioProcessor::popTaps (std::vector<Tap>& into)
{
    const auto scope = tapFifo.read (tapFifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i) into.push_back (tapBuffer[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i) into.push_back (tapBuffer[(size_t) (scope.startIndex2 + i)]);
    return scope.blockSize1 + scope.blockSize2;
}

void ParagonX1AudioProcessor::runTapping (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& notes,
                                          const synth::drums::KitSettings& kit) noexcept
{
    const auto numSamples = buffer.getNumSamples();
    if (! tapping.load())
    {
        tapsWaiting = 0;
        clickAt = -1;
        return;
    }
    if (const auto restart = tapRestart.load(); restart != tapRestartSeen)
    {
        tapRestartSeen = restart;
        tapSamples = 0;
        clickAt = -1;
    }
    const auto perSample = tapBpm.load() / 60.0 / getSampleRate();
    const auto loop = tapLoopBeats.load();
    const auto beatAt = [&] (int sample) { return std::fmod ((double) (tapSamples + sample) * perSample, loop); };
    const auto record = [&] (int lane, double beat)
    {
        const auto scope = tapFifo.write (1);
        if (scope.blockSize1 > 0)
            tapBuffer[(size_t) scope.startIndex1] = { lane, beat };
        else if (scope.blockSize2 > 0)
            tapBuffer[(size_t) scope.startIndex2] = { lane, beat };
    };

    // The pads clicked since the last block: their drums now, their taps here. Notes played: where they fall.
    if (const auto pads = tapsWaiting.exchange (0); pads != 0)
        for (int lane = 0; lane < grooves::numLanes; ++lane)
            if ((pads & (1 << lane)) != 0)
            {
                drumKit.trigger (grooves::slotOfLane (lane), 0.8f, kit);
                record (lane, beatAt (0));
            }
    for (const auto metadata : notes)
        if (const auto& m = metadata.getMessage(); m.isNoteOn())
            if (const auto lane = grooves::laneOf (kit.slotFor (m.getNoteNumber())); lane >= 0)
                record (lane, beatAt (juce::jlimit (0, numSamples - 1, metadata.samplePosition)));

    // THE METRONOME: a short click on each beat (higher and on the bar's first), added to MAIN after everything.
    const auto clickLength = (int) (0.03 * getSampleRate());
    const auto perBar = tapBeatsPerBar.load();
    for (int i = 0; i < numSamples; ++i)
    {
        const auto now = (double) (tapSamples + i) * perSample;
        const auto before = (double) (tapSamples + i - 1) * perSample;
        if (tapSamples + i == 0 || std::floor (now) != std::floor (before))
        {
            clickAt = 0;
            clickAccent = std::fmod (std::floor (std::fmod (now, loop) + 1.0e-9), perBar) < 0.5;
        }
        if (clickAt < 0)
            continue;
        const auto t = clickAt / getSampleRate();
        const auto s = metronomeLevel * (float) (std::sin (juce::MathConstants<double>::twoPi * (clickAccent ? 1600.0 : 1100.0) * t) * std::exp (-t / 0.008));
        for (int c = 0; c < juce::jmin (2, getMainBusNumOutputChannels(), buffer.getNumChannels()); ++c)
            buffer.addSample (c, i, s);
        if (++clickAt >= clickLength)
            clickAt = -1;
    }
    tapSamples += numSamples;
    tapPosition = beatAt (0);
}

synth::drums::KitSettings ParagonX1AudioProcessor::getKitSettings() const noexcept
{
    synth::drums::KitSettings kit;
    kit.on = true;   // PR7-D: the kit always plays (PR7-K's DRUMS ON switch is gone)
    kit.rootKey = juce::roundToInt (param (ParamIDs::drumsRoot));
    // THE NOTE MAP: ROOT works it out from rootKey; the others give each note's slot.
    if (const auto layout = getMapLayout(); layout != synth::drums::notemap::Layout::root)
    {
        kit.mapped = true;
        if (layout == synth::drums::notemap::Layout::custom)
            for (size_t n = 0; n < kit.keys.size(); ++n)
                kit.keys[n] = (std::int8_t) customForAudio[n].load();
        else
        {
            static const auto groove = synth::drums::notemap::grooveLibrary(), gm = synth::drums::notemap::generalMidi();
            kit.keys = layout == synth::drums::notemap::Layout::grooveLibrary ? groove : gm;
        }
    }
    kit.velocity = param (ParamIDs::drumsVelocity);
    kit.gain = juce::Decibels::decibelsToGain (param (ParamIDs::drumsLevel));
    kit.sendOn = { isOn (ParamIDs::delayOn), isOn (ParamIDs::reverbOn) };
    for (size_t k = 0; k < kit.slots.size(); ++k)
    {
        const auto& v = drumParameters[k].values;
        const auto get = [&v] (int index) { return v[(size_t) index]->load(); };
        using ParamIDs::drumSetting;
        auto& s = kit.slots[k];
        s.model = (synth::drums::Model) juce::jlimit (0, synth::drums::modelNames.size() - 1, juce::roundToInt (get (drumSetting ("Model"))));
        const auto level = get (drumSetting ("Level"));
        s.gain = level <= -47.95f ? 0.0f : juce::Decibels::decibelsToGain (level);
        s.pan = get (drumSetting ("Pan"));
        s.pitch = get (drumSetting ("Pitch"));
        s.decay = get (drumSetting ("Decay"));
        s.tone = get (drumSetting ("Tone"));
        s.snap = get (drumSetting ("Snap"));
        s.mute = get (drumSetting ("Mute")) > 0.5f;
        s.solo = get (drumSetting ("Solo")) > 0.5f;
        s.choke = juce::roundToInt (get (drumSetting ("Choke")));
        s.insertBus = juce::roundToInt (get (drumSetting ("Insert")));
        s.output = juce::jlimit (0, synth::numOutputs - 1, juce::roundToInt (get (drumSetting ("Output"))));
        s.send = { get (drumSetting ("SendDelay")), get (drumSetting ("SendReverb")) };
        s.sample = samplesForAudio[k].load();
        s.start = get (drumSetting ("SampleStart"));
        s.end = get (drumSetting ("SampleEnd"));
        // REVERSE: a SAMPLE slot plays its own file backwards; a synth drum its rendered hit.
        s.reverseBaked = s.model != synth::drums::Model::sample;
        s.reverseSource = s.reverseBaked ? reversesForAudio[k].load() : s.sample;
        // THE SAMPLE EDITOR (6c).
        s.trim = juce::Decibels::decibelsToGain (get (drumSetting ("Gain")));
        s.mono = get (drumSetting ("Mono")) > 0.5f;
        s.delay = get (drumSetting ("Delay")) * 0.001f;
        s.lowPass = get (drumSetting ("LowPass"));
        s.highPass = get (drumSetting ("HighPass"));
        if (const auto* compiled = matrixForAudio.load())
        {
            s.envelope = &compiled->drumEnvelopes[k];
            if (! compiled->velocityCurves[k].isStraight())
                s.velocityCurve = &compiled->velocityCurves[k];
        }
        // THE SAMPLE ENGINE (0.19.0). With an instrument, its loudest layer's first hit stands as the slot's sample
        // for the page's picture and REVERSE (a hit plays the instrument's).
        if (const auto* instrument = instrumentsForAudio[k].load(); instrument != nullptr && s.model == synth::drums::Model::sample)
        {
            s.instrument = instrument;
            for (auto layer = instrument->layers.rbegin(); layer != instrument->layers.rend() && s.sample == nullptr; ++layer)
                if (! layer->hits.empty())
                    s.sample = layer->hits.front().sample.get();
            s.start = 0.0f;
            s.end = 1.0f;
            s.reverseSource = s.sample;
        }
        s.randomize = get (drumSetting ("Randomize")) > 0.5f;
        s.adjacent = get (drumSetting ("Adjacent")) > 0.5f;
        s.velocityToVolume = get (drumSetting ("VelToVol")) > 0.5f;
        // 0.29.0: ITS LAYER.
        s.layer = instrumentsForAudio[(size_t) layerOf (k)].load();
        const auto layerDb = get (drumSetting ("LayerLevel"));
        s.layerGain = layerDb <= -59.95f ? 0.0f : juce::Decibels::decibelsToGain (layerDb);
        s.layerChannel = juce::roundToInt (get (drumSetting ("LayerOut"))) - 1;
        s.layerLimit = juce::roundToInt (get (drumSetting ("LayerLimit")));
        s.voices = juce::jlimit (1, synth::drums::Kit::maxVoices, juce::roundToInt (get (drumSetting ("Voices"))));
        s.position = get (drumSetting ("Position"));
        s.shell = get (drumSetting ("Shell"));
        s.variation = get (drumSetting ("Variation"));
        s.miked = get (drumSetting ("Mics")) > 0.5f;
        s.velMin = get (drumSetting ("VelMin")) / 127.0f - 1.0e-4f;
        s.velMax = get (drumSetting ("VelMax")) / 127.0f + 1.0e-4f;
        s.envelopeSeconds = hitLengths[k].load();
        s.active = get (drumSetting ("On")) > 0.5f;
        s.muteTail = (synth::drums::MuteTail) juce::jlimit (0, synth::drums::muteTailNames.size() - 1, juce::roundToInt (get (drumSetting ("MuteTail"))));
    }
    for (int i = 0; i < 4; ++i)
        kit.pedalSteps[(size_t) i] = juce::roundToInt (param (ParamIDs::drumsPedalSteps[i].toRawUTF8()));
    // THE MIXER (0.20.0; USER 1-8 since 0.23.0).
    for (int c = 0; c < synth::mixer::numMicChannels; ++c)
    {
        auto& ch = kit.channels[(size_t) c];
        const auto& values = mixParameters[(size_t) c];
        const auto get = [&values] (int index) { return values[(size_t) index]->load(); };
        using ParamIDs::mixSetting;
        const auto level = get (mixSetting ("Level")), bleed = get (mixSetting ("Bleed"));
        ch.gain = level <= -59.95f ? 0.0f : juce::Decibels::decibelsToGain (level);
        ch.pan = get (mixSetting ("Pan"));
        ch.mute = get (mixSetting ("Mute")) > 0.5f;
        ch.solo = get (mixSetting ("Solo")) > 0.5f;
        ch.invert = get (mixSetting ("Phase")) > 0.5f;
        ch.bleedGain = bleed <= -59.95f ? 0.0f : juce::Decibels::decibelsToGain (bleed);
        ch.bleedOn = get (mixSetting ("BleedOn")) > 0.5f;
        ch.offsetSeconds = get (mixSetting ("Offset")) * 0.001;
        ch.releaseSeconds = get (mixSetting ("Release")) * 0.001;
        ch.output = juce::jlimit (0, synth::numOutputs - 1, juce::roundToInt (get (mixSetting ("Output"))));
        ch.route = juce::jlimit (0, synth::mixer::routeNames.size() - 1, juce::roundToInt (get (mixSetting ("Route"))));
    }
    // 0.21.0: the buses (ParamIDs::busSettings' order) and MUTE MODE.
    for (int b = 0; b < synth::mixer::numBuses; ++b)
    {
        auto& bus = kit.buses[(size_t) b];
        const auto& values = busParameters[(size_t) b];
        const auto level = values[0]->load();
        bus.gain = level <= -59.95f ? 0.0f : juce::Decibels::decibelsToGain (level);
        bus.pan = values[1]->load();
        bus.mute = values[2]->load() > 0.5f;
        bus.solo = values[3]->load() > 0.5f;
        bus.invert = values[4]->load() > 0.5f;
        bus.output = juce::jlimit (0, synth::numOutputs - 1, juce::roundToInt (values[5]->load()));
        bus.route = juce::jlimit (0, synth::mixer::routeNames.size() - 1, juce::roundToInt (values[6]->load()));
    }
    kit.muteMode = muteModeParameter->load() > 0.5f ? synth::mixer::MuteMode::postSend : synth::mixer::MuteMode::preSend;
    if (const auto* compiled = matrixForAudio.load())
    {
        kit.bleed = &compiled->bleed;
        kit.sends = &compiled->sends;
        kit.micRoutes = &compiled->micRoutes;
    }
    kit.effects = &mixerEffects;
    kit.streamer = streamerForAudio.load();   // 0.28.0: DISK STREAMING
    kit.waitForDisk = isNonRealtime();        // bouncing waits for the disk; live plays on
    // SIMULATED MICS AND ROOM (0.26.0).
    kit.micSim.on = param (ParamIDs::drumsMicSim) > 0.5f;
    kit.micSim.room = juce::jlimit (0, synth::micsim::roomNames.size() - 1, juce::roundToInt (param (ParamIDs::drumsRoom)));
    kit.micSim.decay = param (ParamIDs::drumsRoomDecay);
    kit.micSim.tone = param (ParamIDs::drumsRoomTone);
    // MIDI IN (0.18.0).
    kit.channel = juce::jlimit (0, 16, juce::roundToInt (param (ParamIDs::drumsMidiChannel)));
    if (const auto* in = midiInForAudio.load())
    {
        if (! in->curve.isStraight())
            kit.inputCurve = &in->curve;
        kit.zones = in->zones;
    }
    return kit;
}

synth::drumseq::Settings ParagonX1AudioProcessor::getDrumSeqSettings() const noexcept
{
    synth::drumseq::Settings s;
    s.on = isOn (ParamIDs::dseqOn);
    s.trigger = (synth::drumseq::Trigger) juce::jlimit (0, 1, juce::roundToInt (param (ParamIDs::dseqTrigger)));
    s.pattern = juce::jlimit (0, synth::drumseq::numPatterns - 1, juce::roundToInt (param (ParamIDs::dseqPattern)));
    s.division = juce::jlimit (0, synth::seq::divisionNames.size() - 1, juce::roundToInt (param (ParamIDs::dseqDivision)));
    s.timing = (synth::seq::Timing) juce::jlimit (0, synth::seq::timingNames.size() - 1, juce::roundToInt (param (ParamIDs::dseqTiming)));
    s.lengthBeats = juce::jlimit (1, synth::drumseq::maxBeats, juce::roundToInt (param (ParamIDs::dseqLength)));
    s.shuffle = param (ParamIDs::shuffle);
    s.velocityAmount = param (ParamIDs::dseqVelocity);
    return s;
}

void ParagonX1AudioProcessor::updateDrumReverses()
{
    // Which synth drums a reversed note plays.
    std::array<bool, ParamIDs::numDrums> wanted {};
    for (const auto& pattern : matrix.drumPatterns)
        for (const auto& n : pattern.notes)
            if (n.reverse != synth::drumseq::Reverse::off)
                wanted[(size_t) juce::jlimit (0, ParamIDs::numDrums - 1, n.row)] = true;
    const auto kit = getKitSettings();
    const auto rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (size_t k = 0; k < (size_t) ParamIDs::numDrums; ++k)
    {
        // The hit alone: VOLUME, GAIN, LP / HP and the envelope are put on as it plays (the envelope by where it is
        // in the hit), so none of them go into the recording (before 6c, VOLUME did, and counted twice).
        auto s = kit.slots[k];
        s.gain = s.trim = 1.0f;
        s.envelope = nullptr;
        s.lowPass = 20000.0f;
        s.highPass = 20.0f;
        s.delay = 0.0f;

        // The drum synths' hit lengths, which their drawn envelopes span (only measured for one that's drawn).
        juce::String lengthKey;
        if (s.model != synth::drums::Model::sample && synth::drums::envelopeShapes (kit.slots[k].envelope))
            lengthKey << (int) s.model << ":" << s.pitch << ":" << s.decay << ":" << s.tone << ":" << s.snap;
        if (lengthKey != hitLengthKeys[k])
        {
            hitLengthKeys[k] = lengthKey;
            hitLengths[k] = lengthKey.isNotEmpty() ? (float) synth::drums::Kit::hitSeconds (s) : 0.0f;
        }

        juce::String key;
        if (wanted[k] && s.model != synth::drums::Model::sample)
            key << (int) s.model << ":" << s.pitch << ":" << s.decay << ":" << s.tone << ":" << s.snap << ":" << rate;
        if (key == reverseKeys[k])
            continue;
        reverseKeys[k] = key;
        std::shared_ptr<synth::drums::Sample> hit;
        if (key.isNotEmpty())
        {
            // The hit at full velocity (its level, pitch and shaping in it), to where it has died away (-60 dB).
            const auto rendered = synth::drums::Kit::renderHit (s, 1.0f, rate, 4.0);
            const auto* x = rendered.getReadPointer (0);
            auto peak = 0.0f;
            for (int i = 0; i < rendered.getNumSamples(); ++i)
                peak = juce::jmax (peak, std::abs (x[i]));
            auto length = rendered.getNumSamples();
            while (length > 3 && std::abs (x[length - 1]) < peak * 0.001f)
                --length;
            hit = std::make_shared<synth::drums::Sample>();
            hit->name = "reversed";
            hit->sampleRate = rate;
            hit->channels = { std::vector<float> (x, x + juce::jmax (3, length)) };
        }
        if (reverseHits[k] != nullptr)
            retiredSamples.emplace_back (reverseHits[k], now);
        reverseHits[k] = hit;
        reversesForAudio[k] = hit.get();
    }
}

//==============================================================================
// KITS AND SEQUENCES (6c part 2).
namespace
{
    // What each file holds: which parameters (the rest of the sound is untouched).
    bool isKitParameter (const juce::String& id)
    {
        // The kit's LEVEL and VELOCITY, and every slot's "drum<n>..." setting (not DRUMS ON or ROOT: how it's played).
        // Not each drum's OUTPUT (0.3.0): that's the DAW project's mixer, as for presets.
        return id == ParamIDs::drumsLevel || id == ParamIDs::drumsVelocity
            || id == ParamIDs::drumsMicSim || id == ParamIDs::drumsRoom || id == ParamIDs::drumsRoomDecay || id == ParamIDs::drumsRoomTone
            || (id.startsWith ("drum") && juce::CharacterFunctions::isDigit (id[4]) && ! ParamIDs::isOutputChoice (id));
    }
    const juce::StringArray sequenceParameters { ParamIDs::dseqDivision, ParamIDs::dseqTiming, ParamIDs::dseqLength, ParamIDs::dseqVelocity };

    juce::String cleanName (const juce::String& name)
    {
        return juce::File::createLegalFileName (name.trim()).substring (0, 64);
    }

    juce::Array<juce::File> filesIn (const juce::File& folder, const juce::String& pattern)
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, pattern);
        files.sort();
        return files;
    }
}

juce::File ParagonX1AudioProcessor::drumKitsFolder() const { return rigLibrary->getFolder().getChildFile ("Drum Kits"); }
juce::File ParagonX1AudioProcessor::drumSequencesFolder() const { return rigLibrary->getFolder().getChildFile ("Drum Sequences"); }
juce::Array<juce::File> ParagonX1AudioProcessor::listDrumKits() const { return filesIn (drumKitsFolder(), "*.pr7kit"); }
juce::Array<juce::File> ParagonX1AudioProcessor::listDrumSequences() const { return filesIn (drumSequencesFolder(), "*.pr7seq"); }

std::unique_ptr<juce::XmlElement> ParagonX1AudioProcessor::captureDrumKit() const
{
    auto xml = std::make_unique<juce::XmlElement> ("DrumKit");
    xml->setAttribute ("version", 4);   // 2: 0.17.0 (older kits load with drums 21 to 23 off); 3: 0.33.0 (24 to 29); 4: 0.36.0 (30 to 36)
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter); ranged != nullptr && isKitParameter (ranged->paramID))
        {
            auto* e = xml->createNewChildElement ("Param");
            e->setAttribute ("id", ranged->paramID);
            e->setAttribute ("value", ranged->convertFrom0to1 (ranged->getValue()));
        }
    for (int k = 0; k < ParamIDs::numDrums; ++k)
    {
        if (const auto& sample = drumSamples[(size_t) k])
        {
            auto* e = xml->createNewChildElement ("DrumSample");
            e->setAttribute ("slot", k + 1);
            e->setAttribute ("name", sample->name);
            if (sample->path.isNotEmpty())
                e->setAttribute ("path", sample->path);
            e->addTextElement (synth::drums::sampleFiles::encode (*sample));
        }
        const auto& envelope = matrix.drumEnvelopes[(size_t) k];
        if (! (envelope == ModShapeData::flatDrumEnvelope()))
        {
            auto* e = xml->createNewChildElement ("DrumEnvelope");
            e->setAttribute ("index", k + 1);
            e->setAttribute ("points", envelope.encode());
        }
        if (const auto& curve = matrix.drumVelocityCurves[(size_t) k]; ! curve.isStraight())
        {
            auto* e = xml->createNewChildElement ("VelocityCurve");
            e->setAttribute ("index", k + 1);
            e->setAttribute ("curve", curve.encode());
        }
        for (const auto index : { k, layerOf (k) })   // 0.29.0: its layer as "slot" numDrums + k + 1
            if (instrumentRefs[(size_t) index].isNotEmpty())
            {
                auto* e = xml->createNewChildElement ("DrumInstrument");
                e->setAttribute ("slot", index + 1);
                e->setAttribute ("ref", instrumentRefs[(size_t) index]);
            }
    }
    return xml;
}

std::unique_ptr<juce::XmlElement> ParagonX1AudioProcessor::captureDrumSequence() const
{
    auto xml = std::make_unique<juce::XmlElement> ("DrumSequence");
    xml->setAttribute ("version", 1);
    for (const auto& id : sequenceParameters)
        if (auto* ranged = parameters.getParameter (id))
        {
            auto* e = xml->createNewChildElement ("Param");
            e->setAttribute ("id", id);
            e->setAttribute ("value", ranged->convertFrom0to1 (ranged->getValue()));
        }
    for (int p = 0; p < (int) matrix.drumPatterns.size(); ++p)
    {
        auto* e = xml->createNewChildElement ("DrumPattern");
        e->setAttribute ("pattern", synth::drumseq::patternNames[p]);
        e->setAttribute ("notes", matrix.drumPatterns[(size_t) p].encode());
    }
    return xml;
}

bool ParagonX1AudioProcessor::applyDrumKit (const juce::XmlElement& xml)
{
    if (! xml.hasTagName ("DrumKit"))
        return false;
    // Every kit parameter: what the file says, the rest of the kit's to its default (as a preset does).
    std::map<juce::String, float> values;
    for (const auto* e : xml.getChildWithTagNameIterator ("Param"))
        values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");
    if (xml.getIntAttribute ("version", 1) < 2)
        for (const auto& id : ParamIDs::articulationsAddedOff)
            values[id] = 0.0f;
    if (xml.getIntAttribute ("version", 1) < 3)
        for (const auto& id : ParamIDs::slotsAdded033Off)
            values[id] = 0.0f;
    if (xml.getIntAttribute ("version", 1) < 4)
        for (const auto& id : ParamIDs::slotsAdded036Off)
            values[id] = 0.0f;
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter); ranged != nullptr && isKitParameter (ranged->paramID))
        {
            const auto found = values.find (ranged->paramID);
            ranged->setValueNotifyingHost (found != values.end() ? ranged->convertTo0to1 (ranged->getNormalisableRange().snapToLegalValue (found->second))
                                                                 : ranged->getDefaultValue());
        }
    std::array<std::shared_ptr<const synth::drums::Sample>, ParamIDs::numDrums> samples {};
    for (const auto* e : xml.getChildWithTagNameIterator ("DrumSample"))
        if (const auto slot = e->getIntAttribute ("slot") - 1; slot >= 0 && slot < ParamIDs::numDrums)
            if (auto sample = synth::drums::sampleFiles::decode (e->getStringAttribute ("name"), e->getAllSubText()))
            {
                std::const_pointer_cast<synth::drums::Sample> (sample)->path = e->getStringAttribute ("path");
                samples[(size_t) slot] = std::move (sample);
            }
    for (int k = 0; k < ParamIDs::numDrums; ++k)
        setDrumSample (k, samples[(size_t) k]);
    auto m = matrix;
    for (auto& envelope : m.drumEnvelopes)
        envelope = ModShapeData::flatDrumEnvelope();
    for (const auto* e : xml.getChildWithTagNameIterator ("DrumEnvelope"))
        if (const auto index = e->getIntAttribute ("index") - 1; index >= 0 && index < ModMatrix::numDrumSlots)
            if (auto d = ModShapeData::decode (e->getStringAttribute ("points"), -1, -1, -1, true); d.points.size() >= 2)
                m.drumEnvelopes[(size_t) index] = d;
    for (auto& curve : m.drumVelocityCurves)
        curve = {};
    {
        std::vector<std::pair<int, juce::String>> refs;
        std::array<bool, 2 * ParamIDs::numDrums> has {};
        const auto savedBy = xml.getIntAttribute ("version", 1);
        for (const auto* e : xml.getChildWithTagNameIterator ("DrumInstrument"))
            if (const auto slot = ParamIDs::instrumentIndexFrom (e->getIntAttribute ("slot") - 1, savedBy); slot >= 0 && slot < 2 * ParamIDs::numDrums)
            {
                refs.emplace_back (slot, e->getStringAttribute ("ref"));
                has[(size_t) slot] = true;
            }
        for (int k = 0; k < 2 * ParamIDs::numDrums; ++k)
            if (! has[(size_t) k])
            {
                setDrumInstrument (k, nullptr);
                instrumentRefs[(size_t) k] = {};
            }
        startLoading (refs);
    }
    for (const auto* e : xml.getChildWithTagNameIterator ("VelocityCurve"))
        if (const auto index = e->getIntAttribute ("index") - 1; index >= 0 && index < ModMatrix::numDrumSlots)
            m.drumVelocityCurves[(size_t) index] = synth::drums::VelocityCurve::decode (e->getStringAttribute ("curve"));
    setModMatrix (m);
    markModified();
    requestCheckpoint();
    return true;
}

bool ParagonX1AudioProcessor::applyDrumSequence (const juce::XmlElement& xml)
{
    if (! xml.hasTagName ("DrumSequence"))
        return false;
    for (const auto* e : xml.getChildWithTagNameIterator ("Param"))
        if (const auto id = e->getStringAttribute ("id"); sequenceParameters.contains (id))
            if (auto* ranged = parameters.getParameter (id))
                ranged->setValueNotifyingHost (ranged->convertTo0to1 (ranged->getNormalisableRange().snapToLegalValue ((float) e->getDoubleAttribute ("value"))));
    auto m = matrix;
    for (auto& pattern : m.drumPatterns)
        pattern = {};
    for (const auto* e : xml.getChildWithTagNameIterator ("DrumPattern"))
        if (const auto p = synth::drumseq::patternNames.indexOf (e->getStringAttribute ("pattern")); p >= 0)
            m.drumPatterns[(size_t) p] = synth::drumseq::Pattern::decode (e->getStringAttribute ("notes"));
    setModMatrix (m);
    markModified();
    requestCheckpoint();
    return true;
}

bool ParagonX1AudioProcessor::saveDrumKit (const juce::String& name)
{
    const auto clean = cleanName (name);
    if (clean.isEmpty())
        return false;
    drumKitsFolder().createDirectory();
    if (! captureDrumKit()->writeTo (drumKitsFolder().getChildFile (clean + ".pr7kit")))
        return false;
    drumKitName = clean;
    if (drumLink && ! saveDrumSequenceOnly (clean))
        return false;
    return true;
}

bool ParagonX1AudioProcessor::saveDrumSequence (const juce::String& name)
{
    const auto clean = cleanName (name);
    if (clean.isEmpty() || ! saveDrumSequenceOnly (clean))
        return false;
    if (drumLink)
    {
        drumKitsFolder().createDirectory();
        if (! captureDrumKit()->writeTo (drumKitsFolder().getChildFile (clean + ".pr7kit")))
            return false;
        drumKitName = clean;
    }
    return true;
}

bool ParagonX1AudioProcessor::saveDrumSequenceOnly (const juce::String& clean)
{
    drumSequencesFolder().createDirectory();
    if (! captureDrumSequence()->writeTo (drumSequencesFolder().getChildFile (clean + ".pr7seq")))
        return false;
    drumSequenceName = clean;
    return true;
}

bool ParagonX1AudioProcessor::loadDrumKit (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! applyDrumKit (*xml))
        return false;
    drumKitName = file.getFileNameWithoutExtension();
    if (drumLink)
        if (const auto partner = drumSequencesFolder().getChildFile (drumKitName + ".pr7seq"); partner.existsAsFile())
            if (const auto seq = juce::XmlDocument::parse (partner); seq != nullptr && applyDrumSequence (*seq))
                drumSequenceName = drumKitName;
    return true;
}

bool ParagonX1AudioProcessor::loadDrumSequence (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! applyDrumSequence (*xml))
        return false;
    drumSequenceName = file.getFileNameWithoutExtension();
    if (drumLink)
        if (const auto partner = drumKitsFolder().getChildFile (drumSequenceName + ".pr7kit"); partner.existsAsFile())
            if (const auto kit = juce::XmlDocument::parse (partner); kit != nullptr && applyDrumKit (*kit))
                drumKitName = drumSequenceName;
    return true;
}

bool ParagonX1AudioProcessor::loadNeighbourSample (int slot, int direction)
{
    slot = juce::jlimit (0, ParamIDs::numDrums - 1, slot);
    const auto& sample = drumSamples[(size_t) slot];
    if (sample == nullptr || sample->path.isEmpty())
        return false;
    const juce::File current (sample->path);
    auto files = current.getParentDirectory().findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    if (files.isEmpty())
        return false;
    // In natural order ("Kick 2" before "Kick 10"), as sample packs number their files.
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    });
    auto at = files.indexOf (current);
    if (at < 0)
    {
        // The file's gone or renamed: the first by name after where it was.
        at = 0;
        while (at < files.size() && files[at].getFileName().compareNatural (current.getFileName()) < 0)
            ++at;
        at -= direction > 0 ? 1 : 0;
    }
    const auto next = ((at + (direction > 0 ? 1 : -1)) % files.size() + files.size()) % files.size();
    return loadDrumSample (slot, files[next]);
}

juce::MidiFile ParagonX1AudioProcessor::drumPatternToMidi (int pattern) const
{
    const auto p = (size_t) juce::jlimit (0, synth::drumseq::numPatterns - 1, pattern);
    // Drums with an anchor (6c) written early, as the sequencer plays them; each on its home note in the map, so the
    // file plays back on this kit as it is (a drum the map has no note for is left out).
    const auto kit = getKitSettings();
    std::array<double, synth::drumseq::numRows> lead {};
    std::array<int, synth::drumseq::numRows> notes {};
    for (size_t k = 0; k < lead.size(); ++k)
    {
        lead[k] = kit.slots[k].anchorSeconds();
        notes[k] = homeNote ((int) k);
    }
    return synth::drumseq::toMidi (matrix.drumPatterns[p], getDrumSeqSettings(), notes, juce::jmax (1.0, getHostBpm()), lead);
}

bool ParagonX1AudioProcessor::exportDrumMidi (int pattern, const juce::File& file) const
{
    file.getParentDirectory().createDirectory();
    juce::FileOutputStream out (file);
    if (! out.openedOk())
        return false;
    out.setPosition (0);
    out.truncate();
    return drumPatternToMidi (pattern).writeTo (out);
}

juce::File ParagonX1AudioProcessor::drumMidiDragFile (int pattern) const
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Paragon PR7-D")
                    .getChildFile ("Drums " + synth::drumseq::patternNames[juce::jlimit (0, 3, pattern)] + ".mid");
    exportDrumMidi (pattern, file);
    return file;
}

void ParagonX1AudioProcessor::parameterGestureChanged (int, bool gestureIsStarting)
{
    if (gestureIsStarting)
    {
        ++activeGestures;
    }
    else
    {
        // Never below zero, even if a host sends an unmatched "end".
        auto active = activeGestures.load();
        while (active > 0 && ! activeGestures.compare_exchange_weak (active, active - 1)) {}
        requestCheckpoint();
    }
}

ParagonX1AudioProcessor::Snapshot ParagonX1AudioProcessor::takeSnapshot() const
{
    return { captureRig(), currentRig, rigModified.load(), songData };
}

void ParagonX1AudioProcessor::recordCheckpointIfReady()
{
    // Wait until the edit is really finished: no knob still held.
    if (! checkpointWanted.load() || activeGestures.load() > 0)
        return;

    checkpointWanted = false;
    history.record (takeSnapshot());
}

void ParagonX1AudioProcessor::restoreSnapshot (const Snapshot& snapshot)
{
    applyRig (snapshot.rig);
    if (snapshot.song != nullptr && ! (*snapshot.song == *songData))
        installSong (snapshot.song);
    currentRig = snapshot.current;
    rigModified = snapshot.modified;
    rigBroadcaster.sendChangeMessage();
}

void ParagonX1AudioProcessor::undo()
{
    // Record anything not yet recorded first, so undo takes back the latest edit rather than the one before it.
    recordCheckpointIfReady();
    if (const auto snapshot = history.undo())
        restoreSnapshot (*snapshot);
}

void ParagonX1AudioProcessor::redo()
{
    recordCheckpointIfReady();
    if (const auto snapshot = history.redo())
        restoreSnapshot (*snapshot);
}

//==============================================================================
void ParagonX1AudioProcessor::markModified() noexcept
{
    if (! applyingRig.load())
        rigModified = true;
}

void ParagonX1AudioProcessor::parameterChanged (const juce::String&, float)
{
    // Can be called on the audio thread (automation), so only touch the atomic.
    markModified();
}

void ParagonX1AudioProcessor::useRigFolder (const juce::File& folder)
{
    rigLibrary = std::make_unique<RigLibrary> (folder);
    rack->setFolder (folder);
    stopGroove();
    grooveFolders = std::make_unique<grooves::Library> (folder.getChildFile ("Grooves.xml"), folder.getChildFile ("Groove Index.xml"));
    grooveFolders->setUserFolder (folder.getChildFile ("User MIDI"));
    applyGroovePreview();
    rigBroadcaster.sendChangeMessage();
}

Rig ParagonX1AudioProcessor::captureRig() const
{
    Rig rig;
    rig.name = currentRig.name;
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (! ParamIDs::notInRigs.contains (ranged->paramID))
                rig.parameters[ranged->paramID] = ranged->convertFrom0to1 (ranged->getValue());
    for (int k = 0; k < ParamIDs::numDrums; ++k)
    {
        if (drumSamples[(size_t) k] != nullptr)
            rig.drumSamples[k] = drumSamples[(size_t) k];
        for (const auto index : { k, layerOf (k) })
            if (instrumentRefs[(size_t) index].isNotEmpty())
                rig.drumInstruments[index] = instrumentRefs[(size_t) index];
    }
    rig.modulation = matrix;
    return rig;
}

void ParagonX1AudioProcessor::applyRig (const Rig& rig)
{
    applyingRig = true;

    for (auto* parameter : AudioProcessor::getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        if (ranged == nullptr || ParamIDs::notInRigs.contains (ranged->paramID))
            continue;

        // Anything the preset doesn't mention goes back to its default.
        const auto found = rig.parameters.find (ranged->paramID);
        const auto normalised = found != rig.parameters.end()
                                  ? ranged->convertTo0to1 (ranged->getNormalisableRange().snapToLegalValue (found->second))
                                  : ranged->getDefaultValue();
        ranged->setValueNotifyingHost (normalised);
    }

    for (int k = 0; k < ParamIDs::numDrums; ++k)
    {
        const auto found = rig.drumSamples.find (k);
        setDrumSample (k, found != rig.drumSamples.end() ? found->second : nullptr);
    }
    // THE SAMPLE ENGINE: the preset's instruments loaded again (the ones it already has kept).
    {
        std::vector<std::pair<int, juce::String>> refs;
        for (int k = 0; k < 2 * ParamIDs::numDrums; ++k)
        {
            const auto found = rig.drumInstruments.find (k);
            const auto ref = found != rig.drumInstruments.end() ? found->second : juce::String();
            if (ref == instrumentRefs[(size_t) k] && (ref.isEmpty() || drumInstruments[(size_t) k] != nullptr))
                continue;
            if (ref.isEmpty())
            {
                setDrumInstrument (k, nullptr);
                instrumentRefs[(size_t) k] = {};
            }
            else
                refs.emplace_back (k, ref);
        }
        startLoading (refs);
    }
    if (! (matrix == rig.modulation))
    {
        matrix = rig.modulation;
        compileMatrix();
    }

    applyingRig = false;
    rigModified = false;
}

void ParagonX1AudioProcessor::loadRig (const RigLibrary::Entry& entry)
{
    // Copy first: applying parameters can't invalidate it, but be safe.
    const auto rig = entry.rig;
    currentRig = { rig.name, entry.file, entry.isFactory ? rig.name : juce::String() };
    applyRig (rig);
    requestCheckpoint();
    rigBroadcaster.sendChangeMessage();
}

const RigLibrary::Entry* ParagonX1AudioProcessor::findCurrentRigEntry() const
{
    if (currentRig.isUserRig())
        return rigLibrary->find (currentRig.file);
    if (currentRig.factoryName.isNotEmpty())
        return rigLibrary->findFactory (currentRig.factoryName);
    return nullptr;
}

bool ParagonX1AudioProcessor::saveCurrentRig()
{
    const auto* entry = findCurrentRigEntry();
    if (entry == nullptr || entry->isFactory)
        return false;

    // Keep how it's filed; replace the sound.
    auto rig = captureRig();
    rig.name = entry->rig.name;
    rig.bank = entry->rig.bank;
    rig.number = entry->rig.number;
    rig.tags = entry->rig.tags;
    rig.favourite = entry->rig.favourite;
    rig.notes = entry->rig.notes;

    const auto file = entry->file;
    if (! rigLibrary->overwrite (file, rig))
        return false;

    rigModified = false;
    history.replaceCurrent (takeSnapshot());
    rigBroadcaster.sendChangeMessage();
    return true;
}

bool ParagonX1AudioProcessor::saveCurrentRigAs (const juce::String& name)
{
    auto rig = captureRig();
    rig.name = name;

    // File it next to the preset it came from, in the first free slot.
    if (const auto* entry = findCurrentRigEntry())
    {
        rig.bank = entry->rig.bank;
        rig.tags = entry->rig.tags;
        rig.notes = entry->rig.notes;
    }
    rig.number = rigLibrary->nextFreeNumber (rig.bank);

    const auto file = rigLibrary->saveAsNew (rig);
    if (file == juce::File())
        return false;

    const auto* saved = rigLibrary->find (file);
    currentRig = { saved != nullptr ? saved->rig.name : name, file, {} };
    rigModified = false;
    // Saving isn't an edit to undo, but undo shouldn't bring back the old name either.
    history.replaceCurrent (takeSnapshot());
    rigBroadcaster.sendChangeMessage();
    return true;
}

void ParagonX1AudioProcessor::stepRig (int direction)
{
    if (const auto* next = rigLibrary->step (findCurrentRigEntry(), direction))
        loadRig (*next);
}

bool ParagonX1AudioProcessor::updateRigDetails (const RigLibrary::Entry& entry, const Rig& newDetails)
{
    if (entry.isFactory)
        return false;

    auto rig = entry.rig;
    rig.name = newDetails.name.trim().isEmpty() ? rig.name : newDetails.name.trim();
    rig.bank = newDetails.bank;
    rig.number = newDetails.number;
    rig.tags = newDetails.tags;

    const auto file = entry.file;
    const auto isCurrent = currentRig.file == file;
    if (! rigLibrary->overwrite (file, rig))
        return false;

    if (isCurrent)
    {
        currentRig.name = rig.name;
        history.replaceCurrent (takeSnapshot());
    }
    rigBroadcaster.sendChangeMessage();
    return true;
}

bool ParagonX1AudioProcessor::deleteRig (const RigLibrary::Entry& entry)
{
    if (entry.isFactory)
        return false;

    const auto file = entry.file;
    if (! rigLibrary->remove (file))
        return false;

    // The sound is still loaded; it just isn't saved anywhere any more.
    if (currentRig.file == file)
    {
        currentRig.file = juce::File();
        rigModified = true;
        history.replaceCurrent (takeSnapshot());
    }
    rigBroadcaster.sendChangeMessage();
    return true;
}

//==============================================================================
juce::AudioProcessorEditor* ParagonX1AudioProcessor::createEditor()
{
    return new ParagonX1AudioProcessorEditor (*this);
}

void ParagonX1AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (parameterVersionKey, parameterVersion, nullptr);
    state.setProperty (rigNameKey, currentRig.name, nullptr);
    state.setProperty (rigFileKey, currentRig.file.getFullPathName(), nullptr);
    state.setProperty (rigFactoryKey, currentRig.factoryName, nullptr);
    state.setProperty (rigModifiedKey, rigModified.load(), nullptr);

    juce::ValueTree kitSamples (drumSamplesKey);
    for (int k = 0; k < ParamIDs::numDrums; ++k)
        if (const auto& sample = drumSamples[(size_t) k])
            kitSamples.appendChild (juce::ValueTree (drumSampleKey, { { "slot", k + 1 }, { "name", sample->name }, { "path", sample->path },
                                                                    { "data", synth::drums::sampleFiles::encode (*sample) } }), nullptr);
    state.appendChild (kitSamples, nullptr);
    juce::ValueTree instruments ("DrumInstruments");
    for (int k = 0; k < 2 * ParamIDs::numDrums; ++k)
        if (instrumentRefs[(size_t) k].isNotEmpty())
            instruments.appendChild (juce::ValueTree ("DrumInstrument", { { "slot", k + 1 }, { "ref", instrumentRefs[(size_t) k] } }), nullptr);
    if (instruments.getNumChildren() > 0)
        state.appendChild (instruments, nullptr);

    if (! matrix.isEmpty())
        state.appendChild (juce::ValueTree::fromXml (*matrix.toXml()), nullptr);
    if (! midiLinks.isEmpty())
        state.appendChild (juce::ValueTree::fromXml (*midiLinks.toXml()), nullptr);
    state.appendChild (juce::ValueTree (noteMapKey, { { "keys", synth::drums::notemap::encode (customMap) } }), nullptr);
    // MIDI IN (0.18.0): the incoming curve and CC zones, and which MIDI IN preset they came from.
    if (auto in = midiIn.toXml(); in->getNumAttributes() > 0 || in->getNumChildElements() > 0 || midiInPresetName != "Default")
    {
        in->setAttribute ("preset", midiInPresetName);
        state.appendChild (juce::ValueTree::fromXml (*in), nullptr);
    }
    if (! songData->isEmpty())
        state.appendChild (juce::ValueTree::fromXml (*songData->toXml()), nullptr);
    // THE DRUM MACHINE's pads (0.39.0), when they aren't in the kit's order.
    {
        juce::StringArray pads;
        auto moved = false;
        for (int i = 0; i < ParamIDs::numDrums; ++i)
        {
            pads.add (juce::String (machinePads[(size_t) i]));
            moved = moved || machinePads[(size_t) i] != i;
        }
        if (moved)
            state.appendChild (juce::ValueTree ("MachinePads", { { "slots", pads.joinIntoString (" ") } }), nullptr);
    }

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ParagonX1AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    // The kit's samples and the patterns come out first: they aren't parameters.
    std::array<std::shared_ptr<const synth::drums::Sample>, ParamIDs::numDrums> kitSamples {};
    if (auto saved = state.getChildWithName (drumSamplesKey); saved.isValid())
    {
        for (const auto& sample : saved)
        {
            const auto slot = (int) sample["slot"] - 1;
            if (slot >= 0 && slot < ParamIDs::numDrums)
            {
                kitSamples[(size_t) slot] = synth::drums::sampleFiles::decode (sample["name"].toString(), sample["data"].toString());
                if (kitSamples[(size_t) slot] != nullptr)
                    std::const_pointer_cast<synth::drums::Sample> (kitSamples[(size_t) slot])->path = sample["path"].toString();
            }
        }
        state.removeChild (saved, nullptr);
    }
    // THE SAMPLE ENGINE (0.19.0): the project's instruments, loaded again from their files.
    std::vector<std::pair<int, juce::String>> instrumentsToLoad;
    if (auto saved = state.getChildWithName ("DrumInstruments"); saved.isValid())
    {
        const auto savedBy = (int) state.getProperty (parameterVersionKey, 1);
        for (const auto& e : saved)
            if (const auto slot = ParamIDs::instrumentIndexFrom ((int) e["slot"] - 1, savedBy); slot >= 0 && slot < 2 * ParamIDs::numDrums)
                instrumentsToLoad.emplace_back (slot, e["ref"].toString());
        state.removeChild (saved, nullptr);
    }
    ModMatrix newMatrix;
    if (auto saved = state.getChildWithName (modulationKey); saved.isValid())
    {
        if (const auto xml = saved.createXml())
            newMatrix = ModMatrix::fromXml (*xml);
        state.removeChild (saved, nullptr);
    }
    if ((int) state.getProperty (parameterVersionKey, 1) < 5)
        std::fill (newMatrix.busNames.begin(), newMatrix.busNames.end(), juce::String());   // before the drum buses: unnamed
    // MIDI LEARN's links: the project's, if it has any (else the ones this instance has: its default).
    if (auto saved = state.getChildWithName (midiLinksKey); saved.isValid())
    {
        if (const auto links = saved.createXml())
            setMidiLinks (MidiLinks::fromXml (*links));
        state.removeChild (saved, nullptr);
    }
    // THE NOTE MAP's CUSTOM map: the project's, if it has one (else the one this instance has: its default).
    if (auto saved = state.getChildWithName (noteMapKey); saved.isValid())
    {
        setCustomMap (synth::drums::notemap::decode (saved["keys"].toString()));
        state.removeChild (saved, nullptr);
    }
    // MIDI IN (0.18.0): the project's (none saved: straight, no zones).
    if (auto saved = state.getChildWithName ("MidiIn"); saved.isValid())
    {
        if (const auto inXml = saved.createXml())
        {
            setMidiIn (MidiIn::fromXml (*inXml));
            midiInPresetName = inXml->getStringAttribute ("preset", "Default");
        }
        state.removeChild (saved, nullptr);
    }
    else
    {
        setMidiIn ({});
        midiInPresetName = "Default";
    }
    // THE SONG TRACK: the project's (none saved: an empty song).
    auto newSong = std::make_shared<song::Song>();
    // THE DRUM MACHINE's pads (0.39.0): the project's order, if it's a whole one (else the kit's).
    for (int i = 0; i < ParamIDs::numDrums; ++i)
        machinePads[(size_t) i] = i;
    if (auto saved = state.getChildWithName ("MachinePads"); saved.isValid())
    {
        const auto tokens = juce::StringArray::fromTokens (saved["slots"].toString(), " ", {});
        std::array<int, ParamIDs::numDrums> pads {};
        std::array<bool, ParamIDs::numDrums> seen {};
        auto whole = tokens.size() == ParamIDs::numDrums;
        for (int i = 0; whole && i < ParamIDs::numDrums; ++i)
        {
            const auto slot = tokens[i].getIntValue();
            whole = slot >= 0 && slot < ParamIDs::numDrums && ! seen[(size_t) slot];
            if (whole)
            {
                pads[(size_t) i] = slot;
                seen[(size_t) slot] = true;
            }
        }
        if (whole)
            machinePads = pads;
        state.removeChild (saved, nullptr);
    }
    if (auto saved = state.getChildWithName ("Song"); saved.isValid())
    {
        if (const auto songXml = saved.createXml())
            *newSong = song::Song::fromXml (*songXml);
        state.removeChild (saved, nullptr);
    }
    installSong (std::move (newSong));
    // Before 0.17.0: the articulations it added start off, so the project sounds as it did (0.33.0's drums likewise).
    juce::StringArray startOff;
    if ((int) state.getProperty (parameterVersionKey, 1) < 2)
        startOff.addArray (ParamIDs::articulationsAddedOff);
    if ((int) state.getProperty (parameterVersionKey, 1) < 3)
        startOff.addArray (ParamIDs::slotsAdded033Off);
    if ((int) state.getProperty (parameterVersionKey, 1) < 4)
        startOff.addArray (ParamIDs::slotsAdded036Off);   // 0.36.0's seven
    // Before 0.39.0's drum buses: a route the project doesn't give is its channel's OUTPUT, as it was then.
    if ((int) state.getProperty (parameterVersionKey, 1) < 5)
        for (const auto& id : ParamIDs::routesOnBuses039)
            if (! state.getChildWithProperty ("id", id).isValid())
            {
                juce::ValueTree param ("PARAM");
                param.setProperty ("id", id, nullptr);
                param.setProperty ("value", 0.0f, nullptr);
                state.appendChild (param, nullptr);
            }
    for (const auto& id : startOff)
        {
            auto param = state.getChildWithProperty ("id", id);
            if (! param.isValid())
            {
                param = juce::ValueTree ("PARAM");
                param.setProperty ("id", id, nullptr);
                state.appendChild (param, nullptr);
            }
            param.setProperty ("value", 0.0f, nullptr);
        }
    parameters.replaceState (state);
    matrix = newMatrix;
    compileMatrix();
    for (int k = 0; k < ParamIDs::numDrums; ++k)
    {
        setDrumSample (k, kitSamples[(size_t) k]);
        for (const auto index : { k, layerOf (k) })
        {
            setDrumInstrument (index, nullptr);
            instrumentRefs[(size_t) index] = {};
        }
    }
    startLoading (instrumentsToLoad);

    // Restoring the project changed parameters, which marked the preset as
    // modified; put back what was saved instead.
    const auto rigFile = state[rigFileKey].toString();
    currentRig = { state.getProperty (rigNameKey, "Init").toString(),
                   rigFile.isNotEmpty() ? juce::File (rigFile) : juce::File(),
                   state[rigFactoryKey].toString() };
    rigModified = (bool) state.getProperty (rigModifiedKey, false);

    // A different project or preset: start the undo history afresh from it.
    history.clear();
    requestCheckpoint();
    rigBroadcaster.sendChangeMessage();
}

// JUCE calls this to create new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ParagonX1AudioProcessor();
}

//==============================================================================
// THE GROOVE PLAYER (0.4.0)
synth::drums::notemap::Map ParagonX1AudioProcessor::grooveLayout (int folder) const
{
    return grooveFolders->layoutFor (folder, customMap, juce::roundToInt (param (ParamIDs::drumsRoot)));
}

void ParagonX1AudioProcessor::playGroove (const grooves::Groove& groove, int folder, bool loop, unsigned parts)
{
    auto preview = std::make_shared<GroovePreview>();
    const auto map = grooveLayout (folder);
    for (const auto& h : groove.hits)
        if (const auto slot = synth::drums::notemap::slotOf (map, h.note); slot >= 0 && grooves::inParts (parts, slot))
            preview->hits.push_back ({ h.beat, slot, h.velocity });
    preview->lengthBeats = juce::jmax (0.25, groove.lengthBeats);
    preview->endBeat = juce::jmax (preview->lengthBeats, groove.hits.empty() ? 0.0 : groove.hits.back().beat + 1.0e-6);
    preview->bpm = groove.bpm;
    preview->loop = loop;
    preview->file = groove.file;
    if (groovePreview != nullptr)
        retiredGrooves.emplace_back (groovePreview, juce::Time::getMillisecondCounterHiRes());
    groovePreview = preview;
    grooveForAudio = groovePreview.get();
}

void ParagonX1AudioProcessor::stopGroove()
{
    grooveForAudio = nullptr;
    if (groovePreview != nullptr)
        retiredGrooves.emplace_back (groovePreview, juce::Time::getMillisecondCounterHiRes());
    groovePreview = nullptr;
    groovePosition = -1.0;
}

void ParagonX1AudioProcessor::applyGroovePreview()
{
    const auto p = grooveFolders->getPreview();
    grooveSongTempo = p.songTempo;
    grooveFactor = grooves::factorOf (p.factor);
    grooveVelocity = p.velocity;
}

double ParagonX1AudioProcessor::grooveTempo (double grooveBpm) const noexcept
{
    // RIFF MATCH: while a riff is heard with them, the previews play at its tempo.
    if (const auto* riff = riffForAudio.load(); riff != nullptr && riffListening.load())
        return riff->bpm * grooveFactor.load();
    return (grooveSongTempo.load() ? juce::jmax (1.0, hostBpm.load()) : grooveBpm) * grooveFactor.load();
}

void ParagonX1AudioProcessor::setRiff (std::shared_ptr<const juce::AudioBuffer<float>> audio, double rate, double startSeconds,
                                       double loopSeconds, double bpm)
{
    auto riff = std::make_shared<RiffPlayback>();
    riff->audio = std::move (audio);
    riff->rate = juce::jmax (1.0, rate);
    riff->start = juce::jmax (0.0, startSeconds);
    riff->loop = juce::jmax (0.05, loopSeconds);
    riff->bpm = juce::jlimit (20.0, 400.0, bpm);
    if (riffPlayback != nullptr)
        retiredRiffs.emplace_back (riffPlayback, juce::Time::getMillisecondCounterHiRes());
    riffPlayback = riff;
    riffForAudio = riffPlayback.get();
}

void ParagonX1AudioProcessor::clearRiff()
{
    riffForAudio = nullptr;
    if (riffPlayback != nullptr)
        retiredRiffs.emplace_back (riffPlayback, juce::Time::getMillisecondCounterHiRes());
    riffPlayback = nullptr;
}

void ParagonX1AudioProcessor::runRiff (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    // The riff's part, looping, from the preview's start: the preview's seconds since it began (its beats over its
    // tempo) into the part; read between samples (linear) at the riff's own rate.
    const auto* riff = riffForAudio.load();
    if (riff == nullptr || riff->audio == nullptr || ! riffListening.load() || riffFrom < 0.0 || riffPerSample <= 0.0)
        return;
    const auto& audio = *riff->audio;
    const auto frames = audio.getNumSamples();
    if (frames < 2)
        return;
    const auto* x = audio.getReadPointer (0);
    const auto rate = getSampleRate();
    const auto secondsAtStart = riffFrom / (riffPerSample * rate);
    const auto gain = riffLevel.load();
    const auto channels = juce::jmin (2, buffer.getNumChannels(), getMainBusNumOutputChannels());
    for (int i = 0; i < numSamples; ++i)
    {
        const auto t = secondsAtStart + i / rate;
        const auto at = (riff->start + std::fmod (t, riff->loop)) * riff->rate;
        const auto k = (int) at;
        if (k < 0 || k + 1 >= frames)
            continue;
        const auto f = (float) (at - k);
        const auto v = gain * (x[k] + f * (x[k + 1] - x[k]));
        for (int c = 0; c < channels; ++c)
            buffer.addSample (c, i, v);
    }
}

int ParagonX1AudioProcessor::collectGrooveHits (int numSamples) noexcept
{
    // The preview's hits in the next numSamples, at the preview's tempo: from its start when it's new, round and round
    // its whole bars when looping, once through (to its last hit) when not.
    const auto* g = grooveForAudio.load();
    if (g != grooveNow)
    {
        grooveNow = g;
        grooveBaseBeat = 0.0;
        grooveSamples = 0;
        groovePerSample = 0.0;
        grooveDone = false;
    }
    if (g == nullptr || grooveDone)
    {
        groovePosition = -1.0;
        return 0;
    }
    const auto perSample = grooveTempo (g->bpm) / 60.0 / getSampleRate();
    if (perSample != groovePerSample)
    {
        grooveBaseBeat += (double) grooveSamples * groovePerSample;   // the tempo changed: carry on from here
        grooveSamples = 0;
        groovePerSample = perSample;
    }
    const auto from = grooveBaseBeat + (double) grooveSamples * perSample;
    const auto to = grooveBaseBeat + (double) (grooveSamples + numSamples) * perSample;
    if (riffFirstChunk)
    {
        // RIFF MATCH: where the preview is at the block's start (runRiff follows it).
        riffFirstChunk = false;
        riffFrom = from;
        riffPerSample = perSample;
    }
    auto count = 0;
    const auto addCycle = [&] (double cycleStart, double limit)
    {
        // The hits at cycleStart + beat in [from, to), beat below `limit`.
        auto it = std::lower_bound (g->hits.begin(), g->hits.end(), from - cycleStart, [] (const auto& h, double b) { return h.beat < b; });
        for (; it != g->hits.end() && it->beat < limit && cycleStart + it->beat < to && count < (int) grooveHits.size(); ++it)
        {
            // To the sample it falls in (a hollow millionth of a sample under: a hit right on a sample, worked out in
            // floating point, isn't put one early).
            const auto offset = juce::jlimit (0, numSamples - 1, (int) std::floor ((cycleStart + it->beat - from) / perSample + 1.0e-6));
            grooveHits[(size_t) count++] = { offset, it->slot, juce::jlimit (0.0f, 1.0f, it->velocity * grooveVelocity.load()) };
        }
    };
    if (g->loop)
    {
        for (auto cycle = std::floor (from / g->lengthBeats) * g->lengthBeats; cycle < to; cycle += g->lengthBeats)
            addCycle (cycle, g->lengthBeats);
        groovePosition = std::fmod (to, g->lengthBeats);
    }
    else
    {
        addCycle (0.0, std::numeric_limits<double>::max());
        grooveDone = to >= g->endBeat;
        groovePosition = grooveDone ? -1.0 : to;
    }
    grooveSamples += numSamples;
    return count;
}

std::function<int (int)> ParagonX1AudioProcessor::grooveNoteFor (int folder, unsigned parts) const
{
    // GROOVE PARTS: notes on drums outside `parts` left out, whichever way the rest goes.
    if (parts != grooves::allParts)
    {
        const auto map = grooveLayout (folder);
        return [map, parts, rest = grooveNoteFor (folder)] (int note)
        {
            const auto slot = synth::drums::notemap::slotOf (map, note);
            return grooves::inParts (parts, slot) ? rest (note) : -1;
        };
    }
    // As written: FOR THIS KIT off, or the folder's map is the one the kit plays (its notes already right, every
    // articulation kept). Else each note's drum on the kit's note for it (dropped if the kit has none).
    const auto layout = folder >= 0 && folder < (int) grooveFolders->getFolders().size() ? grooveFolders->getFolders()[(size_t) folder].layout
                                                                                          : synth::drums::notemap::Layout::grooveLibrary;
    if (! grooveFolders->getPreview().forThisKit || layout == getMapLayout())
        return [] (int note) { return note; };
    const auto map = grooveLayout (folder);
    std::array<int, synth::drums::numSlots> home {};
    for (int k = 0; k < synth::drums::numSlots; ++k)
        home[(size_t) k] = homeNote (k);
    return [map, home] (int note)
    {
        const auto slot = synth::drums::notemap::slotOf (map, note);
        return slot >= 0 ? home[(size_t) slot] : -1;
    };
}

ParagonX1AudioProcessor::GrooveToPattern ParagonX1AudioProcessor::grooveToPattern (const grooves::Groove& groove, int folder, int pattern)
{
    return grooveToPattern (groove, grooveLayout (folder), pattern);
}

ParagonX1AudioProcessor::GrooveToPattern ParagonX1AudioProcessor::grooveFileToPattern (const juce::File& file, int pattern)
{
    const auto map = getNoteMap();
    if (const auto g = grooves::read (file, map))
        return grooveToPattern (*g, map, pattern);
    return {};
}

ParagonX1AudioProcessor::GrooveToPattern ParagonX1AudioProcessor::grooveToPattern (const grooves::Groove& groove,
                                                                                   const synth::drums::notemap::Map& map, int pattern)
{
    namespace ds = synth::drumseq;
    GrooveToPattern result;
    const auto p = grooveFolders->getPreview();
    const auto scale = 1.0 / grooves::factorOf (p.factor);   // as dragged out: 2x puts the notes twice as close
    const auto length = juce::jlimit (1, ds::maxBeats, (int) std::ceil (groove.lengthBeats * scale - 1.0e-6));
    const auto beatsPerStep = getDrumSeqSettings().beatsPerStep();
    ds::Pattern made;
    for (const auto& h : groove.hits)
    {
        const auto slot = synth::drums::notemap::slotOf (map, h.note);
        const auto beat = h.beat * scale;
        if (slot < 0)
            ++result.noDrum;
        else if (beat >= length - 1.0e-9 || (int) made.notes.size() >= ds::maxNotes)
            ++result.pastEnd;
        else
        {
            ds::Note n;
            n.row = slot;
            n.start = beat / beatsPerStep;
            n.velocity = juce::jlimit (0.0f, 1.0f, h.velocity * p.velocity);
            made.notes.push_back (n);
            ++result.placed;
        }
    }
    auto m = matrix;
    m.drumPatterns[(size_t) juce::jlimit (0, ds::numPatterns - 1, pattern)] = std::move (made);
    setModMatrix (m);
    modMatrixEditFinished();   // an undo step of its own (the settings below may not change and make one)
    const auto set = [this] (const char* id, float value)
    {
        if (auto* parameter = parameters.getParameter (id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            parameter->endChangeGesture();
        }
    };
    set (ParamIDs::dseqLength, (float) length);
    set (ParamIDs::dseqPattern, (float) pattern);
    set (ParamIDs::dseqOn, 1.0f);
    result.lengthBeats = length;
    return result;
}

ParagonX1AudioProcessor::GrooveToPattern ParagonX1AudioProcessor::layerParts (const grooves::Groove& groove, int folder, unsigned parts,
                                                                              int pattern)
{
    namespace ds = synth::drumseq;
    GrooveToPattern result;
    const auto p = grooveFolders->getPreview();
    const auto scale = 1.0 / grooves::factorOf (p.factor);
    const auto settings = getDrumSeqSettings();
    const auto length = (double) settings.lengthBeats;
    const auto partLength = juce::jmax (0.25, groove.lengthBeats * scale);
    const auto map = grooveLayout (folder);
    pattern = juce::jlimit (0, ds::numPatterns - 1, pattern);

    // The pattern's notes on other drums kept; then the groove's parts, round and round to LENGTH.
    auto m = matrix;
    auto& notes = m.drumPatterns[(size_t) pattern].notes;
    std::erase_if (notes, [parts] (const ds::Note& n) { return grooves::inParts (parts, n.row); });
    for (auto repeat = 0.0; repeat < length - 1.0e-9; repeat += partLength)
        for (const auto& h : groove.hits)
        {
            const auto slot = synth::drums::notemap::slotOf (map, h.note);
            if (! grooves::inParts (parts, slot))
            {
                result.noDrum += repeat == 0.0 && slot < 0 ? 1 : 0;
                continue;
            }
            const auto beat = repeat + h.beat * scale;
            if (h.beat * scale >= partLength - 1.0e-9 || beat >= length - 1.0e-9 || (int) notes.size() >= ds::maxNotes)
            {
                result.pastEnd += repeat == 0.0 ? 1 : 0;
                continue;
            }
            ds::Note n;
            n.row = slot;
            n.start = beat / settings.beatsPerStep();
            n.velocity = juce::jlimit (0.0f, 1.0f, h.velocity * p.velocity);
            notes.push_back (n);
            ++result.placed;
        }
    setModMatrix (m);
    modMatrixEditFinished();   // an undo step of its own
    result.lengthBeats = settings.lengthBeats;
    return result;
}

juce::File ParagonX1AudioProcessor::grooveDragFile (const grooves::Groove& groove, int folder, unsigned parts) const
{
    const auto p = grooveFolders->getPreview();
    const auto midi = grooves::toMidi (groove, grooveTempo (groove.bpm), p.velocity, grooveNoteFor (folder, parts), p.factor);
    const auto folderOut = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PR7-D Grooves");
    folderOut.createDirectory();
    // GROOVE PARTS: a part on its own named for it ("... - Verse 01 HI-HAT").
    juce::String partsName;
    if (parts != grooves::allParts)
        for (int k = 0; k < grooves::numDrumParts; ++k)
            if ((parts & (1u << k)) != 0)
                partsName << " " << grooves::drumPartNames[k];
    const auto name = juce::File::createLegalFileName (groove.file.getParentDirectory().getFileName() + " - " + groove.name + partsName);
    const auto file = folderOut.getChildFile (name + ".mid");
    file.deleteFile();
    if (juce::FileOutputStream out (file); out.openedOk())
        midi.writeTo (out);
    return file;
}

//==============================================================================
// THE NOTE MAP (0.2.0)
synth::drums::notemap::Layout ParagonX1AudioProcessor::getMapLayout() const noexcept
{
    return (synth::drums::notemap::Layout) juce::jlimit (0, synth::drums::notemap::layoutNames.size() - 1, juce::roundToInt (param (ParamIDs::drumsMap)));
}

synth::drums::notemap::Map ParagonX1AudioProcessor::getNoteMap() const
{
    using namespace synth::drums::notemap;
    switch (getMapLayout())
    {
        case Layout::root: return rootLayout (juce::roundToInt (param (ParamIDs::drumsRoot)));
        case Layout::grooveLibrary: return grooveLibrary();
        case Layout::generalMidi: return generalMidi();
        case Layout::custom: return customMap;
    }
    return customMap;
}

void ParagonX1AudioProcessor::setCustomMap (const synth::drums::notemap::Map& map)
{
    customMap = map;
    for (size_t n = 0; n < map.size(); ++n)
        customForAudio[n] = map[n];
}

void ParagonX1AudioProcessor::mapNote (int note, int slot)
{
    if (note < 0 || note > 127)
        return;
    auto map = getNoteMap();   // under another layout: that layout, copied to CUSTOM
    map[(size_t) note] = (std::int8_t) ((slot >= 0 && slot < synth::drums::numSlots) || slot == synth::drums::notemap::PEDAL ? slot : -1);
    setCustomMap (map);
    if (auto* layout = parameters.getParameter (ParamIDs::drumsMap); layout != nullptr && getMapLayout() != synth::drums::notemap::Layout::custom)
        layout->setValueNotifyingHost (layout->convertTo0to1 ((float) synth::drums::notemap::Layout::custom));
}

int ParagonX1AudioProcessor::homeNote (int slot) const
{
    return synth::drums::notemap::homeNote (getNoteMap(), getMapLayout(), slot, juce::roundToInt (param (ParamIDs::drumsRoot)));
}

juce::File ParagonX1AudioProcessor::noteMapDefaultFile() const
{
    return rigLibrary->getFolder().getChildFile ("Note Map.xml");
}

bool ParagonX1AudioProcessor::saveNoteMapAsDefault() const
{
    juce::XmlElement xml (noteMapKey);
    xml.setAttribute ("keys", synth::drums::notemap::encode (customMap));
    noteMapDefaultFile().getParentDirectory().createDirectory();
    return xml.writeTo (noteMapDefaultFile());
}

//==============================================================================
// MIDI IN / E-DRUMS (0.18.0)
bool ParagonX1AudioProcessor::MidiIn::operator== (const MidiIn& o) const noexcept
{
    if (! (curve == o.curve))
        return false;
    for (size_t i = 0; i < zones.size(); ++i)
    {
        const auto& a = zones[i];
        const auto& b = o.zones[i];
        if (a.note != b.note || a.cc != b.cc || a.count != b.count || a.slots != b.slots || a.bounds != b.bounds)
            return false;
    }
    return true;
}

std::unique_ptr<juce::XmlElement> ParagonX1AudioProcessor::MidiIn::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("MidiIn");
    if (! curve.isStraight())
        xml->setAttribute ("curve", curve.encode());
    for (const auto& z : zones)
        if (z.note >= 0 && z.count >= 2)
        {
            auto* e = xml->createNewChildElement ("CcZones");
            e->setAttribute ("note", z.note);
            e->setAttribute ("cc", z.cc);
            e->setAttribute ("count", z.count);
            juce::StringArray slots, bounds;
            for (int i = 0; i < z.count; ++i)
                slots.add (juce::String (z.slots[(size_t) i]));
            for (int i = 0; i < z.count - 1; ++i)
                bounds.add (juce::String (z.bounds[(size_t) i]));
            e->setAttribute ("slots", slots.joinIntoString (" "));
            e->setAttribute ("bounds", bounds.joinIntoString (" "));
        }
    return xml;
}

ParagonX1AudioProcessor::MidiIn ParagonX1AudioProcessor::MidiIn::fromXml (const juce::XmlElement& xml)
{
    MidiIn m;
    if (xml.hasAttribute ("curve"))
        m.curve = synth::drums::VelocityCurve::decode (xml.getStringAttribute ("curve"));
    size_t next = 0;
    for (const auto* e : xml.getChildWithTagNameIterator ("CcZones"))
    {
        if (next >= m.zones.size())
            break;
        auto& z = m.zones[next];
        z.note = (std::int8_t) juce::jlimit (-1, 127, e->getIntAttribute ("note", -1));
        z.cc = (std::int8_t) juce::jlimit (0, 127, e->getIntAttribute ("cc", 16));
        z.count = (std::int8_t) juce::jlimit (0, 4, e->getIntAttribute ("count"));
        const auto slots = juce::StringArray::fromTokens (e->getStringAttribute ("slots"), " ", {});
        const auto bounds = juce::StringArray::fromTokens (e->getStringAttribute ("bounds"), " ", {});
        for (int i = 0; i < 4; ++i)
            z.slots[(size_t) i] = (std::int8_t) (i < slots.size() ? juce::jlimit (-1, synth::drums::numSlots - 1, slots[i].getIntValue()) : -1);
        for (int i = 0; i < 3 && i < bounds.size(); ++i)
            z.bounds[(size_t) i] = (std::uint8_t) juce::jlimit (0, 127, bounds[i].getIntValue());
        if (z.note >= 0 && z.count >= 2)
            ++next;
        else
            z = {};
    }
    return m;
}

synth::drums::KitSettings::CcZones* ParagonX1AudioProcessor::MidiIn::zonesFor (int note)
{
    for (auto& z : zones)
        if (z.note == note)
            return &z;
    for (auto& z : zones)
        if (z.note < 0 || z.count < 2)
        {
            z = {};
            z.note = (std::int8_t) note;
            return &z;
        }
    return nullptr;
}

void ParagonX1AudioProcessor::setMidiIn (const MidiIn& in)
{
    midiIn = in;
    auto shared = std::make_shared<const MidiIn> (in);
    if (midiInShared != nullptr)
        retiredMidiIns.emplace_back (midiInShared, juce::Time::getMillisecondCounterHiRes());
    midiInShared = shared;
    midiInForAudio = midiInShared.get();
}

juce::File ParagonX1AudioProcessor::midiInPresetsFolder() const { return rigLibrary->getFolder().getChildFile ("MIDI In Presets"); }

juce::Array<juce::File> ParagonX1AudioProcessor::listMidiInPresets() const { return filesIn (midiInPresetsFolder(), "*.pr7midi"); }

std::unique_ptr<juce::XmlElement> ParagonX1AudioProcessor::captureMidiInPreset() const
{
    auto xml = midiIn.toXml();
    xml->setTagName ("MidiInPreset");
    xml->setAttribute ("version", 1);
    xml->setAttribute ("layout", synth::drums::notemap::layoutNames[(int) getMapLayout()]);
    xml->setAttribute ("keys", synth::drums::notemap::encode (getNoteMap()));
    xml->setAttribute ("channel", juce::roundToInt (param (ParamIDs::drumsMidiChannel)));
    juce::StringArray steps;
    for (const auto& id : ParamIDs::drumsPedalSteps)
        steps.add (juce::String (juce::roundToInt (param (id.toRawUTF8()))));
    xml->setAttribute ("pedalSteps", steps.joinIntoString (" "));
    return xml;
}

bool ParagonX1AudioProcessor::applyMidiInPreset (const juce::XmlElement& xml)
{
    if (! xml.hasTagName ("MidiInPreset"))
        return false;
    const auto set = [this] (const juce::String& id, float value)
    {
        if (auto* p = parameters.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (value)));
    };
    // The map: a fixed layout by name; otherwise (CUSTOM) the notes as saved.
    const auto layout = synth::drums::notemap::layoutNames.indexOf (xml.getStringAttribute ("layout"));
    if (layout < 0 || layout == (int) synth::drums::notemap::Layout::custom)
    {
        setCustomMap (synth::drums::notemap::decode (xml.getStringAttribute ("keys")));
        set (ParamIDs::drumsMap, (float) synth::drums::notemap::Layout::custom);
    }
    else
        set (ParamIDs::drumsMap, (float) layout);
    set (ParamIDs::drumsMidiChannel, (float) xml.getIntAttribute ("channel"));
    const auto steps = juce::StringArray::fromTokens (xml.getStringAttribute ("pedalSteps"), " ", {});
    if (steps.size() == 4)
        for (int i = 0; i < 4; ++i)
            set (ParamIDs::drumsPedalSteps[i], (float) steps[i].getIntValue());
    auto plain = xml;
    plain.setTagName ("MidiIn");
    setMidiIn (MidiIn::fromXml (plain));
    return true;
}

bool ParagonX1AudioProcessor::saveMidiInPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return false;
    midiInPresetsFolder().createDirectory();
    if (! captureMidiInPreset()->writeTo (midiInPresetsFolder().getChildFile (clean + ".pr7midi")))
        return false;
    midiInPresetName = clean;
    return true;
}

bool ParagonX1AudioProcessor::loadMidiInPreset (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! applyMidiInPreset (*xml))
        return false;
    midiInPresetName = file.getFileNameWithoutExtension();
    return true;
}

void ParagonX1AudioProcessor::applyBuiltInMidiInPreset (int which)
{
    juce::XmlElement xml ("MidiInPreset");
    xml.setAttribute ("layout", synth::drums::notemap::layoutNames[(int) (which == 1 ? synth::drums::notemap::Layout::generalMidi
                                                                                         : synth::drums::notemap::Layout::grooveLibrary)]);
    xml.setAttribute ("channel", 0);
    const synth::drums::KitSettings defaults;
    xml.setAttribute ("pedalSteps", juce::String (defaults.pedalSteps[0]) + " " + juce::String (defaults.pedalSteps[1]) + " "
                                        + juce::String (defaults.pedalSteps[2]) + " " + juce::String (defaults.pedalSteps[3]));
    applyMidiInPreset (xml);
    midiInPresetName = which == 1 ? "General MIDI" : "Default";
}

//==============================================================================
// MIDI LEARN (stage 4f)
void ParagonX1AudioProcessor::setMidiLinks (const MidiLinks& links)
{
    midiLinks = links;
    std::array<juce::RangedAudioParameter*, 128> targets {};
    for (const auto& l : midiLinks.links)
        if (auto* p = parameters.getParameter (l.parameterId))
            targets[(size_t) l.controller] = p;
    for (size_t cc = 0; cc < targets.size(); ++cc)
        controllerTargets[cc] = targets[cc];
}

void ParagonX1AudioProcessor::finishMidiLearn()
{
    // A controller moved since a knob asked to learn: link them.
    if (learning.isNotEmpty() && controllerCount.load() != learnFromCount && lastController.load() >= 0)
    {
        auto links = midiLinks;
        links.link (lastController.load(), learning);
        learning = {};
        setMidiLinks (links);
    }
}

void ParagonX1AudioProcessor::startMidiLearn (const juce::String& parameterId)
{
    learning = parameterId;
    learnFromCount = controllerCount.load();
}

juce::File ParagonX1AudioProcessor::midiLinksDefaultFile() const
{
    return rigLibrary->getFolder().getChildFile ("MIDI Links.xml");
}

bool ParagonX1AudioProcessor::saveMidiLinksAsDefault() const
{
    return exportMidiLinks (midiLinksDefaultFile());
}

bool ParagonX1AudioProcessor::exportMidiLinks (const juce::File& file) const
{
    file.getParentDirectory().createDirectory();
    return midiLinks.toXml()->writeTo (file);
}

bool ParagonX1AudioProcessor::importMidiLinks (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("MidiLinks"))
        return false;
    setMidiLinks (MidiLinks::fromXml (*xml));
    return true;
}

void ParagonX1AudioProcessor::controllerForTest (int controller, float value)
{
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, controller, juce::roundToInt (value * 127.0f)), 0);
    juce::AudioBuffer<float> buffer (2, 64);
    buffer.clear();
    processBlock (buffer, midi);
}

//==============================================================================
// THE SONG TRACK (0.8.0)
void ParagonX1AudioProcessor::installSong (std::shared_ptr<const song::Song> newSong)
{
    // Its hits worked out here (each block's groove repeated to fill it), for the audio thread to step through.
    auto playback = std::make_shared<SongPlayback>();
    playback->events = newSong->events();
    // 0.48.0: the ends of notes with a length, as lettings-go (negative velocity) on their beats.
    {
        std::vector<song::Hit> releases;
        for (const auto& h : playback->events)
            if (h.length > 0.0)
                releases.push_back ({ h.beat + h.length, h.slot, -1.0f });
        if (! releases.empty())
        {
            playback->events.insert (playback->events.end(), releases.begin(), releases.end());
            std::stable_sort (playback->events.begin(), playback->events.end(), [] (const song::Hit& a, const song::Hit& b) { return a.beat < b.beat; });
        }
    }
    if (songSolo != 0)
        std::erase_if (playback->events, [this] (const song::Hit& h) { return (songSolo & song::slotBit (h.slot)) == 0; });
    if (songHeard.has_value())
        std::erase_if (playback->events, [this] (const song::Hit& h) { return (*songHeard & song::slotBit (h.slot)) == 0; });
    playback->endBeat = newSong->endBeat();
    playback->timing.tempos = newSong->tempos;
    playback->followDaw = newSong->followDaw;
    playback->loopOn = newSong->hasLoop();
    playback->loopStart = newSong->loopStart;
    playback->loopEnd = newSong->loopEnd;
    playback->click = newSong->click;
    playback->countIn = newSong->countIn;
    playback->barLines = newSong->barLines (juce::jmax (newSong->endBeat(), newSong->loopEnd) + 64.0);
    songData = std::move (newSong);
    if (songPlayback != nullptr)
        retiredSongs.emplace_back (songPlayback, juce::Time::getMillisecondCounterHiRes());
    songPlayback = playback;
    songForAudio = songPlayback.get();
    songBroadcaster.sendChangeMessage();
}

void ParagonX1AudioProcessor::setSong (song::Song newSong)
{
    installSong (std::make_shared<const song::Song> (std::move (newSong)));
    requestCheckpoint();
}

void ParagonX1AudioProcessor::previewSong (song::Song newSong)
{
    installSong (std::make_shared<const song::Song> (std::move (newSong)));
}

void ParagonX1AudioProcessor::setSongHeard (std::optional<song::SlotMask> heard)
{
    if (heard == songHeard)
        return;
    songHeard = heard;
    installSong (songData);
}

void ParagonX1AudioProcessor::setSongSolo (song::SlotMask slots)
{
    if (slots == songSolo)
        return;
    songSolo = slots;
    installSong (songData);
}

song::Block ParagonX1AudioProcessor::songBlockFrom (const grooves::Groove& groove, int folder) const
{
    const auto p = grooveFolders->getPreview();
    return song::blockFrom (groove, grooveLayout (folder), p.factor, p.velocity);
}

int ParagonX1AudioProcessor::addToSong (const grooves::Groove& groove, int folder, double beat)
{
    return addBlockToSong (songBlockFrom (groove, folder), beat);
}

int ParagonX1AudioProcessor::addBlockToSong (song::Block block, double beat)
{
    auto edited = *songData;
    if (edited.isEmpty())
    {
        // The first block: the song's bars are the DAW's.
        edited.numerator = hostNumerator.load();
        edited.denominator = hostDenominator.load();
    }
    const auto index = edited.place (std::move (block), beat < 0.0 ? edited.endBeat() : beat);
    setSong (std::move (edited));
    return index;
}

void ParagonX1AudioProcessor::playSong (double fromBeat)
{
    songLocalFrom = juce::jmax (0.0, fromBeat);
    ++songLocalStarts;
    songLocal = true;
}

void ParagonX1AudioProcessor::stopSong()
{
    songLocal = false;
}

bool ParagonX1AudioProcessor::SongPlayback::isBarLine (double beat) const noexcept
{
    const auto it = std::lower_bound (barLines.begin(), barLines.end(), beat - 1.0e-6);
    if (it != barLines.end())
        return std::abs (*it - beat) < 1.0e-6;
    // Past the lines worked out: the last bars' length on from the last line.
    if (barLines.size() < 2)
        return std::abs (std::fmod (beat, 4.0)) < 1.0e-6;
    const auto bar = barLines.back() - barLines[barLines.size() - 2];
    const auto into = std::fmod (beat - barLines.back(), juce::jmax (1.0e-3, bar));
    return into < 1.0e-6 || bar - into < 1.0e-6;
}

int ParagonX1AudioProcessor::collectSongHits (int numSamples, double ppq, double bpm, synth::drums::SlotHit* into, int room) noexcept
{
    // Where the song is in this piece: the DAW's position while it plays; else PLAY's clock (whole samples since PLAY,
    // as the preview's, so it doesn't drift); else nowhere.
    const auto* s = songForAudio.load();
    const auto sampleRate = getSampleRate();
    const auto perSample = juce::jmax (1.0, bpm) / 60.0 / sampleRate;
    // With the song's TEMPO CHANGES (0.11.0, PLAY only: the DAW leads when it plays): seconds since PLAY's start, through
    // the tempo map to beats (exact for glides too, and as drift-free as the whole-sample clock).
    const auto timed = s != nullptr && ! s->timing.tempos.empty();
    auto count = 0;
    // The song's hits from `from` to `to` (a stretch of `n` samples starting `offset` samples into the piece).
    const auto collect = [&] (double from, double to, double samplesFrom, bool local, int offset, int n)
    {
        if (s == nullptr || n <= 0)
            return;
        // Each hit's sample worked out first, then played in the piece that sample is in: a hit right on the line between
        // two pieces (where the DAW's position, rounded, can land a hair either side) is played once, on its own sample,
        // never squeezed onto the last sample of the piece before. So the search starts half a sample early.
        const auto margin = 0.5 * (to - from) / juce::jmax (1, n);
        auto it = std::lower_bound (s->events.begin(), s->events.end(), from - margin, [] (const auto& h, double b) { return h.beat < b; });
        for (; it != s->events.end() && it->beat < to + margin && count < room; ++it)
        {
            // Its sample: by beats at the DAW's (or steady) tempo, by samples through the tempo changes (counted in samples
            // from the song's start, not seconds: a hit on a whole sample stays on it).
            const auto at = local && timed ? s->timing.secondsAt (it->beat, bpm) * sampleRate - samplesFrom : (it->beat - from) / perSample;
            const auto sample = (int) std::floor (at + 1.0e-4);
            if (sample < 0)
                continue;   // played in the piece before
            if (sample >= n)
                break;      // in the next piece (or past the loop's end)
            into[count++] = { offset + sample, it->slot, it->velocity };
        }
    };

    // FOLLOW DAW off: the DAW's position is ignored (PLAY alone plays the song, the DAW playing or not).
    if (ppq >= 0.0 && (s == nullptr || s->followDaw))
    {
        const auto from = ppq, to = from + numSamples * perSample;
        songLocal = false;   // the DAW started: it leads (PLAY doesn't carry on from its own place when the DAW stops)
        songCountingIn = false;
        songChunk = { true, false, from, perSample, 0.0, bpm, s };
        collect (from, to, 0.0, false, 0, numSamples);
        songPosition = to;
        return count;
    }
    if (! songLocal.load())
    {
        songPosition = -1.0;
        songChunk.active = false;
        songCountingIn = false;
        return 0;
    }

    const auto restartAt = [&] (double beat)
    {
        songBase = beat;
        songSamples = 0;
        songPerSample = perSample;
        songTimed = timed;
        songStartSeconds = timed ? s->timing.secondsAt (beat, bpm) : 0.0;
    };
    if (const auto starts = songLocalStarts.load(); starts != songLocalSeen)
    {
        songLocalSeen = starts;
        restartAt (songLocalFrom.load());
        // THE COUNT-IN (0.45.0): PLAY with REC armed waits a bar of the song's time at the start's tempo, clicking it in.
        songCountInLeft = songCountInDone = 0;
        if (s != nullptr && s->countIn && songRecording.load())
        {
            const auto from = songLocalFrom.load();
            const auto bpmHere = timed ? s->timing.bpmAt (from, bpm) : bpm;
            songCountInSpb = 60.0 / juce::jmax (1.0, bpmHere) * sampleRate;
            songCountInBeats = juce::jmax (1, (int) std::round (s->barLengthAt (from)));
            songCountInLeft = (juce::int64) std::llround (songCountInBeats * songCountInSpb);
        }
    }
    if (timed != songTimed)
        restartAt (juce::jmax (0.0, songPosition.load()));   // tempo changes added or all gone: carry on from here
    // The count-in's part of this piece first (the song waits); the song from where it ends.
    const auto pre = (int) juce::jmin ((juce::int64) numSamples, songCountInLeft);
    const auto countInFrom = (double) songCountInDone;
    songCountInLeft -= pre;
    songCountInDone += pre;
    songCountingIn = songCountInLeft > 0;
    if (pre >= numSamples)
    {
        songChunk = { true, false, songBase, perSample, 0.0, bpm, s };
        songChunk.startAt = numSamples;
        songChunk.countInBeats = songCountInBeats;
        songChunk.countInFrom = countInFrom;
        songChunk.countInSpb = songCountInSpb;
        songPosition = songBase;
        return count;
    }
    const auto length = numSamples - pre;
    // Where PLAY's clock is for the next `n` samples.
    struct Where { double from = 0.0, to = 0.0, secondsFrom = 0.0, samplesFrom = 0.0; };
    const auto where = [&] (int n)
    {
        Where w;
        if (timed)
        {
            w.secondsFrom = songStartSeconds + (double) songSamples / sampleRate;
            w.samplesFrom = songStartSeconds * sampleRate + (double) songSamples;
            w.from = s->timing.beatAtSeconds (w.secondsFrom, bpm);
            w.to = s->timing.beatAtSeconds (w.secondsFrom + n / sampleRate, bpm);
        }
        else
        {
            if (perSample != songPerSample)
                restartAt (songBase + (double) songSamples * songPerSample);   // the DAW's tempo changed: on from here
            w.from = songBase + (double) songSamples * perSample;
            w.to = w.from + n * perSample;
        }
        return w;
    };
    auto w = where (length);
    // THE LOOP AREA (0.44.0): reaching its end, PLAY goes back to its start on the very sample (the rest of the piece
    // from there). Only when PLAY is before its end: started after it, the song plays on to its end as before.
    const auto looping = s != nullptr && s->loopOn && s->loopEnd > s->loopStart + 1.0e-9;
    auto first = length;
    if (looping && w.from < s->loopEnd - 1.0e-9 && w.to >= s->loopEnd - 1.0e-9)
    {
        const auto atEnd = timed ? (s->timing.secondsAt (s->loopEnd, bpm) - w.secondsFrom) * sampleRate : (s->loopEnd - w.from) / perSample;
        first = juce::jlimit (0, length, (int) std::ceil (atEnd - 1.0e-4));
    }
    // The piece's mapping (sample -> beat) from its own first sample: the song's part starts `pre` samples in.
    songChunk = { true, timed, w.from - pre * perSample, perSample, w.samplesFrom - pre, bpm, s };
    songChunk.startAt = pre;
    songChunk.countInBeats = pre > 0 ? songCountInBeats : 0;
    songChunk.countInFrom = countInFrom;
    songChunk.countInSpb = songCountInSpb;
    collect (w.from, first < length ? s->loopEnd : w.to, w.samplesFrom, true, pre, first);
    songSamples += first;
    auto to = w.to;
    if (first < length)
    {
        restartAt (s->loopStart);
        const auto rest = length - first;
        const auto w2 = where (rest);
        songChunk.wrapAt = pre + first;
        songChunk.from2 = w2.from;
        songChunk.samplesFrom2 = w2.samplesFrom;
        collect (w2.from, w2.to, w2.samplesFrom, true, pre + first, rest);
        songSamples += rest;
        to = w2.to;
    }
    else if (looping && w.from < s->loopEnd - 1.0e-9 && w.to >= s->loopEnd - 1.0e-9)
    {
        // The loop's end exactly on this piece's end: the next piece starts at the loop's start (else PLAY would carry on
        // past the end, never inside it again).
        restartAt (s->loopStart);
        to = s->loopStart;
    }

    // PLAY stops at the song's end (not while it's going round a loop that reaches past it).
    if (s == nullptr || (to >= s->endBeat && ! (looping && to < s->loopEnd + 1.0e-9)))
    {
        songLocal = false;
        songPosition = -1.0;
    }
    else
    {
        songPosition = to;
    }
    return count;
}

double ParagonX1AudioProcessor::SongPlayback::barLengthAt (double beat) const noexcept
{
    // The bar a beat is in: from the bar line at or before it to the next (past the lines worked out: the last bar's).
    const auto it = std::upper_bound (barLines.begin(), barLines.end(), beat + 1.0e-6);
    if (it != barLines.end() && it != barLines.begin())
        return *it - *(it - 1);
    if (barLines.size() >= 2)
        return barLines.back() - barLines[barLines.size() - 2];
    return 4.0;
}


void ParagonX1AudioProcessor::noteSongClicks (int blockOffset, int numSamples, double bpm) noexcept
{
    // THE CLICK (0.44.0): a click on every beat this piece crosses (where the song is: songChunk), accented on a bar line.
    const auto* s = songChunk.playback;
    if (! songChunk.active || s == nullptr)
        return;
    // THE COUNT-IN's clicks (0.45.0: whatever CLICK is set to: it's how the count-in is heard), the first accented.
    for (int k = 0; k < songChunk.countInBeats && numSongClicks < (int) songClicks.size(); ++k)
    {
        const auto at = k * songChunk.countInSpb - songChunk.countInFrom;
        const auto sample = (int) std::ceil (at - 1.0e-4);
        if (sample >= 0 && sample < songChunk.startAt && sample < numSamples)
            songClicks[(size_t) numSongClicks++] = { blockOffset + sample, k == 0 };
    }
    if (s->click == song::Song::Click::off || (s->click == song::Song::Click::recording && ! songRecording.load()))
        return;
    const auto sampleRate = getSampleRate();
    const auto stretch = [&] (double from, double samplesFrom, bool timed, int offset, int n)
    {
        const auto beatAt = [&] (int i)
        {
            return timed ? s->timing.beatAtSeconds ((samplesFrom + i) / sampleRate, bpm) : from + i * songChunk.perSample;
        };
        // (0.45.0: from the song's own first sample in the piece, after any count-in)
        const auto skip = offset == 0 ? juce::jmin (n, songChunk.startAt) : 0;
        const auto to = beatAt (n);
        for (auto beat = std::ceil (beatAt (skip) - 1.0e-9); beat < to - 1.0e-9 && numSongClicks < (int) songClicks.size(); beat += 1.0)
        {
            if (beat < 0.0)
                continue;
            const auto at = timed ? s->timing.secondsAt (beat, bpm) * sampleRate - samplesFrom : (beat - from) / songChunk.perSample;
            const auto sample = juce::jlimit (0, n - 1, (int) std::ceil (at - 1.0e-4));
            songClicks[(size_t) numSongClicks++] = { blockOffset + offset + sample, s->isBarLine (beat) };
        }
    };
    const auto first = juce::jmin (numSamples, songChunk.wrapAt);
    stretch (songChunk.from, songChunk.samplesFrom, songChunk.timed, 0, first);
    if (first < numSamples)
        stretch (songChunk.from2, songChunk.samplesFrom2, songChunk.timed, first, numSamples - first);
}

void ParagonX1AudioProcessor::runSongClick (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    // The clicks noted this block, as TAP TO FIND's metronome sounds (higher on the bar's first beat), into MAIN.
    const auto clickLength = (int) (0.03 * getSampleRate());
    auto next = 0;
    const auto channels = juce::jmin (2, getMainBusNumOutputChannels(), buffer.getNumChannels());
    for (int i = 0; i < numSamples; ++i)
    {
        while (next < numSongClicks && songClicks[(size_t) next].sample <= i)
        {
            songClickAt = 0;
            songClickAccent = songClicks[(size_t) next].accent;
            ++next;
        }
        if (songClickAt < 0)
            continue;
        const auto t = songClickAt / getSampleRate();
        const auto v = metronomeLevel * (float) (std::sin (juce::MathConstants<double>::twoPi * (songClickAccent ? 1600.0 : 1100.0) * t) * std::exp (-t / 0.008));
        for (int c = 0; c < channels; ++c)
            buffer.addSample (c, i, v);
        if (++songClickAt >= clickLength)
            songClickAt = -1;
    }
    numSongClicks = 0;
}

juce::MidiFile ParagonX1AudioProcessor::songToMidi() const
{
    return songData->toMidi (hostBpm.load(), [this] (int slot) { return homeNote (slot); });
}

juce::File ParagonX1AudioProcessor::songDragFile() const
{
    const auto folderOut = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PR7-D Grooves");
    folderOut.createDirectory();
    const auto file = folderOut.getChildFile ("PR7-D Song.mid");
    file.deleteFile();
    if (juce::FileOutputStream out (file); out.openedOk())
        songToMidi().writeTo (out);
    return file;
}

void ParagonX1AudioProcessor::copyGrooveForSong (const grooves::Groove& groove, int folder, unsigned parts)
{
    auto block = songBlockFrom (groove, folder);
    song::removeDrums (block, song::allSlots & ~song::slotsOf (parts));
    songClipboard = std::move (block);
}

//==============================================================================
// THE SONG CREATOR (0.10.0)
song::Columns ParagonX1AudioProcessor::songCreatorColumns (const juce::File& source) const
{
    juce::File root;
    for (const auto& f : grooveFolders->getFolders())
        if (source.isAChildOf (f.folder))
            root = f.folder;
    if (root == juce::File())
        return {};   // not in a linked folder: no index to look round in
    return song::variationsOf (source, grooveFolders->getEntries(), root);
}

std::optional<song::Block> ParagonX1AudioProcessor::songBlockFor (const grooves::Entry& e) const
{
    if (const auto g = grooves::read (e.file, grooveLayout (e.folder)))
        return songBlockFrom (*g, e.folder);
    return std::nullopt;
}

song::Song ParagonX1AudioProcessor::songFromStructure (const std::vector<song::Section>& sections, const song::Columns& columns) const
{
    song::Song made;
    std::array<int, song::numSongParts> counts {};
    for (size_t p = 0; p < columns.size(); ++p)
        counts[p] = (int) columns[p].size();
    // The song's bars: the first groove's time signature (else the DAW's).
    made.numerator = hostNumerator.load();
    made.denominator = hostDenominator.load();
    for (const auto& c : columns)
        if (! c.empty())
        {
            made.numerator = c.front().numerator;
            made.denominator = c.front().denominator;
            break;
        }
    const auto bar = made.beatsPerBar();
    std::map<juce::String, std::optional<song::Block>> read;   // each file read once
    const auto picks = song::pick (sections, counts);
    for (size_t i = 0; i < sections.size(); ++i)
    {
        const auto length = sections[i].bars * bar;
        if (picks[i].part < 0)
        {
            // No groove for it (a fill the folder hasn't): the one before plays on.
            if (! made.blocks.empty())
                made.blocks.back().length += length;
            continue;
        }
        const auto& entry = columns[(size_t) picks[i].part][(size_t) picks[i].index];
        auto& block = read[entry.file.getFullPathName()];
        if (! block.has_value())
            block = songBlockFor (entry);
        if (! block.has_value())
            continue;
        auto b = *block;
        b.start = made.endBeat();
        b.length = length;
        b.part = sections[i].part;   // coloured as the structure's part (a verse groove standing in for a pre-chorus)
        made.blocks.push_back (std::move (b));
    }
    return made;
}

juce::File ParagonX1AudioProcessor::songStructuresFile() const
{
    return rigLibrary->getFolder().getChildFile ("Song Structures.xml");
}

std::vector<song::Structure> ParagonX1AudioProcessor::getSongStructures() const
{
    auto all = song::factoryStructures();
    for (auto& s : song::loadStructures (songStructuresFile()))
        all.push_back (std::move (s));
    return all;
}

bool ParagonX1AudioProcessor::saveSongStructure (const juce::String& name, const std::vector<song::Section>& sections)
{
    if (name.trim().isEmpty() || sections.empty())
        return false;
    auto mine = song::loadStructures (songStructuresFile());
    std::erase_if (mine, [&name] (const song::Structure& s) { return s.name == name.trim(); });
    mine.push_back ({ name.trim(), sections, true });
    return song::saveStructures (songStructuresFile(), mine);
}

bool ParagonX1AudioProcessor::renameSongStructure (const juce::String& from, const juce::String& to)
{
    auto mine = song::loadStructures (songStructuresFile());
    const auto it = std::find_if (mine.begin(), mine.end(), [&from] (const song::Structure& s) { return s.name == from; });
    if (it == mine.end() || to.trim().isEmpty())
        return false;
    std::erase_if (mine, [&to, &from] (const song::Structure& s) { return s.name == to.trim() && s.name != from; });
    std::find_if (mine.begin(), mine.end(), [&from] (const song::Structure& s) { return s.name == from; })->name = to.trim();
    return song::saveStructures (songStructuresFile(), mine);
}

bool ParagonX1AudioProcessor::removeSongStructure (const juce::String& name)
{
    auto mine = song::loadStructures (songStructuresFile());
    const auto before = mine.size();
    std::erase_if (mine, [&name] (const song::Structure& s) { return s.name == name; });
    return mine.size() < before && song::saveStructures (songStructuresFile(), mine);
}

int ParagonX1AudioProcessor::importSongMidi (const juce::File& file, bool withTempo)
{
    juce::FileInputStream in (file);
    juce::MidiFile midi;
    if (! in.openedOk() || ! midi.readFrom (in))
        return 0;
    auto imported = song::importMidi (midi, getNoteMap(), file.getFileNameWithoutExtension(), withTempo);
    if (imported.isEmpty())
        return 0;
    auto s = *songData;
    if (s.isEmpty())
    {
        // The song is the file's (its bars the DAW's when its own time signature isn't taken).
        if (! withTempo)
        {
            imported.numerator = hostNumerator.load();
            imported.denominator = hostDenominator.load();
        }
        const auto added = (int) imported.blocks.size();
        setSong (std::move (imported));
        return added;
    }
    // After the song there, from the next bar line: its blocks, and with its tempo, its changes moved along too.
    const auto from = s.snap (s.endBeat() + s.beatsPerBarAt (s.endBeat()) * 0.49);
    for (auto b : imported.blocks)
    {
        b.start += from;
        s.blocks.push_back (std::move (b));
    }
    if (withTempo)
    {
        for (const auto& t : imported.tempos)
            s.tempos[(size_t) s.addTempo (t.beat + from, t.bpm)].ramp = t.ramp;
        s.setMeter (from, imported.numerator, imported.denominator);
        for (const auto& m : imported.meters)
            s.setMeter (m.beat + from, m.numerator, m.denominator);
    }
    const auto added = (int) imported.blocks.size();
    setSong (std::move (s));
    return added;
}

//==============================================================================
// RECORD (0.12.0)
void ParagonX1AudioProcessor::recordSongNotes (const juce::MidiBuffer& midi, int offset, int numSamples, const synth::drums::KitSettings& kit) noexcept
{
    if (! songRecording.load() || ! songChunk.active)
        return;
    for (const auto metadata : midi)
    {
        const auto pos = metadata.samplePosition - offset;
        if (pos < songChunk.startAt || pos >= numSamples)
            continue;   // (0.45.0: notes during the count-in aren't the song's)
        const auto& m = metadata.getMessage();
        if (! m.isNoteOn())
            continue;
        const auto slot = kit.slotFor (m.getNoteNumber());
        if (slot < 0)
            continue;
        // Its beat: by the beat a sample here (the DAW's tempo, or PLAY's steady one), or through the tempo changes.
        // 0.44.0: after the LOOP AREA's wrap in this piece, from its start.
        const auto wrapped = pos >= songChunk.wrapAt;
        const auto at = wrapped ? pos - songChunk.wrapAt : pos;
        const auto beat = songChunk.timed && songChunk.playback != nullptr
                              ? songChunk.playback->timing.beatAtSeconds (((wrapped ? songChunk.samplesFrom2 : songChunk.samplesFrom) + at) / getSampleRate(), songChunk.bpm)
                              : (wrapped ? songChunk.from2 : songChunk.from) + at * songChunk.perSample;
        const auto scope = recordFifo.write (1);
        const RecordedNote note { juce::jmax (0.0, beat), slot, m.getFloatVelocity() };
        if (scope.blockSize1 > 0)
            recordBuffer[(size_t) scope.startIndex1] = note;
        else if (scope.blockSize2 > 0)
            recordBuffer[(size_t) scope.startIndex2] = note;
    }
}

//==============================================================================
// THE DRUM MACHINE (0.38.0)
void ParagonX1AudioProcessor::recordMachineNote (double ppq, double bpm, int slot, float velocity,
                                                 const synth::drumseq::Settings& sequencer) noexcept
{
    juce::ignoreUnused (bpm);
    if (! machineRecording.load() || ppq < 0.0 || ! sequencer.on || sequencer.trigger != synth::drumseq::Trigger::song)
        return;
    const auto length = sequencer.lengthSteps();
    const MachineNote note { std::fmod (ppq / sequencer.beatsPerStep(), length), slot, velocity, sequencer.pattern };
    const auto scope = machineFifo.write (1);
    if (scope.blockSize1 > 0)
        machineBuffer[(size_t) scope.startIndex1] = note;
    else if (scope.blockSize2 > 0)
        machineBuffer[(size_t) scope.startIndex2] = note;
}

void ParagonX1AudioProcessor::setMachinePlaying (bool on)
{
    if (on)
    {
        // The sequencer on and following the song, so PLAY's clock (or the DAW's) drives it.
        if (! isOn (ParamIDs::dseqOn))
            setParameterNow (ParamIDs::dseqOn, 1.0f);
        if (juce::roundToInt (param (ParamIDs::dseqTrigger)) != (int) synth::drumseq::Trigger::song)
            setParameterNow (ParamIDs::dseqTrigger, (float) synth::drumseq::Trigger::song);
        machineRestart = true;
        machinePlaying = true;
        return;
    }
    machinePlaying = false;
    setMachineRecording (false);   // STOP ends a take
}

void ParagonX1AudioProcessor::setMachineRecording (bool on)
{
    if (on == machineRecording.load())
        return;
    if (on)
    {
        machineFifo.reset();
        machineRecording = true;
        return;
    }
    machineRecording = false;
    mergeMachineNotes();
    requestCheckpoint();   // the take: one undo step
}

int ParagonX1AudioProcessor::mergeMachineNotes()
{
    std::vector<MachineNote> notes;
    if (const auto ready = machineFifo.getNumReady(); ready > 0)
    {
        const auto scope = machineFifo.read (ready);
        for (int i = 0; i < scope.blockSize1; ++i)
            notes.push_back (machineBuffer[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i)
            notes.push_back (machineBuffer[(size_t) (scope.startIndex2 + i)]);
    }
    if (notes.empty())
        return 0;
    const auto settings = getDrumSeqSettings();
    const auto length = juce::roundToInt (settings.lengthSteps());
    auto m = getModMatrix();
    auto added = 0;
    for (const auto& note : notes)
    {
        auto step = juce::roundToInt (note.step);
        if (step >= length)
            step = 0;   // just before the loop's end: its first step
        auto& pattern = m.drumPatterns[(size_t) juce::jlimit (0, synth::drumseq::numPatterns - 1, note.pattern)];
        auto found = false;
        for (auto& existing : pattern.notes)
            if (existing.row == note.slot && std::abs (existing.start - step) < 0.5)
            {
                existing.velocity = juce::jmax (existing.velocity, note.velocity);
                found = true;
            }
        if (! found && (int) pattern.notes.size() < synth::drumseq::maxNotes)
        {
            synth::drumseq::Note n;
            n.row = note.slot;
            n.start = step;
            n.velocity = juce::jlimit (0.0f, 1.0f, note.velocity);
            pattern.notes.push_back (n);
        }
        ++added;
    }
    setModMatrix (m);
    return added;
}

void ParagonX1AudioProcessor::setSongRecording (bool on)
{
    if (on == songRecording.load())
        return;
    if (on)
    {
        recordFifo.reset();   // nothing from before the take
        takeBlock = -1;
        songRecording = true;
        return;
    }
    songRecording = false;
    mergeRecordedNotes();
    takeBlock = -1;
    requestCheckpoint();   // the take: one undo step
}

int ParagonX1AudioProcessor::mergeRecordedNotes()
{
    std::vector<RecordedNote> notes;
    const auto ready = recordFifo.getNumReady();
    if (ready == 0)
        return 0;
    {
        const auto scope = recordFifo.read (ready);
        for (int i = 0; i < scope.blockSize1; ++i)
            notes.push_back (recordBuffer[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i)
            notes.push_back (recordBuffer[(size_t) (scope.startIndex2 + i)]);
    }
    auto s = *songData;
    for (const auto& n : notes)
    {
        auto index = s.blockAt (n.beat);
        if (index < 0)
        {
            // No block there: this take's "Recorded" block grows to the bar (it ends on the bar line before), else a new
            // one, its bar long (not into a block after it).
            const auto lines = s.barLines (n.beat);
            auto barStart = 0.0;
            for (const auto line : lines)
                if (line <= n.beat + 1.0e-9)
                    barStart = line;
            auto barEnd = lines.back() > n.beat + 1.0e-9 ? lines.back() : barStart + s.beatsPerBarAt (barStart);
            for (const auto& b : s.blocks)
                if (b.start > n.beat && b.start < barEnd)
                    barEnd = b.start;
            if (takeBlock >= 0 && takeBlock < (int) s.blocks.size() && std::abs (s.blocks[(size_t) takeBlock].end() - barStart) < 1.0e-9
                && s.blocks[(size_t) takeBlock].length <= s.blocks[(size_t) takeBlock].loopLength + 1.0e-9)
            {
                auto& b = s.blocks[(size_t) takeBlock];
                b.length = b.loopLength = barEnd - b.start;
                index = takeBlock;
            }
            else
            {
                song::Block b;
                b.name = "Recorded";
                b.start = barStart;
                b.length = b.loopLength = barEnd - barStart;
                index = takeBlock = s.place (std::move (b), barStart);
            }
        }
        auto& b = s.blocks[(size_t) index];
        if (b.length > b.loopLength + 1.0e-9)
            song::flatten (b);   // a repeating block: written out, so the note lands only where it was played
        song::addHit (b, { n.beat - b.start, n.slot, n.velocity });
    }
    installSong (std::make_shared<const song::Song> (std::move (s)));   // no undo step yet: the take is one, when it ends
    return (int) notes.size();
}

//==============================================================================
// BOUNCE (0.12.0)
namespace
{
    struct StoppedHost final : juce::AudioPlayHead
    {
        double bpm = 120.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setIsPlaying (false);
            return info;
        }
    };
}

ParagonX1AudioProcessor::SongBouncer::SongBouncer (ParagonX1AudioProcessor& from, BounceOptions o, juce::File f)
    : options (std::move (o)), folder (std::move (f))
{
    // A copy of the plugin as it is now (the kit, its samples, the effects, the song), in the same presets folder.
    copy = std::make_unique<ParagonX1AudioProcessor>();
    copy->useRigFolder (from.getRigLibrary().getFolder());
    juce::MemoryBlock state;
    from.getStateInformation (state);
    copy->setStateInformation (state.getData(), (int) state.getSize());
    copy->setNonRealtime (true);   // 0.28.0: a streamed kit waits for the disk here, so the bounce is whole
    {
        // 0.44.0: the whole song, once: no LOOP AREA, no CLICK in the bounce.
        auto whole = copy->getSong();
        whole.loopOn = false;
        whole.click = song::Song::Click::off;
        copy->installSong (std::make_shared<const song::Song> (std::move (whole)));
    }
    sampleRate = from.getSampleRate() > 0.0 ? from.getSampleRate() : 48000.0;
    bpm = from.getHostBpm();
    if (options.perOutput)
        copy->enableAllBuses();   // OUT 2 to 16 on: each drum where it's routed
}

ParagonX1AudioProcessor::SongBouncer::~SongBouncer() = default;

juce::Array<juce::File> ParagonX1AudioProcessor::SongBouncer::run()
{
    juce::Array<juce::File> written;
    const auto& s = copy->getSong();
    if (s.isEmpty())
    {
        error = "The song is empty.";
        return written;
    }
    constexpr int blockSize = 512;
    StoppedHost host;
    host.bpm = bpm;
    copy->setPlayHead (&host);
    copy->setRateAndBufferSizeDetails (sampleRate, blockSize);   // as a host does before preparing it
    copy->prepareToPlay (sampleRate, blockSize);
    const auto latency = copy->getLatencySamples();
    const auto songSamples = (int) std::ceil (s.secondsAt (s.endBeat(), bpm) * sampleRate);
    const auto maxSamples = songSamples + latency + (int) (options.tailSeconds * sampleRate);
    const auto channels = juce::jmax (2, copy->getTotalNumOutputChannels());

    // The outputs' channels in the buffer: MAIN, and OUT 2 to 16 when they're on.
    std::vector<std::pair<juce::String, int>> outputs { { "Main", 0 } };
    if (options.perOutput)
        for (int o = 1; o < synth::numOutputs; ++o)
            if (const auto* bus = copy->getBus (false, o); bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() == 2)
                outputs.emplace_back ("Out " + juce::String (o + 1), bus->getChannelIndexInProcessBlockBuffer (0));
    juce::AudioBuffer<float> rendered (channels, maxSamples);
    rendered.clear();
    juce::AudioBuffer<float> block (channels, blockSize);
    juce::MidiBuffer none;
    copy->playSong (0.0);
    auto done = 0, quiet = 0;
    const auto quietNeeded = (int) (0.25 * sampleRate);
    while (done < maxSamples)
    {
        if (cancel.load())
        {
            copy->setPlayHead (nullptr);
            error = "Cancelled.";
            return written;
        }
        const auto n = juce::jmin (blockSize, maxSamples - done);
        block.setSize (channels, n, false, false, true);
        block.clear();
        copy->processBlock (block, none);
        for (int c = 0; c < channels; ++c)
            rendered.copyFrom (c, done, block, c, 0, n);
        done += n;
        progress = (float) juce::jmin (1.0, (double) done / (songSamples + latency));
        // Past the song's end: on until a quarter second of silence (the cymbals and reverb died away).
        if (done > songSamples + latency)
        {
            quiet = block.getMagnitude (0, n) < 1.0e-5f ? quiet + n : 0;
            if (quiet >= quietNeeded)
                break;
        }
    }
    copy->setPlayHead (nullptr);
    progress = 1.0f;

    // Each output's file: the plugin's own delay taken off the front, the gain, the bits; OUT 2 to 16 only if used.
    const auto length = juce::jmax (0, done - latency);
    const auto gain = juce::Decibels::decibelsToGain (options.gainDb);
    folder.createDirectory();
    for (const auto& [label, first] : outputs)
    {
        if (first < 0 || first + 1 >= channels)
            continue;
        juce::AudioBuffer<float> out (2, length);
        for (int c = 0; c < 2; ++c)
        {
            out.copyFrom (c, 0, rendered, first + c, latency, length);
            out.applyGain (c, 0, length, gain);
        }
        if (label != "Main" && out.getMagnitude (0, length) < 1.0e-6f)
            continue;
        const auto file = folder.getChildFile (juce::File::createLegalFileName (options.name + " - " + label) + ".wav");
        file.deleteFile();
        auto stream = std::make_unique<juce::FileOutputStream> (file);
        if (! stream->openedOk())
        {
            error = "Couldn't write " + file.getFullPathName();
            continue;
        }
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sampleRate, 2, juce::jlimit (16, 32, options.bits), {}, 0));
        if (writer == nullptr)
        {
            error = "Couldn't write " + file.getFullPathName();
            continue;
        }
        stream.release();   // the writer owns it now
        writer->writeFromAudioSampleBuffer (out, 0, length);
        writer.reset();
        written.add (file);
    }
    return written;
}
