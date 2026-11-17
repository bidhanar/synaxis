// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "synaxis/catalog.hpp"
#include "synaxis/platform.hpp"
#include "synaxis/process.hpp"

namespace {

namespace fs = std::filesystem;
using namespace synaxis;

TEST(PlatformTest, PathsRoundTripAsUtf8) {
    const std::string name = "Am\xC3\xA9lie (2001) \xE5\x8D\x83\xE3\x81\xA8\xE5\x8D\x83\xE5\xB0\x8B.mkv";
    fs::path path = PathFromUtf8(name);
    EXPECT_EQ(PathToUtf8(path), name);
    EXPECT_EQ(PathToUtf8(path.stem()), name.substr(0, name.size() - 4));
}

TEST(PlatformTest, NonAsciiFilesAreScannedWithTheirRealNames) {
    fs::path dir = fs::temp_directory_path() / PathFromUtf8("synaxis_plat_\xC3\xA9");
    fs::remove_all(dir);
    fs::create_directories(dir / ".hidden");
    const std::string name = "Am\xC3\xA9lie.2001.1080p.mkv";
    std::ofstream(dir / PathFromUtf8(name)) << "x";
    std::ofstream(dir / ".hidden" / "Other.2001.mkv") << "x";

    ScanOptions options;
    options.roots = {dir};
    auto files = ScanVideoFiles(options);
    fs::remove_all(dir);
    ASSERT_EQ(files.size(), 1u);  // the dot-folder is skipped
    EXPECT_EQ(PathToUtf8(files[0].path.filename()), name);
    EXPECT_EQ(files[0].metadata.title, "Am\xC3\xA9lie");
}

TEST(PlatformTest, TrimsTrailingSeparatorsButKeepsRoots) {
    EXPECT_EQ(TrimTrailingSeparators("/media/films///"), "/media/films");
    EXPECT_EQ(TrimTrailingSeparators("/"), "/");
#ifdef _WIN32
    EXPECT_EQ(TrimTrailingSeparators("D:\\Films\\"), "D:\\Films");
    EXPECT_EQ(TrimTrailingSeparators("D:\\"), "D:\\");
#endif
}

TEST(PlatformTest, ExpandsHome) {
    EXPECT_EQ(ExpandHome("~"), PathToUtf8(HomeDir()));
    EXPECT_EQ(ExpandHome("~/Videos"), PathToUtf8(HomeDir()) + "/Videos");
    EXPECT_EQ(ExpandHome("/tmp/~x"), "/tmp/~x");
}

TEST(PlatformTest, FindsAFreePort) {
    int port = FreeLocalPort();
    EXPECT_GT(port, 1023);
    EXPECT_LE(port, 65535);
}

TEST(ProcessTest, ReportsExitCodes) {
#ifdef _WIN32
    auto ok = Process::Spawn({"cmd.exe", "/c", "exit 0"}, false, nullptr);
    auto bad = Process::Spawn({"cmd.exe", "/c", "exit 3"}, false, nullptr);
#else
    auto ok = Process::Spawn({"true"}, false, nullptr);
    auto bad = Process::Spawn({"sh", "-c", "exit 3"}, false, nullptr);
#endif
    ASSERT_TRUE(ok && bad);
    EXPECT_EQ(ok->Wait(), 0);
    EXPECT_EQ(bad->Wait(), 3);
    EXPECT_FALSE(ok->Running());
}

TEST(ProcessTest, TerminatesALongRunningProcess) {
#ifdef _WIN32
    auto p = Process::Spawn({"ping.exe", "-n", "30", "127.0.0.1"}, true, nullptr);
#else
    auto p = Process::Spawn({"sleep", "30"}, true, nullptr);
#endif
    ASSERT_TRUE(p);
    EXPECT_TRUE(p->Running());
    p->Terminate();
    EXPECT_FALSE(p->Running());
}

TEST(ProcessTest, MissingProgramIsAnError) {
    std::string error;
    EXPECT_FALSE(Process::Spawn({"synaxis-no-such-program"}, false, &error));
    EXPECT_NE(error.find("synaxis-no-such-program"), std::string::npos);
}

#ifdef _WIN32
TEST(ProcessTest, QuotesArgumentsTheWayMicrosoftsRuntimeSplitsThem) {
    EXPECT_EQ(QuoteWindowsArgument(L"plain"), L"plain");
    EXPECT_EQ(QuoteWindowsArgument(L""), L"\"\"");
    EXPECT_EQ(QuoteWindowsArgument(L"C:\\My Films\\a b.mkv"), L"\"C:\\My Films\\a b.mkv\"");
    EXPECT_EQ(QuoteWindowsArgument(L"say \"hi\""), L"\"say \\\"hi\\\"\"");
    EXPECT_EQ(QuoteWindowsArgument(L"C:\\dir with space\\"), L"\"C:\\dir with space\\\\\"");
}
#endif

} // namespace
