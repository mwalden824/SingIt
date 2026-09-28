#include "MusicDatabase.h"

#include "sqlite3.h"

#include <stdexcept>

MusicDatabase::MusicDatabase(const std::string& databasePath)
    : db(nullptr)
{
    int result = sqlite3_open(databasePath.c_str(), &db);

    if (result != SQLITE_OK)
    {
        std::string message = "Failed to open database";

        if (db != nullptr)
        {
            message += ": ";
            message += sqlite3_errmsg(db);
            sqlite3_close(db);
            db = nullptr;
        }

        throw std::runtime_error(message);
    }

    // Enable foreign-key constraints.
    char* errorMessage = nullptr;

    result = sqlite3_exec(
        db,
        "PRAGMA foreign_keys = ON;",
        nullptr,
        nullptr,
        &errorMessage
        );

    if (result != SQLITE_OK)
    {
        std::string message = "Failed to enable foreign keys";

        if (errorMessage != nullptr)
        {
            message += ": ";
            message += errorMessage;
            sqlite3_free(errorMessage);
        }

        sqlite3_close(db);
        db = nullptr;

        throw std::runtime_error(message);
    }

    // Load sqlite-vec.
    loadSqliteVec();
}

MusicDatabase::~MusicDatabase()
{
    if (db != nullptr)
    {
        sqlite3_close(db);
        db = nullptr;
    }
}


void MusicDatabase::checkSqliteResult(
    int result,
    const char* operation
    ) const
{
    if (result != SQLITE_OK &&
        result != SQLITE_DONE &&
        result != SQLITE_ROW)
    {
        std::string message = operation;

        message += ": ";

        if (db != nullptr)
        {
            message += sqlite3_errmsg(db);
        }

        throw std::runtime_error(message);
    }
}


void MusicDatabase::loadSqliteVec()
{
    char* errorMessage = nullptr;
    sqlite3_enable_load_extension(db, 1);

    int result = sqlite3_load_extension(
        db,
        "third_party/sqlite-vec/vec0",
        nullptr,
        &errorMessage
        );

    if (result != SQLITE_OK)
    {
        std::string message = "Failed to load sqlite-vec";

        if (errorMessage != nullptr)
        {
            message += ": ";
            message += errorMessage;
            sqlite3_free(errorMessage);
        }

        throw std::runtime_error(message);
    }
}


void MusicDatabase::initialize()
{
    const char* sql = R"SQL(

        CREATE TABLE IF NOT EXISTS Songs
        (
            Id INTEGER PRIMARY KEY AUTOINCREMENT,

            Artist TEXT NOT NULL,

            TrackName TEXT NOT NULL,

            Filename TEXT NOT NULL,

            FileNumber INTEGER NOT NULL
        );


        CREATE TABLE IF NOT EXISTS Lyrics
        (
            Id INTEGER PRIMARY KEY AUTOINCREMENT,

            SongId INTEGER NOT NULL,

            StartTimeMs INTEGER NOT NULL,

            StopTimeMs INTEGER NOT NULL,

            LyricText TEXT NOT NULL,

            FOREIGN KEY (SongId)
                REFERENCES Songs(Id)
                ON DELETE CASCADE
        );


        CREATE INDEX IF NOT EXISTS idx_Lyrics_SongId
        ON Lyrics(SongId);


        CREATE INDEX IF NOT EXISTS idx_Songs_Artist
        ON Songs(Artist);


        CREATE INDEX IF NOT EXISTS idx_Songs_TrackName
        ON Songs(TrackName);


        CREATE VIRTUAL TABLE IF NOT EXISTS LyricEmbeddings
        USING vec0
        (
            embedding float[384]
        );

    )SQL";

    char* errorMessage = nullptr;

    int result = sqlite3_exec(
        db,
        sql,
        nullptr,
        nullptr,
        &errorMessage
        );

    if (result != SQLITE_OK)
    {
        std::string message = "Failed to initialize database";

        if (errorMessage != nullptr)
        {
            message += ": ";
            message += errorMessage;
            sqlite3_free(errorMessage);
        }

        throw std::runtime_error(message);
    }
}


int64_t MusicDatabase::addSong(
    const std::string& artist,
    const std::string& trackName,
    const std::string& filename,
    int fileNumber
    )
{
    const char* sql = R"SQL(
        INSERT INTO Songs
        (
            Artist,
            TrackName,
            Filename,
            FileNumber
        )
        VALUES (?, ?, ?, ?);
    )SQL";

    sqlite3_stmt* statement = nullptr;

    int result = sqlite3_prepare_v2(
        db,
        sql,
        -1,
        &statement,
        nullptr
        );

    checkSqliteResult(
        result,
        "Preparing addSong"
        );

    sqlite3_bind_text(
        statement,
        1,
        artist.c_str(),
        -1,
        SQLITE_TRANSIENT
        );

    sqlite3_bind_text(
        statement,
        2,
        trackName.c_str(),
        -1,
        SQLITE_TRANSIENT
        );

    sqlite3_bind_text(
        statement,
        3,
        filename.c_str(),
        -1,
        SQLITE_TRANSIENT
        );

    sqlite3_bind_int(
        statement,
        4,
        fileNumber
        );

    result = sqlite3_step(statement);

    sqlite3_finalize(statement);

    checkSqliteResult(
        result,
        "Adding song"
        );

    return sqlite3_last_insert_rowid(db);
}


