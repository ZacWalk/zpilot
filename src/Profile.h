// zpilot - persistent pilot record: one row per planet, written atomically so
// a crash mid-save cannot cost the player their progress.
#pragma once
#include "Common.h"

namespace zp {

constexpr int kPlanetCols = 7;
constexpr int kPlanetRows = 5;
constexpr int kPlanetCount = kPlanetCols * kPlanetRows;

enum Medal : int { MEDAL_NONE = 0, MEDAL_BRONZE = 1, MEDAL_SILVER = 2, MEDAL_GOLD = 3 };

struct PlanetRecord {
    uint8_t attempted = 0;
    uint8_t completed = 0;
    uint8_t medal = MEDAL_NONE;
    uint8_t relics = 0;     // best relic count recovered
    uint8_t bestScore = 0;  // percent, 0..100
    uint32_t attempts = 0;
};

class Profile {
public:
    // Keeps the record in `folder` instead of %APPDATA%\zpilot. The scripted
    // capture flies real runs, and they must not land in the player's log.
    static void UseFolder(const wchar_t* folder);

    void Load();
    void Save() const;

    PlanetRecord& At(int i) { return rows_[(size_t)clampi(i)]; }
    const PlanetRecord& At(int i) const { return rows_[(size_t)clampi(i)]; }

    // Best-of merge. Returns true when this run improved the record.
    bool Submit(int planet, int percent, int relics, bool completed);

    int TotalPercent() const;   // sum of best scores
    int MedalCount(int atLeast) const;
    // Rows unlock once enough of the previous row has been cleared.
    bool RowUnlocked(int row) const;
    bool Unlocked(int planet) const { return RowUnlocked(planet / kPlanetCols); }

private:
    static int clampi(int i) { return i < 0 ? 0 : (i >= kPlanetCount ? kPlanetCount - 1 : i); }
    PlanetRecord rows_[kPlanetCount] = {};
};

int MedalFor(int percent);

}  // namespace zp
