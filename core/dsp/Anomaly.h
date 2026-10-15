#pragma once

#include <array>
#include "DelayLine.h"

namespace lab::dsp
{
    // ANOMALY (LAB X-0): the TIME and VOLUME lanes, Gross Beat style. A region (a run of identical slots on a lane) takes
    // its anchor when it begins; repeats play the first pass live and then replay it, so every read looks into the past
    // only. Every jump of the read position crossfades over 2.5 ms between two read heads. X-0 read its regions from
    // its step sequencer; here they come from any RegionSource (ANOMALY's pattern grid).
    namespace AnomalySlot
    {
        enum Time : uint8_t { timeNone = 0, spring4, spring8, spring16, spring32, spring64, whirl8, whirl16, vortex, comet, jelly,
                              burner, bubble, numTime };
        enum Volume : uint8_t { volNone = 0, gate8, gate16, gate32, fadeIn, fadeOut, duck, trem16, mute, numVolume };

        struct Info { const char* code; const char* name; char text; };
        inline constexpr Info timeInfo[numTime] = {
            { "",    "None",                     '.' }, { "S4",  "Springboard 1/4",          'q' },
            { "S8",  "Springboard 1/8",          'e' }, { "S16", "Springboard 1/16",         's' },
            { "S32", "Springboard 1/32",         't' }, { "S64", "Springboard 1/64",         'u' },
            { "W8",  "Whirligig scratch 1/8",    'w' }, { "W16", "Whirligig scratch 1/16",   'W' },
            { "VX",  "Vortex reverse",           'v' }, { "CM",  "Comet tape stop",          'c' },
            { "JL",  "Jelly half speed",         'j' }, { "BN",  "Burner accelerating roll", 'b' },
            { "BB",  "Bubble random stutter",    'o' },
        };
        inline constexpr Info volumeInfo[numVolume] = {
            { "",    "None",             '.' }, { "G8",  "Electro gate 1/8",  'e' }, { "G16", "Electro gate 1/16", 's' },
            { "G32", "Electro gate 1/32", 't' }, { "FI",  "Fade in",           'i' }, { "FO",  "Fade out",          'o' },
            { "DK",  "Duck",              'd' }, { "TR",  "Tremolo 1/16",      'r' }, { "MU",  "Mute",              'm' },
        };
    }

    struct AnomalyRegion
    {
        int slot = 0;
        double startQ = 0.0, endQ = 0.0;   // quarters, absolute
        int64_t id = -1;                   // unique per occurrence (its first step), -1 = none
    };

    struct AnomalyRegionSource
    {
        virtual ~AnomalyRegionSource() = default;
        virtual AnomalyRegion regionAt (int lane, double q) const noexcept = 0;   // lane 0 TIME, 1 VOLUME
        virtual double stepQuarters() const noexcept = 0;
        virtual double nextBoundary (double q) const noexcept = 0;               // the first step boundary after q
    };

    class AnomalyEngine
    {
    public:
        struct Context
        {
            const AnomalyRegionSource* source = nullptr;
            bool running = false;
            double q0 = 0.0, qps = 0.0;   // quarter position at the block start, quarters per sample
            float timeMix = 1.0f, volumeMix = 1.0f, scratchDepth = 0.6f;
            uint32_t salt = 0;            // the callsign: Bubble's stutters are hash01 decisions
        };

        void prepare (double sr, double historySeconds = 16.0)
        {
            sampleRate = sr;
            history.prepare ((int) std::ceil (historySeconds * sr) + 8);
            xfadeLength = std::max (8, (int) (0.0025 * sr));
            volumeSlew = 1.0f - std::exp (-1.0f / (0.0007f * (float) sr));
            reset();
        }

        void reset() noexcept
        {
            history.reset();
            now = 0;
            timeRegion = volumeRegion = AnomalyRegion();
            timeAnchor = volumeAnchor = 0.0;
            nextBoundaryQ = -1.0e18;
            heads[0] = heads[1] = Head();
            current = 0;
            xfade = 1.0f;
            volumeGain = 1.0f;
            haveTarget = false;
            activeTime = activeVolume = 0;
        }

        // The regions changed between step boundaries (a MIDI hold): look them up again at the next sample.
        void invalidate() noexcept { nextBoundaryQ = -1.0e18; }

        int activeTimeSlot() const noexcept   { return activeTime; }
        int activeVolumeSlot() const noexcept { return activeVolume; }
        const StereoDelayLine& getHistory() const noexcept { return history; }

