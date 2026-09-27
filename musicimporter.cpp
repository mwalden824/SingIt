#include "musicimporter.h"

#include <sqlite3.h>

#include <QDebug>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>

MusicImporter::MusicImporter(
    MusicDatabase& musicDatabase,
    const std::string& sourceDatabasePath,
    const std::string& csvPath,
    const std::string& musicDirectory,
    SimSearchModel& model
    )
    : musicDatabase(musicDatabase),
    sourceDatabasePath(sourceDatabasePath),
    csvPath(csvPath),
    musicDirectory(musicDirectory),
    model(model)
{
}

MusicImporter::~MusicImporter()
{
    closeSourceDatabase();
}

// ------------------------------------------------------------
// Main import
// ------------------------------------------------------------

void MusicImporter::importAll(
    const std::string& logPath)
{
    logFile.open(logPath);

    if (!logFile.is_open())
    {
        throw std::runtime_error(
            "Could not open import log file: " +
            logPath
            );
    }

    try
    {
        std::vector<CsvTrack> csvTracks =
            loadCsvTracks();

        qDebug()
            << "Loaded"
            << csvTracks.size()
            << "CSV tracks.";

        openSourceDatabase();

        size_t importedCount = 0;
        size_t skippedCount = 0;

        for (
            const auto& entry :
            std::filesystem::directory_iterator(
                musicDirectory
                ))
        {
            if (!entry.is_regular_file())
                continue;

            const std::string filename =
                entry.path().filename().string();

            const std::string extension =
                toLower(
                    entry.path().extension().string()
                    );

            if (extension != ".mp3")
                continue;

            const int fileNumber =
                extractFileNumber(filename);

            if (fileNumber < 0)
            {
                writeLog(
                    "INVALID_FILENAME",
                    -1,
                    filename,
                    "",
                    "",
                    0
                    );

                ++skippedCount;
                continue;
            }

            /*
             * File names are expected to look like:
             *
             * 00001-something.mp3
             * 00002-something.mp3
             * etc.
             *
             * extractFileNumber() converts 00001 -> 0.
             */
            const size_t csvIndex =
                static_cast<size_t>(
                    fileNumber
                    );

            if (csvIndex >= csvTracks.size())
            {
                writeLog(
                    "CSV_INDEX_OUT_OF_RANGE",
                    fileNumber,
                    filename,
                    "",
                    "",
                    0
                    );

                ++skippedCount;
                continue;
            }

            const CsvTrack& track =
                csvTracks[csvIndex];

            qDebug()
                << "Processing:"
                << QString::fromStdString(filename)
                << "-"
                << QString::fromStdString(track.trackName)
                << "-"
                << QString::fromStdString(track.artistName);

            LyricsResult lyricsResult =
                findSyncedLyrics(
                    track.trackName,
                    track.artistName
                    );

            if (lyricsResult.syncedLyrics.empty())
            {
                writeLog(
                    "NO_LYRICS",
                    fileNumber,
                    filename,
                    track.trackName,
                    track.artistName,
                    0
                    );

                ++skippedCount;
                continue;
            }

            const int64_t trackDurationMs =
                static_cast<int64_t>(
                    std::llround(
                        lyricsResult.durationSeconds *
                        1000.0
                        )
                    );

            std::vector<ParsedLyric> lyrics =
                parseSyncedLyrics(
                    lyricsResult.syncedLyrics,
                    trackDurationMs
                    );

            if (lyrics.empty())
            {
                writeLog(
                    "NO_PARSED_LYRICS",
                    fileNumber,
                    filename,
                    track.trackName,
                    track.artistName,
                    0
                    );

                ++skippedCount;
                continue;
            }

            const int songId =
                musicDatabase.addSong(
                    track.trackName,
                    track.artistName,
                    filename,
                    fileNumber
                    );

            for (const ParsedLyric& lyric : lyrics)
            {
                const int lyricId =
                    musicDatabase.addLyric(
                        songId,
                        lyric.startTimeMs,
                        lyric.stopTimeMs,
                        lyric.text
                        );

                std::vector<float> embedding =
                    createEmbedding(
                        lyric.text
                        );

                if (embedding.empty())
                {
                    throw std::runtime_error(
                        "Embedding vector is empty."
                        );
                }

                musicDatabase.addEmbedding(
                    lyricId,
                    embedding
                    );
            }

            writeLog(
                "SUCCESS",
                fileNumber,
                filename,
                track.trackName,
                track.artistName,
                lyrics.size()
                );

            ++importedCount;
        }

        qDebug()
            << "Import complete.";

        qDebug()
            << "Imported:"
            << importedCount;

        qDebug()
            << "Skipped:"
            << skippedCount;
    }
    catch (...)
    {
        closeSourceDatabase();

        if (logFile.is_open())
            logFile.close();

        throw;
    }

    closeSourceDatabase();

    if (logFile.is_open())
        logFile.close();
}

