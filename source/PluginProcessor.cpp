#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::Identifier rigNameKey { "rigName" }, rigFileKey { "rigFile" }, rigFactoryKey { "rigFactory" },
                           rigModifiedKey { "rigModified" }, guitarFolderKey { "guitarFolder" };
    constexpr double retiredMs = 2000.0;
}

ParagonX1AudioProcessor::ParagonX1AudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ParagonX1", createParameterLayout())
{
    const auto find = [this] (const char* id)
    {
        auto* value = parameters.getRawParameterValue (id);
        jassert (value != nullptr);
        return value;
    };
    toneValue = find (ParamIDs::tone);
    fretModeValue = find (ParamIDs::fretMode);
    pickModeValue = find (ParamIDs::pickMode);
    forceStringValue = find (ParamIDs::forceString);
    handValue = find (ParamIDs::handPosition);
    velToVolValue = find (ParamIDs::velocityToVolume);
    releaseValue = find (ParamIDs::release);
    transposeValue = find (ParamIDs::transpose);
    patchLevelValue = find (ParamIDs::patchLevel);
    outputValue = find (ParamIDs::outputGain);
    bypassValue = find (ParamIDs::bypass);

    rigLibrary = std::make_unique<RigLibrary>();
    if (midiLinksDefaultFile().existsAsFile())
        importMidiLinks (midiLinksDefaultFile());
    currentRig = { "Init", {}, "Init" };

    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            if (! ParamIDs::notInRigs.contains (withId->paramID))
            {
                parameters.addParameterListener (withId->paramID, this);
                parameter->addListener (this);
            }

    // The guitar chosen last (on this computer), else "My Guitar" beside the presets folder.
    {
        auto folder = rigLibrary->getFolder().getParentDirectory().getChildFile ("Guitars").getChildFile ("My Guitar");
        if (const auto xml = juce::XmlDocument::parse (guitarSettingsFile()); xml != nullptr && xml->hasTagName ("Guitar"))
            if (const auto saved = xml->getStringAttribute ("folder"); juce::File::isAbsolutePath (saved))
                folder = juce::File (saved);
        guitarFolder = folder;
        reloadGuitar();
    }

    startTimerHz (30);
    history.record (takeSnapshot());
    checkpointWanted = false;
}

ParagonX1AudioProcessor::~ParagonX1AudioProcessor()
{
    stopTimer();
    loadCancel = importCancel = true;
    jobs.removeAllJobs (true, 10000);
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            if (! ParamIDs::notInRigs.contains (withId->paramID))
            {
                parameters.removeParameterListener (withId->paramID, this);
                parameter->removeListener (this);
            }
}

void ParagonX1AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    guitar.prepare (sampleRate);
    gain.reset (sampleRate, 0.02);
    gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (patchLevelValue->load() + outputValue->load()));
    toneState = {};
    scratch.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
}

bool ParagonX1AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // An instrument: no audio in; one output, mono or stereo.
    const auto output = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet().isDisabled()
        && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

juce::AudioProcessorParameter* ParagonX1AudioProcessor::getBypassParameter() const
{
    return parameters.getParameter (ParamIDs::bypass);
}

guitar::Settings ParagonX1AudioProcessor::getGuitarSettings() const noexcept
{
    guitar::Settings s;
    s.fretMode = (guitar::FretMode) juce::jlimit (0, ParamIDs::fretModeNames.size() - 1, juce::roundToInt (fretModeValue->load()));
    s.pick = (guitar::PickMode) juce::jlimit (0, ParamIDs::pickModeNames.size() - 1, juce::roundToInt (pickModeValue->load()));
    // FORCE STRING's choices: Off, then string 7 (the low A, string 0 here) up to string 1.
    s.forceString = juce::roundToInt (forceStringValue->load()) - 1;
    s.hand = juce::roundToInt (handValue->load());
    s.velocityToVolume = velToVolValue->load();
    s.releaseSeconds = releaseValue->load();
    s.transpose = juce::roundToInt (transposeValue->load());
    return s;
}

