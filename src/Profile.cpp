#include "Profile.h"

#include <cstdio>

#include "Field.h"

namespace zp {

static const uint32_t kMagic = 0x5A50524Fu;  // 'ZPRO'
static const uint32_t kFileVersion = 2;

static wchar_t g_folder[MAX_PATH] = L"";

void Profile::UseFolder(const wchar_t* folder) {
    wcscpy_s(g_folder, folder);
}

int MedalFor(int percent) {
    if (percent >= 90) return MEDAL_GOLD;
    if (percent >= 70) return MEDAL_SILVER;
    if (percent >= 40) return MEDAL_BRONZE;
    return MEDAL_NONE;
}

static bool ProfilePath(wchar_t* out, size_t count) {
    wchar_t dir[MAX_PATH];
    if (g_folder[0]) {
        wcscpy_s(dir, g_folder);
    } else {
        wchar_t appData[MAX_PATH];
        if (!GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH)) return false;
        swprintf_s(dir, L"%s\\z-pilot", appData);
    }
    CreateDirectoryW(dir, nullptr);
    swprintf_s(out, count, L"%s\\profile.dat", dir);
    return true;
}

void Profile::Load() {
    for (auto& r : rows_) r = PlanetRecord();

    wchar_t path[MAX_PATH];
    if (!ProfilePath(path, MAX_PATH)) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) return;

    uint32_t magic = 0, ver = 0, gen = 0, count = 0;
    bool ok = fread(&magic, sizeof(magic), 1, f) == 1 && magic == kMagic &&
              fread(&ver, sizeof(ver), 1, f) == 1 && ver == kFileVersion &&
              fread(&gen, sizeof(gen), 1, f) == 1 && fread(&count, sizeof(count), 1, f) == 1;
    // A generator change means the saved planets no longer exist. Rather than
    // show scores against caverns nobody can play, start the log again.
    PlanetRecord rows[kPlanetCount];
    if (ok && gen == kGenVersion && count == (uint32_t)kPlanetCount &&
        fread(rows, sizeof(PlanetRecord), kPlanetCount, f) == (size_t)kPlanetCount) {
        // Only Submit writes a record, so anything it could never produce means
        // the file was damaged. Keep what can be trusted rather than show a
        // card at 255%.
        for (int i = 0; i < kPlanetCount; ++i) {
            PlanetRecord& r = rows[i];
            r.attempted = r.attempted ? 1 : 0;
            r.completed = r.completed ? 1 : 0;
            r.bestScore = (uint8_t)std::min<int>(r.bestScore, 100);
            r.medal = (uint8_t)MedalFor(r.bestScore);
            r.relics = (uint8_t)std::min<int>(r.relics, 3);
            rows_[i] = r;
        }
    }
    fclose(f);
}

void Profile::Save() const {
    wchar_t path[MAX_PATH];
    if (!ProfilePath(path, MAX_PATH)) return;
    wchar_t tmp[MAX_PATH];
    swprintf_s(tmp, L"%s.tmp", path);

    FILE* f = nullptr;
    if (_wfopen_s(&f, tmp, L"wb") != 0 || !f) return;
    uint32_t gen = kGenVersion, count = kPlanetCount;
    fwrite(&kMagic, sizeof(kMagic), 1, f);
    fwrite(&kFileVersion, sizeof(kFileVersion), 1, f);
    fwrite(&gen, sizeof(gen), 1, f);
    fwrite(&count, sizeof(count), 1, f);
    fwrite(rows_, sizeof(PlanetRecord), kPlanetCount, f);
    fclose(f);

    MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

bool Profile::Submit(int planet, int percent, int relics, bool completed) {
    PlanetRecord& r = At(planet);
    bool improved = false;
    r.attempted = 1;
    ++r.attempts;
    if (completed) r.completed = 1;
    if (percent > r.bestScore) {
        r.bestScore = (uint8_t)clampf((float)percent, 0.0f, 100.0f);
        r.medal = (uint8_t)MedalFor(r.bestScore);
        improved = true;
    }
    if (relics > r.relics) {
        r.relics = (uint8_t)relics;
        improved = true;
    }
    return improved;
}

int Profile::TotalPercent() const {
    int sum = 0;
    for (auto& r : rows_) sum += r.bestScore;
    return sum;
}

int Profile::MedalCount(int atLeast) const {
    int n = 0;
    for (auto& r : rows_)
        if (r.medal >= atLeast) ++n;
    return n;
}

bool Profile::RowUnlocked(int row) const {
    if (row <= 0) return true;
    if (row >= kPlanetRows) return false;
    int cleared = 0;
    for (int i = 0; i < kPlanetCols; ++i)
        if (rows_[(size_t)((row - 1) * kPlanetCols + i)].completed) ++cleared;
    // Four of seven: enough to prove competence, loose enough that one nasty
    // planet never becomes a wall.
    return cleared >= 4;
}

}  // namespace zp
