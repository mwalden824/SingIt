#ifndef MUSICDATABASE_H
#define MUSICDATABASE_H

#include <string>
#include <vector>
#include <cstdint>

struct SearchResult
{
    std::string artist;
    std::string trackName;
    std::string filename;
    int fileNumber;

    int64_t startTimeMs;
    int64_t stopTimeMs;

    std::string lyricText;

    float distance;
};

class MusicDatabase
{
public:
    explicit MusicDatabase(const std::string& databasePath);
    ~MusicDatabase();

    MusicDatabase(const MusicDatabase&) = delete;
    MusicDatabase& operator=(const MusicDatabase&) = delete;

    void initialize();

    int64_t addSong(
        const std::string& artist,
        const std::string& trackName,
        const std::string& filename,
        int fileNumber
        );

    int64_t addLyric(
        int64_t songId,
        int64_t startTimeMs,
        int64_t stopTimeMs,
        const std::string& lyricText
        );

    void addEmbedding(
        int64_t lyricId,
        const std::vector<float>& embedding
        );

    std::vector<SearchResult> searchSimilar(
        const std::vector<float>& queryEmbedding,
        int limit
        ) const;

private:
    struct sqlite3* db;

    void checkSqliteResult(
        int result,
        const char* operation
        ) const;

    void loadSqliteVec();

    static constexpr int EmbeddingDimension = 384;
};

#endif // MUSICDATABASE_H