// ------------------------------------------------------------
// CSV
// ------------------------------------------------------------

std::vector<CsvTrack>
MusicImporter::loadCsvTracks() const
{
    std::ifstream file(csvPath);

    if (!file.is_open())
    {
        throw std::runtime_error(
            "Could not open CSV file: " +
            csvPath
            );
    }

    std::vector<CsvTrack> tracks;

    std::string line;

    // Skip header.
    if (!std::getline(file, line))
    {
        return tracks;
    }

    while (std::getline(file, line))
    {
        if (trim(line).empty())
            continue;

        std::vector<std::string> fields =
            parseCsvLine(line);

        if (fields.size() < 2)
            continue;

        CsvTrack track;

        track.trackName =
            trim(fields[0]);

        track.artistName =
            trim(fields[1]);

        tracks.push_back(track);
    }

    return tracks;
}

std::vector<std::string>
MusicImporter::parseCsvLine(
    const std::string& line)
{
    std::vector<std::string> fields;

    std::string current;

    bool insideQuotes = false;

    for (size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];

        if (c == '"')
        {
            if (
                insideQuotes &&
                i + 1 < line.size() &&
                line[i + 1] == '"'
                )
            {
                current += '"';
                ++i;
            }
            else
            {
                insideQuotes = !insideQuotes;
            }
        }
        else if (
            c == ',' &&
            !insideQuotes
            )
        {
            fields.push_back(current);
            current.clear();
        }
        else
        {
            current += c;
        }
    }

    fields.push_back(current);

    return fields;
}

// ------------------------------------------------------------
// Files
// ------------------------------------------------------------

int MusicImporter::extractFileNumber(
    const std::string& filename)
{
    /*
     * Expected:
     *
     * 00001-...
     * 00002-...
     *
     * Convert:
     *
     * 00001 -> 0
     * 00002 -> 1
     */

    static const std::regex pattern(
        R"(^(\d{5})-)"
        );

    std::smatch match;

    if (!std::regex_search(
            filename,
            match,
            pattern
            ))
    {
        return -1;
    }

    try
    {
        return std::stoi(
                   match[1].str()
                   ) - 1;
    }
    catch (...)
    {
        return -1;
    }
}

// ------------------------------------------------------------
// LRCLIB database
// ------------------------------------------------------------

void MusicImporter::openSourceDatabase()
{
    if (sourceDb != nullptr)
        return;

    const int result =
        sqlite3_open(
            sourceDatabasePath.c_str(),
            &sourceDb
            );

    if (result != SQLITE_OK)
    {
        std::string error =
            sourceDb != nullptr
                ? sqlite3_errmsg(sourceDb)
                : "Unknown SQLite error";

        closeSourceDatabase();

        throw std::runtime_error(
            "Could not open source database: " +
            error
            );
    }
}

