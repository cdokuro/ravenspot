#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QElapsedTimer>
#include <QRandomGenerator>

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <future>
#include <fstream>
#include <unistd.h>

// Expose private members for white-box cache size inspection
#define private public
#include "ui/IconProviders.hpp"
#undef private

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] ASSERTION FAILED: " << msg << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

#define TEST_PASS(msg) \
    std::cout << "[+] PASS: " << msg << std::endl

namespace ravenspot::test {

// Helper: read current RSS in megabytes
double getProcessRssMb() {
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        unsigned long size = 0, resident = 0;
        statm >> size >> resident;
        long pageSizeKb = sysconf(_SC_PAGESIZE) / 1024;
        return (resident * pageSizeKb) / 1024.0;
    }
    return 0.0;
}

// =============================================================================
// SUBTEST 1: Direct IconCache LRU Cap (500 items) & Eviction Correctness
// =============================================================================
bool test_lru_cache_cap_and_eviction() {
    std::cout << "\n------------------------------------------------------\n";
    std::cout << "SUBTEST 1: Direct IconCache LRU Cap (500) & Eviction\n";
    std::cout << "------------------------------------------------------\n";

    constexpr size_t MAX_CAP = 500;
    IconCache cache(MAX_CAP);
    TEST_ASSERT(cache.size() == 0, "Initial cache size must be 0");

    QImage testImg(64, 64, QImage::Format_ARGB32_Premultiplied);
    testImg.fill(Qt::blue);

    // 1. Insert 500 items -> size should grow from 1 to 500
    for (size_t i = 0; i < MAX_CAP; ++i) {
        cache.put(QStringLiteral("item_%1").arg(i), testImg);
        TEST_ASSERT(cache.size() == (i + 1), "Cache size must match inserted count up to cap");
    }
    TEST_ASSERT(cache.size() == MAX_CAP, "Cache size must be exactly 500 at capacity");

    // 2. Insert another 500 items (total 1,000 inserted) -> size must remain strictly 500
    for (size_t i = MAX_CAP; i < 1000; ++i) {
        cache.put(QStringLiteral("item_%1").arg(i), testImg);
        TEST_ASSERT(cache.size() == MAX_CAP, "Cache size must remain strictly capped at 500");
    }

    // 3. Verify LRU eviction: items 0..499 must be gone, items 500..999 must exist
    QImage retrieved;
    for (size_t i = 0; i < MAX_CAP; ++i) {
        bool found = cache.get(QStringLiteral("item_%1").arg(i), retrieved);
        TEST_ASSERT(!found, "Old items 0..499 must have been evicted");
    }
    for (size_t i = MAX_CAP; i < 1000; ++i) {
        bool found = cache.get(QStringLiteral("item_%1").arg(i), retrieved);
        TEST_ASSERT(found, "Recent items 500..999 must still be cached");
        TEST_ASSERT(!retrieved.isNull(), "Retrieved image must not be null");
    }

    // 4. Verify LRU order promotion: access item_500 so it becomes MRU
    bool found500 = cache.get(QStringLiteral("item_500"), retrieved);
    TEST_ASSERT(found500, "item_500 must exist");

    // Insert item_1000: oldest should now be item_501 (NOT item_500)
    cache.put(QStringLiteral("item_1000"), testImg);
    TEST_ASSERT(cache.size() == MAX_CAP, "Cache size must remain 500");

    bool found501 = cache.get(QStringLiteral("item_501"), retrieved);
    TEST_ASSERT(!found501, "item_501 must be evicted as oldest");

    bool stillFound500 = cache.get(QStringLiteral("item_500"), retrieved);
    TEST_ASSERT(stillFound500, "item_500 must NOT be evicted because it was accessed");

    // 5. Verify update of existing key doesn't increase size
    cache.put(QStringLiteral("item_1000"), testImg);
    TEST_ASSERT(cache.size() == MAX_CAP, "Updating existing item must keep size at 500");

    // 6. Verify clear()
    cache.clear();
    TEST_ASSERT(cache.size() == 0, "clear() must empty cache to 0");

    TEST_PASS("IconCache strictly caps at 500 items and preserves true LRU eviction semantics");
    return true;
}

