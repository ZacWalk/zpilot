#include "../src/Profile.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace fs = std::filesystem;

static int checks = 0;

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
    std::wstring previousAppData;
    const auto length = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
    if (length) {
        previousAppData.resize(length);
        GetEnvironmentVariableW(L"APPDATA", previousAppData.data(), length);
        previousAppData.resize(length - 1);
    }
    const auto root = fs::temp_directory_path() /
        ("zpilot-profile-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    if (!fs::create_directory(root)) {
        std::cerr << "FAIL: could not create an exclusive profile fixture\n";
        return 1;
    }
    int result = 0;
    try {
        Check(SetEnvironmentVariableW(L"APPDATA", root.c_str()) != 0, "set isolated APPDATA");
        const auto legacy = root / "z-pilot";
        const auto legacyFile = legacy / "profile.dat";
        const auto currentFile = root / "zpilot" / "profile.dat";
        zp::Profile::UseFolder(legacy.c_str());
        zp::Profile saved;
        saved.Submit(0, 73, 2, true);
        saved.Save();
        const auto original = Read(legacyFile);

        zp::Profile::UseFolder(L"");
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

        const auto isolated = root / "capture";
        zp::Profile::UseFolder(isolated.c_str());
        current.Load();
        Check(current.At(0).bestScore == 0, "an isolated capture must not import the player's legacy profile");
        current.Submit(0, 42, 1, false);
        current.Save();
        Check(fs::exists(isolated / "profile.dat"), "isolated saves stay in the requested folder");
        Check(Read(currentFile) == "invalid", "an isolated save must not change the new player profile");
        Check(Read(legacyFile) == original, "an isolated save must not change the old player profile");

        zp::Profile::UseFolder(L"");
        fs::remove(currentFile);
        fs::create_directory(currentFile);
        current.Load();
        Check(current.At(0).bestScore == 0, "an unreadable new profile must not fall back to the old one");
        current.Submit(0, 99, 3, true);
        current.Save();
        Check(fs::is_directory(currentFile), "a failed replacement must preserve its destination");
        Check(Read(legacyFile) == original, "a failed new save must not change the legacy file");
    }
    catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    zp::Profile::UseFolder(L"");
    if (!SetEnvironmentVariableW(L"APPDATA", length ? previousAppData.c_str() : nullptr)) {
        std::cerr << "FAIL: could not restore APPDATA\n";
        result = 1;
    }
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    if (cleanupError) {
        std::cerr << "FAIL: could not remove profile fixtures: " << cleanupError.message() << '\n';
        result = 1;
    }
    std::cout << checks << " profile checks, " << (result ? "FAILED" : "passed") << '\n';
    return result;
}
