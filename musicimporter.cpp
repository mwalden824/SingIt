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


// ------------------------------------------------------------
// Constructor / Destructor
// ------------------------------------------------------------

MusicImporter::MusicImporter(
    MusicDatabase& musicDatabase,
    const std::string& sourceDatabasePath,
    const std::string& csvPath,
    const std::string& musicDirectory,
    ModelContext& model)
    :
    musicDatabase(musicDatabase),
    sourceDatabasePath(sourceDatabasePath),
    csvPath(csvPath),
    musicDirectory(musicDirectory),
    model(model)
{
}

MusicImporter::~MusicImporter()
{
    closeSourceDatabase();

    if (logFile.is_open())
        logFile.close();
}


// ------------------------------------------------------------
// Main import function
// ------------------------------------------------------------

void MusicImporter::importAll(
    const std::string& logPath)
{
    qDebug() << "Starting music import...";

    // Open log file in append mode so previous import history
    // is preserved.
    logFile.open(
        logPath,
        std::ios::out | std::ios::app);

    if (!logFile)
    {
        throw std::runtime_error(
            "Could not open import log: " + logPath);
    }

    // Load Spotify CSV.
    std::vector<CsvTrack> csvTracks =
        loadCsvTracks();

    qDebug()
        << "Loaded"
        << csvTracks.size()
        << "tracks from CSV.";

    // Open source LRCLIB database.
    openSourceDatabase();

    size_t processedCount = 0;
    size_t noLyricsCount = 0;
    size_t skippedCount = 0;

    try
    {
        for (const auto& entry :
             std::filesystem::directory_iterator(musicDirectory))
        {
            if (!entry.is_regular_file())
                continue;

            const std::string filename =
                entry.path().filename().string();

            const std::string extension =
                entry.path().extension().string();

            if (extension != ".mp3" &&
                extension != ".MP3")
            {
                continue;
            }

            qDebug()
                << ""
                << "Processing:"
                << QString::fromStdString(filename);

            // ------------------------------------------------
            // Extract five-digit CSV number.
            // ------------------------------------------------

            int fileNumber;

            try
            {
                fileNumber =
                    extractFileNumber(filename);
            }
            catch (const std::exception& e)
            {
                qDebug()
                << "Invalid filename:"
                << e.what();

                writeLog(
                    "INVALID_FILENAME",
                    -1,
                    filename,
                    "",
                    "");

                ++skippedCount;
                continue;
            }

            // ------------------------------------------------
            // Convert file number to CSV index.
            //
            // Example:
            // 00001-foo-bar.mp3 -> csvTracks[0]
            // 00002-foo-bar.mp3 -> csvTracks[1]
            // ------------------------------------------------

            const size_t csvIndex =
                static_cast<size_t>(fileNumber - 1);

            if (csvIndex >= csvTracks.size())
            {
                qDebug()
                << "CSV row out of range:"
                << fileNumber;

                writeLog(
                    "INVALID_CSV_ROW",
                    fileNumber,
                    filename,
                    "",
                    "");

                ++skippedCount;
                continue;
            }

            const CsvTrack& csvTrack =
                csvTracks[csvIndex];

            // ------------------------------------------------
            // Find synced lyrics in LRCLIB.
            // ------------------------------------------------

            LyricsResult lyricsResult =
                findSyncedLyrics(
                    csvTrack.trackName,
                    csvTrack.artistName);

            if (lyricsResult.syncedLyrics.empty())
            {
                qDebug()
                << "No synced lyrics.";

                writeLog(
                    "NO_SYNCED_LYRICS",
                    fileNumber,
                    filename,
                    csvTrack.trackName,
                    csvTrack.artistName);

                ++noLyricsCount;
                continue;
            }

            // ------------------------------------------------
            // Parse synchronized lyrics.
            // ------------------------------------------------

            const int64_t trackDurationMs =
                static_cast<int64_t>(
                    lyricsResult.durationSeconds * 1000.0);

            std::vector<ParsedLyric> lyrics =
                parseSyncedLyrics(
                    lyricsResult.syncedLyrics,
                    trackDurationMs);

            if (lyrics.empty())
            {
                qDebug()
                << "Synced lyrics contained no usable lines.";

                writeLog(
                    "NO_USABLE_LYRICS",
                    fileNumber,
                    filename,
                    csvTrack.trackName,
                    csvTrack.artistName);

                ++skippedCount;
                continue;
            }

            // ------------------------------------------------
            // Add song to our database.
            // ------------------------------------------------

            const int64_t songId =
                musicDatabase.addSong(
                    csvTrack.artistName,
                    csvTrack.trackName,
                    filename,
                    fileNumber);

            // ------------------------------------------------
            // Add each lyric and its embedding.
            // ------------------------------------------------

            for (const ParsedLyric& lyric : lyrics)
            {
                const int64_t lyricId =
                    musicDatabase.addLyric(
                        songId,
                        lyric.startTimeMs,
                        lyric.stopTimeMs,
                        lyric.text);

                std::vector<float> embedding =
                    createEmbedding(lyric.text);

                if (embedding.size() !=
                    MusicDatabase::EmbeddingDimension)
                {
                    throw std::runtime_error(
                        "Embedding has incorrect dimension.");
                }

                musicDatabase.addEmbedding(
                    lyricId,
                    embedding);
            }

            // ------------------------------------------------
            // IMPORTANT:
            //
            // Only write SUCCESS after the entire song has
            // been processed.
            // ------------------------------------------------

            writeLog(
                "SUCCESS",
                fileNumber,
                filename,
                csvTrack.trackName,
                csvTrack.artistName,
                lyrics.size());

            ++processedCount;

            qDebug()
                << "Successfully processed:"
                << QString::fromStdString(
                       csvTrack.artistName)
                << "-"
                << QString::fromStdString(
                       csvTrack.trackName);
        }
    }
    catch (...)
    {
        closeSourceDatabase();
        throw;
    }

    closeSourceDatabase();

    qDebug()
        << ""
        << "Import complete."
        << "Processed:"
        << processedCount
        << "No lyrics:"
        << noLyricsCount
        << "Skipped:"
        << skippedCount;
}


