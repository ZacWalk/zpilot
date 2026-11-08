#include "../src/Profile.h"
#include <libdesktop/platform.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

#include <windows.h>

#include "../src/Synth.h"

namespace fs = std::filesystem;

static int checks = 0;
int RunWindowChecks();

static void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

static std::string Read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    Check(file.good(), "profile fixture must exist");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int main()
{
    const auto root = fs::temp_directory_path() /
        ("zpilot-profile-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    if (!fs::create_directory(root)) {
        std::cerr << "FAIL: could not create an exclusive profile fixture\n";
        return 1;
    }
    int result = 0;
    try {
        zp::Profile::UseDefaultRootForTests(core::folder_path(root.string()));
        const auto legacy = root / "z-pilot";
        const auto legacyFile = legacy / "profile.dat";
        const auto currentFile = root / "zpilot" / "profile.dat";
        zp::Profile::UseFolder(core::folder_path(legacy.string()));
        zp::Profile saved;
        saved.Submit(0, 73, 2, true);
        saved.Save();
        const auto original = Read(legacyFile);

        zp::Profile::UseFolder({});
        zp::Profile migrated;
        migrated.Load();
        Check(migrated.At(0).bestScore == 73 && migrated.At(0).relics == 2 &&
              migrated.At(0).completed == 1, "load the legacy profile without losing progress");
        Check(!fs::exists(currentFile.parent_path()), "loading a legacy profile must not write or move it");
        migrated.Submit(0, 88, 3, true);
        migrated.Save();
        Check(Read(legacyFile) == original, "saving under the new name must preserve the old profile");
        Check(fs::exists(currentFile), "save to the zpilot folder");

        zp::Profile current;
        current.Load();
        Check(current.At(0).bestScore == 88 && current.At(0).relics == 3, "the new profile wins over the old one");

        {
            std::ofstream broken(currentFile, std::ios::binary | std::ios::trunc);
            broken << "invalid";
        }
        current.Load();
        Check(current.At(0).bestScore == 0, "an existing corrupt profile must not resurrect old progress");

        const auto strayBackup = currentFile.parent_path() / "profile.dat.bak";
        {
            std::ofstream backup(strayBackup, std::ios::binary | std::ios::trunc);
            backup << "keep this unrelated file";
        }
        current.Submit(0, 77, 1, false);
        current.Save();
        Check(Read(strayBackup) == "keep this unrelated file",
              "publishing must not delete an unrelated backup-looking file");
        {
            std::ofstream broken(currentFile, std::ios::binary | std::ios::trunc);
            broken << "invalid";
        }
        const auto unchanged = Read(currentFile);
        platform::test_set_atomic_file_failure(platform::atomic_file_test_failure::stage_collision);
        current.Save();
        platform::test_set_atomic_file_failure(platform::atomic_file_test_failure::none);
        Check(Read(currentFile) != unchanged,
              "an atomic stage collision must retry without losing the live profile");
        {
            std::ofstream broken(currentFile, std::ios::binary | std::ios::trunc);
            broken << "invalid";
        }
        for (const auto failure : {platform::atomic_file_test_failure::open,
                                   platform::atomic_file_test_failure::write,
                                   platform::atomic_file_test_failure::flush,
                                   platform::atomic_file_test_failure::commit}) {
            platform::test_set_atomic_file_failure(failure);
            current.Save();
            platform::test_set_atomic_file_failure(platform::atomic_file_test_failure::none);
            Check(Read(currentFile) == unchanged,
                  "a failed atomic publish must preserve the live profile");
        }

        const auto isolated = root / "capture";
        zp::Profile::UseFolder(core::folder_path(isolated.string()));
        current.Load();
        Check(current.At(0).bestScore == 0, "an isolated capture must not import the player's legacy profile");
        current.Submit(0, 42, 1, false);
        current.Save();
        Check(fs::exists(isolated / "profile.dat"), "isolated saves stay in the requested folder");
        Check(Read(currentFile) == "invalid", "an isolated save must not change the new player profile");
        Check(Read(legacyFile) == original, "an isolated save must not change the old player profile");

        const core::folder_path unicodeFolder(root.string() + "\\capture-\xC3\xA9");
        zp::Profile::UseFolder(unicodeFolder);
        zp::Profile unicode;
        unicode.Submit(0, 51, 1, false);
        unicode.Save();
        Check(platform::file_attributes(unicodeFolder.combine_file("profile.dat")).exists(),
              "shared file paths preserve non-ASCII capture folders");

        zp::Profile::UseFolder({});
        fs::remove(currentFile);
        fs::create_directory(currentFile);
        current.Load();
        Check(current.At(0).bestScore == 0, "an unreadable new profile must not fall back to the old one");
        current.Submit(0, 99, 3, true);
        current.Save();
        Check(fs::is_directory(currentFile), "a failed replacement must preserve its destination");
        Check(Read(legacyFile) == original, "a failed new save must not change the legacy file");

        checks += RunWindowChecks();
        Check(zp::AudioSampleToPcm16(-2.0f) == -32767, "PCM clips negative samples");
        Check(zp::AudioSampleToPcm16(-1.0f) == -32767, "PCM preserves negative full scale");
        Check(zp::AudioSampleToPcm16(0.5f) == 16384, "PCM rounds positive samples");
        Check(zp::AudioSampleToPcm16(1.0f) == 32767, "PCM preserves positive full scale");
        Check(zp::AudioSampleToPcm16(2.0f) == 32767, "PCM clips positive samples");
        Check(zp::AudioSampleToPcm16(std::numeric_limits<float>::quiet_NaN()) == 0,
              "PCM sanitises non-finite samples");
    }
    catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    zp::Profile::UseFolder({});
    zp::Profile::UseDefaultRootForTests({});
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    if (cleanupError) {
        std::cerr << "FAIL: could not remove profile fixtures: " << cleanupError.message() << '\n';
        result = 1;
    }
    std::cout << checks << " profile/input checks, " << (result ? "FAILED" : "passed") << '\n';
    return result;
}
