#include <pianolab/engine_core/error.hpp>
#include <pianolab/engine_core/note_event.hpp>
#include <pianolab/engine_core/performance.hpp>
#include <pianolab/music_io/musicxml.hpp>
#include <pianolab/music_io/reading.hpp>

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <expected>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The MusicXML 4.0 reference: https://www.w3.org/2021/06/musicxml40/
//
// Reading works in two passes. The first walks each part's measures, collecting notes timed in
// quarter notes, plus the tempo and dynamic changes. The second resolves each note's velocity
// from its part's dynamics and turns quarter notes into seconds with the tempo map.

namespace pianolab::music_io {

using engine_core::Error;
using juce::XmlElement;

namespace {

/// Quarter notes per minute when a score gives no tempo (the MusicXML default).
constexpr double defaultTempo = 120.0;
/// The MIDI velocity MusicXML's dynamics percentages are relative to: forte (MusicXML 4.0,
/// the `sound` and `note` elements).
constexpr double forteVelocity = 90.0;
/// The largest MIDI velocity, which maps to 1.0.
constexpr double maxVelocity = 127.0;
/// The velocity of notes in a score with no dynamics at all: mf.
constexpr double defaultMidiVelocity = 80.0;
/// How close two times (in quarter notes) must be to count as the same, e.g. to join a tie.
constexpr double timeTolerance = 1e-6;
/// Semitones in an octave.
constexpr int semitonesPerOctave = 12;

/// MIDI velocities for dynamic markings without a `<sound dynamics>`, as MuseScore plays them
/// (MuseScore source, src/engraving/dom/dynamic.cpp, `Dynamic::DYN_LIST`).
constexpr std::array<std::pair<std::string_view, double>, 14> markingVelocities{{
    {"pppppp", 1.0},
    {"ppppp", 5.0},
    {"pppp", 10.0},
    {"ppp", 16.0},
    {"pp", 33.0},
    {"p", 49.0},
    {"mp", 64.0},
    {"mf", 80.0},
    {"f", 96.0},
    {"ff", 112.0},
    {"fff", 126.0},
    {"ffff", 127.0},
    {"fffff", 127.0},
    {"ffffff", 127.0},
}};

// The warnings, one per kind of problem. Each says what happened and what was done about it.

/// Warning: repeat signs, volta endings, D.C., D.S., coda and fine.
constexpr std::string_view repeats =
    "repeats, endings and jumps are not supported yet; played straight through";
/// Warning: grace notes.
constexpr std::string_view graceNotes = "grace notes are not supported yet; skipped";
/// Warning: fermatas.
constexpr std::string_view fermatas = "fermatas are not supported yet; ignored";
/// Warning: trills, turns, mordents and tremolos.
constexpr std::string_view ornaments =
    "ornaments and tremolos are not supported yet; played as written";
/// Warning: staccato, accents, tenuto and the like.
constexpr std::string_view articulations =
    "articulations (staccato, accents, ...) are not supported yet; played as written";
/// Warning: rolled chords.
constexpr std::string_view arpeggios = "arpeggios are not supported yet; played as plain chords";
/// Warning: glissandos and slides.
constexpr std::string_view glissandos = "glissandos and slides are not supported yet; ignored";
/// Warning: crescendo and diminuendo hairpins.
constexpr std::string_view hairpins =
    "hairpins (crescendo, diminuendo) are not supported yet; the level stays until the next "
    "dynamic";
/// Warning: dynamics that aren't a plain level, such as sf, sfz and fp.
constexpr std::string_view accentDynamics =
    "accent dynamics (sf, fp, ...) are not supported yet; ignored";
/// Warning: pedal markings and pedal playback settings.
constexpr std::string_view pedals = "pedals are not supported yet; ignored";
/// Warning: alterations that aren't whole semitones.
constexpr std::string_view microtones =
    "microtones are not supported; rounded to the nearest semitone";
/// Warning: percussion notes, which have no pitch.
constexpr std::string_view unpitched = "unpitched (percussion) notes can't be played; skipped";
/// Warning: notes whose pitch is missing or malformed.
constexpr std::string_view badPitch = "notes with an unreadable pitch were skipped";
/// Warning: notes with no (or a zero) duration.
constexpr std::string_view noDuration = "notes without a duration were skipped";
/// Warning: a `<backup>` that went back before its measure began.
constexpr std::string_view badBackup =
    "a backup went past the start of its measure; moved to the measure start";

/// A change of value at a time in quarter notes from the start: a tempo (quarter notes per
/// minute), or a part's dynamic level (velocity 0-1).
struct Change {
    double at = 0.0;
    double value = 0.0;
};

/// A note being collected, timed in quarter notes from the start of the score.
struct ScoreNote {
    double start = 0.0;
    double end = 0.0;
    int pitch = 0;
    std::optional<double> velocity; ///< the note's own dynamics, if it has any
    bool tiedForward = false;       ///< a tie continues into a later note
};

/// Where something was found, for warnings.
struct Where {
    std::string part;
    std::string measure;
};

/// Unsupported notation of one kind: how often it occurs and where it first appears.
struct Tally {
    std::size_t count = 0;
    Where first;
};

/// What's collected for the whole score while reading.
struct Score {
    std::vector<Change> tempos;
    std::vector<ScoreNote> notes; ///< every part's notes, velocities resolved
    std::map<std::string_view, Tally> unsupported;
};

/// One part's reading position and what's collected for it.
struct Part {
    Where where;
    double divisions = 1.0; ///< duration units per quarter note
    double cursor = 0.0;    ///< the current time, in quarter notes
    double measureStart = 0.0;
    double furthest = 0.0;   ///< the latest time reached in the current measure
    double chordStart = 0.0; ///< where the last note that wasn't a chord note started
    std::vector<ScoreNote> notes;
    std::vector<Change> dynamics;
};

/// Counts one occurrence of the unsupported notation `what` at the part's current position.
void report(Score& score, const Part& part, std::string_view what) {
    Tally& tally = score.unsupported[what];
    if (tally.count == 0) {
        tally.first = part.where;
    }
    ++tally.count;
}

/// The number in the child element `name` of `element`, or nothing if there's no such child or
/// it's empty.
std::optional<double> childNumber(const XmlElement& element, const char* name) {
    const XmlElement* child = element.getChildByName(name);
    if (child == nullptr) {
        return std::nullopt;
    }
    const juce::String text = child->getAllSubText().trim();
    if (text.isEmpty()) {
        return std::nullopt;
    }
    return text.getDoubleValue();
}

/// A MusicXML dynamics percentage (of forte, MIDI velocity 90) as a velocity from 0 to 1.
double percentToVelocity(double percent) {
    return std::clamp(percent / 100.0 * forteVelocity / maxVelocity, 0.0, 1.0);
}

/// The velocity (0-1) for a dynamic marking such as "mf", or nothing if it isn't a plain level.
std::optional<double> markingVelocity(std::string_view marking) {
    const auto found =
        std::ranges::find(markingVelocities, marking, &std::pair<std::string_view, double>::first);
    if (found == markingVelocities.end()) {
        return std::nullopt;
    }
    return found->second / maxVelocity;
}

/// The tempo in quarter notes per minute of a metronome mark like "dotted quarter = 60", or
/// nothing if it can't be read as one.
std::optional<double> metronomeTempo(const XmlElement& metronome) {
    const juce::String unit = metronome.getChildElementAllSubText("beat-unit", {}).trim();
    constexpr std::array<std::pair<std::string_view, double>, 6> unitQuarters{{
        {"whole", 4.0},
        {"half", 2.0},
        {"quarter", 1.0},
        {"eighth", 0.5},
        {"16th", 0.25},
        {"32nd", 0.125},
    }};
    const std::string unitName = unit.toStdString();
    const auto found = std::ranges::find(unitQuarters, std::string_view(unitName),
                                         &std::pair<std::string_view, double>::first);
    const auto perMinute = childNumber(metronome, "per-minute");
    if (found == unitQuarters.end() || !perMinute || *perMinute <= 0.0) {
        return std::nullopt;
    }
    // Each dot adds half of the previous value.
    double quarters = found->second;
    double dotValue = quarters / 2.0;
    for (const auto* child : metronome.getChildIterator()) {
        if (child->hasTagName("beat-unit-dot")) {
            quarters += dotValue;
            dotValue /= 2.0;
        }
    }
    return *perMinute * quarters;
}

/// What a `<sound>` element set.
struct SoundSet {
    bool tempo = false;
    bool dynamics = false;
};

/// Applies a `<sound>` element at time `at`: tempo and dynamics, and reports jumps and pedals.
SoundSet readSound(const XmlElement& sound, double at, Part& part, Score& score) {
    SoundSet set;
    if (sound.hasAttribute("tempo")) {
        const double tempo = sound.getDoubleAttribute("tempo");
        if (tempo > 0.0) {
            score.tempos.push_back({.at = at, .value = tempo});
            set.tempo = true;
        }
    }
    if (sound.hasAttribute("dynamics")) {
        part.dynamics.push_back(
            {.at = at, .value = percentToVelocity(sound.getDoubleAttribute("dynamics"))});
        set.dynamics = true;
    }
    for (const char* jump : {"dacapo", "dalsegno", "tocoda", "fine"}) {
        if (sound.hasAttribute(jump)) {
            report(score, part, repeats);
        }
    }
    for (const char* pedal : {"damper-pedal", "soft-pedal", "sostenuto-pedal"}) {
        if (sound.hasAttribute(pedal) && sound.getStringAttribute(pedal) != "no") {
            report(score, part, pedals);
        }
    }
    return set;
}

/// Reads a `<direction>`: dynamics, tempo marks, and reports hairpins and pedals.
void readDirection(const XmlElement& direction, Part& part, Score& score) {
    const double at = part.cursor + childNumber(direction, "offset").value_or(0.0) / part.divisions;
    std::optional<double> marking;
    std::optional<double> metronome;
    for (const auto* type : direction.getChildIterator()) {
        if (!type->hasTagName("direction-type")) {
            continue;
        }
        for (const auto* item : type->getChildIterator()) {
            if (item->hasTagName("dynamics")) {
                for (const auto* mark : item->getChildIterator()) {
                    const std::string name = mark->getTagName().toStdString();
                    if (const auto velocity = markingVelocity(name)) {
                        marking = velocity;
                    } else {
                        report(score, part, accentDynamics);
                    }
                }
            } else if (item->hasTagName("wedge")) {
                if (item->getStringAttribute("type") != "stop") {
                    report(score, part, hairpins);
                }
            } else if (item->hasTagName("pedal")) {
                const juce::String pedalType = item->getStringAttribute("type");
                if (pedalType != "stop" && pedalType != "continue") {
                    report(score, part, pedals);
                }
            } else if (item->hasTagName("metronome")) {
                metronome = metronomeTempo(*item);
            }
        }
    }

    SoundSet set;
    if (const XmlElement* sound = direction.getChildByName("sound")) {
        set = readSound(*sound, at, part, score);
    }
    // A <sound> gives the exact playback values, so it wins over the marking.
    if (!set.dynamics && marking) {
        part.dynamics.push_back({.at = at, .value = *marking});
    }
    if (!set.tempo && metronome) {
        score.tempos.push_back({.at = at, .value = *metronome});
    }
}

/// The MIDI note number of a `<pitch>` element, or nothing if it can't be read. Microtones are
/// rounded to the nearest semitone and reported.
std::optional<int> readPitch(const XmlElement& pitch, const Part& part, Score& score) {
    const juce::String step = pitch.getChildElementAllSubText("step", {}).trim().toUpperCase();
    constexpr std::string_view steps = "C D EF G A B"; // index = semitones above C
    const auto octave = childNumber(pitch, "octave");
    const auto index =
        step.length() == 1 ? steps.find(static_cast<char>(step[0])) : std::string_view::npos;
    if (!octave || index == std::string_view::npos || step[0] == ' ') {
        report(score, part, badPitch);
        return std::nullopt;
    }
    const double alter = childNumber(pitch, "alter").value_or(0.0);
    const double semitones = std::round(alter);
    if (std::abs(alter - semitones) > timeTolerance) {
        report(score, part, microtones);
    }
    return (static_cast<int>(*octave) + 1) * semitonesPerOctave + static_cast<int>(index) +
           static_cast<int>(semitones);
}

/// Reads the `<notations>` attached to a note that starts at `start`: a dynamic level there
/// (e.g. `<dynamics><ff/></dynamics>`) sets the part's level from this note on, like a dynamic
/// marking; anything unsupported is reported.
void readNotations(const XmlElement& note, double start, Part& part, Score& score) {
    for (const auto* notations : note.getChildIterator()) {
        if (!notations->hasTagName("notations")) {
            continue;
        }
        for (const auto* item : notations->getChildIterator()) {
            if (item->hasTagName("dynamics")) {
                for (const auto* mark : item->getChildIterator()) {
                    if (const auto velocity = markingVelocity(mark->getTagName().toStdString())) {
                        part.dynamics.push_back({.at = start, .value = *velocity});
                    } else {
                        report(score, part, accentDynamics);
                    }
                }
            } else if (item->hasTagName("fermata")) {
                report(score, part, fermatas);
            } else if (item->hasTagName("ornaments")) {
                report(score, part, ornaments);
            } else if (item->hasTagName("articulations")) {
                report(score, part, articulations);
            } else if (item->hasTagName("arpeggiate")) {
                report(score, part, arpeggios);
            } else if (item->hasTagName("glissando") || item->hasTagName("slide")) {
                report(score, part, glissandos);
            }
        }
    }
}

/// True if `note` has a `<tie>` of the given type ("start" or "stop").
bool hasTie(const XmlElement& note, const char* type) {
    for (const auto* child : note.getChildIterator()) {
        if (child->hasTagName("tie") && child->getStringAttribute("type") == type) {
            return true;
        }
    }
    return false;
}

/// Reads a `<note>`: moves the part's time on, and collects the note if it's played.
void readNote(const XmlElement& element, Part& part, Score& score) {
    if (element.getChildByName("grace") != nullptr) {
        report(score, part, graceNotes); // grace notes take no time in the measure
        return;
    }

    const auto durationUnits = childNumber(element, "duration");
    const double duration = durationUnits.value_or(0.0) / part.divisions;
    const bool isChord = element.getChildByName("chord") != nullptr;
    const double start = isChord ? part.chordStart : part.cursor;
    if (!isChord) {
        part.chordStart = part.cursor;
        part.cursor += duration;
        part.furthest = std::max(part.furthest, part.cursor);
    }

    if (element.getChildByName("cue") != nullptr || element.getChildByName("rest") != nullptr) {
        return; // cue notes take time but aren't played
    }
    if (element.getChildByName("unpitched") != nullptr) {
        report(score, part, unpitched);
        return;
    }
    const XmlElement* pitchElement = element.getChildByName("pitch");
    if (pitchElement == nullptr) {
        report(score, part, badPitch);
        return;
    }
    const auto pitch = readPitch(*pitchElement, part, score);
    if (!pitch) {
        return;
    }
    if (duration <= 0.0) {
        report(score, part, noDuration);
        return;
    }
    readNotations(element, start, part, score);

    const bool tieStart = hasTie(element, "start");
    if (hasTie(element, "stop")) {
        // Continue the tied note this one belongs to, instead of striking again.
        const auto tied = std::ranges::find_if(part.notes, [&](const ScoreNote& note) {
            return note.tiedForward && note.pitch == *pitch &&
                   std::abs(note.end - start) < timeTolerance;
        });
        if (tied != part.notes.end()) {
            tied->end = start + duration;
            tied->tiedForward = tieStart;
            return;
        }
    }

    std::optional<double> velocity;
    if (element.hasAttribute("dynamics")) {
        velocity = percentToVelocity(element.getDoubleAttribute("dynamics"));
    }
    part.notes.push_back({.start = start,
                          .end = start + duration,
                          .pitch = *pitch,
                          .velocity = velocity,
                          .tiedForward = tieStart});
}

/// True if a `<barline>` holds a repeat sign or a volta ending.
bool hasRepeat(const XmlElement& barline) {
    return barline.getChildByName("repeat") != nullptr ||
           barline.getChildByName("ending") != nullptr;
}

/// Reads one `<measure>` of a part.
void readMeasure(const XmlElement& measure, Part& part, Score& score) {
    part.where.measure = measure.getStringAttribute("number", "?").toStdString();
    part.cursor = part.measureStart;
    part.furthest = part.measureStart;
    part.chordStart = part.measureStart;

    for (const auto* child : measure.getChildIterator()) {
        if (child->hasTagName("note")) {
            readNote(*child, part, score);
        } else if (child->hasTagName("backup")) {
            part.cursor -= childNumber(*child, "duration").value_or(0.0) / part.divisions;
            if (part.cursor < part.measureStart - timeTolerance) {
                report(score, part, badBackup);
                part.cursor = part.measureStart;
            }
        } else if (child->hasTagName("forward")) {
            part.cursor += childNumber(*child, "duration").value_or(0.0) / part.divisions;
            part.furthest = std::max(part.furthest, part.cursor);
        } else if (child->hasTagName("attributes")) {
            if (const auto divisions = childNumber(*child, "divisions");
                divisions && *divisions > 0.0) {
                part.divisions = *divisions;
            }
        } else if (child->hasTagName("direction")) {
            readDirection(*child, part, score);
        } else if (child->hasTagName("sound")) {
            readSound(*child, part.cursor, part, score);
        } else if (child->hasTagName("barline") && hasRepeat(*child)) {
            report(score, part, repeats);
        }
    }
    // A measure lasts until the latest time any of its voices reached.
    part.measureStart = std::max(part.furthest, part.cursor);
}

/// The velocity each note plays at: its own dynamics, else the part's latest dynamic level at
/// its start, else mf.
void resolveVelocities(Part& part) {
    std::ranges::stable_sort(part.dynamics, {}, &Change::at);
    for (auto& note : part.notes) {
        if (note.velocity) {
            continue;
        }
        note.velocity = defaultMidiVelocity / maxVelocity;
        for (const auto& change : part.dynamics) {
            if (change.at > note.start + timeTolerance) {
                break;
            }
            note.velocity = change.value;
        }
    }
}

/// Reads one `<part>` into the score.
void readPart(const XmlElement& element, Score& score) {
    Part part;
    part.where.part = element.getStringAttribute("id", "?").toStdString();
    for (const auto* measure : element.getChildIterator()) {
        if (measure->hasTagName("measure")) {
            readMeasure(*measure, part, score);
        }
    }
    resolveVelocities(part);
    score.notes.insert(score.notes.end(), part.notes.begin(), part.notes.end());
}

/// Seconds from the start at `quarters` quarter notes, following the tempo changes (sorted).
double secondsAt(double quarters, std::span<const Change> tempos) {
    double seconds = 0.0;
    double position = 0.0;
    double tempo = defaultTempo;
    for (const auto& change : tempos) {
        if (change.at >= quarters) {
            break;
        }
        seconds += (change.at - position) * 60.0 / tempo;
        position = change.at;
        tempo = change.value;
    }
    return seconds + (quarters - position) * 60.0 / tempo;
}

/// One warning for each kind of unsupported notation, saying where it first appears.
std::vector<Error> unsupportedWarnings(const Score& score) {
    std::vector<Error> warnings;
    for (const auto& [what, tally] : score.unsupported) {
        const std::string where =
            std::format("part {}, measure {}", tally.first.part, tally.first.measure);
        warnings.push_back({tally.count == 1 ? std::format("{} (in {})", what, where)
                                             : std::format("{} ({} times, first in {})", what,
                                                           tally.count, where)});
    }
    return warnings;
}

/// The text of the entry at `index` in `zip`, or nothing if it can't be unpacked.
std::optional<juce::String> entryText(juce::ZipFile& zip, int index) {
    const std::unique_ptr<juce::InputStream> entry(zip.createStreamForEntry(index));
    if (entry == nullptr) {
        return std::nullopt;
    }
    return entry->readEntireStreamAsString();
}

/// The index of the score in an `.mxl` zip: the root file named in `META-INF/container.xml`,
/// else the first `.musicxml` or `.xml` entry outside `META-INF`. -1 if there's none.
int scoreEntry(juce::ZipFile& zip) {
    const int container = zip.getIndexOfFileName("META-INF/container.xml");
    if (container >= 0) {
        if (const auto text = entryText(zip, container)) {
            if (const auto xml = juce::XmlDocument::parse(*text)) {
                if (const auto* rootfiles = xml->getChildByName("rootfiles")) {
                    if (const auto* rootfile = rootfiles->getChildByName("rootfile")) {
                        const int index =
                            zip.getIndexOfFileName(rootfile->getStringAttribute("full-path"));
                        if (index >= 0) {
                            return index;
                        }
                    }
                }
            }
        }
    }
    for (int index = 0; index < zip.getNumEntries(); ++index) {
        const juce::String name = zip.getEntry(index)->filename;
        if (!name.startsWith("META-INF") &&
            (name.endsWithIgnoreCase(".musicxml") || name.endsWithIgnoreCase(".xml"))) {
            return index;
        }
    }
    return -1;
}

} // namespace

std::expected<Reading, Error> readMusicXml(std::string_view xml) {
    if (xml.size() > static_cast<std::size_t>(INT_MAX)) {
        return std::unexpected(Error{"the file is too large to be a score"});
    }
    juce::XmlDocument document(juce::String::fromUTF8(xml.data(), static_cast<int>(xml.size())));
    const std::unique_ptr<XmlElement> root = document.getDocumentElement();
    if (root == nullptr) {
        return std::unexpected(
            Error{std::format("not valid XML: {}", document.getLastParseError().toStdString())});
    }
    if (root->hasTagName("score-timewise")) {
        return std::unexpected(Error{"timewise MusicXML isn't supported; export it as partwise "
                                     "(MuseScore and most programs do)"});
    }
    if (!root->hasTagName("score-partwise")) {
        return std::unexpected(Error{std::format("not a MusicXML score (the root element is <{}>)",
                                                 root->getTagName().toStdString())});
    }

    Score score;
    for (const auto* part : root->getChildIterator()) {
        if (part->hasTagName("part")) {
            readPart(*part, score);
        }
    }
    std::ranges::stable_sort(score.tempos, {}, &Change::at);

    Reading reading;
    reading.warnings = unsupportedWarnings(score);
    for (const auto& note : score.notes) {
        reading.performance.events.push_back(
            {.seconds = secondsAt(note.start, score.tempos),
             .event = engine_core::NoteOn{.note = note.pitch,
                                          .velocity = static_cast<float>(note.velocity.value_or(
                                              defaultMidiVelocity / maxVelocity))}});
        reading.performance.events.push_back({.seconds = secondsAt(note.end, score.tempos),
                                              .event = engine_core::NoteOff{.note = note.pitch}});
    }
    if (score.notes.empty()) {
        reading.warnings.push_back({"the score has no notes"});
    }
    return reading;
}

std::expected<Reading, Error> readCompressedMusicXml(std::span<const std::byte> data) {
    juce::MemoryInputStream stream(data.data(), data.size(), false);
    juce::ZipFile zip(stream);
    if (zip.getNumEntries() == 0) {
        return std::unexpected(Error{"not a compressed MusicXML (.mxl) file"});
    }
    const int index = scoreEntry(zip);
    if (index < 0) {
        return std::unexpected(Error{"the .mxl file holds no MusicXML score"});
    }
    const auto text = entryText(zip, index);
    if (!text) {
        return std::unexpected(Error{"couldn't unpack the score from the .mxl file"});
    }
    return readMusicXml(text->toStdString());
}

} // namespace pianolab::music_io