void MusicImporter::closeSourceDatabase()
{
    if (sourceDb != nullptr)
    {
        sqlite3_close(sourceDb);
        sourceDb = nullptr;
    }
}

LyricsResult
MusicImporter::findSyncedLyrics(
    const std::string& trackName,
    const std::string& artistName) const
{
    if (sourceDb == nullptr)
    {
        throw std::runtime_error(
            "Source database is not open."
            );
    }

    LyricsResult result;

    result.durationSeconds = 0.0;

    const char* sql =
        "SELECT syncedLyrics, duration "
        "FROM tracks "
        "WHERE name = ? "
        "AND artist = ? "
        "LIMIT 1;";

    sqlite3_stmt* statement = nullptr;

    int rc =
        sqlite3_prepare_v2(
            sourceDb,
            sql,
            -1,
            &statement,
            nullptr
            );

    if (rc != SQLITE_OK)
    {
        throw std::runtime_error(
            "Could not prepare LRCLIB query: " +
            std::string(
                sqlite3_errmsg(sourceDb)
                )
            );
    }

    sqlite3_bind_text(
        statement,
        1,
        trackName.c_str(),
        -1,
        SQLITE_TRANSIENT
        );

    sqlite3_bind_text(
        statement,
        2,
        artistName.c_str(),
        -1,
        SQLITE_TRANSIENT
        );

    rc =
        sqlite3_step(statement);

    if (rc == SQLITE_ROW)
    {
        const unsigned char* lyrics =
            sqlite3_column_text(
                statement,
                0
                );

        if (lyrics != nullptr)
        {
            result.syncedLyrics =
                reinterpret_cast<
                    const char*
                    >(lyrics);
        }

        result.durationSeconds =
            sqlite3_column_double(
                statement,
                1
                );
    }
    else if (rc != SQLITE_DONE)
    {
        std::string error =
            sqlite3_errmsg(sourceDb);

        sqlite3_finalize(statement);

        throw std::runtime_error(
            "Error executing LRCLIB query: " +
            error
            );
    }

    sqlite3_finalize(statement);

    return result;
}

// ------------------------------------------------------------
// Lyrics
// ------------------------------------------------------------

int64_t MusicImporter::parseTimestampMs(
    const std::string& timestamp)
{
    /*
     * Expected:
     *
     * [mm:ss.xx]
     *
     * Examples:
     *
     * [00:12.34]
     * [03:45.67]
     */

    static const std::regex pattern(
        R"(\[(\d+):(\d+(?:\.\d+)?)\])"
        );

    std::smatch match;

    if (!std::regex_match(
            timestamp,
            match,
            pattern
            ))
    {
        return -1;
    }

    try
    {
        const int64_t minutes =
            std::stoll(
                match[1].str()
                );

        const double seconds =
            std::stod(
                match[2].str()
                );

        return
            minutes * 60'000 +
            static_cast<int64_t>(
                std::llround(
                    seconds * 1000.0
                    )
                );
    }
    catch (...)
    {
        return -1;
    }
}