// ------------------------------------------------------------
// CSV loading
// ------------------------------------------------------------

std::vector<CsvTrack> MusicImporter::loadCsvTracks() const
{
    std::ifstream file(csvPath);

    if (!file)
    {
        throw std::runtime_error(
            "Could not open CSV file: " + csvPath);
    }

    std::vector<CsvTrack> tracks;

    std::string line;

    // Skip header.
    if (!std::getline(file, line))
    {
        throw std::runtime_error(
            "CSV file is empty.");
    }

    while (std::getline(file, line))
    {
        if (line.empty())
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

        tracks.push_back(
            std::move(track));
    }

    return tracks;
}


// ------------------------------------------------------------
// Basic CSV parser
// ------------------------------------------------------------

std::vector<std::string> MusicImporter::parseCsvLine(
    const std::string& line)
{
    std::vector<std::string> fields;

    std::string field;
    bool insideQuotes = false;

    for (size_t i = 0; i < line.size(); ++i)
    {
        char c = line[i];

        if (c == '"')
        {
            if (insideQuotes &&
                i + 1 < line.size() &&
                line[i + 1] == '"')
            {
                // Escaped quote: ""
                field += '"';
                ++i;
            }
            else
            {
                insideQuotes = !insideQuotes;
            }
        }
        else if (c == ',' && !insideQuotes)
        {
            fields.push_back(field);
            field.clear();
        }
        else
        {
            field += c;
        }
    }

    fields.push_back(field);

    return fields;
}


// ------------------------------------------------------------
// Extract five-digit number from filename
// ------------------------------------------------------------

int MusicImporter::extractFileNumber(
    const std::string& filename)
{
    static const std::regex pattern(
        R"(^(\d{5})-)");

    std::smatch match;

    if (!std::regex_search(
            filename,
            match,
            pattern))
    {
        throw std::runtime_error(
            "Filename does not begin with a five-digit number: "
            + filename);
    }

    return std::stoi(match[1].str()) - 1;
}


// ------------------------------------------------------------
// LRCLIB source database
// ------------------------------------------------------------

void MusicImporter::openSourceDatabase()
{
    if (sourceDb != nullptr)
        return;

    int result =
        sqlite3_open_v2(
            sourceDatabasePath.c_str(),
            &sourceDb,
            SQLITE_OPEN_READONLY,
            nullptr);

    if (result != SQLITE_OK)
    {
        std::string error =
            sourceDb != nullptr
                ? sqlite3_errmsg(sourceDb)
                : "unknown SQLite error";

        if (sourceDb != nullptr)
            sqlite3_close(sourceDb);

        sourceDb = nullptr;

        throw std::runtime_error(
            "Could not open LRCLIB database: "
            + error);
    }

    qDebug()
        << "LRCLIB database opened.";
}


void MusicImporter::closeSourceDatabase()
{
    if (sourceDb != nullptr)
    {
        sqlite3_close(sourceDb);
        sourceDb = nullptr;
    }
}


// ------------------------------------------------------------
// Find synchronized lyrics
// ------------------------------------------------------------

