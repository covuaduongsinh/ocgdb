/**
 * This file is part of Open Chess Game Database Standard.
 *
 * Copyright (c) 2021-2022 Nguyen Pham (github@nguyenpham)
 * Copyright (c) 2021-2022 Developers
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */

#ifndef OCGDB_DBREAD_H
#define OCGDB_DBREAD_H

#include "dbcore.h"

namespace ocgdb {


class DbRead : public virtual DbCore
{
public:
    DbRead();
    virtual ~DbRead();

    virtual void processAGame(const bslib::PgnRecord& record, const std::vector<int8_t>& moveVec);
    
protected:
    virtual void processAGameWithAThread(ThreadRecord* t, const bslib::PgnRecord& record, const std::vector<int8_t>& moveVec);

public:
    static SearchField getMoveField(SQLite::Database* db, bool* hashMoves = nullptr);

    // True if `db` has a table named `name` -- used to probe for optional
    // derived tables (GameMaterial, OpeningTree, ...) before relying on
    // them, since older/unprocessed databases won't have them yet.
    static bool hasTable(SQLite::Database* db, const std::string& name);

    static void extractHeader(SQLite::Statement& query, bslib::PgnRecord& record);
    // queryVariations: optional (nullptr = skip -- most callers never
    // need round-trip variation/NAG fidelity, only PGN export does), a
    // prepared "SELECT Ply, Variation FROM GameTree WHERE GameID = ?"
    // reused across calls the same way queryComments already is. Reads
    // Comments.Nag too, but only if that column exists on this database
    // (see the no-throw column-name check in the .cpp -- older/non-
    // keepvariations databases don't have it).
    static void queryForABoard( bslib::PgnRecord& record,
                                SearchField searchField,
                                SQLite::Statement* query,
                                SQLite::Statement* queryComments,
                                bslib::BoardCore* board,
                                SQLite::Statement* queryVariations = nullptr);
    
    virtual bool readADb(const std::string& dbPath, const std::string& sqlString);

public:
    static const std::string fullGameQueryString;
    static const std::string searchFieldNames[];
    static const char* tagNames[];

protected:
    virtual bool openDB(const std::string& dbPath);
    virtual void closeDb();

    static void printGamePGNByIDs(SQLite::Database& db, const std::vector<int>& gameIDVec, SearchField);
    
    static void printGamePGNByIDs(QueryGameRecord&, const std::vector<int>&);


protected:
    std::function<bool(const std::vector<uint64_t>&, const bslib::BoardCore*, const bslib::PgnRecord*)> checkToStop = nullptr;
    std::function<bool(const bslib::BoardCore*, const bslib::PgnRecord*)> boardCallback = nullptr;

private:
    void threadProcessAGame(const bslib::PgnRecord& record, const std::vector<int8_t>& moveVec);

private:
    QueryGameRecord* qgr = nullptr;

};

} // namespace ocdb

#endif /* OCGDB_DBREAD_H */