        void process (float* l, float* r, int n, const Context& c) noexcept
        {
            const bool lanes = c.running && c.source != nullptr && c.qps > 0.0;
            const double spq = lanes ? 1.0 / c.qps : sampleRate;   // samples per quarter
            const double stepQ = c.source != nullptr ? c.source->stepQuarters() : 0.25;
            const float timeMix = std::clamp (c.timeMix, 0.0f, 1.0f), volumeMix = std::clamp (c.volumeMix, 0.0f, 1.0f);

            for (int i = 0; i < n; ++i)
            {
                history.push (l[i], r[i]);
                ++now;
                const double t = (double) now - 1.0;   // absolute time of the newest sample

                if (lanes)
                {
                    const double q = c.q0 + (double) i * c.qps;
                    if (q >= nextBoundaryQ || q < nextBoundaryQ - 2.0 * stepQ)
                        updateRegions (c, q, t);
                }
                else if (timeRegion.slot != 0 || volumeRegion.slot != 0)
                {
                    timeRegion = volumeRegion = AnomalyRegion();
                    nextBoundaryQ = -1.0e18;
                }

                // TIME: where in the past to read.
                double target = t;
                float timeGain = 1.0f;
                if (timeRegion.slot != 0)
                {
                    const double e = t - timeAnchor;
                    const double regionLength = std::max (1.0, (timeRegion.endQ - timeRegion.startQ) * spq);
                    double p = e;
                    sourceFor (timeRegion.slot, e, spq, regionLength, c.scratchDepth, timeRegion.id, c.salt, p, timeGain);
                    target = timeAnchor + p;
                    if (t - target > (double) history.capacity() - 64.0)
                        target = t;   // the anchor has left the buffer: back to live
                }

                // Two heads: a jump hands the old head to the fade-out while the new one fades in. A head that has just
                // jumped has no speed yet: it takes its speed from the next step (any step under 4 samples), so a turn
                // from forward to backward (Vortex after live play) is one jump, not a jump on every sample.
                auto& head = heads[(size_t) current];
                if (! haveTarget)
                {
                    head = { target, 1.0, false };
                    haveTarget = true;
                }
                else if (head.settled ? std::abs (target - (head.src + head.speed)) > 1.5 : std::abs (target - head.src) > 4.0)
                {
                    heads[(size_t) (1 - current)] = head;
                    current = 1 - current;
                    heads[(size_t) current] = { target, heads[(size_t) (1 - current)].speed, false };
                    xfade = 0.0f;
                }
                else
                {
                    auto& h = heads[(size_t) current];
                    h.speed = target - h.src;
                    h.src = target;
                    h.settled = true;
                }

                float wl, wr;
                {
                    const auto& h = heads[(size_t) current];
                    wl = history.readHermite (0, t - h.src);
                    wr = history.readHermite (1, t - h.src);
                }
                if (xfade < 1.0f)
                {
                    auto& old = heads[(size_t) (1 - current)];
                    old.src = std::min (old.src + old.speed, t);
                    const float ol = history.readHermite (0, t - old.src), orr = history.readHermite (1, t - old.src);
                    xfade = std::min (1.0f, xfade + 1.0f / (float) xfadeLength);
                    wl = wl * xfade + ol * (1.0f - xfade);
                    wr = wr * xfade + orr * (1.0f - xfade);
                }

                // VOLUME
                float volumeTarget = 1.0f;
                if (volumeRegion.slot != 0)
                {
                    const double e = t - volumeAnchor;
                    const double regionLength = std::max (1.0, (volumeRegion.endQ - volumeRegion.startQ) * spq);
                    volumeTarget = volumeFor (volumeRegion.slot, e, spq, regionLength, lanes ? stepQ * spq : regionLength);
                }
                volumeGain += (volumeTarget - volumeGain) * volumeSlew;

                const float tg = 1.0f + (timeGain - 1.0f) * timeMix;
                const float vg = 1.0f + (volumeGain - 1.0f) * volumeMix;
                l[i] = (l[i] * (1.0f - timeMix) + wl * timeMix) * tg * vg;
                r[i] = (r[i] * (1.0f - timeMix) + wr * timeMix) * tg * vg;
            }
            activeTime = timeRegion.slot;
            activeVolume = volumeRegion.slot;
        }

    private:
        struct Head { double src = 0.0, speed = 1.0; bool settled = false; };

        void updateRegions (const Context& c, double q, double t) noexcept
        {
            nextBoundaryQ = c.source->nextBoundary (q);
            auto take = [&] (int lane, AnomalyRegion& region, double& anchor)
            {
                const auto next = c.source->regionAt (lane, q);
                if (next.slot == region.slot && next.id == region.id)
                    return;
                region = next;
                if (region.slot == 0)
                    return;
                // A region entered at its start anchors there exactly; one entered late (a host jump, a pattern switch,
                // a MIDI hold) anchors where it was entered.
                const double late = (q - region.startQ) / c.qps;
                anchor = late <= 2.0 ? std::round (t - std::max (0.0, late)) : t;
            };
            take (0, timeRegion, timeAnchor);
            take (1, volumeRegion, volumeAnchor);
        }