void ParagonX1AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0 || numSamples == 0)
        return;

    // MIDI LEARN: the last controller moved, and the knobs linked to controllers.
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

    // The DAW's notes (and the keyboard state's, for tests and hosts' own keyboards) to the guitar.
    keyboardMidi.clear();
    keyboardMidi.addEvents (midi, 0, numSamples, 0);
    keyboardState.processNextMidiBuffer (keyboardMidi, 0, numSamples, true);

    buffer.clear();
    auto settings = getGuitarSettings();
    settings.library = libraryForAudio.load();
    settings.streamer = &streamer;
    settings.waitForDisk = isNonRealtime();   // bouncing: every note whole
    if ((int) scratch.size() < numSamples)
        scratch.resize ((size_t) numSamples);   // (a host sending more than it promised)
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : scratch.data();
    if (buffer.getNumChannels() == 1)
        std::fill (scratch.begin(), scratch.begin() + numSamples, 0.0f);
    guitar.process (left, right, numSamples, keyboardMidi, 0, settings);
    if (buffer.getNumChannels() == 1)
        for (int i = 0; i < numSamples; ++i)
            left[i] = 0.5f * (left[i] + right[i]);

    // TONE: a gentle (6 dB per octave) low-pass; the top of its range is open.
    if (const auto hz = toneValue->load(); hz < ParamRanges::toneMaxHz * 0.995f)
    {
        const auto a = (float) std::exp (-juce::MathConstants<double>::twoPi * hz / getSampleRate());
        for (int c = 0; c < juce::jmin (2, buffer.getNumChannels()); ++c)
        {
            auto* x = buffer.getWritePointer (c);
            auto y = toneState[(size_t) c];
            for (int i = 0; i < numSamples; ++i)
                x[i] = y = x[i] + a * (y - x[i]);
            toneState[(size_t) c] = y;
        }
    }

    // The preset's level and OUTPUT (smoothed), BYPASS (silence: an instrument has nothing to pass through).
    gain.setTargetValue (bypassValue->load() > 0.5f ? 0.0f : juce::Decibels::decibelsToGain (patchLevelValue->load() + outputValue->load()));
    if (gain.isSmoothing())
        for (int i = 0; i < numSamples; ++i)
        {
            const auto g = gain.getNextValue();
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.getWritePointer (c)[i] *= g;
        }
    else
        buffer.applyGain (gain.getTargetValue());

    outputMeter.pushSamples (buffer.getReadPointer (0), numSamples);
    midi.clear();   // an instrument: nothing goes on down the MIDI chain
}

//==============================================================================
void ParagonX1AudioProcessor::timerCallback()
{
    finishMidiLearn();
    {
        // Libraries replaced over two seconds ago: let go.
        const std::lock_guard<std::mutex> lock (libraryLock);
        const auto now = juce::Time::getMillisecondCounterHiRes();
        std::erase_if (retiredLibraries, [now] (const auto& r) { return now - r.second > retiredMs; });
    }
    recordCheckpointIfReady();
}

//==============================================================================
// THE GUITAR
juce::File ParagonX1AudioProcessor::guitarSettingsFile() const
{
    return rigLibrary->getFolder().getChildFile ("Guitar.xml");
}

juce::File ParagonX1AudioProcessor::getGuitarFolder() const
{
    const std::lock_guard<std::mutex> lock (libraryLock);
    return guitarFolder;
}

void ParagonX1AudioProcessor::setGuitarFolder (const juce::File& folder)
{
    {
        const std::lock_guard<std::mutex> lock (libraryLock);
        guitarFolder = folder;
    }
    juce::XmlElement xml ("Guitar");
    xml.setAttribute ("folder", folder.getFullPathName());
    guitarSettingsFile().getParentDirectory().createDirectory();
    xml.writeTo (guitarSettingsFile());
    importReports.fill ({});
    reloadGuitar();
}

std::shared_ptr<const guitar::Library> ParagonX1AudioProcessor::getLibrary() const
{
    const std::lock_guard<std::mutex> lock (libraryLock);
    return library;
}

void ParagonX1AudioProcessor::installLibrary (std::shared_ptr<const guitar::Library> loaded)
{
    {
        const std::lock_guard<std::mutex> lock (libraryLock);
        retiredLibraries.emplace_back (library, juce::Time::getMillisecondCounterHiRes());
        library = std::move (loaded);
        libraryForAudio = library.get();
    }
    guitarBroadcaster.sendChangeMessage();
}

void ParagonX1AudioProcessor::reloadGuitar()
{
    loadProgress = 0.0f;
    loadCancel = false;
    const auto folder = getGuitarFolder();
    jobs.addJob ([this, folder]
    {
        auto loaded = guitar::Library::load (folder, &loadProgress, &loadCancel);
        if (! loadCancel.load())
            installLibrary (std::move (loaded));
        loadProgress = -1.0f;
    });
}

