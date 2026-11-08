#include "Profile.h"

#include <cstring>
#include <string>
#include <vector>

#include "Field.h"

namespace zp {

static const uint32_t kMagic = 0x5A50524Fu;  // 'ZPRO'
static const uint32_t kFileVersion = 2;

static core::folder_path g_folder;
static core::folder_path g_defaultRootForTests;

void Profile::UseFolder(const core::folder_path& folder) {
    g_folder = folder;
}

void Profile::UseDefaultRootForTests(const core::folder_path& root) {
    g_defaultRootForTests = root;
}

int MedalFor(int percent) {
    if (percent >= 90) return MEDAL_GOLD;
    if (percent >= 70) return MEDAL_SILVER;
    if (percent >= 40) return MEDAL_BRONZE;
    return MEDAL_NONE;
}

static std::string LastPlatformError() {
    const std::string error = platform::last_error_message();
    return error.empty() ? "unknown platform error" : error;
}

static void ProfileError(const char* operation, const std::string_view detail) {
    std::string message = "zpilot: profile ";
    message += operation;
    message += " failed";
    if (!detail.empty()) {
        message += " (";
        message += detail;
        message += ')';
    }
    core::trace(message);
}

static bool ProfilePath(core::file_path* out, const bool legacy = false) {
    if (!g_folder.is_empty()) {
        *out = g_folder.combine_file("profile.dat");
        return true;
    }

    core::folder_path root = g_defaultRootForTests;
    if (root.is_empty()) root = platform::known_path(platform::known_folder::roaming_app_data);
    if (root.is_empty()) {
        ProfileError("path", "roaming application-data folder unavailable");
        return false;
    }

    *out = root.combine(legacy ? "z-pilot" : "zpilot").combine_file("profile.dat");
    return true;
}

static bool ReadExact(const platform::file_ptr& file, uint8_t* data, const uint32_t bytes) {
    uint32_t offset = 0;
    while (offset < bytes) {
        const uint64_t read = file->read(data + offset, bytes - offset);
        if (read == 0) return false;
        offset += static_cast<uint32_t>(read);
    }
    return true;
}

void Profile::Load() {
    for (auto& r : rows_) r = PlanetRecord();

    core::file_path path;
    if (!ProfilePath(&path)) return;

    auto opened = platform::open_file(path, platform::file_open_mode::read);
    if (!opened && platform::file_attributes(path).presence == platform::file_presence::not_found &&
        g_folder.is_empty()) {
        if (!ProfilePath(&path, true)) return;
        opened = platform::open_file(path, platform::file_open_mode::read);
    }
    if (!opened) {
        const auto attributes = platform::file_attributes(path);
        if (attributes.presence != platform::file_presence::not_found)
            ProfileError("read", LastPlatformError());
        return;
    }
    const auto& file = opened;

    constexpr uint32_t kHeaderBytes = sizeof(uint32_t) * 4;
    constexpr uint32_t kRecordBytes = sizeof(PlanetRecord) * kPlanetCount;
    constexpr uint32_t kFileBytes = kHeaderBytes + kRecordBytes;
    if (file->size() < kFileBytes) {
        ProfileError("read", "profile is shorter than its record format");
        return;
    }

    std::vector<uint8_t> bytes(kFileBytes);
    if (!ReadExact(file, bytes.data(), kFileBytes)) {
        ProfileError("read", "profile read was incomplete");
        return;
    }

    uint32_t magic = 0, ver = 0, gen = 0, count = 0;
    size_t offset = 0;
    auto read_u32 = [&](uint32_t* value) {
        std::memcpy(value, bytes.data() + offset, sizeof(*value));
        offset += sizeof(*value);
    };
    read_u32(&magic);
    read_u32(&ver);
    read_u32(&gen);
    read_u32(&count);

    // A generator change means the saved planets no longer exist. Rather than
    // show scores against caverns nobody can play, start the log again.
    PlanetRecord rows[kPlanetCount];
    if (magic != kMagic || ver != kFileVersion || gen != kGenVersion ||
        count != (uint32_t)kPlanetCount)
        return;

    std::memcpy(rows, bytes.data() + offset, sizeof(rows));
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

void Profile::Save() const {
    core::file_path path;
    if (!ProfilePath(&path)) return;

    const auto folder = path.folder();
    const auto created = platform::create_folder(folder);
    if (created.failed() && !platform::is_directory(folder)) {
        ProfileError("directory creation", created.error_message);
        return;
    }

    constexpr uint32_t kHeaderBytes = sizeof(uint32_t) * 4;
    constexpr uint32_t kRecordBytes = sizeof(PlanetRecord) * kPlanetCount;
    constexpr uint32_t kFileBytes = kHeaderBytes + kRecordBytes;
    std::vector<uint8_t> bytes(kFileBytes);
    size_t offset = 0;
    auto write_u32 = [&](const uint32_t value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
        offset += sizeof(value);
    };
    write_u32(kMagic);
    write_u32(kFileVersion);
    write_u32(kGenVersion);
    write_u32(kPlanetCount);
    std::memcpy(bytes.data() + offset, rows_, sizeof(rows_));

    const auto opened = platform::create_atomic_file_writer_result(path);
    if (!opened.success()) {
        ProfileError("staging", opened.result.error_message);
        return;
    }
    const auto writer = opened.writer;
    const auto written = writer->write(std::span<const uint8_t>(bytes.data(), bytes.size()));
    if (!written.success()) {
        ProfileError("write", written.error_message);
        return;
    }
    const auto flushed = writer->flush();
    if (!flushed.success()) {
        ProfileError("flush", flushed.error_message);
        return;
    }
    const auto committed = writer->commit();
    if (committed.failed()) ProfileError("replacement", committed.error_message);
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
