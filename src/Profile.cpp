#include "Profile.h"

#include <cerrno>
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

static void ProfileError(const wchar_t* operation, unsigned long code) {
    wchar_t message[192];
    swprintf_s(message, L"zpilot: profile %s failed (error %lu)\n", operation, code);
    OutputDebugStringW(message);
}

static bool ProfilePath(wchar_t* out, size_t count, bool legacy = false) {
    std::wstring dir;
    if (g_folder[0]) {
        dir = g_folder;
    } else {
        wchar_t appData[MAX_PATH];
        const auto length = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
        if (!length || length >= MAX_PATH) {
            ProfileError(L"path", length ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
            return false;
        }
        dir = std::wstring(appData) + (legacy ? L"\\z-pilot" : L"\\zpilot");
    }
    const auto path = dir + L"\\profile.dat";
    if (path.size() >= count) {
        ProfileError(L"path", ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
    wcscpy_s(out, count, path.c_str());
    return true;
}

void Profile::Load() {
    for (auto& r : rows_) r = PlanetRecord();

    wchar_t path[MAX_PATH];
    if (!ProfilePath(path, MAX_PATH)) return;
    FILE* f = nullptr;
    auto error = _wfopen_s(&f, path, L"rb");
    // Import only when the new profile is absent. Never replace it with an older save, and
    // never consult the player's directories during an isolated capture or test.
    if (error == ENOENT && !g_folder[0]) {
        if (!ProfilePath(path, MAX_PATH, true)) return;
        error = _wfopen_s(&f, path, L"rb");
    }
    if (error != 0 || !f) {
        if (error != ENOENT) ProfileError(L"read", error);
        return;
    }

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
    std::wstring folder(path);
    folder.resize(folder.find_last_of(L'\\'));
    if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        ProfileError(L"directory creation", GetLastError());
        return;
    }
    wchar_t tmp[MAX_PATH + 4];
    swprintf_s(tmp, L"%s.tmp", path);

    FILE* f = nullptr;
    const auto error = _wfopen_s(&f, tmp, L"wb");
    if (error != 0 || !f) {
        ProfileError(L"write", error);
        return;
    }
    uint32_t gen = kGenVersion, count = kPlanetCount;
    const bool written = fwrite(&kMagic, sizeof(kMagic), 1, f) == 1 &&
                         fwrite(&kFileVersion, sizeof(kFileVersion), 1, f) == 1 &&
                         fwrite(&gen, sizeof(gen), 1, f) == 1 &&
                         fwrite(&count, sizeof(count), 1, f) == 1 &&
                         fwrite(rows_, sizeof(PlanetRecord), kPlanetCount, f) == (size_t)kPlanetCount;
    const auto closed = fclose(f);
    if (!written || closed != 0) {
        ProfileError(L"write", errno);
        return;
    }

    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        ProfileError(L"replacement", GetLastError());
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