LyricsResult MusicImporter::findSyncedLyrics(
    const std::string& trackName,
    const std::string& artistName) const
{
    if (sourceDb == nullptr)
    {
        throw std::runtime_error(
            "LRCLIB database is not open.");
    }

    const char* sql = R"(
        SELECT
            l.synced_lyrics,
            t.duration
        FROM lyrics l
        INNER JOIN tracks t
            ON l.track_id = t.id
        WHERE t.name_lower = ?
          AND t.artist_name_lower = ?
          AND l.has_synced_lyrics = 1
          AND l.synced_lyrics IS NOT NULL
          AND l.synced_lyrics != ''
        ORDER BY l.id DESC
        LIMIT 1;
    )";

    sqlite3_stmt* statement = nullptr;

    int result =
        sqlite3_prepare_v2(
            sourceDb,
            sql,
            -1,
            &statement,
            nullptr);

    if (result != SQLITE_OK)
    {
        throw std::runtime_error(
            "Could not prepare LRCLIB query: " +
            std::string(
                sqlite3_errmsg(sourceDb)));
    }

    const std::string trackNameLower =
        toLower(trackName);

    const std::string artistNameLower =
        toLower(artistName);

    sqlite3_bind_text(
        statement,
        1,
        trackNameLower.c_str(),
        -1,
        SQLITE_TRANSIENT);

    sqlite3_bind_text(
        statement,
        2,
        artistNameLower.c_str(),
        -1,
        SQLITE_TRANSIENT);

    LyricsResult resultData;

    result = sqlite3_step(statement);

    if (result == SQLITE_ROW)
    {
        const unsigned char* lyrics =
            sqlite3_column_text(
                statement,
                0);

        if (lyrics != nullptr)
        {
            resultData.syncedLyrics =
                reinterpret_cast<const char*>(
                    lyrics);
        }

        resultData.durationSeconds =
            sqlite3_column_double(
                statement,
                1);
    }
    else if (result != SQLITE_DONE)
    {
        std::string error =
            sqlite3_errmsg(sourceDb);

        sqlite3_finalize(statement);

        throw std::runtime_error(
            "LRCLIB query failed: " + error);
    }

    sqlite3_finalize(statement);

    return resultData;
}


// ------------------------------------------------------------
// Timestamp parsing
// ------------------------------------------------------------

int64_t MusicImporter::parseTimestampMs(
    const std::string& timestamp)
{
    const size_t colon =
        timestamp.find(':');

    if (colon == std::string::npos)
    {
        throw std::runtime_error(
            "Invalid lyric timestamp: " +
            timestamp);
    }

    const int minutes =
        std::stoi(
            timestamp.substr(0, colon));

    const double seconds =
        std::stod(
            timestamp.substr(colon + 1));

    return static_cast<int64_t>(
        minutes * 60000 +
        seconds * 1000.0);
}


// ------------------------------------------------------------
// Parse LRC synchronized lyrics
// ------------------------------------------------------------

std::vector<ParsedLyric>
MusicImporter::parseSyncedLyrics(
    const std::string& syncedLyrics,
    int64_t trackDurationMs)
{
    std::vector<ParsedLyric> lyrics;

    std::stringstream stream(
        syncedLyrics);

    std::string line;

    static const std::regex pattern(
        R"(^\[([0-9]+:[0-9]+(?:\.[0-9]+)?)\]\s*(.*)$)");

    while (std::getline(stream, line))
    {
        // Handle Windows CRLF line endings.
        if (!line.empty() &&
            line.back() == '\r')
        {
            line.pop_back();
        }

        std::smatch match;

        if (!std::regex_match(
                line,
                match,
                pattern))
        {
            continue;
        }

        const std::string timestamp =
            match[1].str();

        const std::string text =
            trim(match[2].str());

        // Ignore timestamp-only lines.
        if (text.empty())
            continue;

        ParsedLyric lyric;

        lyric.startTimeMs =
            parseTimestampMs(timestamp);

        lyric.stopTimeMs = 0;

        lyric.text = text;

        lyrics.push_back(
            std::move(lyric));
    }

    // Each lyric ends when the next lyric begins.
    for (size_t i = 0;
         i + 1 < lyrics.size();
         ++i)
    {
        lyrics[i].stopTimeMs =
            lyrics[i + 1].startTimeMs;
    }

    // Last lyric ends at the track duration.
    if (!lyrics.empty())
    {
        lyrics.back().stopTimeMs =
            trackDurationMs;
    }

    return lyrics;
}


// ------------------------------------------------------------
// Create embedding
// ------------------------------------------------------------

std::vector<float>
MusicImporter::createEmbedding(
    const std::string& lyricText)
{
    // Do not use the verbose tokenizeString() here.
    // This importer may process thousands of lyric lines.

    std::vector<int32_t> tokenIds =
        model.tokenizer->Encode(lyricText);

    return MainWindow::calculateEmbeddingVector(
        model.session,
        tokenIds);
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
        std::chrono::system_clock::to_time_t(now);

    logFile
        << std::put_time(
               std::localtime(&time),
               "%Y-%m-%d %H:%M:%S")
        << " | "
        << status
        << " | CSV="
        << fileNumber
        << " | "
        << filename
        << " | "
        << artistName
        << " | "
        << trackName;

    if (lyricCount > 0)
    {
        logFile
            << " | lyrics="
            << lyricCount;
    }

    logFile << '\n';

    // Make sure the result is written immediately.
    logFile.flush();
}


// ------------------------------------------------------------
// Utility
// ------------------------------------------------------------

std::string MusicImporter::toLower(
    const std::string& value)
{
    std::string result = value;

    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(
                std::tolower(c));
        });

    return result;
}


std::string MusicImporter::trim(
    const std::string& value)
{
    size_t start = 0;

    while (start < value.size() &&
           std::isspace(
               static_cast<unsigned char>(
                   value[start])))
    {
        ++start;
    }

    size_t end = value.size();

    while (end > start &&
           std::isspace(
               static_cast<unsigned char>(
                   value[end - 1])))
    {
        --end;
    }

    return value.substr(
        start,
        end - start);
}
