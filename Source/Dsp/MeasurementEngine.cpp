/*
  ------------------------------------------------------------------------------
    MeasurementEngine.cpp

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU LGPL Version 3.0
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "MeasurementEngine.h"
#include <algorithm>

namespace smt
{

static constexpr float tailSeconds = 1.0f;      // capture the decay
static constexpr float fadeSeconds = 0.02f;     // stimulus fade in/out

// Filename grammar shared by the writer (MeasurementEngine) and the reader
// (scanMeasurementFolder): "ch<NN>_pos<PPP>.wav", "in<NN>_pos<PPP>.wav", or
// "sub_pos<PPP>.wav" (stem passed in without the .wav extension).
static bool parseCaptureName (const juce::String& stem, juce::String& tag,
                              int& channelNumber, int& position)
{
    const auto posIdx = stem.lastIndexOf ("_pos");
    if (posIdx < 0)
        return false;

    position = stem.substring (posIdx + 4).getIntValue();
    const auto head = stem.substring (0, posIdx);

    if (head == "sub")          { tag = "sub"; channelNumber = -1; return true; }
    if (head.startsWith ("ch")) { tag = "ch";  channelNumber = head.substring (2).getIntValue(); return true; }
    if (head.startsWith ("in")) { tag = "in";  channelNumber = head.substring (2).getIntValue(); return true; }
    return false;
}

static void sortFilesByPosition (juce::Array<juce::File>& files)
{
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        juce::String tag; int chan = -1, posA = 0, posB = 0;
        parseCaptureName (a.getFileNameWithoutExtension(), tag, chan, posA);
        parseCaptureName (b.getFileNameWithoutExtension(), tag, chan, posB);
        return posA < posB;
    });
}

void MeasurementEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    inScratch.setSize (numChannels, juce::jmax (1, maxBlockSize));
    stop();
}

bool MeasurementEngine::start (const Settings& s)
{
    if (isRunning())
        return false;

    channelList.clear();
    for (int o = 0; o < numChannels; ++o)
        if (s.channelsToMeasure[(size_t) o])
            channelList.push_back (o);

    if (channelList.empty() || s.folder.isEmpty())
        return false;

    const juce::File folder (s.folder);
    if (! folder.isDirectory())
        return false;

    settings = s;
    settings.durationS = juce::jlimit (5.0f, 30.0f, s.durationS);
    levelGain = juce::Decibels::decibelsToGain (settings.levelDb);

    // Each channel's next position number: scan the folder for existing files
    // with that channel's tag and take max + 1 (channels measured together in
    // one run share a run, so they naturally stay in sync across runs).
    positions.clear();
    filesWrittenThisRun.clear();
    const bool fullForPositions = settings.mode == MeasureMode::fullSystem;
    for (int ch : channelList)
    {
        const bool isSub = (! fullForPositions && ch == settings.subChannel);
        const juce::String pattern = isSub
            ? juce::String ("sub_pos*.wav")
            : (fullForPositions ? "in" : "ch") + juce::String (ch + 1).paddedLeft ('0', 2) + "_pos*.wav";

        int maxPos = 0;
        for (auto& f : folder.findChildFiles (juce::File::findFiles, false, pattern))
        {
            juce::String tag; int chanNum = -1, pos = 0;
            if (parseCaptureName (f.getFileNameWithoutExtension(), tag, chanNum, pos))
                maxPos = juce::jmax (maxPos, pos);
        }
        positions.push_back (maxPos + 1);
    }

    stimulusSamples = (int) (settings.durationS * sr);
    totalSamples    = stimulusSamples + (int) (tailSeconds * sr);
    capture.setSize (2, totalSamples);

    // Log sweep parameters (f1 = 10 Hz, f2 = 20 kHz):
    //   phase(t) = K * (exp(t/L) - 1),  L = T/ln(f2/f1),  K = 2*pi*f1*L
    const double f1 = 10.0, f2 = 20000.0, T = (double) settings.durationS;
    sweepL = T / std::log (f2 / f1);
    sweepK = 2.0 * juce::MathConstants<double>::pi * f1 * sweepL;

    // Band-limit the white noise to 10 Hz .. 20 kHz.
    noiseHp.c = fxme::BiquadCoeffs::highpass (sr, 10.0f, 0.707f);
    noiseLp.c = fxme::BiquadCoeffs::lowpass (sr, juce::jmin (20000.0f, (float) (0.45 * sr)), 0.707f);

    currentChannel = 0;
    startCurrentOutput();
    setStatus (channelStatus (0));
    state.store (State::playing);
    sendChangeMessage();
    return true;
}

juce::String MeasurementEngine::channelStatus (int idx) const
{
    const bool full = settings.mode == MeasureMode::fullSystem;
    return "Measuring " + juce::String (full ? "input " : "output ")
         + juce::String (channelList[(size_t) idx] + 1) + "...";
}

juce::File MeasurementEngine::captureFile (int ch) const
{
    const bool full = settings.mode == MeasureMode::fullSystem;
    const bool isSub = (! full && ch == settings.subChannel);
    const int position = positions[(size_t) currentChannel];

    const juce::String name = (isSub ? juce::String ("sub")
                                     : (full ? "in" : "ch") + juce::String (ch + 1).paddedLeft ('0', 2))
                             + "_pos" + juce::String (position).paddedLeft ('0', 3) + ".wav";
    return juce::File (settings.folder).getChildFile (name);
}

void MeasurementEngine::stop()
{
    state.store (State::idle);
    progress.store (0.0f);
    setStatus ("Stopped");
    sendChangeMessage();
}

void MeasurementEngine::startCurrentOutput()
{
    capture.clear();
    capturePos = 0;
    genPos = 0;
    noiseHp.reset();
    noiseLp.reset();
}

void MeasurementEngine::setStatus (const juce::String& s)
{
    juce::ScopedLock sl (statusLock);
    statusText = s;
}

float MeasurementEngine::nextStimulusSample()
{
    if (genPos >= stimulusSamples)
        return 0.0f;

    const double t = (double) genPos / sr;
    float v;

    if (settings.signalType == SignalType::logSweep)
    {
        v = (float) std::sin (sweepK * (std::exp (t / sweepL) - 1.0));
    }
    else
    {
        v = random.nextFloat() * 2.0f - 1.0f;
        v = noiseLp.processSample (noiseHp.processSample (v));
    }

    // Fade in/out to avoid clicks.
    const int fadeSamples = juce::jmax (1, (int) (fadeSeconds * sr));
    if (genPos < fadeSamples)
        v *= (float) genPos / (float) fadeSamples;
    else if (genPos > stimulusSamples - fadeSamples)
        v *= (float) (stimulusSamples - genPos) / (float) fadeSamples;

    ++genPos;
    return v * levelGain;
}

bool MeasurementEngine::process (const float* micInput, juce::AudioBuffer<float>& output,
                                 int n, MatrixEngine& engine,
                                 const std::array<bool, numConfigs>& configActive)
{
    const auto st = state.load();
    if (st == State::idle)
        return false;

    for (int c = 0; c < output.getNumChannels(); ++c)
        output.clear (c, 0, n);

    // Hold silence while a finished capture is being written to disk, so
    // the monitoring matrix does not blast between consecutive measurements.
    if (st == State::finishing)
        return true;

    const int ch = channelList[(size_t) currentChannel];
    const bool full = settings.mode == MeasureMode::fullSystem;

    // Output modes write the stimulus straight onto the measured output;
    // fullSystem injects it into the measured input and runs the engine.
    auto* dest = (! full && ch < output.getNumChannels()) ? output.getWritePointer (ch) : nullptr;
    float* sIn = nullptr;
    if (full)
    {
        inScratch.clear();
        if (ch < inScratch.getNumChannels() && n <= inScratch.getNumSamples())
            sIn = inScratch.getWritePointer (ch);
    }

    auto* sent = capture.getWritePointer (0);
    auto* rec  = capture.getWritePointer (1);

    const int todo = juce::jmin (n, totalSamples - capturePos);
    for (int i = 0; i < todo; ++i)
    {
        const float v = nextStimulusSample();
        sent[capturePos] = v;                   // raw stimulus, pre-everything
        rec[capturePos]  = micInput != nullptr ? micInput[i] : 0.0f;
        if (dest != nullptr) dest[i] = v;
        if (sIn  != nullptr) sIn[i]  = v;
        ++capturePos;
    }

    if (full)
    {
        // Complete system: matrix routing, crossover filters, output FIRs and
        // inter-output latency compensation, exactly as monitored. Master gain
        // is bypassed (unity) so the level is set by the stimulus alone.
        engine.process (inScratch.getArrayOfReadPointers(), output, n, configActive, 1.0f);
    }
    else if (settings.mode == MeasureMode::outputFir && dest != nullptr)
    {
        // Verify the correction: emitted signal through the output trim + FIR
        // (the captured "sent" stays the raw stimulus). dryOutput does neither.
        engine.processOutputChainOnly (output, ch, n, true);
    }

    progress.store (((float) currentChannel + (float) capturePos / (float) totalSamples)
                    / (float) channelList.size());

    if (capturePos >= totalSamples)
    {
        state.store (State::finishing);
        triggerAsyncUpdate();       // write the file on the message thread
    }

    return true;
}

void MeasurementEngine::handleAsyncUpdate()
{
    if (state.load() != State::finishing)
        return;

    const int ch = channelList[(size_t) currentChannel];
    const auto file = captureFile (ch);

    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    bool ok = false;

    if (stream != nullptr)
    {
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), sr, 2, 32, {}, 0));
        if (writer != nullptr)
        {
            stream.release();       // writer owns the stream now
            ok = writer->writeFromAudioSampleBuffer (capture, 0, totalSamples);
        }
    }

    if (! ok)
    {
        setStatus ("ERROR writing " + file.getFullPathName());
        state.store (State::idle);
        sendChangeMessage();
        return;
    }

    filesWrittenThisRun.add (file);

    if (++currentChannel < (int) channelList.size())
    {
        setStatus (channelStatus (currentChannel));
        startCurrentOutput();
        state.store (State::playing);
    }
    else
    {
        writeManifests();
        setStatus ("Done (" + juce::String (channelList.size()) + " files written)");
        progress.store (1.0f);
        state.store (State::idle);
    }

    sendChangeMessage();
}

void MeasurementEngine::writeManifests() const
{
    const juce::File folder (settings.folder);
    const juce::File xmlFile = folder.getChildFile ("measurement.xml");
    const juce::File readme = folder.getChildFile ("readme_measurement.md");
    const bool full = settings.mode == MeasureMode::fullSystem;

    // Re-scan the folder (source of truth) for every regular channel ever
    // written here. The sub's original channel number can't be recovered
    // from its filename (sub_pos*.wav has no channel digits), so that comes
    // from the settings just used instead — a folder is expected to keep
    // one stable sub channel across runs.
    std::vector<int> channels;
    bool haveSub = false;
    int maxPosition = 0;

    for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.wav"))
    {
        juce::String tag; int channelNumber = -1, position = 0;
        if (! parseCaptureName (f.getFileNameWithoutExtension(), tag, channelNumber, position))
            continue;
        maxPosition = juce::jmax (maxPosition, position);
        if (tag == "sub")
            haveSub = true;
        else if (std::find (channels.begin(), channels.end(), channelNumber) == channels.end())
            channels.push_back (channelNumber);
    }
    if (haveSub && settings.subChannel >= 0
        && std::find (channels.begin(), channels.end(), settings.subChannel + 1) == channels.end())
        channels.push_back (settings.subChannel + 1);
    std::sort (channels.begin(), channels.end());

    const auto now = juce::Time::getCurrentTime();

    // ── measurement.xml: the machine-readable manifest Group analysis's
    // folder loader reads (scanMeasurementFolder). The readme below stays
    // purely human documentation.
    {
        juce::XmlElement root ("SuperMoToMeasurements");
        root.setAttribute ("version", 1);
        root.setAttribute ("subChannel",
                           haveSub && settings.subChannel >= 0 ? settings.subChannel + 1 : 0);
        root.setAttribute ("positions", maxPosition);
        root.setAttribute ("lastRun", now.toISO8601 (true));

        if (settings.generalComment.isNotEmpty())
            root.createNewChildElement ("GeneralComment")
                ->addTextElement (settings.generalComment);

        for (int ch : channels)
            root.createNewChildElement ("Channel")->setAttribute ("number", ch);

        auto* run = root.createNewChildElement ("Run");
        run->setAttribute ("time", now.toISO8601 (true));
        run->setAttribute ("mode", full ? "system"
                                   : settings.mode == MeasureMode::outputFir ? "fir" : "dry");
        run->setAttribute ("signal", settings.signalType == SignalType::logSweep ? "sweep" : "noise");
        run->setAttribute ("durationS", settings.durationS);
        run->setAttribute ("levelDb", settings.levelDb);
        run->setAttribute ("micInput", settings.micInput + 1);
        if (settings.runComment.isNotEmpty())
            run->setAttribute ("comment", settings.runComment);
        for (auto& f : filesWrittenThisRun)
            run->createNewChildElement ("File")->setAttribute ("name", f.getFileName());

        // Preserve prior runs' log entries beneath the new one.
        if (auto prior = juce::parseXML (xmlFile))
            if (prior->hasTagName (root.getTagName()))
                for (auto* priorRun : prior->getChildWithTagNameIterator ("Run"))
                    root.addChildElement (new juce::XmlElement (*priorRun));

        root.writeTo (xmlFile);
    }

    juce::String out;
    out << "# SuperMoTo measurement folder\n\n";
    if (settings.generalComment.isNotEmpty())
        out << settings.generalComment.trim() << "\n\n";
    out << "Channels: ";
    for (size_t i = 0; i < channels.size(); ++i)
        out << (i > 0 ? ", " : "") << channels[i];
    out << "\n";
    out << "Sub channel: "
        << (haveSub && settings.subChannel >= 0 ? juce::String (settings.subChannel + 1) : juce::String ("none"))
        << "\n";
    out << "Positions so far: " << maxPosition << "\n";
    out << "Last run: " << now.toString (true, true) << "\n\n";
    out << "## Measurement log\n\n";

    // New run entry, most recent first.
    out << "### " << now.toString (true, true) << "\n\n";
    if (settings.runComment.isNotEmpty())
        out << "- Comment: " << settings.runComment << "\n";
    out << "- Mode: " << (full ? "Full system"
                          : settings.mode == MeasureMode::outputFir ? "Output + FIR" : "Dry outputs") << "\n";
    out << "- Signal: " << (settings.signalType == SignalType::logSweep ? "Log sweep" : "White noise")
        << ", " << juce::String (settings.durationS, 0) << " s, "
        << juce::String (settings.levelDb, 1) << " dB\n";
    out << "- Mic input: " << (settings.micInput + 1) << "\n";
    out << "- Channels measured: ";
    for (int i = 0; i < (int) channelList.size(); ++i)
    {
        const bool isSub = (! full && channelList[(size_t) i] == settings.subChannel);
        out << (i > 0 ? ", " : "") << (channelList[(size_t) i] + 1) << (isSub ? " (sub)" : "");
    }
    out << "\n";
    out << "- Files written:\n";
    for (auto& f : filesWrittenThisRun)
        out << "    - " << f.getFileName() << "\n";
    out << "\n";

    // Preserve prior runs' log entries beneath the new one.
    if (readme.existsAsFile())
    {
        const auto prior = readme.loadFileAsString();
        static const juce::String marker ("## Measurement log");
        const auto idx = prior.indexOf (marker);
        if (idx >= 0)
        {
            const auto afterHeading = prior.substring (idx + marker.length());
            const auto nl = afterHeading.indexOfChar ('\n');
            if (nl >= 0)
                out << afterHeading.substring (nl + 1).trimStart();
        }
    }

    readme.replaceWithText (out);
}

MeasurementFolderContents scanMeasurementFolder (const juce::File& folder)
{
    MeasurementFolderContents result;

    // The channel set and sub identity come from a manifest; the actual file
    // lists are always re-derived from the wavs on disk below, so a stale or
    // hand-edited manifest can't misorder the position pairing.
    std::vector<int> channels;
    int subChannel = -1;    // 1-based, -1 = none
    bool haveManifest = false;

    // Preferred: the machine-readable measurement.xml (written alongside the
    // readme by MeasurementEngine::writeManifests()).
    if (auto xml = juce::parseXML (folder.getChildFile ("measurement.xml")))
    {
        if (xml->hasTagName ("SuperMoToMeasurements"))
        {
            const int sc = xml->getIntAttribute ("subChannel", 0);
            subChannel = sc > 0 ? sc : -1;
            for (auto* c : xml->getChildWithTagNameIterator ("Channel"))
            {
                const int ch = c->getIntAttribute ("number", 0);
                if (ch > 0 && std::find (channels.begin(), channels.end(), ch) == channels.end())
                    channels.push_back (ch);
            }
            haveManifest = true;
        }
    }

    // Fallback for folders recorded before measurement.xml existed: parse the
    // human-readable readme's "Channels:" / "Sub channel:" lines.
    const juce::File readme = folder.getChildFile ("readme_measurement.md");
    if (! haveManifest && readme.existsAsFile())
    {
        const auto text = readme.loadFileAsString();
        auto lineStartingWith = [&text] (const juce::String& prefix) -> juce::String
        {
            for (const auto& line : juce::StringArray::fromLines (text))
                if (line.startsWith (prefix))
                    return line.substring (prefix.length()).trim();
            return {};
        };

        const auto channelsLine = lineStartingWith ("Channels:");
        const auto subLine      = lineStartingWith ("Sub channel:");

        if (channelsLine.isNotEmpty())
        {
            subChannel = subLine.equalsIgnoreCase ("none") ? -1 : subLine.getIntValue();
            if (subChannel <= 0)
                subChannel = -1;

            juce::StringArray channelTokens;
            channelTokens.addTokens (channelsLine, ",", "");
            for (auto& tok : channelTokens)
            {
                const int ch = tok.trim().getIntValue();
                if (ch > 0 && std::find (channels.begin(), channels.end(), ch) == channels.end())
                    channels.push_back (ch);
            }
            haveManifest = true;
        }
    }

    if (! haveManifest)
    {
        result.error = "No measurement.xml or readable readme_measurement.md found in "
                       + folder.getFullPathName() + ".";
        return result;
    }

    for (int ch : channels)
    {
        if (ch == subChannel)
            continue;

        auto files = folder.findChildFiles (juce::File::findFiles, false,
                                            "ch" + juce::String (ch).paddedLeft ('0', 2) + "_pos*.wav");
        if (files.isEmpty())
            continue;
        sortFilesByPosition (files);

        MeasurementFolderContents::Channel c;
        c.channelNumber = ch;
        c.files = files;
        result.speakers.push_back (c);
    }
    std::sort (result.speakers.begin(), result.speakers.end(),
              [] (const MeasurementFolderContents::Channel& a, const MeasurementFolderContents::Channel& b)
              { return a.channelNumber < b.channelNumber; });

    if (subChannel > 0)
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, "sub_pos*.wav");
        if (! files.isEmpty())
        {
            sortFilesByPosition (files);
            result.sub.channelNumber = subChannel;
            result.sub.files = files;
        }
    }

    result.ok = true;
    return result;
}

juce::String readMeasurementGeneralComment (const juce::File& folder)
{
    if (auto xml = juce::parseXML (folder.getChildFile ("measurement.xml")))
        if (xml->hasTagName ("SuperMoToMeasurements"))
            if (auto* comment = xml->getChildByName ("GeneralComment"))
                return comment->getAllSubText().trim();
    return {};
}

} // namespace smt
