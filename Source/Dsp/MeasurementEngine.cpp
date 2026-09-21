/*
  ------------------------------------------------------------------------------
    MeasurementEngine.cpp

    Author: Olivier Doaré, github.com/odoare
    Licenced under the GNU AGPL Version 3.0, or commercial terms (LICENSE.md)
    SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-FXME-Commercial
  ------------------------------------------------------------------------------
*/

#include "MeasurementEngine.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace smt
{

static constexpr float tailSeconds   = 1.0f;    // capture the decay
static constexpr float fadeInSeconds = 0.02f;   // stimulus fade in, both signals

// Fade-OUT length. A long one is what Farina (AES 122, 2007, section 3.1) warns
// against: on a sweep it tapers the top of the swept band, which smears the
// deconvolved peak. Measured on a loopback, 20 ms costs 15..25 dB of ringing
// around the peak while 0.5 ms costs at most 0.6 dB, so a sweep gets a token
// fade-out only.
//
// It cannot be dropped altogether. The sweep now ends AT Nyquist, where every
// remaining sample carries the same magnitude — so Farina's other remedy, cutting
// at the last zero crossing, has nothing to cut to, and the final sample would
// step from near full scale straight to silence. That is a broadband click into
// the tweeter even though the deconvolution itself tolerates it.
//
// Noise is analysed by Welch and never deconvolved, so it keeps a normal fade.
static constexpr float sweepFadeOutSeconds = 0.0005f;
static constexpr float noiseFadeOutSeconds = 0.02f;

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

    // This run's sweep band: up to Nyquist over a whole number of octaves.
    sweepBandFor (sr, sweepF1, sweepF2);

    // Log sweep parameters (f1 = sweepF1, f2 = sweepF2):
    //   phase(t) = K * (exp(t/L) - 1),  K = 2*pi*f1*L,
    // with L QUANTIZED so f1*L is an integer — the synchronized swept-sine
    // of Novak et al. (JAES 2015). That makes the harmonic impulse responses
    // separate with true phase when the recording is deconvolved by the
    // sweep's analytic inverse (the sweep sounds the same; only its exact
    // duration moves slightly off the requested one). With f2/f1 an exact
    // power of two, that same condition also puts the sweep's total phase at
    // an integer multiple of 2*pi, so it ends at a zero crossing.
    const double T = (double) settings.durationS;
    sweepL = fxme::SynchronizedSweep::synchronizedL (sweepF1, sweepF2, T);
    sweepK = 2.0 * juce::MathConstants<double>::pi * sweepF1 * sweepL;

    // Sweep runs use the exact synchronized duration L*ln(f2/f1); noise runs
    // keep the requested duration.
    const double actualT = settings.signalType == SignalType::logSweep
                               ? sweepL * std::log (sweepF2 / sweepF1)
                               : T;
    stimulusSamples = (int) std::llround (actualT * sr);
    totalSamples    = stimulusSamples + (int) (tailSeconds * sr);
    capture.setSize (2, totalSamples);

    // Band-limit the white noise to its own fixed band, so it is unaffected by
    // where the sweep band happens to land at this sample rate.
    noiseHp.c = fxme::BiquadCoeffs::highpass (sr, (float) noiseF1Hz, 0.707f);
    noiseLp.c = fxme::BiquadCoeffs::lowpass (sr, juce::jmin ((float) noiseF2Hz, (float) (0.45 * sr)), 0.707f);

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

void MeasurementEngine::sweepBandFor (double sampleRate, double& f1, double& f2) noexcept
{
    if (sampleRate <= 0.0)      // not prepared yet: fall back to the legacy band
    {
        f1 = 10.0;
        f2 = 20000.0;
        return;
    }

    f2 = 0.5 * sampleRate;

    // P octaves below Nyquist, P picked to land f1 as close as possible to
    // targetF1Hz: 11 octaves gives 10.8 Hz at 44.1 kHz and 11.7 Hz at 48 kHz,
    // 12 octaves the same two figures at 88.2 and 96 kHz. Both sit below the
    // half-octave skirt under the analysis band's 20 Hz default, so nothing
    // downstream loses range against the old fixed 10 Hz start.
    constexpr double targetF1Hz = 10.5;
    const int p = juce::jlimit (4, 16, (int) std::llround (std::log2 (f2 / targetF1Hz)));
    f1 = f2 / std::pow (2.0, (double) p);
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

    // Fade in/out to avoid clicks. The sweep's fade-out is deliberately a
    // token one; see sweepFadeOutSeconds for why it is neither 20 ms nor zero.
    const bool sweeping = settings.signalType == SignalType::logSweep;
    const int fadeIn  = juce::jmax (1, (int) (fadeInSeconds * sr));
    const int fadeOut = juce::jmax (1, (int) ((sweeping ? sweepFadeOutSeconds
                                                        : noiseFadeOutSeconds) * sr));
    if (genPos < fadeIn)
        v *= (float) genPos / (float) fadeIn;
    else if (genPos > stimulusSamples - fadeOut)
        v *= (float) (stimulusSamples - genPos) / (float) fadeOut;

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

    // Must be declared as unique_ptr<OutputStream> rather than to the concrete
    // type: createWriterFor() binds it by reference and moves ownership out only
    // on success (so no manual release, and the stream is freed here on failure).
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    bool ok = false;

    if (stream != nullptr)
    {
        auto writer = wav.createWriterFor (stream,
                                           juce::AudioFormatWriterOptions{}
                                               .withSampleRate    (sr)
                                               .withNumChannels   (2)
                                               .withBitsPerSample (32));
        if (writer != nullptr)
            ok = writer->writeFromAudioSampleBuffer (capture, 0, totalSamples);
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
    std::set<int> outputChannels;   // chNN captures: the ones an output
                                    // description belongs to, as opposed to the
                                    // inNN captures a System run writes
    bool haveSub = false;
    int maxPosition = 0;

    for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.wav"))
    {
        juce::String tag; int channelNumber = -1, position = 0;
        if (! parseCaptureName (f.getFileNameWithoutExtension(), tag, channelNumber, position))
            continue;
        maxPosition = juce::jmax (maxPosition, position);
        if (tag == "sub")
        {
            haveSub = true;
            continue;
        }
        if (tag == "ch")
            outputChannels.insert (channelNumber);
        if (std::find (channels.begin(), channels.end(), channelNumber) == channels.end())
            channels.push_back (channelNumber);
    }
    if (haveSub && settings.subChannel >= 0)
    {
        outputChannels.insert (settings.subChannel + 1);
        if (std::find (channels.begin(), channels.end(), settings.subChannel + 1) == channels.end())
            channels.push_back (settings.subChannel + 1);
    }
    std::sort (channels.begin(), channels.end());

    const auto now = juce::Time::getCurrentTime();

    // Prior manifest, reused below both to carry the run log over and to keep
    // the folder's calibration record when this run was made without one.
    const auto priorXml = juce::parseXML (xmlFile);
    const bool priorValid = priorXml != nullptr
                         && priorXml->hasTagName ("SuperMoToMeasurements");

    // Calibration in effect: what this run was made with, or — when absent —
    // what the previous manifest recorded (one physical mic per folder, so a
    // run made with the calibration not loaded shouldn't erase it).
    juce::String micCalName = settings.micCalName;
    juce::String micCalText = settings.micCalText;
    bool  splCalibrated = settings.splCalibrated;
    float splOffsetDb   = settings.splOffsetDb;

    if (priorValid && micCalText.isEmpty())
        if (auto* mc = priorXml->getChildByName ("MicCalibration"))
        {
            micCalName = mc->getStringAttribute ("name");
            micCalText = mc->getAllSubText();
        }
    if (priorValid && ! splCalibrated)
        if (auto* sc = priorXml->getChildByName ("SplCalibration"))
        {
            splCalibrated = true;
            splOffsetDb = (float) sc->getDoubleAttribute ("offsetDb");
        }

    // What the preset calls each output, for the manifest and the readme.
    // Carried over from the previous manifest the way the calibration record
    // is: a run that cannot name a channel must not erase the name an earlier
    // one wrote. A System run names nothing (its channels are inputs), and a
    // dry run of two speakers in a folder that holds four leaves the other two
    // as they were.
    std::map<int, juce::String> priorDescriptions;
    if (priorValid)
        for (auto* c : priorXml->getChildWithTagNameIterator ("Channel"))
            if (const auto d = c->getStringAttribute ("description").trim(); d.isNotEmpty())
                priorDescriptions[c->getIntAttribute ("number")] = d;

    auto descriptionOf = [this, &outputChannels, &priorDescriptions] (int ch) -> juce::String
    {
        if (outputChannels.count (ch) == 0)
            return {};      // an input of a System run: not an output at all
        if (ch >= 1 && ch <= numChannels)
            if (const auto d = settings.outputDescriptions[(size_t) (ch - 1)].trim(); d.isNotEmpty())
                return d;
        const auto it = priorDescriptions.find (ch);
        return it != priorDescriptions.end() ? it->second : juce::String();
    };

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

        // Folder-level calibration record (effective values, see above).
        // dB SPL = dBFS + offsetDb; the MicCalibration text is the verbatim
        // REW/miniDSP/FRD file, ready for MicCalibration::loadFromText().
        if (splCalibrated)
            root.createNewChildElement ("SplCalibration")
                ->setAttribute ("offsetDb", (double) splOffsetDb);
        if (micCalText.isNotEmpty())
        {
            auto* mc = root.createNewChildElement ("MicCalibration");
            mc->setAttribute ("name", micCalName);
            mc->addTextElement (micCalText);
        }

        for (int ch : channels)
        {
            auto* c = root.createNewChildElement ("Channel");
            c->setAttribute ("number", ch);
            if (const auto d = descriptionOf (ch); d.isNotEmpty())
                c->setAttribute ("description", d);
        }

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

        // Sweep identity, so a deconvolution-based analysis can rebuild the
        // exact stimulus: phase(t) = 2*pi*f1*L*(exp(t/L) - 1), L in seconds.
        if (settings.signalType == SignalType::logSweep)
        {
            run->setAttribute ("sweepF1", sweepF1);
            run->setAttribute ("sweepF2", sweepF2);
            run->setAttribute ("sweepL", sweepL);
        }

        // Calibration actually in effect for THIS run (the folder-level
        // elements above may be carried over from earlier runs).
        if (settings.splCalibrated)
            run->setAttribute ("splOffsetDb", (double) settings.splOffsetDb);
        if (settings.micCalName.isNotEmpty())
            run->setAttribute ("micCal", settings.micCalName);

        for (auto& f : filesWrittenThisRun)
            run->createNewChildElement ("File")->setAttribute ("name", f.getFileName());

        // Preserve prior runs' log entries beneath the new one.
        if (priorValid)
            for (auto* priorRun : priorXml->getChildWithTagNameIterator ("Run"))
                root.addChildElement (new juce::XmlElement (*priorRun));

        root.writeTo (xmlFile);
    }

    juce::String out;
    out << "# SuperMoTo measurement folder\n\n";
    if (settings.generalComment.isNotEmpty())
        out << settings.generalComment.trim() << "\n\n";
    out << "Channels: ";
    for (size_t i = 0; i < channels.size(); ++i)
    {
        out << (i > 0 ? ", " : "") << channels[i];
        if (const auto d = descriptionOf (channels[i]); d.isNotEmpty())
            out << " (" << d << ")";
    }
    out << "\n";
    out << "Sub channel: "
        << (haveSub && settings.subChannel >= 0 ? juce::String (settings.subChannel + 1) : juce::String ("none"))
        << "\n";
    out << "Positions so far: " << maxPosition << "\n";
    if (splCalibrated)
        out << "SPL calibration: 0 dBFS = " << juce::String (splOffsetDb, 1) << " dB SPL\n";
    if (micCalText.isNotEmpty())
        out << "Mic calibration: " << micCalName << " (embedded in measurement.xml)\n";
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
        << juce::String (settings.levelDb, 1) << " dB";
    if (settings.signalType == SignalType::logSweep)
        out << ", " << juce::String (sweepF1, 2) << " Hz .. "
            << juce::String (sweepF2, 0) << " Hz";
    out << "\n";
    out << "- Mic input: " << (settings.micInput + 1) << "\n";
    if (settings.micCalName.isNotEmpty())
        out << "- Mic calibration: " << settings.micCalName << "\n";
    if (settings.splCalibrated)
        out << "- SPL calibration: 0 dBFS = "
            << juce::String (settings.splOffsetDb, 1) << " dB SPL\n";
    out << "- Channels measured: ";
    for (int i = 0; i < (int) channelList.size(); ++i)
    {
        const int ch = channelList[(size_t) i] + 1;
        juce::StringArray notes;
        if (! full && channelList[(size_t) i] == settings.subChannel)
            notes.add ("sub");
        if (! full)
            if (const auto d = descriptionOf (ch); d.isNotEmpty())
                notes.add (d);

        out << (i > 0 ? ", " : "") << ch;
        if (! notes.isEmpty())
            out << " (" << notes.joinIntoString (", ") << ")";
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

} // namespace smt