bool ParagonX1AudioProcessor::waitUntilIdle (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (jobs.getNumJobs() > 0)
    {
        if (juce::Time::getMillisecondCounter() > end)
            return false;
        juce::Thread::sleep (5);
    }
    return true;
}

juce::File ParagonX1AudioProcessor::renderListFile (guitar::Articulation a, int string) const
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Paragon PR7-I")
                          .getChildFile ("Render Lists").getChildFile (guitar::renders::fileName (a, string) + ".mid");
    saveRenderList (a, string, file);
    return file;
}

bool ParagonX1AudioProcessor::saveRenderList (guitar::Articulation a, int string, const juce::File& file) const
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream out (file);
    return out.openedOk() && guitar::renders::toMidi (a, string).writeTo (out);
}

bool ParagonX1AudioProcessor::saveAllRenderLists (const juce::File& folder) const
{
    auto ok = true;
    for (int s = 0; s < guitar::numStrings; ++s)
        ok = saveRenderList (guitar::Articulation::sustain, s,
                             folder.getChildFile (guitar::renders::fileName (guitar::Articulation::sustain, s) + ".mid")) && ok;
    return ok;
}

void ParagonX1AudioProcessor::startImport (const juce::File& render, guitar::Articulation a, int string)
{
    if (importingString.load() >= 0)
        return;   // one at a time
    importingString = string;
    importProgress = 0.0f;
    importCancel = false;
    const auto folder = getGuitarFolder();
    jobs.addJob ([this, render, a, string, folder]
    {
        const auto report = guitar::renders::importFile (render, a, string, folder, &importProgress, &importCancel);
        {
            const std::lock_guard<std::mutex> lock (libraryLock);
            importReports[(size_t) string] = report.summary();
        }
        importProgress = -1.0f;
        importingString = -1;
        if (report.ok())
            reloadGuitar();   // (queued after this job)
        else
            guitarBroadcaster.sendChangeMessage();
    });
}

juce::String ParagonX1AudioProcessor::getImportReport (int string) const
{
    const std::lock_guard<std::mutex> lock (libraryLock);
    return string >= 0 && string < guitar::numStrings ? importReports[(size_t) string] : juce::String();
}

//==============================================================================
// UNDO
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
    return { captureRig(), currentRig, rigModified.load() };
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

void ParagonX1AudioProcessor::setParameterNow (const juce::String& id, float value)
{
    if (auto* p = parameters.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

//==============================================================================
// PRESETS
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
    applyingRig = false;
    rigModified = false;
}

void ParagonX1AudioProcessor::loadRig (const RigLibrary::Entry& entry)
{
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
    state.setProperty (rigNameKey, currentRig.name, nullptr);
    state.setProperty (rigFileKey, currentRig.file.getFullPathName(), nullptr);
    state.setProperty (rigFactoryKey, currentRig.factoryName, nullptr);
    state.setProperty (rigModifiedKey, rigModified.load(), nullptr);
    state.setProperty (guitarFolderKey, getGuitarFolder().getFullPathName(), nullptr);
    if (! midiLinks.isEmpty())
        state.appendChild (juce::ValueTree::fromXml (*midiLinks.toXml()), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ParagonX1AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (auto links = state.getChildWithName ("MidiLinks"); links.isValid())
    {
        if (const auto linksXml = links.createXml())
            setMidiLinks (MidiLinks::fromXml (*linksXml));
        state.removeChild (links, nullptr);
    }
    else
        setMidiLinks ({});
    const auto folder = state[guitarFolderKey].toString();
    parameters.replaceState (state);

    // THE GUITAR: the project's folder (as it was saved), not necessarily the one chosen last.
    if (juce::File::isAbsolutePath (folder) && juce::File (folder) != getGuitarFolder())
    {
        {
            const std::lock_guard<std::mutex> lock (libraryLock);
            guitarFolder = juce::File (folder);
        }
        importReports.fill ({});
        reloadGuitar();
    }

    // Restoring the project changed parameters, which marked the preset as modified; put back what was saved instead.
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

//==============================================================================
// MIDI LEARN
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

// JUCE calls this to create new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ParagonX1AudioProcessor();
}
