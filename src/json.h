/**
 * This file is part of Open Chess Game Database Standard.
 *
 * A minimal JSON writer used by the web server (WebServer, src/server.cpp).
 * It only ever EMITS JSON -- request parameters arrive as URL query strings,
 * parsed by httplib, so no JSON parser is needed. The vendored SQLite build
 * (3.36.0) does not define SQLITE_ENABLE_JSON1, so json_object() etc. are not
 * available from SQL either; this is the substitute.
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#ifndef OCGDB_JSON_H
#define OCGDB_JSON_H

#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>

#include "3rdparty/SQLiteCpp/SQLiteCpp.h"

namespace ocgdb {

// Escapes a UTF-8 string for embedding inside a JSON string literal.
// Bytes >= 0x80 are passed through unchanged: JSON text is UTF-8, and
// player/event/site names routinely contain accented characters.
inline std::string jsonEscape(const std::string& in)
{
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

// Small streaming JSON builder. Tracks, per open object/array, whether a
// comma is needed before the next member/element. Not for concurrent use by
// multiple threads on the same instance -- create one per request.
class Json
{
public:
    Json& objBegin() { comma(); s += '{'; needComma.push_back(false); return *this; }
    Json& objEnd()   { s += '}'; closeScope(); return *this; }

    Json& arrBegin() { comma(); s += '['; needComma.push_back(false); return *this; }
    Json& arrEnd()   { s += ']'; closeScope(); return *this; }

    // Writes a bare key (caller must follow with exactly one value/obj/arr).
    Json& key(const char* k) {
        comma();
        s += '"'; s += jsonEscape(k); s += "\":";
        // the value that follows must not itself add a leading comma
        afterKey = true;
        return *this;
    }

    Json& val(const std::string& v) { comma(); s += '"'; s += jsonEscape(v); s += '"'; return *this; }
    Json& val(const char* v)        { return val(std::string(v ? v : "")); }
    Json& val(int64_t v)            { comma(); s += std::to_string(v); return *this; }
    Json& val(int v)                { return val(static_cast<int64_t>(v)); }
    Json& val(double v) {
        comma();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.6g", v);
        s += buf;
        return *this;
    }
    Json& val(bool v)   { comma(); s += (v ? "true" : "false"); return *this; }
    Json& valNull()     { comma(); s += "null"; return *this; }

    // Convenience: key + scalar value in one call.
    Json& kv(const char* k, const std::string& v) { key(k); return val(v); }
    Json& kv(const char* k, const char* v)        { key(k); return val(v); }
    Json& kv(const char* k, int64_t v)             { key(k); return val(v); }
    Json& kv(const char* k, int v)                 { key(k); return val(v); }
    Json& kv(const char* k, double v)              { key(k); return val(v); }
    Json& kv(const char* k, bool v)                { key(k); return val(v); }
    Json& kvNull(const char* k)                    { key(k); return valNull(); }

    // Emits a SQLite column value honoring its actual storage type
    // (NULL / INTEGER / FLOAT / TEXT); BLOB columns are emitted as null
    // since callers never want raw move bytes in a JSON response.
    Json& kv(const char* k, SQLite::Column c) {
        key(k);
        switch (c.getType()) {
            case SQLITE_INTEGER: return val(static_cast<int64_t>(c.getInt64()));
            case SQLITE_FLOAT:   return val(c.getDouble());
            case SQLITE3_TEXT:   return val(c.getString());
            default:             return valNull();
        }
    }

    const std::string& str() const { return s; }

private:
    void comma() {
        if (!needComma.empty()) {
            if (afterKey) {
                afterKey = false; // value right after a key never gets a comma of its own
            } else {
                if (needComma.back()) s += ',';
                needComma.back() = true;
            }
        }
    }
    void closeScope() {
        if (!needComma.empty()) needComma.pop_back();
        if (!needComma.empty()) needComma.back() = true;
    }

    std::string s;
    std::vector<bool> needComma;
    bool afterKey = false;
};

} // namespace ocgdb

#endif /* OCGDB_JSON_H */