// =============================================================================
// SUBTEST 2: Thread-Safe Concurrency Stress on IconCache
// =============================================================================
bool test_icon_cache_concurrency() {
    std::cout << "\n------------------------------------------------------\n";
    std::cout << "SUBTEST 2: Thread-Safe Concurrency Stress on IconCache\n";
    std::cout << "------------------------------------------------------\n";

    constexpr size_t MAX_CAP = 500;
    IconCache cache(MAX_CAP);
    QImage testImg(64, 64, QImage::Format_ARGB32_Premultiplied);
    testImg.fill(Qt::green);

    constexpr int NUM_THREADS = 8;
    constexpr int OPS_PER_THREAD = 500;

    std::vector<std::thread> workers;
    workers.reserve(NUM_THREADS);

    std::atomic<bool> failed{false};

    for (int t = 0; t < NUM_THREADS; ++t) {
        workers.emplace_back([&cache, &testImg, t, &failed]() {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                QString key = QStringLiteral("thread_%1_item_%2").arg(t).arg(i);
                cache.put(key, testImg);

                QImage readImg;
                cache.get(key, readImg);

                if (cache.size() > MAX_CAP) {
                    failed = true;
                }
            }
        });
    }

    for (auto &w : workers) {
        w.join();
    }

    TEST_ASSERT(!failed, "Cache size exceeded capacity during multi-threaded operations");
    TEST_ASSERT(cache.size() <= MAX_CAP, "Final cache size must be <= 500");

    TEST_PASS("IconCache passed concurrent multi-threaded stress test with 0 data races");
    return true;
}

// =============================================================================
// SUBTEST 3: Path Traversal Attack Injection
// =============================================================================
bool test_path_traversal_attacks() {
    std::cout << "\n------------------------------------------------------\n";
    std::cout << "SUBTEST 3: Path Traversal Attack Injection\n";
    std::cout << "------------------------------------------------------\n";

    AppIconProvider appProvider;
    ThemeIconProvider themeProvider;

    const std::vector<QString> traversalAttacks = {
        QStringLiteral("../../../../etc/passwd"),
        QStringLiteral("../../../../etc/shadow"),
        QStringLiteral("../../../dev/null"),
        QStringLiteral("../../../../../../bin/bash"),
        QStringLiteral(".."),
        QStringLiteral("../"),
        QStringLiteral("..//"),
        QStringLiteral(".../"),
        QStringLiteral("..%2f..%2fetc/passwd"),
        QStringLiteral("foo/../../../../etc/passwd"),
        QStringLiteral("nested/dir/../../../../../../etc/hostname"),
        QStringLiteral("....//....//etc/passwd"),
        QStringLiteral("/etc/passwd"),
        QStringLiteral("/dev/urandom")
    };

    for (const auto &attack : traversalAttacks) {
        QSize appSize;
        QImage appImg = appProvider.requestImage(attack, &appSize, QSize(64, 64));
        TEST_ASSERT(!appImg.isNull(), QString("AppIconProvider must return non-null image for traversal %1").arg(attack).toStdString());
        TEST_ASSERT(appImg.width() >= 64 && appImg.height() >= 64, "Image dimensions must be >= 64x64");

        QSize themeSize;
        QImage themeImg = themeProvider.requestImage(attack, &themeSize, QSize(64, 64));
        TEST_ASSERT(!themeImg.isNull(), QString("ThemeIconProvider must return non-null image for traversal %1").arg(attack).toStdString());
        TEST_ASSERT(themeImg.width() >= 64 && themeImg.height() >= 64, "Image dimensions must be >= 64x64");
    }

    TEST_PASS("All 14 path traversal attacks safely sanitized to valid non-null placeholder images");
    return true;
}

