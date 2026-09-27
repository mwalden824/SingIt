#ifndef MUSICIMPORTER_H
#define MUSICIMPORTER_H

#include "musicdatabase.h"
#include "mainwindow.h"

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>

    // Forward declaration of SQLite connection type.
    struct sqlite3;

struct CsvTrack
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
    MusicImporter(
        MusicDatabase& musicDatabase,
        const std::string& sourceDatabasePath,
        const std::string& csvPath,
        const std::string& musicDirectory,
        ModelContext& model
        );

    ~MusicImporter();

    MusicImporter(const MusicImporter&) = delete;
    MusicImporter& operator=(const MusicImporter&) = delete;

    void importAll(const std::string& logPath);

private:
    MusicDatabase& musicDatabase;

    std::string sourceDatabasePath;
    std::string csvPath;
    std::string musicDirectory;

    ModelContext& model;

    sqlite3* sourceDb = nullptr;
    std::ofstream logFile;

    // CSV
    std::vector<CsvTrack> loadCsvTracks() const;
    static std::vector<std::string> parseCsvLine(
        const std::string& line);

    // Files
    static int extractFileNumber(
        const std::string& filename);

    // LRCLIB database
    void openSourceDatabase();
    void closeSourceDatabase();

    LyricsResult findSyncedLyrics(
        const std::string& trackName,
        const std::string& artistName) const;

    // Lyrics
    static int64_t parseTimestampMs(
        const std::string& timestamp);

    static std::vector<ParsedLyric> parseSyncedLyrics(
        const std::string& syncedLyrics,
        int64_t trackDurationMs);

    // Embeddings
    std::vector<float> createEmbedding(
        const std::string& lyricText);

    // Logging
    void writeLog(
        const std::string& status,
        int fileNumber,
        const std::string& filename,
        const std::string& trackName,
        const std::string& artistName,
        size_t lyricCount = 0);

    // Utility
    static std::string toLower(
        const std::string& value);

    static std::string trim(
        const std::string& value);
};

#endif // MUSICIMPORTER_H
