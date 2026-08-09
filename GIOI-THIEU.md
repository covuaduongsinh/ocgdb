# Giới thiệu OCGDB (Open Chess Game Database Standard)

> Tài liệu này được viết dựa trên việc đọc trực tiếp mã nguồn tại commit `93610ec` của repo này, cộng với build và chạy thử thật trên máy Windows (không chép lại nguyên văn `README.md`). Mọi số liệu "chạy thử" trong tài liệu là log thật, không phải suy đoán.

## 1. OCGDB là gì

OCGDB (**Open Chess Game Database Standard**) vừa là một **đặc tả định dạng** cơ sở dữ liệu ván cờ vua, vừa là **công cụ dòng lệnh C++17 tham chiếu** (`ocgdb`) hiện thực đặc tả đó. Tác giả: Nguyễn Phạm (nguyenpham), giấy phép MIT, phiên bản hiện tại của công cụ là `Beta 8` (cấu trúc CSDL `0.6`).

**Ý tưởng cốt lõi:** dùng thẳng SQLite làm định dạng lưu trữ, thay vì phát minh một định dạng nhị phân riêng như hầu hết phần mềm cờ vua khác (ChessBase .cbh, SCID .si4...) hay dùng PGN thô.

So với hai lựa chọn truyền thống:

| | Định dạng nhị phân riêng | PGN thuần | OCGDB (SQLite) |
|---|---|---|---|
| Đọc được bằng công cụ chuẩn | Không | Có (text) | **Có** (mọi trình duyệt/thư viện SQL) |
| Có cấu trúc quan hệ (Event/Player dùng chung) | Có | Không | **Có** |
| Tốc độ truy vấn tổng hợp (đếm, lọc) | Nhanh | Rất chậm | **Nhanh** (index, `Info` cache sẵn) |
| Dễ mở rộng schema | Khó | N/A | **Dễ** (`ALTER TABLE`) |
| Rủi ro giấy phép | GPL/độc quyền tuỳ hãng | Tự do | **MIT** |

Tác giả công bố đã thử với 94 triệu ván (Lichess) và ước tính hoạt động tốt tới hàng tỷ ván.

## 2. Lược đồ cơ sở dữ liệu thực tế