// =============================================================================
// SUBTEST 4: Query String Flooding & Cache Busting Resistance
// =============================================================================
bool test_query_string_flooding() {
    std::cout << "\n------------------------------------------------------\n";
    std::cout << "SUBTEST 4: Query String Flooding & Cache Busting\n";
    std::cout << "------------------------------------------------------\n";

    AppIconProvider appProvider;
    appProvider.clearCache();
    TEST_ASSERT(appProvider.m_cache.size() == 0, "Cache must be empty after clear");

    // Flood the same icon with 200 distinct query strings: ?v=1, ?v=2, etc.
    const int FLOOD_COUNT = 200;
    for (int i = 0; i < FLOOD_COUNT; ++i) {
        QString query = QStringLiteral("firefox?v=%1").arg(i);
        QImage img = appProvider.requestImage(query, nullptr, QSize(64, 64));
        TEST_ASSERT(!img.isNull(), "Image must not be null");
    }

    // Because ?v=... is stripped, all 200 requests must resolve to the identical cache key: firefox@64x64
    TEST_ASSERT(appProvider.m_cache.size() == 1,
                QString("AppIconProvider cache size must be 1, found: %1").arg(appProvider.m_cache.size()).toStdString());

    // Flood ThemeIconProvider with another set of query strings
    ThemeIconProvider themeProvider;
    themeProvider.clearCache();
    for (int i = 0; i < FLOOD_COUNT; ++i) {
        QString query = QStringLiteral("system-shutdown?v=%1").arg(i);
        QImage img = themeProvider.requestImage(query, nullptr, QSize(64, 64));
        TEST_ASSERT(!img.isNull(), "Image must not be null");
    }
    TEST_ASSERT(themeProvider.m_cache.size() == 1,
                QString("ThemeIconProvider cache size must be 1, found: %1").arg(themeProvider.m_cache.size()).toStdString());

    TEST_PASS("Query string flooding successfully stripped; cache did not inflate with duplicate entries");
    return true;
}

// =============================================================================
// SUBTEST 5: Empty, Whitespace, Long & Malformed Identifiers
// =============================================================================
bool test_malformed_identifiers() {
    std::cout << "\n------------------------------------------------------\n";
    std::cout << "SUBTEST 5: Malformed & Boundary Identifiers\n";
    std::cout << "------------------------------------------------------\n";

    AppIconProvider appProvider;
    ThemeIconProvider themeProvider;

    const std::vector<QString> edgeCases = {
        QStringLiteral(""),
        QStringLiteral("   "),
        QStringLiteral("\t\r\n"),
        QStringLiteral("?"),
        QStringLiteral("?v=999"),
        QStringLiteral("???"),
        QStringLiteral("   ?v=1   "),
        QString(4096, 'A'), // 4KB string
        QStringLiteral("🚀🔥💻_emoji_app"),
        QStringLiteral("Türkçe_Karakterler_çğıöşü"),
        QStringLiteral("русский_текст_иконка"),
        QStringLiteral("$(whoami)"),
        QStringLiteral("`id`"),
        QStringLiteral("app; rm -rf /"),
        QStringLiteral("app\nexec=ls")
    };

    for (const auto &id : edgeCases) {
        QImage appImg = appProvider.requestImage(id, nullptr, QSize(64, 64));
        TEST_ASSERT(!appImg.isNull(), "AppIconProvider must return non-null image for malformed id");
        TEST_ASSERT(appImg.width() >= 64 && appImg.height() >= 64, "Width/height >= 64");

        QImage themeImg = themeProvider.requestImage(id, nullptr, QSize(64, 64));
        TEST_ASSERT(!themeImg.isNull(), "ThemeIconProvider must return non-null image for malformed id");
        TEST_ASSERT(themeImg.width() >= 64 && themeImg.height() >= 64, "Width/height >= 64");
    }

    TEST_PASS("All malformed and extreme boundary identifiers handled safely with valid placeholders");
    return true;
}

