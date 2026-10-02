#ifndef MUSICIMPORTER_H
#define MUSICIMPORTER_H

#include "musicdatabase.h"
#include "simsearchmodel.h"

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <atomic>
#include <functional>

// Forward declaration of SQLite connection type.
struct sqlite3;


struct Mp3Metadata
{
    std::string trackName;
    std::string artistName;
};


struct LyricsResult
{
    std::string syncedLyrics;
    double durationSeconds;
};


struct ParsedLyric
{
    int64_t startTimeMs;
    int64_t stopTimeMs;
    std::string text;
};


class MusicImporter
{
public:
    using ProgressCallback =
        std::function<void(
            int current,
            int total,
            const std::string& filename)>;

    MusicImporter(
        MusicDatabase& musicDatabase,
        const std::string& sourceDatabasePath,
        const std::string& musicDirectory,
        SimSearchModel& model,
        ProgressCallback progressCallback = {}
        );

    ~MusicImporter();


    MusicImporter(const MusicImporter&) = delete;

    MusicImporter& operator=(
        const MusicImporter&
        ) = delete;


    int getImportedCount() const;
    int getSkippedCount() const;

    void cancel();
    bool isCancelled() const;

    void importAll(
        const std::string& logPath
        );

private:
    std::atomic<bool> cancelled{false};
    ProgressCallback progressCallback;

    int importedCount = 0;
    int skippedCount = 0;

    MusicDatabase& musicDatabase;

    std::string sourceDatabasePath;

    std::string musicDirectory;

    SimSearchModel& model;


    sqlite3* sourceDb = nullptr;

    std::ofstream logFile;


    // --------------------------------------------------------
    // MP3 metadata
    // --------------------------------------------------------

    Mp3Metadata readMp3Metadata(
        const std::string& filename
        );


    // --------------------------------------------------------
    // LRCLIB database
    // --------------------------------------------------------

    void openSourceDatabase();

    void closeSourceDatabase();


    LyricsResult findSyncedLyrics(
        const std::string& trackName,
        const std::string& artistName
        ) const;


    // --------------------------------------------------------
    // Lyrics
    // --------------------------------------------------------

    static int64_t parseTimestampMs(
        const std::string& timestamp
        );


    static std::vector<ParsedLyric> parseSyncedLyrics(
        const std::string& syncedLyrics,
        int64_t trackDurationMs
        );


    // --------------------------------------------------------
    // Embeddings
    // --------------------------------------------------------

    std::vector<float> createEmbedding(
        const std::string& lyricText
        );


    // --------------------------------------------------------
    // Logging
    // --------------------------------------------------------

    void writeLog(
        const std::string& status,
        const std::string& filename,
        const std::string& trackName,
        const std::string& artistName,
        size_t lyricCount = 0
        );


    // --------------------------------------------------------
    // Utility
    // --------------------------------------------------------

    static std::string toLower(
        const std::string& value
        );


    static std::string trim(
        const std::string& value
        );
};

#endif // MUSICIMPORTER_H