std::vector<ParsedLyric>
MusicImporter::parseSyncedLyrics(
    const std::string& syncedLyrics,
    int64_t trackDurationMs)
{
    std::vector<ParsedLyric> result;

    std::istringstream stream(
        syncedLyrics
        );

    std::string line;

    struct TimestampedLine
    {
        int64_t startTimeMs;
        std::string text;
    };

    std::vector<TimestampedLine> lines;

    while (std::getline(stream, line))
    {
        line = trim(line);

        if (line.empty())
            continue;

        /*
         * A line can contain multiple timestamps:
         *
         * [00:10.00][00:20.00]Some lyric
         *
         * Extract all timestamps from the
         * beginning of the line.
         */

        static const std::regex timestampPattern(
            R"(\[(\d+:\d+(?:\.\d+)?)\])"
            );

        std::sregex_iterator begin(
            line.begin(),
            line.end(),
            timestampPattern
            );

        std::sregex_iterator end;

        if (begin == end)
            continue;

        std::string lyricText;

        const std::smatch& firstMatch =
            *begin;

        lyricText =
            line.substr(
                firstMatch.position() +
                firstMatch.length()
                );

        lyricText =
            trim(lyricText);

        for (
            auto it = begin;
            it != end;
            ++it)
        {
            const std::string timestamp =
                (*it)[1].str();

            const int64_t startTimeMs =
                parseTimestampMs(
                    timestamp
                    );

            if (startTimeMs < 0)
                continue;

            lines.push_back(
                {
                    startTimeMs,
                    lyricText
                }
                );
        }
    }

    std::sort(
        lines.begin(),
        lines.end(),
        [](const TimestampedLine& a,
           const TimestampedLine& b)
        {
            return
                a.startTimeMs <
                b.startTimeMs;
        }
        );

    for (size_t i = 0;
         i < lines.size();
         ++i)
    {
        const int64_t startTimeMs =
            lines[i].startTimeMs;

        int64_t stopTimeMs;

        if (i + 1 < lines.size())
        {
            stopTimeMs =
                lines[i + 1].startTimeMs;
        }
        else
        {
            stopTimeMs =
                trackDurationMs;
        }

        if (stopTimeMs <= startTimeMs)
            continue;

        ParsedLyric lyric;

        lyric.startTimeMs =
            startTimeMs;

        lyric.stopTimeMs =
            stopTimeMs;

        lyric.text =
            lines[i].text;

        if (trim(lyric.text).empty())
            continue;

        result.push_back(
            lyric
            );
    }

    return result;
}

// ------------------------------------------------------------
// Embeddings
// ------------------------------------------------------------

std::vector<float>
MusicImporter::createEmbedding(
    const std::string& lyricText)
{
    /*
     * SimSearchModel owns the tokenizer and
     * ONNX Runtime session.
     *
     * MusicImporter should not access those
     * implementation details directly.
     */

    std::vector<int32_t> tokenIds =
        model.tokenizeString(
            lyricText
            );

    return model.calculateEmbeddingVector(
        tokenIds
        );
}

// ------------------------------------------------------------
// Logging
// ------------------------------------------------------------

void MusicImporter::writeLog(
    const std::string& status,
    int fileNumber,
    const std::string& filename,
    const std::string& trackName,
    const std::string& artistName,
    size_t lyricCount)
{
    if (!logFile.is_open())
        return;

    const auto now =
        std::chrono::system_clock::now();

    const std::time_t time =
        std::chrono::system_clock::to_time_t(
            now
            );

    std::tm localTime{};

#ifdef _WIN32
    localtime_s(
        &localTime,
        &time
        );
#else
    localtime_r(
        &time,
        &localTime
        );
#endif

    logFile
        << std::put_time(
               &localTime,
               "%Y-%m-%d %H:%M:%S"
               )
        << " | "
        << status
        << " | FileNumber="
        << fileNumber
        << " | Filename="
        << filename
        << " | Track="
        << trackName
        << " | Artist="
        << artistName
        << " | Lyrics="
        << lyricCount
        << '\n';

    logFile.flush();
}

// ------------------------------------------------------------
// Utility
// ------------------------------------------------------------

std::string MusicImporter::toLower(
    const std::string& value)
{
    std::string result =
        value;

    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(
                std::tolower(c)
                );
        }
        );

    return result;
}

std::string MusicImporter::trim(
    const std::string& value)
{
    const auto first =
        std::find_if(
            value.begin(),
            value.end(),
            [](unsigned char c)
            {
                return !std::isspace(c);
            }
            );

    if (first == value.end())
        return {};

    const auto last =
        std::find_if(
            value.rbegin(),
            value.rend(),
            [](unsigned char c)
            {
                return !std::isspace(c);
            }
            ).base();

    return std::string(
        first,
        last
        );
}