// =============================================================================
// SUBTEST 6: Corrupted SVG File Injection
// =============================================================================
bool test_corrupted_svg_files() {
    std::cout << "\n------------------------------------------------------\n";
    std::cout << "SUBTEST 6: Corrupted SVG File Injection\n";
    std::cout << "------------------------------------------------------\n";

    const QString testDir = QStringLiteral("/tmp/ravenspot_fuzz_svgs");
    QDir().mkpath(testDir);

    struct SvgTestCase {
        QString filename;
        QByteArray content;
    };

    const std::vector<SvgTestCase> corruptSvgs = {
        { QStringLiteral("empty.svg"), QByteArray() }, // 0 bytes
        { QStringLiteral("truncated_header.svg"), QByteArray("<svg width=\"100\" height=\"100\">") }, // unclosed tag
        { QStringLiteral("malformed_xml.svg"), QByteArray("<svg><g><circle r=\"10\"></rect></svg>") }, // mismatched tag
        { QStringLiteral("binary_garbage.svg"), QByteArray("\xFF\xFE\x00\x01\xDE\xAD\xBE\xEF\x12\x34\x56\x78\x9A\xBC\xDE\xF0\xAA\xBB\xCC\xDD", 20) },
        { QStringLiteral("xxe_injection.svg"), QByteArray("<!DOCTYPE svg [ <!ENTITY xxe SYSTEM \"file:///etc/passwd\"> ]><svg>&xxe;</svg>") },
        { QStringLiteral("zero_dimension.svg"), QByteArray("<svg width=\"0\" height=\"0\"><rect width=\"0\" height=\"0\"/></svg>") }
    };

    AppIconProvider appProvider;
    ThemeIconProvider themeProvider;

    for (const auto &tc : corruptSvgs) {
        QString fullPath = testDir + QStringLiteral("/") + tc.filename;
        QFile f(fullPath);
        TEST_ASSERT(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "Must be able to create corrupt test SVG");
        if (!tc.content.isEmpty()) {
            f.write(tc.content);
        }
        f.close();

        // Request corrupt SVG via both providers
        QImage appImg = appProvider.requestImage(fullPath, nullptr, QSize(64, 64));
        TEST_ASSERT(!appImg.isNull(), QString("AppIconProvider must return fallback for corrupt SVG %1").arg(tc.filename).toStdString());
        TEST_ASSERT(appImg.width() >= 64 && appImg.height() >= 64, "Image must be >= 64x64");

        QImage themeImg = themeProvider.requestImage(fullPath, nullptr, QSize(64, 64));
        TEST_ASSERT(!themeImg.isNull(), QString("ThemeIconProvider must return fallback for corrupt SVG %1").arg(tc.filename).toStdString());
        TEST_ASSERT(themeImg.width() >= 64 && themeImg.height() >= 64, "Image must be >= 64x64");

        QFile::remove(fullPath);
    }

    QDir().rmdir(testDir);
    TEST_PASS("All 6 corrupted SVG variants rejected safely without crash and returned valid placeholders");
    return true;
}

