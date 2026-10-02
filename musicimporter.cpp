#include "musicimporter.h"

#include <sqlite3.h>

#include <QDebug>
#include <QEventLoop>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <regex>
#include <utility>

// ------------------------------------------------------------
// Constructor / Destructor
// ------------------------------------------------------------
MusicImporter::MusicImporter(
    MusicDatabase& musicDatabase,
    const std::string& sourceDatabasePath,
    const std::string& musicDirectory,
    SimSearchModel& model,
    ProgressCallback progressCallback
    )
    : musicDatabase(musicDatabase),
    sourceDatabasePath(sourceDatabasePath),
    musicDirectory(musicDirectory),
    model(model),
    progressCallback(std::move(progressCallback))
{
}

MusicImporter::~MusicImporter()
{
    closeSourceDatabase();
}

int MusicImporter::getImportedCount() const
{
    return importedCount;
}

int MusicImporter::getSkippedCount() const
{
    return skippedCount;
}

void MusicImporter::cancel()
{
    cancelled.store(true);
}

bool MusicImporter::isCancelled() const
{
    return cancelled.load();
}

// ------------------------------------------------------------
// Main import
// ------------------------------------------------------------

void MusicImporter::importAll(
    const std::string& logPath)
{
    qDebug() << "Starting music import...";

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
        openSourceDatabase();

        importedCount = 0;
        skippedCount = 0;

        int total = 0;
        int current = 0;

        for (const auto& entry : std::filesystem::directory_iterator(musicDirectory))
        {
            if (!entry.is_regular_file())
                continue;

            if (entry.path().extension() == ".mp3")
                ++total;
        }

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

            if (cancelled.load())
                break;

            ++current;
            if (progressCallback)
            {
                progressCallback(
                    current,
                    total,
                    filename
                    );
            }

            if (musicDatabase.doesSongExistInDatabase(filename))
            {
                ++skippedCount;
                continue;
            }

            // ------------------------------------------------
            // Read artist and track name from MP3 metadata.
            // ------------------------------------------------

            Mp3Metadata metadata =
                readMp3Metadata(
                    entry.path().string()
                    );

            if (metadata.trackName.empty() ||
                metadata.artistName.empty())
            {
                writeLog(
                    "NO_METADATA",
                    filename,
                    metadata.trackName,
                    metadata.artistName,
                    0
                    );

                ++skippedCount;
                continue;
            }

            qDebug()
                << "Processing:"
                << QString::fromStdString(filename)
                << "-"
                << QString::fromStdString(
                       metadata.trackName
                       )
                << "-"
                << QString::fromStdString(
                       metadata.artistName
                       );

            // ------------------------------------------------
            // Find synced lyrics in LRCLIB.
            // ------------------------------------------------

            LyricsResult lyricsResult =
                findSyncedLyrics(
                    metadata.trackName,
                    metadata.artistName
                    );

            if (lyricsResult.syncedLyrics.empty())
            {
                writeLog(
                    "NO_LYRICS",
                    filename,
                    metadata.trackName,
                    metadata.artistName,
                    0
                    );

                ++skippedCount;
                continue;
            }

            // ------------------------------------------------
            // Convert duration to milliseconds.
            // ------------------------------------------------

            const int64_t trackDurationMs =
                static_cast<int64_t>(
                    std::llround(
                        lyricsResult.durationSeconds *
                        1000.0
                        )
                    );

            // ------------------------------------------------
            // Parse timestamped lyrics.
            // ------------------------------------------------

            std::vector<ParsedLyric> lyrics =
                parseSyncedLyrics(
                    lyricsResult.syncedLyrics,
                    trackDurationMs
                    );

            if (lyrics.empty())
            {
                writeLog(
                    "NO_PARSED_LYRICS",
                    filename,
                    metadata.trackName,
                    metadata.artistName,
                    0
                    );

                ++skippedCount;
                continue;
            }

            // ------------------------------------------------
            // Add song to our database.
            // ------------------------------------------------

            const int songId =
                musicDatabase.addSong(
                    metadata.artistName,
                    metadata.trackName,
                    filename
                    );

            // ------------------------------------------------
            // Add lyrics and embeddings.
            // ------------------------------------------------

            for (const ParsedLyric& lyric : lyrics)
            {
                if (cancelled.load())
                    break;

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

            // ------------------------------------------------
            // Log success.
            // ------------------------------------------------

            writeLog(
                "SUCCESS",
                filename,
                metadata.trackName,
                metadata.artistName,
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
// MP3 metadata
// ------------------------------------------------------------

Mp3Metadata MusicImporter::readMp3Metadata(
    const std::string& filename)
{
    Mp3Metadata result;

    QMediaPlayer player;

    QEventLoop loop;

    QTimer timeoutTimer;

    timeoutTimer.setSingleShot(true);

    // --------------------------------------------------------
    // Stop waiting once the media has loaded.
    // --------------------------------------------------------

    QObject::connect(
        &player,
        &QMediaPlayer::mediaStatusChanged,
        &loop,
        [&](QMediaPlayer::MediaStatus status)
        {
            if (status == QMediaPlayer::LoadedMedia ||
                status == QMediaPlayer::InvalidMedia)
            {
                loop.quit();
            }
        }
        );

    // --------------------------------------------------------
    // Stop waiting if Qt reports an error.
    // --------------------------------------------------------

    QObject::connect(
        &player,
        &QMediaPlayer::errorOccurred,
        &loop,
        [&](QMediaPlayer::Error error,
            const QString& errorString)
        {
            Q_UNUSED(error);

            qWarning()
                << "Could not load MP3:"
                << QString::fromStdString(filename)
                << "-"
                << errorString;

            loop.quit();
        }
        );

    // --------------------------------------------------------
    // Safety timeout.
    // --------------------------------------------------------

    QObject::connect(
        &timeoutTimer,
        &QTimer::timeout,
        &loop,
        &QEventLoop::quit
        );

    const QUrl url =
        QUrl::fromLocalFile(
            QString::fromStdString(filename)
            );

    player.setSource(url);

    // Give the media backend up to 10 seconds to load.
    timeoutTimer.start(10000);

    loop.exec();

    timeoutTimer.stop();

    // --------------------------------------------------------
    // Make sure the media actually loaded.
    // --------------------------------------------------------

    if (player.mediaStatus() !=
        QMediaPlayer::LoadedMedia)
    {
        qWarning()
        << "MP3 metadata could not be loaded:"
        << QString::fromStdString(filename);

        return result;
    }

    // --------------------------------------------------------
    // Get metadata.
    // --------------------------------------------------------

    const QMediaMetaData metadata =
        player.metaData();

    // --------------------------------------------------------
    // Track title.
    // --------------------------------------------------------

    result.trackName =
        metadata.value(
                    QMediaMetaData::Title
                    ).toString().toStdString();

    // --------------------------------------------------------
    // Artist.
    //
    // ContributingArtist is normally a QStringList.
    // --------------------------------------------------------

    const QVariant artistValue =
        metadata.value(
            QMediaMetaData::ContributingArtist
            );

    if (artistValue.canConvert<QStringList>())
    {
        const QStringList artists =
            artistValue.toStringList();

        if (!artists.isEmpty())
        {
            result.artistName =
                artists.join(", ").toStdString();
        }
    }
    else
    {
        result.artistName =
            artistValue.toString().toStdString();
    }

    result.trackName =
        trim(result.trackName);

    result.artistName =
        trim(result.artistName);

    qDebug()
        << "MP3 metadata:"
        << QString::fromStdString(
               result.trackName
               )
        << "-"
        << QString::fromStdString(
               result.artistName
               );

    return result;
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

    const std::string trackNameLower =
        toLower(trackName);

    const std::string artistNameLower =
        toLower(artistName);

    sqlite3_bind_text(
        statement,
        1,
        trackNameLower.c_str(),
        -1,
        SQLITE_TRANSIENT
        );

    sqlite3_bind_text(
        statement,
        2,
        artistNameLower.c_str(),
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