int64_t MusicDatabase::addLyric(
    int64_t songId,
    int64_t startTimeMs,
    int64_t stopTimeMs,
    const std::string& lyricText
    )
{
    const char* sql = R"SQL(
        INSERT INTO Lyrics
        (
            SongId,
            StartTimeMs,
            StopTimeMs,
            LyricText
        )
        VALUES (?, ?, ?, ?);
    )SQL";

    sqlite3_stmt* statement = nullptr;

    int result = sqlite3_prepare_v2(
        db,
        sql,
        -1,
        &statement,
        nullptr
        );

    checkSqliteResult(
        result,
        "Preparing addLyric"
        );

    sqlite3_bind_int64(
        statement,
        1,
        songId
        );

    sqlite3_bind_int64(
        statement,
        2,
        startTimeMs
        );

    sqlite3_bind_int64(
        statement,
        3,
        stopTimeMs
        );

    sqlite3_bind_text(
        statement,
        4,
        lyricText.c_str(),
        -1,
        SQLITE_TRANSIENT
        );

    result = sqlite3_step(statement);

    sqlite3_finalize(statement);

    checkSqliteResult(
        result,
        "Adding lyric"
        );

    return sqlite3_last_insert_rowid(db);
}


void MusicDatabase::addEmbedding(
    int64_t lyricId,
    const std::vector<float>& embedding
    )
{
    if (embedding.size() != EmbeddingDimension)
    {
        throw std::invalid_argument(
            "Embedding must contain exactly 384 floats."
            );
    }

    // sqlite-vec uses the rowid of the vector table as the
    // identifier for the corresponding embedding.
    //
    // The lyric ID is therefore used as the rowid.

    const char* sql = R"SQL(
        INSERT INTO LyricEmbeddings
        (
            rowid,
            embedding
        )
        VALUES (?, ?);
    )SQL";

    sqlite3_stmt* statement = nullptr;

    int result = sqlite3_prepare_v2(
        db,
        sql,
        -1,
        &statement,
        nullptr
        );

    checkSqliteResult(
        result,
        "Preparing addEmbedding"
        );

    sqlite3_bind_int64(
        statement,
        1,
        lyricId
        );

    result = sqlite3_bind_blob(
        statement,
        2,
        embedding.data(),
        static_cast<int>(
            embedding.size() * sizeof(float)
            ),
        SQLITE_TRANSIENT
        );

    checkSqliteResult(
        result,
        "Binding embedding"
        );

    result = sqlite3_step(statement);

    sqlite3_finalize(statement);

    checkSqliteResult(
        result,
        "Adding embedding"
        );
}


std::vector<SearchResult> MusicDatabase::searchSimilar(
    const std::vector<float>& queryEmbedding,
    int limit
    ) const
{
    if (queryEmbedding.size() != EmbeddingDimension)
    {
        throw std::invalid_argument(
            "Query embedding must contain exactly 384 floats."
            );
    }

    if (limit <= 0)
    {
        return {};
    }

    const char* sql = R"SQL(

        SELECT
            s.Artist,
            s.TrackName,
            s.Filename,
            s.FileNumber,

            l.StartTimeMs,
            l.StopTimeMs,

            l.LyricText,

            e.distance

        FROM LyricEmbeddings e

        JOIN Lyrics l
            ON l.Id = e.rowid

        JOIN Songs s
            ON s.Id = l.SongId

        WHERE e.embedding MATCH ?
            AND k = ?

        ORDER BY e.distance;

    )SQL";

    sqlite3_stmt* statement = nullptr;

    int result = sqlite3_prepare_v2(
        db,
        sql,
        -1,
        &statement,
        nullptr
        );

    checkSqliteResult(
        result,
        "Preparing searchSimilar"
        );

    result = sqlite3_bind_blob(
        statement,
        1,
        queryEmbedding.data(),
        static_cast<int>(
            queryEmbedding.size() * sizeof(float)
            ),
        SQLITE_TRANSIENT
        );

    checkSqliteResult(
        result,
        "Binding query embedding"
        );

    result = sqlite3_bind_int(
        statement,
        2,
        limit
        );

    checkSqliteResult(
        result,
        "Binding search limit"
        );

    std::vector<SearchResult> results;

    while ((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        SearchResult searchResult;

        searchResult.artist =
            reinterpret_cast<const char*>(
                sqlite3_column_text(statement, 0)
                );

        searchResult.trackName =
            reinterpret_cast<const char*>(
                sqlite3_column_text(statement, 1)
                );

        searchResult.filename =
            reinterpret_cast<const char*>(
                sqlite3_column_text(statement, 2)
                );

        searchResult.fileNumber =
            sqlite3_column_int(statement, 3);

        searchResult.startTimeMs =
            sqlite3_column_int64(statement, 4);

        searchResult.stopTimeMs =
            sqlite3_column_int64(statement, 5);

        searchResult.lyricText =
            reinterpret_cast<const char*>(
                sqlite3_column_text(statement, 6)
                );

        searchResult.distance =
            static_cast<float>(
                sqlite3_column_double(statement, 7)
                );

        results.push_back(
            std::move(searchResult)
            );
    }

    sqlite3_finalize(statement);

    if (result != SQLITE_DONE)
    {
        checkSqliteResult(
            result,
            "Searching embeddings"
            );
    }

    return results;
}