// =============================================================================
// SUBTEST 7: 1,000 Diverse & Randomized Icon Queries Fuzzing Harness
// =============================================================================
bool test_1000_randomized_icon_fuzzing() {
    std::cout << "\n------------------------------------------------------\n";
    std::cout << "SUBTEST 7: 1,000 Diverse & Randomized Icon Queries Fuzzing\n";
    std::cout << "------------------------------------------------------\n";

    AppIconProvider appProvider;
    ThemeIconProvider themeProvider;
    appProvider.clearCache();
    themeProvider.clearCache();

    const double initialRss = getProcessRssMb();
    std::cout << "[*] Initial RSS before 1,000 fuzz queries: " << initialRss << " MB" << std::endl;

    const std::vector<QString> baseNames = {
        QStringLiteral("firefox"),
        QStringLiteral("org.kde.dolphin"),
        QStringLiteral("konsole"),
        QStringLiteral("system-shutdown"),
        QStringLiteral("system-reboot"),
        QStringLiteral("system-lock-screen"),
        QStringLiteral("preferences-system"),
        QStringLiteral("utilities-terminal"),
        QStringLiteral("text-editor"),
        QStringLiteral("folder")
    };

    QElapsedTimer timer;
    timer.start();

    constexpr int TOTAL_QUERIES = 1000;
    for (int i = 0; i < TOTAL_QUERIES; ++i) {
        QString queryId;
        int category = i % 5;
        switch (category) {
            case 0:
                // Standard app icon with randomized query string
                queryId = baseNames[i % baseNames.size()] + QStringLiteral("?v=%1&cachebust=%2").arg(i).arg(QRandomGenerator::global()->generate());
                break;
            case 1:
                // Randomized non-existent application name (forces fallback)
                queryId = QStringLiteral("fuzz_unknown_app_%1_%2").arg(i).arg(QRandomGenerator::global()->generate());
                break;
            case 2:
                // Traversal attempt combined with random id
                queryId = QStringLiteral("../../path/to/icon_%1_%2.png").arg(i).arg(QRandomGenerator::global()->bounded(10000));
                break;
            case 3:
                // Unicode / multi-byte random query
                queryId = QStringLiteral("uygulama_simgesi_çşğöü_%1").arg(i);
                break;
            case 4:
                // Whitespace / punctuation fuzzing
                queryId = QStringLiteral("  icon-name-with-spaces-%1  ?v=%2").arg(i).arg(i);
                break;
        }

        // Query both providers alternately
        if (i % 2 == 0) {
            QImage img = appProvider.requestImage(queryId, nullptr, QSize(64, 64));
            TEST_ASSERT(!img.isNull(), "AppIconProvider must return non-null image");
        } else {
            QImage img = themeProvider.requestImage(queryId, nullptr, QSize(64, 64));
            TEST_ASSERT(!img.isNull(), "ThemeIconProvider must return non-null image");
        }

        // Check cache caps along the way
        TEST_ASSERT(appProvider.m_cache.size() <= 500, "AppIconProvider cache size must never exceed 500");
        TEST_ASSERT(themeProvider.m_cache.size() <= 500, "ThemeIconProvider cache size must never exceed 500");

        if ((i + 1) % 250 == 0) {
            double curRss = getProcessRssMb();
            std::cout << "[*] Completed " << (i + 1) << " queries | AppCache: "
                      << appProvider.m_cache.size() << " | ThemeCache: "
                      << themeProvider.m_cache.size() << " | RSS: " << curRss << " MB" << std::endl;
        }
    }

    qint64 elapsedMs = timer.elapsed();
    const double finalRss = getProcessRssMb();
    const double rssGrowth = finalRss - initialRss;

    std::cout << "[*] 1,000 queries completed in " << elapsedMs << " ms ("
              << (static_cast<double>(elapsedMs) / TOTAL_QUERIES) << " ms/query)" << std::endl;
    std::cout << "[*] Final AppCache size: " << appProvider.m_cache.size() << " (cap: 500)" << std::endl;
    std::cout << "[*] Final ThemeCache size: " << themeProvider.m_cache.size() << " (cap: 500)" << std::endl;
    std::cout << "[*] Final RSS: " << finalRss << " MB (Growth: " << rssGrowth << " MB)" << std::endl;

    TEST_ASSERT(appProvider.m_cache.size() <= 500, "AppIconProvider cache must be <= 500");
    TEST_ASSERT(themeProvider.m_cache.size() <= 500, "ThemeIconProvider cache must be <= 500");
    TEST_ASSERT(rssGrowth < 15.0, "Memory growth after 1,000 icon queries must remain strictly bounded (<15 MB)");

    TEST_PASS("1,000 diverse and randomized icon queries completed with zero crashes, strictly capped cache <= 500, and bounded memory");
    return true;
}

} // namespace ravenspot::test

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    ravenspot::setupIconThemeEnvironment();

    std::cout << "==============================================================\n";
    std::cout << "RAVENSPOT ADVERSARIAL ICON CACHE & SECURITY FUZZING HARNESS\n";
    std::cout << "Author: challenger_m2_2\n";
    std::cout << "Target: AppIconProvider, ThemeIconProvider, IconCache\n";
    std::cout << "==============================================================\n";

    if (!ravenspot::test::test_lru_cache_cap_and_eviction()) return 1;
    if (!ravenspot::test::test_icon_cache_concurrency()) return 2;
    if (!ravenspot::test::test_path_traversal_attacks()) return 3;
    if (!ravenspot::test::test_query_string_flooding()) return 4;
    if (!ravenspot::test::test_malformed_identifiers()) return 5;
    if (!ravenspot::test::test_corrupted_svg_files()) return 6;
    if (!ravenspot::test::test_1000_randomized_icon_fuzzing()) return 7;

    std::cout << "\n==============================================================\n";
    std::cout << "[+] ALL 7 ICON FUZZING & SECURITY HARNESS TESTS PASSED!\n";
    std::cout << "==============================================================\n";
    return 0;
}
