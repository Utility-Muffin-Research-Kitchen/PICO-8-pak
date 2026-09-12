/* Import local Splore favourites after PICO-8 and its downloaders have stopped.
 * SQLite is a private ownership journal, never Leaf's library database. */
#define _DEFAULT_SOURCE 1
#define _DARWIN_C_SOURCE
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <openssl/evp.h>
#include <png.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#define CART_MAX (2 * 1024 * 1024)
static sqlite3 *db;
static int errors, imported, updated;
static char home[PATH_MAX], root[PATH_MAX], cache[PATH_MAX], library[PATH_MAX];
static void problem(const char *id, const char *what) {
    fprintf(stderr, "PICO-8 import: %s: %s\n", id, what); errors++;
}
static bool path(char *out, const char *base, const char *name) {
    int n = snprintf(out, PATH_MAX, "%s/%s", base, name);
    return n > 0 && n < PATH_MAX;
}
static bool regular(const char *p) {
    struct stat st;
    return lstat(p, &st) == 0 && S_ISREG(st.st_mode);
}
static bool directory(const char *p) {
    struct stat st;
    return lstat(p, &st) == 0 && S_ISDIR(st.st_mode);
}
static bool id_ok(const char *s) {
    if (!*s || strlen(s) > 96) return false;
    for (; *s; s++) if (!((*s >= 'a' && *s <= 'z') ||
        (*s >= '0' && *s <= '9') || *s == '_')) return false;
    return true;
}
static char *trim(char *s) {
    while (*s == ' ' || *s == '\r' || *s == '\n') s++;
    size_t n = strlen(s);
    while (n && (s[n-1] == ' ' || s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = 0;
    return s;
}
static bool title_ok(const char *s) {
    if (!*s || strlen(s) > 255) return false;
    for (; *s; s++) if ((unsigned char)*s < 32 || (unsigned char)*s == 127) return false;
    return true;
}
/* Keep supplied titles verbatim. Only prettify a fallback that is exactly the
 * BBS ID; numbers may be part of a real title, so never guess them away. */
static const char *display_title(const char *id, const char *title, char out[256]) {
    if (strcmp(id, title)) return title;
    size_t n = 0;
    bool word = true;
    for (const char *p = id; *p; p++) {
        if (*p == '_') {
            if (n && out[n-1] != ' ') out[n++] = ' ';
            word = true;
        } else {
            out[n++] = word && *p >= 'a' && *p <= 'z' ? *p - 'a' + 'A' : *p;
            word = false;
        }
    }
    if (n && out[n-1] == ' ') n--;
    out[n] = 0;
    return n ? out : id;
}
static int revision(const char *s, const char *id, const char *suffix) {
    size_t n = strlen(id);
    if (strncmp(s, id, n) || s[n] != '-' || s[n+1] < '0' || s[n+1] > '9') return -1;
    char *end;
    errno = 0;
    long v = strtol(s+n+1, &end, 10);
    return !errno && v >= 0 && v <= INT_MAX && !strcmp(end, suffix) ? (int)v : -1;
}
static bool sql(const char *s) {
    if (sqlite3_exec(db, s, NULL, NULL, NULL) == SQLITE_OK) return true;
    problem("journal", sqlite3_errmsg(db)); return false;
}
static bool row(const char *id, int rev, const char *hash, int pending, const char *next) {
    sqlite3_stmt *st = NULL;
    bool ok = sqlite3_prepare_v2(db, "UPDATE imports SET revision=?,hash=?,pending_revision=?,pending_hash=? WHERE id=?", -1, &st, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int(st, 1, rev); sqlite3_bind_text(st, 2, hash, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 3, pending); sqlite3_bind_text(st, 4, next, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, id, -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(st) == SQLITE_DONE;
    }
    sqlite3_finalize(st);
    if (!ok) problem(id, "cannot persist ownership journal");
    return ok;
}
static unsigned char *read_bytes(const char *p, size_t *size, char hash[65]) {
    int fd = open(p, O_RDONLY | O_NOFOLLOW);
    struct stat st;
    if (fd < 0) return NULL;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 1 || st.st_size > CART_MAX) { close(fd); return NULL; }
    *size = (size_t)st.st_size;
    unsigned char *data = malloc(*size);
    size_t off = 0;
    while (data && off < *size) {
        ssize_t n = read(fd, data+off, *size-off);
        if (n <= 0) break;
        off += (size_t)n;
    }
    close(fd);
    unsigned char digest[EVP_MAX_MD_SIZE]; unsigned int length = 0;
    if (!data || off != *size || !EVP_Digest(data, *size, digest, &length, EVP_sha256(), NULL)) { free(data); return NULL; }
    for (unsigned int i = 0; i < length; i++) sprintf(hash+i*2, "%02x", digest[i]);
    return data;
}
static unsigned long be32(const unsigned char *p) {
    return ((unsigned long)p[0]<<24) | ((unsigned long)p[1]<<16) | ((unsigned long)p[2]<<8) | p[3];
}
static bool cart_ok(const unsigned char *p, size_t n) {
    if (n < 33 || memcmp(p, "\211PNG\r\n\032\n", 8)) return false;
    bool end = false;
    for (size_t off = 8; off < n;) {
        if (n-off < 12) return false;
        size_t len = be32(p+off);
        if (len > n-off-12 || crc32(0, p+off+4, (uInt)(len+4)) != be32(p+off+8+len)) return false;
        bool iend = !memcmp(p+off+4, "IEND", 4);
        off += len+12;
        if (iend) { end = len == 0 && off == n; break; }
    }
    if (!end) return false;
    png_image im = {0}; im.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&im, p, n)) return false;
    bool ok = im.width == 160 && im.height == 205;
    im.format = PNG_FORMAT_RGBA;
    unsigned char *pixels = ok ? malloc(PNG_IMAGE_SIZE(im)) : NULL;
    ok = pixels && png_image_finish_read(&im, NULL, pixels, 0, NULL);
    free(pixels); png_image_free(&im);
    return ok;
}
static bool sync_dir(const char *p) {
    int fd = open(p, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) return false;
    bool ok = fsync(fd) == 0;
    close(fd); return ok;
}
static bool write_atomic(const char *dest, const void *data, size_t size, const char *dir) {
    char tmp[PATH_MAX];
    int n = snprintf(tmp, sizeof(tmp), "%s/.leaf-import-XXXXXX", dir);
    if (n < 0 || n >= (int)sizeof(tmp)) return false;
    int fd = mkstemp(tmp);
    if (fd < 0) return false;
    const unsigned char *p = data; size_t off = 0;
    while (off < size) {
        ssize_t count = write(fd, p+off, size-off);
        if (count <= 0) break;
        off += (size_t)count;
    }
    bool ok = off == size && fsync(fd) == 0;
    if (close(fd)) ok = false;
    if (ok) ok = rename(tmp, dest) == 0 && sync_dir(dir);
    unlink(tmp); return ok;
}
static bool favourites(void) {
    char p[PATH_MAX];
    if (!sql("UPDATE imports SET favourite=0") || !path(p, home, "favourites.txt")) return false;
    if (access(p, F_OK) != 0 && errno == ENOENT) return true;
    if (!regular(p)) return false;
    FILE *f = fopen(p, "r"); if (!f) return false;
    sqlite3_stmt *st = NULL;
    bool ok = sqlite3_prepare_v2(db,
        "INSERT INTO imports(id,title,favourite) VALUES(?,?,1) ON CONFLICT(id) DO UPDATE SET title=excluded.title,favourite=1", -1, &st, NULL) == SQLITE_OK;
    char line[2048];
    while (ok && fgets(line, sizeof(line), f)) {
        if (!strchr(line, '\n') && !feof(f)) { problem("favourites", "overlong row"); int c; while ((c=fgetc(f)) != '\n' && c != EOF) {} continue; }
        char *fields[7], *cursor = line; int count = 0;
        while (count < 7 && cursor) {
            fields[count++] = cursor; char *sep = strchr(cursor, '|');
            if (sep) { *sep = 0; cursor = sep+1; } else cursor = NULL;
        }
        for (int i=0; i<count; i++) fields[i] = trim(fields[i]);
        if (count != 7 || cursor || *fields[0] || !id_ok(fields[2]) || revision(fields[1], fields[2], "") < 0) {
            problem("favourites", "unsupported or malformed row skipped"); continue;
        }
        const char *title = title_ok(fields[6]) ? fields[6] : fields[2];
        sqlite3_bind_text(st, 1, fields[2], -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, title, -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_reset(st); sqlite3_clear_bindings(st);
    }
    if (ferror(f)) ok = false;
    sqlite3_finalize(st); fclose(f); return ok;
}
static void import_one(const char *id, const char *title, int rev, const char *hash,
                       int pending, const char *next, bool favourite, FILE *report) {
    char name[128], dest[PATH_MAX];
    snprintf(name, sizeof(name), "%s.p8.png", id);
    if (!path(dest, library, name)) return;
    struct stat st;
    bool exists = lstat(dest, &st) == 0;
    if (!exists && errno != ENOENT) { problem(id, "cannot inspect destination"); return; }
    char current[65] = ""; size_t size = 0;
    unsigned char *bytes = exists ? read_bytes(dest, &size, current) : NULL;
    free(bytes);
    char installed[65]; snprintf(installed, sizeof(installed), "%s", hash);
    if (exists && pending >= 0 && !strcmp(current, next)) {
        if (!row(id, pending, next, -1, "")) return;
        rev = pending; snprintf(installed, sizeof(installed), "%s", next);
    }
    if (exists && (!*current || !*installed || strcmp(current, installed))) {
        problem(id, "destination is unrelated or locally modified; keeping it"); return;
    }
    if (!exists && !favourite) return;
    DIR *d = opendir(cache);
    int newest = -1; char source[PATH_MAX] = "";
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d))) {
            int v = revision(ent->d_name, id, ".p8.png");
            if (v > newest && path(source, cache, ent->d_name)) newest = v;
        }
        closedir(d);
    }
    if (newest >= 0 && newest >= rev && (!exists || newest > rev)) {
        char digest[65] = "";
        bytes = read_bytes(source, &size, digest);
        if (!bytes || !cart_ok(bytes, size)) problem(id, "cached revision is incomplete or invalid; keeping existing cart");
        else if (row(id, rev, installed, newest, digest)) {
            /* Journal the intended hash first. A crash after rename is recovered
               only if the destination matches those exact bytes. */
            if (!write_atomic(dest, bytes, size, library)) problem(id, "copy failed; retry on next Splore exit");
            else if (row(id, newest, digest, -1, "")) {
                if (exists) updated++; else imported++;
                exists = true; rev = newest;
                snprintf(installed, sizeof(installed), "%s", digest);
            }
        }
        free(bytes);
    } else if (!exists) problem(id, "favourite has no complete cached revision");
    if (exists && rev >= 0 && *installed) {
        char readable[256];
        fprintf(report, "%s\t%s\n", id, display_title(id, title, readable));
    }
}
int main(void) {
    const char *h = getenv("UMRK_PICO8_HOME_PATH"), *r = getenv("UMRK_PICO8_ROOT_PATH");
    if (!h || !r || !realpath(h, home) || !realpath(r, root) ||
        !path(cache, home, "bbs/carts") || !directory(cache) ||
        !path(library, root, "Splore") || (mkdir(library, 0755) && errno != EEXIST) || !directory(library)) return 1;
    char p[PATH_MAX];
    if (!path(p, home, "splore-import.lock")) return 1;
    int lock = open(p, O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) return 1;
    if (!path(p, home, "splore-imports.sqlite3")) return 1;
    struct stat st;
    if (!lstat(p, &st) && !S_ISREG(st.st_mode)) return 1;
    if (sqlite3_open(p, &db) != SQLITE_OK) return 1;
    sqlite3_busy_timeout(db, 1000);
    if (!sql("PRAGMA synchronous=FULL; CREATE TABLE IF NOT EXISTS imports("
        "id TEXT PRIMARY KEY,title TEXT NOT NULL,revision INTEGER NOT NULL DEFAULT -1,"
        "hash TEXT NOT NULL DEFAULT '',pending_revision INTEGER NOT NULL DEFAULT -1,"
        "pending_hash TEXT NOT NULL DEFAULT '',favourite INTEGER NOT NULL DEFAULT 0)")) return 1;
    if (!sql("BEGIN IMMEDIATE")) return 1;
    if (!favourites()) { sql("ROLLBACK"); problem("favourites", "cannot read favourites; keeping journal"); }
    else if (!sql("COMMIT")) return 1;
    /* The journal itself records ownership; the report contains only verified
       installed carts for Jawaka's ordinary scan metadata step. */
    FILE *report = tmpfile(); if (!report) return 1;
    sqlite3_stmt *rows = NULL;
    if (sqlite3_prepare_v2(db, "SELECT id,title,revision,hash,pending_revision,pending_hash,favourite FROM imports ORDER BY id", -1, &rows, NULL) != SQLITE_OK) return 1;
    typedef struct { char id[97], title[256], hash[65], next[65]; int rev, pending, favourite; } item;
    item *items = NULL; size_t count = 0;
    int result;
    while ((result = sqlite3_step(rows)) == SQLITE_ROW) {
        const char *raw = (const char *)sqlite3_column_text(rows, 0);
        const char *t = (const char *)sqlite3_column_text(rows, 1);
        if (!raw || !id_ok(raw) || !t || !title_ok(t)) { problem("journal", "invalid row"); continue; }
        item *grown = count < 10000 ? realloc(items, (count+1)*sizeof(*items)) : NULL;
        if (!grown) { result = SQLITE_NOMEM; break; }
        items = grown; item *it = &items[count++];
        snprintf(it->id,sizeof(it->id),"%s",raw); snprintf(it->title,sizeof(it->title),"%s",t);
        snprintf(it->hash,sizeof(it->hash),"%s",sqlite3_column_text(rows,3));
        snprintf(it->next,sizeof(it->next),"%s",sqlite3_column_text(rows,5));
        it->rev=sqlite3_column_int(rows,2); it->pending=sqlite3_column_int(rows,4); it->favourite=sqlite3_column_int(rows,6);
    }
    sqlite3_finalize(rows);
    if (result != SQLITE_DONE) { free(items); return 1; }
    /* End the SELECT before writes: each ownership update must commit durably
       before its corresponding filesystem rename. */
    for (size_t i=0; i<count; i++) {
        item *it=&items[i];
        import_one(it->id,it->title,it->rev,it->hash,it->pending,it->next,it->favourite!=0,report);
    }
    free(items);
    long length = ftell(report); char *body = length >= 0 ? malloc((size_t)length+1) : NULL;
    bool ok = body && !ferror(report) && fseek(report,0,SEEK_SET)==0 && fread(body,1,(size_t)length,report)==(size_t)length;
    if (!ok || !path(p,home,"splore-library.tsv") || !write_atomic(p,body,(size_t)(length<0?0:length),home)) problem("library", "cannot publish import report");
    free(body); fclose(report); sqlite3_close(db); close(lock);
    fprintf(stderr,"PICO-8 import: added=%d updated=%d errors=%d\n",imported,updated,errors);
    return errors ? 1 : 0;
}
