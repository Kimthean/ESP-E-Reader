#ifndef API_H
#define API_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

// API Configuration
#define MANGA_API_BASE_URL "https://mangahook-api.vercel.app"
#define API_TIMEOUT_MS 30000
#define MAX_RETRIES 3

// API Endpoints
#define ENDPOINT_MANGA_LIST "/api/mangaList"
#define ENDPOINT_MANGA_DETAIL "/api/manga"
#define ENDPOINT_MANGA_SEARCH "/api/search"
#define ENDPOINT_MANGA_CHAPTER "/api/chapter"

// Data structures for manga API responses
struct MangaListItem
{
    String id;
    String title;
    String image;
    String chapter;
    String view;
    String description;
};

struct MangaMetadata
{
    int totalStories;
    int totalPages;
    // Categories and filters available
    String types[10];
    String states[5];
    String categories[40];
};

struct MangaDetail
{
    String id;
    String title;
    String image;
    String description;
    String author;
    String status;
    String lastUpdate;
    int totalChapters;
    String genres[20];
    String chapters[100]; // Chapter list
};

struct ChapterData
{
    String id;
    String title;
    String images[50]; // Image URLs for chapter pages
    int totalPages;
    String nextChapter;
    String prevChapter;
};

// API Response structure
struct APIResponse
{
    bool success;
    int statusCode;
    String data;
    String error;
};

// API function declarations
APIResponse makeAPIRequest(const String &endpoint, const String &params = "");
APIResponse getMangaList(int page = 1, const String &category = "", const String &status = "");
APIResponse getMangaDetail(const String &mangaId);
APIResponse searchManga(const String &query, int page = 1);
APIResponse getChapterData(const String &mangaId, const String &chapterId);

// JSON parsing functions
bool parseMangaList(const String &jsonData, MangaListItem *items, int &count);
bool parseMangaDetail(const String &jsonData, MangaDetail &manga);
bool parseChapterData(const String &jsonData, ChapterData &chapter);

// Network utility functions
bool isNetworkConnected();
String urlEncode(const String &str);
void initHTTP();

/*
 * Manga Hook API Documentation
 *
 * Base URL: https://mangahook-api.vercel.app
 *
 * Endpoints:
 *
 * 1. GET /api/mangaList
 *    - Returns list of manga with pagination
 *    - Parameters: page, category, status, type
 *    - Response: JSON with mangaList array and metaData
 *
 * 2. GET /api/manga/{id}
 *    - Returns detailed manga information
 *    - Parameters: manga ID
 *    - Response: JSON with manga details, chapters, etc.
 *
 * 3. GET /api/search
 *    - Search manga by title
 *    - Parameters: query, page
 *    - Response: JSON with search results
 *
 * 4. GET /api/chapter/{mangaId}/{chapterId}
 *    - Returns chapter images and navigation
 *    - Parameters: manga ID, chapter ID
 *    - Response: JSON with image URLs and navigation
 *
 * Example Response Format:
 * {
 *   "mangaList": [
 *     {
 *       "id": "1manga-oa952283",
 *       "image": "https://example.com/image.jpg",
 *       "title": "Attack On Titan",
 *       "chapter": "chapter-139",
 *       "view": "105.8M",
 *       "description": "..."
 *     }
 *   ],
 *   "metaData": {
 *     "totalStories": 10,
 *     "totalPages": 100,
 *     "type": [...],
 *     "state": [...],
 *     "category": [...]
 *   }
 * }
 */

#endif // API_H