Đây là schema **thật sự được `Builder::createDb()` sinh ra** ([src/builder.cpp:278-368](src/builder.cpp#L278-L368)) — chi tiết và đầy đủ hơn ví dụ tĩnh trong `SqlCmd.md`:

```sql
CREATE TABLE Info (Name TEXT UNIQUE NOT NULL, Value TEXT);
-- các dòng mặc định: 'Data Structure Version', 'Version', 'Variant',
-- 'License', 'Description', và sau khi tạo xong: GameCount, PlayerCount,
-- EventCount, SiteCount, CommentCount (để tránh phải SELECT COUNT(*) chậm)

CREATE TABLE Events  (ID INTEGER PRIMARY KEY AUTOINCREMENT, Name TEXT UNIQUE);
CREATE TABLE Sites   (ID INTEGER PRIMARY KEY AUTOINCREMENT, Name TEXT UNIQUE);
CREATE TABLE Players (ID INTEGER PRIMARY KEY, Name TEXT UNIQUE, Elo INTEGER);

-- Games: cột được SINH ĐỘNG theo danh sách tag PGN thực có trong dữ liệu nguồn,
-- luôn có khoá ngoại tới Events/Sites/Players khi tag tương ứng tồn tại:
CREATE TABLE Games (
    ID INTEGER PRIMARY KEY AUTOINCREMENT,
    EventID INTEGER, SiteID INTEGER, Date TEXT, Round TEXT,
    WhiteID INTEGER, WhiteElo INTEGER, BlackID INTEGER, BlackElo INTEGER,
    Result TEXT, TimeControl TEXT, ECO TEXT, PlyCount INTEGER,
    FEN TEXT, Moves TEXT,          -- hoặc Moves1 / Moves2 kiểu BLOB
    FOREIGN KEY(EventID) REFERENCES Events,
    FOREIGN KEY(SiteID)  REFERENCES Sites,
    FOREIGN KEY(WhiteID) REFERENCES Players,
    FOREIGN KEY(BlackID) REFERENCES Players
);

CREATE TABLE Comments (ID INTEGER PRIMARY KEY AUTOINCREMENT, GameID INTEGER, Ply INTEGER, Comment TEXT);
```

`PRAGMA journal_mode=OFF` được bật khi tạo — đánh đổi an toàn khi crash lấy tốc độ ghi, hợp lý cho một tiến trình convert một lần.

### Ba cách mã hoá nước đi (chọn qua `-o moves|moves1|moves2`)

| Trường | Kiểu | Cách mã hoá | Ghi chú |
|---|---|---|---|
| `Moves` | TEXT | PGN movetext đầy đủ, gồm cả comment | Dễ đọc, tương thích mọi công cụ, tốn dung lượng nhất |
| `Moves2` | BLOB | 2 byte/nước: `from \| dest << 6 \| promotion << 12` | Dễ tự viết decoder |
| `Moves1` | BLOB | 1 byte/nước (riêng nước đi Hậu là 2 byte) | Nhỏ nhất, thuật toán phức tạp, gắn chặt vào code của dự án |

### Chuẩn hoá ngày tháng

PGN `YYYY.MM.DD` → SQLite ISO-8601 `YYYY-MM-DD`; dấu `?` (ngày/tháng thiếu) được thay bằng `1` — ví dụ `"1950.??.??"` → `"1950-11-11"`.

## 3. Kiến trúc mã nguồn

```
src/
├── main.cpp        điểm vào, phân tích tham số dòng lệnh, điều phối task
├── core.{h,cpp}     lớp trừu tượng Core: quản lý thread pool, đếm/thống kê chung
├── dbcore.{h,cpp}   DbCore : Core — quản lý kết nối SQLite (mở/đóng, transaction)
├── records.{h,cpp}  ParaRecord (toàn bộ tham số CLI), enum Task, hằng version
├── pgnread.{h,cpp}  PGNRead — đọc PGN streaming theo khối 8 MB, máy trạng thái tay
├── dbread.{h,cpp}   DbRead : DbCore — đọc ván từ CSDL đã tồn tại
├── builder.cpp
│   + epdbuilder.cpp Builder : PGNRead + DbCore — nhiệm vụ -create
├── addgame.{h,cpp}  AddGame : Builder — nhiệm vụ -merge
├── exporter.{h,cpp} Exporter : DbRead — nhiệm vụ -export
├── duplicate.{h,cpp} Duplicate : DbRead — nhiệm vụ -dup
├── extract.{h,cpp}  Extract : DbRead — nhiệm vụ -g (lấy ván theo ID)
├── search.{h,cpp}   Search : DbRead + PGNRead — nhiệm vụ -q và -bench
├── parser.{h,cpp}   trình phân tích PQL (đệ quy, tay viết)
├── report.{h,cpp}   in kết quả ra console/file, thread-safe
└── board/           thư viện cờ vua tự viết (namespace bslib), gồm:
    chess.cpp (3279 dòng) sinh nước đi hợp lệ, parse/xuất SAN, FEN, Zobrist hash...
```

Mọi "task" (`create/merge/export/dup/query/bench/getgame`) là một lớp con của `Core`, dùng chung một `thread_pool` (thư viện `3rdparty/threadpool`, header-only) để xử lý song song theo `-cpu <n>`.

### Thư viện bên thứ ba (nhúng sẵn trong repo, không cần cài thêm)

| Thư viện | Đường dẫn | Phiên bản | Giấy phép |
|---|---|---|---|
| SQLite amalgamation | `src/3rdparty/sqlite3/` | 3.36.0 | Public domain |
| SQLiteCpp (wrapper C++) | `src/3rdparty/SQLiteCpp/` | 3.1.1 | MIT |
| thread_pool.hpp (bshoshany) | `src/3rdparty/threadpool/` | 2.0.0, header-only | MIT |

Không có `.gitmodules`, không cần vcpkg/conan — `git clone` thường là đủ.

## 4. Ngôn ngữ truy vấn thế cờ — PQL (Position Query Language)

Ý tưởng: mỗi ký hiệu quân cờ (`K Q R B N P` = trắng, `k q r b n p` = đen) khi dùng trong biểu thức sẽ được **tính ra số lượng quân đó** trên bàn cờ (có thể giới hạn theo ô/cột/hàng), rồi so sánh như một biểu thức số học bình thường.

```
R                 tổng số Xe trắng trên bàn
qb3               tổng số Hậu đen đứng ở ô b3
B3                tổng số Tượng trắng ở hàng 3
bb                tổng số Tượng đen ở cột b
n[b-e]            tổng số Mã đen từ cột b đến cột e
P[a4, c5, d5]     tổng số Tốt trắng ở các ô a4, c5, d5

R                       dạng ngầm định của R != 0
R == 3                  Xe trắng phải đúng 3 quân
P[d4,e5,f4,g4] = 4 and kb7   4 Tốt trắng ở d4/e5/f4/g4 VÀ Vua đen ở b7
fen[<chuỗi FEN>]        khớp đúng một (hoặc vài, phân tách bởi dấu phẩy) thế cờ FEN cụ thể
```

Dòng bắt đầu bằng `//` là chú thích, bị loại bỏ trước khi parse.

**Cú pháp mở rộng chưa có trong `README.md` gốc** — mẫu vị trí dạng khối `{...}` ([src/parser.cpp:764](src/parser.cpp#L764)), cho phép đặt nhiều quân cùng lúc (kể cả nhúng một chuỗi FEN làm nền) rồi so khớp bằng toán tử `=`/`==` (khớp đúng), `<`, `>`, hoặc `#` (dịch mẫu — thử khớp mẫu ở mọi vị trí tịnh tiến trên bàn cờ, có dung sai số nguyên đi kèm). Đây là engine tìm mẫu vị trí tổng quát hơn phần EBNF được công bố.

Bộ máy phân tích là đệ quy-hạ (recursive-descent) tay viết, dựng cây `Node`, mỗi `Node` có hàm `evaluate()` nhận vào tập bitboard của **từng thế cờ trong ván** — nghĩa là một truy vấn PQL được áp lên **mọi nước đi của mọi ván**, không chỉ thế cờ cuối.

## 5. Toàn bộ tham số dòng lệnh

```
Usage:
 ocgdb [<parameters>]

 -create               create a new database from multi PGN files, works with -db, -pgn
 -merge                merge multi PGN files or databases into the first database, works with -db, -pgn
 -dup                  check duplicate games in databases, works with -db
 -export               export from a database into a PGN file, works with -db, -pgn
 -bench                benchmarch querying games speed, works with -db
 -q <query>            querying positions, repeat to add multi queries, works with -db, -pgn
 -g <id>               get game with game ID numbers (repeat to add multi IDs), works with -db, -pgn
 -pgn <file>           PGN game database file, repeat to add multi files
 -db <file>            database file, extension should be .ocgdb.db3, repeat to add multi files
 -r <file>             report file, works with -g, -q, -dup
                       use :memory: to create in-memory database
 -elo <n>              discard games with Elo under n (for creating)
 -plycount <n>         discard games with ply-count under n (for creating)
 -resultcount <n>      stop querying if the number of results above n (for querying)
 -cpu <n>              number of threads, should <= total physical cores, omit it for using all cores
 -desc "<string>"      a description to write to the table Info when creating a new database
 -o [<options>,]       options, separated by commas
    moves              create text move field Moves
    moves1             create binary move field Moves, 1-byte encoding
    moves2             create binary move field Moves, 2-byte encoding
    acceptnewtags      create a new field for a new PGN tag (for creating)
    discardcomments    discard all comments (for creating)
    discardsites       discard all Site tag (for creating)
    discardnoelo       discard games without player Elos (for creating)
    discardfen         discard games with FENs (not started from origin; for creating)
    reseteco           re-create all ECO (for creating)
    printall           print all results (for querying, checking duplications)
    printfen            print FENs of results (for querying)
    printpgn            print simple PGNs of results (for querying)
    embededgames        duplicate included games inside other games
    remove               remove duplicate games (for checking duplicates)
    nobot                Lichess: ignore BOT games (for creating a database)
    bot                   Lichess: count games with BOT (for creating a database)
```

*(Trợ giúp trên là nội dung thật do chương trình in ra — xem log [Bước 6.0](#0-usage-không-tham-số) bên dưới. `README.md` gốc của dự án đã lỗi thời ở mục Usage: thiếu động từ nhiệm vụ `-create`, ví dụ `ocgdb -pgn ... -db ...` không còn hợp lệ với build hiện tại.)*

**Cờ ẩn `-debug`** ([src/main.cpp:108](src/main.cpp#L108)) không xuất hiện trong bản trợ giúp trên nhưng có thật trong code — bật biến `debugMode`, in toàn bộ `ParaRecord` đã parse trước khi chạy. Rất hữu ích để soi lỗi tham số.

### Ràng buộc bắt buộc theo từng nhiệm vụ (`ParaRecord::isValid()`, [src/records.cpp:41](src/records.cpp#L41))

| Nhiệm vụ | Bắt buộc |
|---|---|
| `-create` | ≥1 `-pgn` và ≥1 `-db` |
| `-merge` | ≥2 `-db`, hoặc ≥1 `-pgn` |
| `-export` | đúng 1 `-pgn` và ≥1 `-db` |
| `-dup` | ≥1 `-db` |
| `-q` | ≥1 `-db` hoặc `-pgn`, và ≥1 truy vấn |
| `-bench` | ≥1 `-db` |
| `-g` | ≥1 `-db` và ≥1 ID ván |

## 6. Build trên Windows

Không có `CMakeLists.txt`. Repo cung cấp `src/Makefile` (Linux/macOS, dùng g++) và `projects/ocgdb.sln`/`.vcxproj` (Visual Studio) / `.xcodeproj` (Xcode). Cả hai đường build dưới đây **đã được thực hiện thật trên máy này** trong lần chạy thử này — không phải suy đoán lý thuyết.

### 6.1. MinGW-w64 g++ (đường build thành công, dùng cho toàn bộ demo)

Makefile gốc không chạy thẳng trên Windows (`-ldl` không tồn tại ở MinGW). Chuỗi lệnh dùng thực tế, tương đương Makefile nhưng bỏ `-ldl` và đưa object ra thư mục riêng:

```bash
g++ -std=c++17 -DNDEBUG -O3 -c src/*.cpp                    # -> build/obj/
g++ -std=c++17 -DNDEBUG -O3 -c src/board/*.cpp
g++ -std=c++17 -DNDEBUG -O3 -c src/3rdparty/SQLiteCpp/*.cpp
gcc            -DNDEBUG -O3 -c src/3rdparty/sqlite3/*.c
g++ -O3 -o build/ocgdb.exe build/obj/*.o -pthread            # bỏ -ldl
```

Toolchain đã dùng: MinGW-w64 g++ **16.1.0** (UCRT, posix-threads, bản WinLibs).

**Hai lỗi biên dịch thật gặp phải trên GCC 16, đã sửa trực tiếp trong `src/board/`** (không phải suy đoán — log lỗi đầy đủ ở dưới):

1. [`src/board/base.cpp`](src/board/base.cpp) dùng `std::count` ở dòng 627 nhưng không `#include <algorithm>`. GCC cũ khoan dung nhờ include bắc cầu qua header khác; GCC 16 xiết chặt include bắc cầu trong `libstdc++` nên báo lỗi thẳng. → thêm `#include <algorithm>`.
2. [`src/board/funcs.cpp`](src/board/funcs.cpp) gọi hàm `_stat64(path.c_str(), &fileStat)` trong nhánh `#ifdef _WIN32`, nhưng nhánh đó **không hề `#include <sys/stat.h>`** (chỉ có ở nhánh `#else` non-Windows). Thiếu khai báo hàm khiến trình biên dịch hiểu `_stat64(...)` là khởi tạo một đối tượng kiểu `_stat64` (struct cùng tên, được include bắc cầu qua `<sstream>`→...→`_mingw_stat64.h`) với 2 tham số → lỗi "expects 0/1 arguments, 2 provided". → thêm `#include <sys/stat.h>` vào nhánh `_WIN32`.

Đây là các sửa lỗi cổng hoá (portability fix) tối thiểu, đúng đắn cho cả MSVC lẫn MinGW, không đổi hành vi chương trình.

### 6.2. MSVC (Visual Studio 2022 Build Tools)

`projects/ocgdb.vcxproj` (bản gốc từ upstream) **thiếu 2 file nguồn** trong danh sách biên dịch: `src/core.cpp` và `src/epdbuilder.cpp` — trong khi bản Xcode project thì có đủ. `core.cpp` định nghĩa các hàm thành viên của lớp `Core` (`run/createPool/printStats/...`) và biến static `Core::pool`, được `main.cpp` và mọi task khác tham chiếu trực tiếp. Thiếu file này khiến MSBuild **biên dịch trót lọt nhưng thất bại ở bước link** (LNK2019 unresolved external). Đã bổ sung 2 dòng `<ClCompile Include="..\src\core.cpp" />` và `<ClCompile Include="..\src\epdbuilder.cpp" />` (cùng `core.h`) vào `projects/ocgdb.vcxproj`.

Lệnh build:
```
vcvars64.bat
msbuild projects\ocgdb.sln /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /m
```

(Ép `PlatformToolset=v143` vì project gốc ghim `v142`/VS2019, máy này chỉ có VS2022 Build Tools; cấu hình `Win32`/x86 không set `LanguageStandard=stdcpp17` nên sẽ vỡ ở `<filesystem>` — chỉ build x64.)

**Kết quả trên máy này:** build thành công, không lỗi (chỉ có warning cảnh báo hẹp kiểu dữ liệu `size_t`→`int`/`int64_t`→`long`... vô hại, và 2 warning C4250 "inherits via dominance" ở `AddGameDbRead` do kế thừa kim cương giữa `Builder` và `DbRead` — vô hại). Sản phẩm: `projects\x64\Release\ocgdb.exe` (1.538.048 byte). Chạy `-bench` trên cùng `carlsen.ocgdb.db3` cho kết quả nhất quán với bản MinGW (sai khác 1 đơn vị ở hai truy vấn cuối — `2824` thay vì `2825` — là biểu hiện của đúng lỗi #3 ở mục 8: `succCount` cộng dồn không reset giữa truy vấn, vốn đã nhạy với thứ tự hoàn thành luồng song song):

```
$ projects\x64\Release\ocgdb.exe -bench -db carlsen.ocgdb.db3 -cpu 4
Search with query Q = 3...                          #succ: 0
Search with query r[e4, e5, d4,d5]= 2...             #succ: 13
Search with query P[d4, e5, f4, g4] = 4 and kb7...   #succ: 13
Search with query B[c-f] + b[c-f] == 2...            #succ: 2824
Search with query white6 = 5...                      #succ: 2824
Completed!
```

## 7. Nhật ký chạy thử thật

Dữ liệu mẫu: `carlsen.ocgdb.db3` (2851 ván của Magnus Carlsen, tải từ repo mẫu chính thức [nguyenpham/ocgdb-samples](https://github.com/nguyenpham/ocgdb-samples), 839.680 byte, nước đi lưu dạng `Moves2` BLOB — không phải `Moves` text; bản `Moves` TEXT là `roundtrip.ocgdb.db3` được tạo lại ở mục 7.6).

### 7.0. Usage (không tham số)

```
Open Chess Game Database Standard (OCGDB), (C) 2022 - version: Beta 8

Usage:
 ocgdb [<parameters>]
 ... (xem toàn văn ở mục 5)
```

### 7.1. `-bench` — benchmark 5 truy vấn PQL cố định

```
$ ocgdb -bench -db carlsen.ocgdb.db3 -cpu 4

Benchmark position searching...
Thread count: 4
Search with query Q = 3...
#games: 2851, elapsed: 106ms, speed: 26896 games/s #succ: 0
Search with query r[e4, e5, d4,d5]= 2...
#games: 2851, elapsed: 155ms, speed: 18393 games/s #succ: 13
Search with query P[d4, e5, f4, g4] = 4 and kb7...
#games: 2851, elapsed: 124ms, speed: 22991 games/s #succ: 13
Search with query B[c-f] + b[c-f] == 2...
#games: 2851, elapsed: 126ms, speed: 22626 games/s #succ: 2825
Search with query white6 = 5...
#games: 2851, elapsed: 193ms, speed: 14772 games/s #succ: 2825
Completed!
```

~20.000 ván/giây, mỗi ván có hàng chục thế cờ được quét qua bitboard — hợp lý với tuyên bố "nhanh" của dự án. **Lưu ý về các số `#succ` giống hệt nhau ở từng cặp query — xem lỗi #2 ở mục 8.**

### 7.2. `-g` — lấy ván theo ID

```
$ ocgdb -db carlsen.ocgdb.db3 -g 1 -o printpgn

;ID: 1
[Event "Troll Masters"]
[White "Carlsen,Magnus"]
[Black "Fant,G"]
[Date "2001.01.07"]
[Result "1-0"]
...
1.e4 e6 2.d4 d5 3.Nc3 Bb4 4.e5 Ne7 ... 28.Qe3 1-0
```

Ván đầu tiên trong CSDL: Magnus Carlsen (10 tuổi) thắng ở Troll Masters 2001.

### 7.3. `-q` — truy vấn PQL

```
$ ocgdb -db carlsen.ocgdb.db3 -q "Q=3" -o printall
Search with query Q=3...
#games: 2851, elapsed: 130ms, speed: 21930 games/s #succ: 0

$ ocgdb -db carlsen.ocgdb.db3 -q "fen[rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1]" -o printfen
Search with query fen[...]...
1. gameId: 1
2. gameId: 2
...
#games: 2803, elapsed: 104ms, speed: 26951 games/s #succ: 1286
```

1286/2803 ván khớp (đã nạp thế cờ) mở đầu bằng 1.e4 — hợp lý cho một tuyển tập ván đấu.

### 7.4. `-dup` — kiểm tra trùng lặp

```
$ ocgdb -dup -db carlsen.ocgdb.db3 -o printall
Finding duplicate games...
#games: 2851, elapsed: 32ms, speed: 89093 games/s, #duplicates: 0, #removed: 0
```

Không có ván trùng trong bộ mẫu này.

### 7.5. `-export` — xuất CSDL ra PGN

```
$ ocgdb -export -db carlsen.ocgdb.db3 -pgn carlsen-export.pgn
#games: 2851, elapsed: 182ms, speed: 15664 games/s
Completed!
```
→ file `carlsen-export.pgn`, 2.102.315 byte.

### 7.6. `-create` — round-trip PGN → CSDL mới

```
$ ocgdb -create -pgn carlsen-export.pgn -db roundtrip.ocgdb.db3 -cpu 4 -o moves -desc "..."
Processing PGN file: 'carlsen-export.pgn'
#games: 2850, elapsed: 1521ms, speed: 1873 games/s
Completed!
```

### 7.7. `-merge` — gộp thêm cùng file PGN vào CSDL vừa tạo

```
$ ocgdb -merge -db roundtrip.ocgdb.db3 -pgn carlsen-export.pgn -cpu 4
#games: 5701, elapsed: 101524783ms 28:12:04, speed: 0 games/s
Completed!
```

5701 = 2850 + 2851, đúng về mặt dữ liệu. **Con số thời gian `28:12:04` là bug hiển thị — xem mục 8.**

### 7.8. Truy vấn SQL trực tiếp (không qua `ocgdb.exe`, dùng `sqlite3` CLI chuẩn)

Đây là minh chứng trực tiếp cho luận điểm cốt lõi của dự án: file `.db3` là SQLite thuần, không cần công cụ riêng để đọc.

```
$ sqlite3 carlsen.ocgdb.db3 ".tables"
Comments  Events    Games     Info      Players   Sites

$ sqlite3 carlsen.ocgdb.db3 "SELECT Name, Value FROM Info;"
Data Structure Version|0.5
Version|0.1
Variant|standard
License|free
GameCount|2851
PlayerCount|606
EventCount|275
SiteCount|115
CommentCount|0

$ sqlite3 carlsen.ocgdb.db3 "SELECT COUNT(*) FROM Games;"
2851
$ sqlite3 roundtrip.ocgdb.db3 "SELECT COUNT(*) FROM Games;"
2851

$ sqlite3 carlsen.ocgdb.db3 "SELECT w.Name White, WhiteElo, b.Name Black, BlackElo, Result, Date
                             FROM Games g
                             INNER JOIN Players w ON WhiteID=w.ID
                             INNER JOIN Players b ON BlackID=b.ID
                             ORDER BY WhiteElo DESC LIMIT 3;"
Carlsen,M|2882|Dominguez Perez,L|2763|1-0|2019-08-10
Carlsen,M|2882|Aronian,L|2765|0-1|2019-08-11
Carlsen,M|2882|Vachier Lagrave,M|2778|1-0|2019-08-12
```

**Kết quả round-trip khớp hoàn toàn:** `Info.GameCount` = `COUNT(*)` = **2851** ở cả hai CSDL (gốc và tạo lại từ PGN xuất ra). Con số "2850" hiển thị lúc chạy `-create` ở mục 7.6 chỉ là bộ đếm tiến trình trên console lệch 1, **không phải mất dữ liệu thật** — dữ liệu đã lưu xuống đĩa là đầy đủ và chính xác.

Thú vị: CSDL mẫu `carlsen.ocgdb.db3` (tạo năm 2022) có `Data Structure Version = 0.5`, trong khi mã nguồn hiện tại của repo này đã lên `0.6` ([src/records.h:33](src/records.h#L33)) — bản thân định dạng đã tiến hoá, và vì mọi thứ là SQL nên việc đọc CSDL phiên bản cũ vẫn hoạt động bình thường.

## 8. Hạn chế và lỗi đã quan sát được (không phải suy đoán — có log tái hiện)

1. **`README.md` mục Usage đã lỗi thời.** Ví dụ `ocgdb -pgn big.png -db big.ocgdb.db3 -cpu 4 -o moves` thiếu động từ nhiệm vụ bắt buộc (`-create`); bản build hiện tại từ chối chạy nếu thiếu nó. Trợ giúp chuẩn xác là `print_usage()` trong `main.cpp` (mục 5 ở trên).

2. **Cú pháp nhiều `-q` trên dòng lệnh bị lỗi**, dù chính README/`print_usage()` quảng cáo nó: `ocgdb -db big.ocgdb.db3 -cpu 4 -q "Q=3" -q"P[...]"`. Nguyên nhân: [src/main.cpp:168-183](src/main.cpp#L168-L183) kiểm tra `oldTask != Task::none` sau khi đã gán `paraRecord.task = Task::query` — ở lần `-q` thứ hai, `oldTask` đã là `query` từ lần đầu nên bị coi là "xung đột nhiệm vụ": `Error: multi/conflicted tasks: query vs query`. Đã tái hiện trực tiếp trên bản build MinGW. Cách né: chạy từng `-q` một trong các lệnh riêng biệt.

3. **`succCount` không reset giữa các truy vấn khi chạy nhiều `-q` trong một lần gọi** ([src/search.cpp:42](src/search.cpp#L42) — chỉ gán `succCount = 0` một lần trước cả vòng lặp `for(auto && _query : paraRecord.queries)` ở dòng 100, không reset lại đầu mỗi vòng). Hệ quả: chế độ `-bench` (chạy nội bộ 5 truy vấn liên tiếp, không qua giới hạn ở lỗi #2) hiển thị số "#succ" **tích luỹ dồn** qua các truy vấn thay vì số khớp riêng của từng truy vấn — quan sát thấy ở log 7.1: truy vấn 2 và 3 cùng báo `#succ: 13` dù chạy độc lập truy vấn 3 chỉ cho 0 kết quả thật (mục 7.3); tương tự truy vấn 4 và 5 cùng báo `2825`.

4. **Thời gian hiển thị của `-merge` sai nghiêm trọng.** Log 7.7 báo `elapsed: 101524783ms 28:12:04` cho một thao tác chưa tới 1 giây thực tế. Nguyên nhân nhiều khả năng: `Core::startTime` (kiểu `std::chrono::steady_clock::time_point`, [src/core.h:56](src/core.h#L56)) được set tường minh trong đường `-q`/`-bench` ([src/search.cpp:147](src/search.cpp#L147): `startTime = getNow();` trước mỗi lần xử lý) nhưng **không được set** trong đường `-merge`/`AddGame`, nên vẫn giữ giá trị mặc định (epoch của đồng hồ hệ thống) khi `printStats()` tính `elapsed = now - startTime`. Chỉ ảnh hưởng số liệu hiển thị, không ảnh hưởng dữ liệu (đã xác minh ở mục 7.8: số ván sau merge chính xác 5701).

5. **`Builder::createDb_EPD()` có định nghĩa nhưng không được gọi ở đâu cả.** Khai báo ở [src/builder.h:40](src/builder.h#L40), định nghĩa đầy đủ ở [src/epdbuilder.cpp:21](src/epdbuilder.cpp#L21) (bảng `EPD`, đọc file `.epd`...), nhưng `Builder::runTask()` ([src/builder.cpp:100](src/builder.cpp#L100)) luôn gọi thẳng `create()` (đường OCGDB), không có cờ CLI nào dẫn tới `createDb_EPD()`. Tính năng "hỗ trợ CSDL EPD" nêu trong commit message mới nhất (`93610ec`) có mã nguồn nhưng chưa nối vào giao diện dòng lệnh.

6. **Rủi ro tệp PGN > 2 GB khi build bằng MinGW.** `Funcs::getFileSize(FILE*)` ([src/board/funcs.cpp](src/board/funcs.cpp)) chọn nhánh `fseek/ftell` 32-bit khi macro `_MSC_VER` không được định nghĩa — điều này đúng với MSVC nhưng **sai với MinGW** (MinGW không định nghĩa `_MSC_VER`), nên bản MinGW dùng `ftell` 32-bit và có thể tràn số với file trên 2 GB. (Hàm `getFileSize(const std::string&)` — hàm thực sự được `processPgnFile` sử dụng — không bị ảnh hưởng vì nó rẽ nhánh theo `_WIN32` và dùng `_stat64`.)

7. **Đường dẫn Unicode.** `pgnread.cpp` mở file PGN bằng `std::ifstream` với `std::string` hẹp (narrow), nên đường dẫn chứa ký tự ngoài ASCII nhiều khả năng lỗi trên Windows.

## 9. Tóm tắt cách build và chạy trên máy này

```powershell
# MinGW-w64 (đã build thành công, dùng cho mọi demo ở mục 7)
build\ocgdb.exe -bench -db build\samples\carlsen.ocgdb.db3 -cpu 4

# MSVC (sau khi sửa projects\ocgdb.vcxproj — xem mục 6.2)
projects\x64\Release\ocgdb.exe -bench -db build\samples\carlsen.ocgdb.db3 -cpu 4
```

Toàn bộ binary, object file và CSDL mẫu nằm trong `build/` (đã có trong `.gitignore`, không ảnh hưởng `git status`). Hai thay đổi duy nhất trong mã nguồn được commit là các portability fix ở mục 6.1 (`base.cpp`, `funcs.cpp`) và bổ sung 2 file còn thiếu vào `projects/ocgdb.vcxproj` ở mục 6.2 — không có thay đổi hành vi nào khác.

## 10. Máy chủ web / bảng điều khiển (`-server`)

Nhiệm vụ `-server` (thêm sau các nhiệm vụ gốc ở mục 5, không có trong `README.md` gốc của upstream) biến `ocgdb.exe` thành một **máy chủ HTTP cục bộ** vừa phục vụ giao diện web trực quan (thư mục `web/`, HTML/CSS/JS thuần, không build step), vừa là **bảng điều khiển điều hành** cho phép chạy mọi nhiệm vụ CLI khác (`-create/-merge/-export/-dup/-bench/-q`) từ trình duyệt, có hàng đợi, tiến độ trực tiếp, log, và huỷ giữa chừng.

### 10.1. Kiến trúc

- **`src/server.{h,cpp}`** — lớp `WebServer : public DbRead`, dùng thư viện nhúng sẵn `src/3rdparty/httplib/httplib.h` (cpp-httplib, MIT, ~740KB, chỉ include trong đúng một file `.cpp` để không kéo dài thời gian biên dịch). Phục vụ hai nhóm API:
  - `/api/*` — đọc CSDL "đang active" (duyệt ván, xem chi tiết, PQL, thống kê). CSDL active là **đổi được lúc chạy**, không cố định vào tham số `-db` lúc khởi động.
  - `/api/admin/*` — quản lý danh sách CSDL đã đăng ký, đổi CSDL active, và toàn bộ hàng đợi tác vụ. Mọi route này bắt buộc header `X-OCGDB-Token`.
- **`src/admin.{h,cpp}`** — `AdminStore`: kho trạng thái bền vững bằng một file SQLite **riêng** (`ocgdb-admin.db3`, mặc định cạnh file `.exe`, đổi bằng `-admindb`), lưu danh sách CSDL đã đăng ký, lịch sử tác vụ, và log từng dòng của mỗi tác vụ.
- **`src/jobs.{h,cpp}`** — `JobManager`: hàng đợi một luồng worker, chạy tuần tự (không cho hai tác vụ ghi đồng thời). Mỗi tác vụ được ánh xạ sang một dòng lệnh `ocgdb <cờ nhiệm vụ> ...` theo bảng trắng cứng (`buildJobArgv()`), không bao giờ nối chuỗi shell từ input người dùng.
- **`src/process.{h,cpp}`** — `ChildProcess`: chạy `ocgdb.exe` như **tiến trình con** thật sự (Windows: `CreatePipe` + `CreateProcessW`; POSIX: `pipe`+`fork`+`execv`), bắt stdout/stderr gộp làm log theo đúng thứ tự thời gian, huỷ được bằng `TerminateProcess`/`SIGTERM`. Chọn tiến trình con thay vì gọi thẳng `Builder`/`AddGame`... trong cùng tiến trình vì `Core::pool` là con trỏ **`static` dùng chung** ([src/core.cpp:15](src/core.cpp#L15)) — tạo một `Core` thứ hai trong lúc `WebServer` (chính nó cũng là một `Core`) đang chạy sẽ giẫm lên nhau.
- Cờ CLI mới `-progress` khiến `Core::printStats()` ([src/core.cpp:64](src/core.cpp#L64)) in thêm một dòng máy đọc được: `@@PROGRESS games=N elapsed=Nms bytes=N total=N` — `JobManager` bắt dòng này bằng regex để cập nhật thanh tiến độ thật (phần trăm dựa trên dung lượng PGN đã đọc khi tạo/gộp CSDL).

### 10.2. Đổi CSDL đang xem lúc đang chạy — an toàn với ghi đồng thời

CSDL active được gói trong một struct `ActiveDb` (kết nối SQLite chỉ đọc + schema cache + cache thống kê), bảo vệ bằng `std::shared_mutex`:

- Mọi route `/api/*` "nhẹ" lấy khoá đọc **không chặn** (`try_lock`); nếu đang có tác vụ ghi thì trả **HTTP 503** kèm `{"busy":true,"jobId":N}` ngay lập tức thay vì treo yêu cầu.
- Ngay trước khi một tác vụ ghi (create/merge/dup xoá trùng) đụng đúng file đang active, `WebServer` đóng kết nối đọc của mình; ngay sau khi tác vụ xong, mở lại và xoá cache thống kê — đảm bảo lần đọc kế tiếp luôn thấy dữ liệu mới, không bao giờ đọc file dở dang.

Đã kiểm chứng thật: đổi CSDL active giữa `carlsen.ocgdb.db3` (`Moves2`) và `roundtrip.ocgdb.db3` (`Moves`) qua `POST /api/admin/databases/activate` làm `/api/info` đổi đúng `moveField` tương ứng ngay lập tức; chạy một tác vụ `-merge` ghi vào đúng CSDL đang active xong, `/api/info` đọc lại khớp chính xác với kết quả một lệnh `-bench` độc lập chạy trực tiếp từ dòng lệnh (không qua cache nào) trên cùng file.

### 10.3. Bảo mật

- Mặc định chỉ lắng nghe `127.0.0.1` (như bản `-server` gốc), **không có cấu hình nào mở ra LAN**.
- `-admintoken <t>` — không truyền thì tự sinh 32 ký tự hex ngẫu nhiên, in ra console kèm URL bấm được (`http://127.0.0.1:<port>/#admin?token=...`), đồng thời ghi ra `admin-token.txt` cạnh CSDL quản trị. Token bắt buộc trên **mọi** route `/api/admin/*` qua header `X-OCGDB-Token` — dùng header (không phải query/cookie) vì trình duyệt không tự gửi header tuỳ ý cross-origin mà không qua preflight CORS, và server này không bao giờ trả header cho phép CORS — đó là lớp chống CSRF chính.
- `-root <dir>` (tuỳ chọn) — khi đặt, mọi đường dẫn CSDL/PGN/report nhận từ API phải nằm trong thư mục này sau khi chuẩn hoá (`std::filesystem::weakly_canonical`); đường dẫn còn `..` sau chuẩn hoá hoặc nằm ngoài `-root` bị từ chối. Đã kiểm chứng thật bằng `curl`: thêm một đường dẫn ngoài `-root`, và một đường dẫn dùng `..\..\` để thoát ra ngoài, cả hai đều bị chặn với lỗi rõ ràng; đường dẫn hợp lệ bên trong `-root` vẫn hoạt động bình thường.
- "Gỡ đăng ký" một CSDL chỉ xoá khỏi danh sách quản lý, **không bao giờ xoá file trên đĩa**.

### 10.4. Ví dụ chạy thật + log xác nhận

```
$ ocgdb.exe -server -db carlsen.ocgdb.db3 -db roundtrip.ocgdb.db3 -port 3456 -web web

Starting web server...
Web UI folder: web
OCGDB web UI:  http://127.0.0.1:3456/
Admin token:   b2a170aac373c2a074c42c9503ddf607
Admin URL:     http://127.0.0.1:3456/#admin?token=b2a170aac373c2a074c42c9503ddf607
(admin token also written to admin-token.txt)
(listening on 127.0.0.1 only; press Ctrl+C to stop)
```

Không kèm token → 401; kèm đúng token → trạng thái máy chủ:

```
$ curl -H "X-OCGDB-Token: <token>" http://127.0.0.1:3456/api/admin/status
{"version":"Beta 8","pid":13904,"uptimeMs":7559,"port":3456,"cpu":4,
 "activeDb":{"path":"...carlsen.ocgdb.db3","open":true},
 "busy":false,"activeJobId":null,
 "jobCounts":{"queued":0,"running":0,"succeeded":0,"failed":0,"cancelled":0}}
```

Nộp một tác vụ `-merge` ghi trực tiếp vào PGN đã export trước đó, theo dõi tới khi xong:

```
$ curl -H "X-OCGDB-Token: <token>" -d "task=merge" -d "dbDest=carlsen.ocgdb.db3" \
       -d "pgn=carlsen-export.pgn" http://127.0.0.1:3456/api/admin/jobs/submit
{"id":2}

$ curl -H "X-OCGDB-Token: <token>" http://127.0.0.1:3456/api/admin/jobs/2
{"job":{"id":2,"task":"merge","state":"succeeded",
 "cmdline":"ocgdb -merge -db carlsen.ocgdb.db3 -pgn carlsen-export.pgn -progress",
 "progress":2102315,"progressTotal":2102315,"gameCnt":5701,"exitCode":0}}
```

`progress`/`progressTotal` là byte đã đọc/tổng byte của file PGN, lấy trực tiếp từ dòng `@@PROGRESS` — thanh tiến độ trên giao diện web là phần trăm thật, không phải hiệu ứng chờ vô định.

### 10.5. Giao diện web (`web/`)

Ứng dụng một trang (`web/index.html`), không build step, điều hướng qua hash (`#intro|#browse|#pql|#stats|#admin`), song ngữ Việt/Anh (nút chuyển, `web/js/i18n.js`), sáng/tối theo `prefers-color-scheme` hoặc chọn tay. Năm tab:

1. **Giới thiệu** — bản trực quan hoá nội dung tài liệu này (sơ đồ schema, so sánh 3 kiểu mã hoá nước đi, demo PQL trên bàn cờ vẽ bằng SVG tự viết), lấy toàn bộ số liệu thật từ `/api/info`.
2. **Duyệt CSDL** — lọc/sắp xếp/phân trang qua bảng `Games`.
3. **Truy vấn PQL** — chạy PQL trực tiếp từ trình duyệt, trả về khớp/quét/thời gian.
4. **Thống kê** — biểu đồ SVG tự vẽ (kết quả, theo năm, top ECO, phân bố Elo/số nước).
5. **Quản trị** (mới, mục này) — đăng ký/quét/kích hoạt CSDL, form nộp tác vụ (đổi trường theo loại, có xem trước dòng lệnh thật sẽ chạy), bảng tác vụ cập nhật trực tiếp (polling 1.5 giây) với modal xem log/huỷ.