        // Source position p (relative to the anchor, never later than e) for each TIME slot.
        static void sourceFor (int slot, double e, double spq, double regionLength, float depth, int64_t id, uint32_t salt,
                               double& p, float& gain) noexcept
        {
            using namespace AnomalySlot;
            const double l4 = spq, l8 = 0.5 * spq, l16 = 0.25 * spq, l32 = 0.125 * spq, l64 = 0.0625 * spq;
            gain = 1.0f;
            switch (slot)
            {
                case spring4:  p = std::fmod (e, l4); break;
                case spring8:  p = std::fmod (e, l8); break;
                case spring16: p = std::fmod (e, l16); break;
                case spring32: p = std::fmod (e, l32); break;
                case spring64: p = std::fmod (e, l64); break;
                case whirl8:
                case whirl16:
                {
                    // Baby scratch: back and forth behind the anchor, peak speed up to twice normal.
                    const double period = slot == whirl8 ? l8 : l16;
                    const double amplitude = (0.15 + 0.85 * (double) std::clamp (depth, 0.0f, 1.0f)) * 2.0 * period / 3.14159265358979;
                    p = -0.5 * amplitude * (1.0 - std::cos (6.28318530717959 * e / period));
                    break;
                }
                case vortex:
                    p = -std::fmod (e, std::max (l4, std::min (regionLength, 4.0 * l4)));
                    break;
                case comet:
                {
                    // Tape stop: the speed falls from 1 to 0 across the region, then a short fade.
                    const double d = regionLength;
                    const double x = std::min (e, d);
                    p = x - x * x / (2.0 * d);
                    const double fadeStart = std::max (0.0, d - 0.005 * spq * 2.0);
                    if (e > fadeStart)
                        gain = (float) std::max (0.0, 1.0 - (e - fadeStart) / std::max (1.0, d - fadeStart));
                    break;
                }
                case jelly:
                    p = 0.5 * e;
                    break;
                case burner:
                {
                    // The first 1/8 live, then repeats of its start at 1/16, 1/32, then 1/64 for good.
                    if (e < l8)
                        p = e;
                    else
                    {
                        double x = e - l8;
                        if (x < l16)               p = x;
                        else if ((x -= l16) < l32) p = x;
                        else                       p = std::fmod (x - l32, l64);
                    }
                    break;
                }
                case bubble:
                {
                    // Random stutter over 1/32 cells: live, repeat, 1/64 stutter or reverse of the previous cell.
                    const double cell = l32;
                    const auto k = (int64_t) std::floor (e / cell);
                    const double within = e - (double) k * cell;
                    const float roll = hash01 (id * 131 + k, 77, salt, 0x0BB1E5u);
                    if (k < 1 || roll < 0.40f)  p = e;
                    else if (roll < 0.65f)      p = (double) (k - 1) * cell + within;
                    else if (roll < 0.90f)      p = (double) (k - 1) * cell + std::fmod (within, 0.5 * cell);
                    else                        p = (double) k * cell - within;
                    break;
                }
                default:
                    p = e;
                    break;
            }
            p = std::min (p, e);
        }

        static float volumeFor (int slot, double e, double spq, double regionLength, double stepLength) noexcept
        {
            using namespace AnomalySlot;
            auto gate = [e] (double cycle) { return std::fmod (e, cycle) < 0.5 * cycle ? 1.0f : 0.0f; };
            switch (slot)
            {
                case gate8:   return gate (0.5 * spq);
                case gate16:  return gate (0.25 * spq);
                case gate32:  return gate (0.125 * spq);
                case fadeIn:  { const float x = (float) std::clamp (e / regionLength, 0.0, 1.0); return x * x; }
                case fadeOut: { const float x = (float) std::clamp (1.0 - e / regionLength, 0.0, 1.0); return x * x; }
                case duck:
                {
                    const float x = (float) std::clamp (std::fmod (e, stepLength) / (0.7 * stepLength), 0.0, 1.0);
                    return x * x * (3.0f - 2.0f * x);
                }
                case trem16:  return 0.5f + 0.5f * std::cos (6.2831853f * (float) (e / (0.25 * spq)));
                case mute:    return 0.0f;
                default:      return 1.0f;
            }
        }

        StereoDelayLine history;
        double sampleRate = 48000.0;
        int64_t now = 0;
        AnomalyRegion timeRegion, volumeRegion;
        double timeAnchor = 0.0, volumeAnchor = 0.0, nextBoundaryQ = -1.0e18;
        std::array<Head, 2> heads {};
        int current = 0, xfadeLength = 120, activeTime = 0, activeVolume = 0;
        float xfade = 1.0f, volumeGain = 1.0f, volumeSlew = 0.03f;
        bool haveTarget = false;
    };
}
