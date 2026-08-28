#ifdef __linux__
#define _GNU_SOURCE   
#endif
#define _POSIX_C_SOURCE 200809L
#include "altair_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <setjmp.h>
#include <ctype.h>

#ifndef DISABLE_FNUMLIST

AltairFNumList *altair_fnumlist_new(void) {
    AltairFNumList *list = (AltairFNumList*)malloc(sizeof(AltairFNumList));
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
    return list;
}

void altair_fnumlist_free(AltairFNumList *list) {
    if (!list) return;
    free(list->items);
    free(list);
}

void altair_fnumlist_append(AltairFNumList *list, double value) {
    if (!list) return;
    if (list->len >= list->cap) {
        int new_cap = list->cap ? list->cap * 2 : 8;
        list->items = (double*)realloc(list->items, sizeof(double) * new_cap);
        list->cap = new_cap;
    }
    list->items[list->len++] = value;
}

AltairVal *altair_fnumlist_to_val(AltairFNumList *list) {
    if (!list) return altair_list_new();
    AltairVal *val = altair_list_new();
    for (int i = 0; i < list->len; i++) {
        altair_list_append(val, altair_num(list->items[i]));
    }
    return val;
}
#endif

#ifndef DISABLE_SB

AltairSB *altair_sb_new(void) {
    AltairSB *sb = (AltairSB*)malloc(sizeof(AltairSB));
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
    return sb;
}

void altair_sb_free(AltairSB *sb) {
    if (!sb) return;
    free(sb->buf);
    free(sb);
}

void altair_sb_append(AltairSB *sb, const char *str) {
    if (!sb || !str) return;
    size_t str_len = strlen(str);
    if (sb->len + str_len >= sb->cap) {
        size_t new_cap = sb->cap ? sb->cap * 2 : 64;
        while (new_cap < sb->len + str_len + 1) new_cap *= 2;
        sb->buf = (char*)realloc(sb->buf, new_cap);
        sb->cap = new_cap;
    }
    memcpy(sb->buf + sb->len, str, str_len);
    sb->len += str_len;
    sb->buf[sb->len] = '\0';
}

void altair_sb_append_val(AltairSB *sb, AltairVal *val) {
    if (!sb || !val) return;
    char *str = altair_val_tostr(val);
    altair_sb_append(sb, str);
    free(str);
}

AltairVal *altair_sb_to_val(AltairSB *sb) {
    if (!sb) return altair_str("");
    return altair_str_own(strdup(sb->buf));
}
#endif

static void altair_persist_save_all(void);

#ifdef __linux__
#  include <sys/mman.h>
#  include <sys/sysinfo.h>
#  include <sys/statvfs.h>
#  include <unistd.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <signal.h>
#  include <pthread.h>
#  include <fcntl.h>
#  include <sys/ioctl.h>
#  include <linux/fs.h>
#  define ALT_LBA_RAW_SUPPORTED 1
#elif defined(__APPLE__)
#  include <sys/mman.h>
#  include <mach/mach.h>
#  include <sys/statvfs.h>
#  include <unistd.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <signal.h>
#  include <pthread.h>
#elif defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOGDI
#    define NOGDI
#  endif
#  ifndef NOUSER
#    define NOUSER
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  include <direct.h>
#  pragma comment(lib, "ws2_32.lib")
#endif

char _altair_prog_name[128]    = "altair";
char _altair_prog_version[64]  = "1.0";
char _altair_prog_author[128]  = "";
char _altair_storage_dir[512]  = "";

jmp_buf     _altair_jmp_stack[ALT_TRY_MAX];
AltairError _altair_err_stack[ALT_TRY_MAX];
int         _altair_try_depth = 0;

static AltairVar *g_var_head = NULL;

#define VAR_HASH_BUCKETS 1024
static AltairVar *g_var_hash[VAR_HASH_BUCKETS];

static inline unsigned var_hash(const char *s) {
    unsigned h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h & (VAR_HASH_BUCKETS - 1);
}

#define MAX_TEMP 4096
static struct { void *ptr; size_t sz; } g_temp[MAX_TEMP];
static int g_ntemp = 0;

AltairServer        _altair_server = {0};
AltairRequest      *_altair_req    = NULL;
AltairResponse     *_altair_res    = NULL;
volatile int        _altair_server_running = 0;
AltairHealthCheck  *_altair_health_checks  = NULL;
AltairMetric        _altair_metrics[64];
int                 _altair_nmetrics = 0;
AltairJob          *_altair_jobs = NULL;

static void mkdirp(const char *path) {
#ifdef _WIN32
    char tmp[512]; snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp+1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p; *p = 0;
            _mkdir(tmp);
            *p = saved;
        }
    }
    _mkdir(tmp);
#else
    char tmp[512]; snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp+1; *p; p++) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
#endif
}

static void build_path(char *out, size_t sz, AltStorage stor,
                        const char *var_name) {
    const char *sub = (stor==ALT_DISK)  ? "disk"  :
                      (stor==ALT_CACHE) ? "cache" : "disk";
    snprintf(out, sz, "%s%s/%s.altv", _altair_storage_dir, sub, var_name);
}

static void build_ttl_path(char *out, size_t sz, const char *var_name) {
    snprintf(out, sz, "%scache/%s.ttl", _altair_storage_dir, var_name);
}

static void *ram_alloc(size_t sz) {
    void *p = malloc(sz);
    if (!p) return NULL;
#if defined(__linux__) || defined(__APPLE__)
    mlock(p, sz);
#elif defined(_WIN32)
    VirtualLock(p, sz);
#endif
    return p;
}

static void ram_free(void *p, size_t sz) {
    if (!p) return;
#if defined(__linux__) || defined(__APPLE__)
    munlock(p, sz);
#elif defined(_WIN32)
    VirtualUnlock(p, sz);
#endif
    free(p);
}

void altair_init(const char *name, const char *version, const char *author) {
#ifdef _WIN32

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    strncpy(_altair_prog_name,    name,    sizeof(_altair_prog_name)-1);
    strncpy(_altair_prog_version, version, sizeof(_altair_prog_version)-1);
    strncpy(_altair_prog_author,  author,  sizeof(_altair_prog_author)-1);
    srand((unsigned)time(NULL));

    const char *home = getenv("HOME");
#ifdef _WIN32
    if (!home) home = getenv("USERPROFILE");
    if (!home) home = getenv("APPDATA");
#endif
    if (!home) home = ".";

    snprintf(_altair_storage_dir, sizeof(_altair_storage_dir),
             "%s/.altair/%s/", home, name);

#ifdef _WIN32
    for (char *p = _altair_storage_dir; *p; p++)
        if (*p == '/') *p = '\\';
    size_t dlen = strlen(_altair_storage_dir);
    if (dlen > 0 && _altair_storage_dir[dlen-1] != '\\') {
        _altair_storage_dir[dlen] = '\\';
        _altair_storage_dir[dlen+1] = '\0';
    }
#endif

    char sub[600];
    snprintf(sub, sizeof(sub), "%sdisk",  _altair_storage_dir); mkdirp(sub);
    snprintf(sub, sizeof(sub), "%scache", _altair_storage_dir); mkdirp(sub);
    snprintf(sub, sizeof(sub), "%ssnap",  _altair_storage_dir); mkdirp(sub);

#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
#endif
}

void altair_shutdown(void) {

    _altair_server_running = 0;

    if (_altair_server.shutdown_fn) _altair_server.shutdown_fn();

    altair_persist_save_all();

    AltairVar *v = g_var_head;
    while (v) {
        AltairVar *nx = v->next;
        if (v->val) { altair_val_free(v->val); v->val = NULL; }
        if (v->storage == ALT_TEMP) {
            memset(v, 0, sizeof(AltairVar));
            free(v);
        } else if (v->storage == ALT_RAM) {
            ram_free(v, sizeof(AltairVar));
        } else {
            free(v);
        }
        v = nx;
    }
    g_var_head = NULL;
    memset(g_var_hash, 0, sizeof(g_var_hash));

    for (int i=0; i<g_ntemp; i++) {
        memset(g_temp[i].ptr, 0, g_temp[i].sz);
        free(g_temp[i].ptr);
    }
    g_ntemp = 0;

    AltairRoute *r = _altair_server.routes;
    while (r) { AltairRoute *nx=r->next; free(r); r=nx; }
    AltairMiddleware *m = _altair_server.middlewares;
    while (m) { AltairMiddleware *nx=m->next; free(m); m=nx; }
    AltairHealthCheck *h = _altair_health_checks;
    while (h) { AltairHealthCheck *nx=(AltairHealthCheck*)h->next; free(h); h=nx; }
    AltairJob *j = _altair_jobs;
    while (j) { AltairJob *nx=(AltairJob*)j->next; free(j); j=nx; }

#ifdef _WIN32
    WSACleanup();
#endif
}

#ifdef _WIN32

void altair_pause_if_own_console(void) {
    DWORD pids[2];
    DWORD n = GetConsoleProcessList(pids, 2);
    if (n <= 1) {
        fprintf(stdout, "\nPresiona ENTER para salir...");
        fflush(stdout);
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {  }
    }
}
#endif

void altair_throw(const char *code, const char *msg, int line) {
    if (_altair_try_depth > 0) {

        int idx = _altair_try_depth - 1;
        strncpy(_altair_err_stack[idx].code,    code, 15);
        strncpy(_altair_err_stack[idx].message, msg,  255);
        _altair_err_stack[idx].line   = line;
        _altair_err_stack[idx].active = 1;
        longjmp(_altair_jmp_stack[idx], 1);
    } else {
        fprintf(stderr, "\n[Altair Error %s]\n%s\n", code, msg);
        if (line > 0) fprintf(stderr, "  at line %d\n", line);
        altair_shutdown();
        exit(1);
    }
}

#ifdef DEBUG_LEAKS
static int total_vals = 0;
#endif

static AltairVal *g_val_pool = NULL;
static AltairVal *val_pool_alloc(void) {
    AltairVal *v = g_val_pool;
    if (v) { g_val_pool = *(AltairVal**)v; return v; }
    #ifdef DEBUG_LEAKS
    total_vals++;
    #endif
    return (AltairVal*)malloc(sizeof(AltairVal));
}
static void val_pool_free(AltairVal *v) {
    *(AltairVal**)v = g_val_pool;
    g_val_pool = v;
    #ifdef DEBUG_LEAKS
    total_vals--;
    #endif
}

AltairVal *altair_num(double n) {
    AltairVal *v = val_pool_alloc();
    v->type = ALT_NUMERIC; v->num = n; v->num_is_int = 0; v->num_i64 = 0;
    return v;
}
AltairVal *altair_num_i64(long long n) {
    AltairVal *v = val_pool_alloc();
    v->type = ALT_NUMERIC; v->num = (double)n; v->num_is_int = 1; v->num_i64 = n;
    return v;
}
AltairVal *altair_str(const char *s) {
    AltairVal *v = val_pool_alloc();
    v->type = ALT_TEXT; v->str = strdup(s ? s : ""); v->str_cap = strlen(v->str)+1; return v;
}

AltairVal *altair_str_own(char *s) {
    AltairVal *v = val_pool_alloc();
    v->type = ALT_TEXT; v->str = s ? s : strdup(""); v->str_cap = strlen(v->str)+1; return v;
}
AltairVal *altair_bool(int b) {
    AltairVal *v = val_pool_alloc();
    v->type = ALT_BOOL; v->boolean = b ? 1 : 0; return v;
}
AltairVal *altair_list_new(void) {
    AltairVal *v = val_pool_alloc();
    v->type = ALT_LIST; v->list.items=NULL; v->list.len=0; v->list.cap=0;
    return v;
}
AltairVal *altair_token_new(AltairVal *inner) {
    AltairVal *v = val_pool_alloc();
    v->type = ALT_TOKEN; v->tok.inner=altair_val_copy(inner); v->tok.consumed=0;
    return v;
}

AltairVal *altair_val_copy(const AltairVal *v) {
    if (!v) return altair_num(0);
    AltairVal *c = val_pool_alloc();
    *c = *v;
    if (v->type==ALT_TEXT)  { c->str = strdup(v->str ? v->str : ""); c->str_cap = strlen(c->str)+1; }
    if (v->type==ALT_LIST) {
        c->list.items = (AltairVal**)malloc(sizeof(AltairVal*)*(v->list.cap>0?v->list.cap:1));
        c->list.cap   = v->list.cap>0?v->list.cap:1;
        c->list.len   = v->list.len;
        for (int i=0;i<v->list.len;i++) c->list.items[i]=altair_val_copy(v->list.items[i]);
    }
    if (v->type==ALT_TOKEN) c->tok.inner=altair_val_copy(v->tok.inner);
    if (v->type==ALT_OBJECT && v->obj) {
        AltairObj *src = v->obj;
        AltairObj *dst = (AltairObj*)calloc(1, sizeof(AltairObj));
        strncpy(dst->class_name, src->class_name, sizeof(dst->class_name)-1);
        dst->nfields = src->nfields;
        if (src->nfields > 0) {
            dst->field_names = (char**)malloc(sizeof(char*)*src->nfields);
            dst->fields      = (AltairVal**)malloc(sizeof(AltairVal*)*src->nfields);
            for (int i=0; i<src->nfields; i++) {
                dst->field_names[i] = strdup(src->field_names[i]);
                dst->fields[i]      = altair_val_copy(src->fields[i]);
            }
        }
        dst->nmethods    = 0;
        dst->method_names= NULL;
        dst->methods     = NULL;
        c->obj = dst;
    }
    return c;
}

void altair_val_free(AltairVal *v) {
    if (!v) return;
    if (v->type==ALT_TEXT)   { free(v->str); v->str=NULL; }
    if (v->type==ALT_LIST)   {
        for(int i=0;i<v->list.len;i++) altair_val_free(v->list.items[i]);
        free(v->list.items);
    }
    if (v->type==ALT_TOKEN)  altair_val_free(v->tok.inner);
    if (v->type==ALT_OBJECT && v->obj) {
        altair_obj_free(v->obj); v->obj=NULL;
    }
    val_pool_free(v);
}

char *altair_val_tostr(const AltairVal *v) {
    if (!v) return strdup("null");
    char buf[256];
    switch (v->type) {
    case ALT_NUMERIC:
        if (v->num_is_int)
            snprintf(buf, sizeof(buf), "%lld", v->num_i64);
        else if (v->num == (long long)v->num)
            snprintf(buf, sizeof(buf), "%lld", (long long)v->num);
        else
            snprintf(buf, sizeof(buf), "%g", v->num);
        return strdup(buf);
    case ALT_TEXT:   return strdup(v->str ? v->str : "");
    case ALT_BOOL:   return strdup(v->boolean ? "true" : "false");
    case ALT_LIST: {

        size_t cap = 64; size_t used = 0;
        char *res = (char*)malloc(cap);
        res[0]='['; used=1;
        for (int i=0; i<v->list.len; i++) {
            char *e = altair_val_tostr(v->list.items[i]);
            size_t el = strlen(e);
            size_t need = used + el + 4;
            if (need > cap) {
                while (cap < need) cap *= 2;
                res = (char*)realloc(res, cap);
            }
            if (i>0) { memcpy(res+used,", ",2); used+=2; }
            memcpy(res+used, e, el); used+=el;
            free(e);
        }
        if (used+2 > cap) res = (char*)realloc(res, used+2);
        res[used++]=']'; res[used]='\0';
        return res;
    }
    case ALT_OBJECT: return strdup("[object]");
    case ALT_TOKEN:  return v->tok.consumed ? strdup("[token:consumed]")
                                            : altair_val_tostr(v->tok.inner);
    default: return strdup("null");
    }
}

void altair_var_register(AltairVar *v) {
    v->prev = NULL;
    v->next = g_var_head;
    if (g_var_head) g_var_head->prev = v;
    g_var_head = v;

    unsigned h = var_hash(v->name);
    v->hprev = NULL;
    v->hnext = g_var_hash[h];
    if (g_var_hash[h]) g_var_hash[h]->hprev = v;
    g_var_hash[h] = v;
}
void altair_var_unregister(const char *name) {
    unsigned h = var_hash(name);
    AltairVar *v = g_var_hash[h];
    while (v && strcmp(v->name, name)!=0) v = v->hnext;
    if (!v) return;

    if (v->hprev) v->hprev->hnext = v->hnext; else g_var_hash[h] = v->hnext;
    if (v->hnext) v->hnext->hprev = v->hprev;

    if (v->prev) v->prev->next = v->next; else g_var_head = v->next;
    if (v->next) v->next->prev = v->prev;
}
AltairVar *altair_var_lookup(const char *name) {
    unsigned h = var_hash(name);
    for (AltairVar *v=g_var_hash[h]; v; v=v->hnext)
        if (strcmp(v->name, name)==0) return v;
    return NULL;
}

static AltairVar *var_alloc_base(const char *name, AltVType t, AltStorage stor,
                                  int is_const, int weight, double expire_secs) {
    AltairVar *v;
    if (stor == ALT_RAM) {
        v = (AltairVar*)ram_alloc(sizeof(AltairVar));
        memset(v, 0, sizeof(AltairVar));
    } else if (stor == ALT_TEMP) {
        v = (AltairVar*)malloc(sizeof(AltairVar));
        memset(v, 0, sizeof(AltairVar));
    } else {
        v = (AltairVar*)calloc(1, sizeof(AltairVar));
    }
    strncpy(v->name, name, sizeof(v->name)-1);
    v->vtype    = t;
    v->storage  = stor;
    v->is_const = is_const;
    v->weight   = weight;
    v->expire_at= expire_secs > 0 ? (time_t)(time(NULL)+expire_secs) : 0;
    v->val      = NULL;
    altair_var_register(v);
    return v;
}

AltairVar *altair_var_new(const char *name, AltVType t, AltStorage stor,
                           int is_const, int weight, double expire_secs) {
    if (stor==ALT_DISK || stor==ALT_CACHE) {
        AltairVar *v = var_alloc_base(name, t, stor, is_const, weight, expire_secs);
        AltairVal *loaded = altair_disk_load(name, stor);
        if (loaded) v->val = loaded;
        return v;
    }
    return var_alloc_base(name, t, stor, is_const, weight, expire_secs);
}

AltairVar *altair_var_orbit(const char *name, AltVType t,
                             AltOrbitEntry *entries, int n) {
    AltStorage first_stor = n>0 ? entries[0].storage : ALT_RAM;
    AltairVar *v = var_alloc_base(name, t, first_stor, 0, 0, 0);
    for (int i=0; i<n && i<16; i++) v->orbit[i] = entries[i];
    v->norbit      = n;
    v->orbit_state = 0;
    v->storage     = first_stor;
    return v;
}

AltairVar *altair_var_prefer(const char *name, AltVType t,
                              AltStorage *prefs, int n) {
    AltStorage chosen = ALT_AUTO;
    for (int i=0; i<n; i++) {
        if (prefs[i]==ALT_RAM) { chosen=ALT_RAM; break; }
        chosen = prefs[i]; break;
    }
    AltairVar *v = var_alloc_base(name, t, chosen, 0, 0, 0);
    for (int i=0; i<n && i<8; i++) v->prefer[i]=prefs[i];
    v->nprefer = n;
    return v;
}

static void check_expire(AltairVar *v) {
    if (v->expire_at && time(NULL) > v->expire_at) {
        if (v->val) { altair_val_free(v->val); v->val=NULL; }
        if (v->storage==ALT_DISK||v->storage==ALT_CACHE)
            altair_disk_delete(v->name, v->storage);
    }
}

void altair_var_set(AltairVar *v, AltairVal *val) {
    if (!v || !val) return;
    check_expire(v);
    if (v->is_const && v->val) {
        altair_throw("ALT0007", "Cannot reassign a const variable.", 0);
        return;
    }
    if (v->val) altair_val_free(v->val);
    v->val = altair_val_copy(val);
    if (v->storage==ALT_DISK || v->storage==ALT_CACHE)
        altair_disk_save(v->name, v->val, v->storage, v->expire_at);
}

void altair_var_set_own(AltairVar *v, AltairVal *val) {
    if (!v) { altair_val_free(val); return; }
    if (!val) return;
    check_expire(v);
    if (v->is_const && v->val) {
        altair_val_free(val);
        altair_throw("ALT0007", "Cannot reassign a const variable.", 0);
        return;
    }
    if (v->val) altair_val_free(v->val);
    v->val = val;
    if (v->storage==ALT_DISK || v->storage==ALT_CACHE)
        altair_disk_save(v->name, v->val, v->storage, v->expire_at);
}

AltairVal *altair_var_get(AltairVar *v) {
    if (!v) { altair_throw("ALT0001","Unknown variable.",0); return altair_num(0); }
    check_expire(v);
    if (!v->val) {
        if (v->storage==ALT_DISK || v->storage==ALT_CACHE)
            v->val = altair_disk_load(v->name, v->storage);
        if (!v->val) v->val = altair_num(0);
    }
    return v->val;
}


AltairVal *altair_text_append_owned(AltairVal *dst, AltairVal *addend) {
    char *add_str = altair_val_tostr(addend);
    altair_val_free(addend);
    size_t add_len = strlen(add_str);

    if (!dst) {
        AltairVal *v = altair_str_own(add_str);
        v->str_cap = add_len + 1;
        return v;
    }

    if (dst->type != ALT_TEXT) {
        char *base = altair_val_tostr(dst);
        if (dst->type==ALT_LIST) {
            for (int i=0;i<dst->list.len;i++) altair_val_free(dst->list.items[i]);
            free(dst->list.items);
        } else if (dst->type==ALT_TOKEN) {
            altair_val_free(dst->tok.inner);
        } else if (dst->type==ALT_OBJECT && dst->obj) {
            altair_obj_free(dst->obj);
        }
        size_t base_len = strlen(base);
        size_t total = base_len + add_len + 1;
        char *buf = (char*)malloc(total);
        memcpy(buf, base, base_len);
        memcpy(buf+base_len, add_str, add_len+1);
        free(base); free(add_str);
        dst->type = ALT_TEXT;
        dst->str = buf;
        dst->str_cap = total;
        return dst;
    }

    size_t cur_len = dst->str ? strlen(dst->str) : 0;
    size_t need = cur_len + add_len + 1;
    if (need > dst->str_cap) {
        size_t newcap = dst->str_cap ? dst->str_cap : (cur_len + 1);
        if (newcap < 8) newcap = 8;
        while (newcap < need) newcap *= 2;
        char *nb = (char*)realloc(dst->str, newcap);
        if (nb) { dst->str = nb; dst->str_cap = newcap; }
    }
    if (dst->str_cap >= need) {
        memcpy(dst->str + cur_len, add_str, add_len + 1);
    }
    free(add_str);
    return dst;
}



void altair_var_plus_assign(AltairVar *v, AltairVal *addend, int line) {
    if (!v) { altair_val_free(addend); return; }
    check_expire(v);
    if (v->is_const && v->val) {
        altair_val_free(addend);
        altair_throw("ALT0007", "Cannot reassign a const variable.", 0);
        return;
    }
    AltairVal *cur = altair_var_get(v);
    if (cur->type == ALT_TEXT || (addend && addend->type == ALT_TEXT)) {
        v->val = altair_text_append_owned(cur, addend);
    } else {
        AltairVal *result = altair_add(cur, addend, line);
        altair_val_free(addend);
        altair_val_free(cur);
        v->val = result;
    }
    if (v->storage==ALT_DISK || v->storage==ALT_CACHE)
        altair_disk_save(v->name, v->val, v->storage, v->expire_at);
}

#define ALT_POINT_MAX 256
static void *g_point_registry[ALT_POINT_MAX];
static int g_point_n = 0;

AltairVal *altair_point(AltairVar *v) {
    if (!v) { altair_throw("ALT_POINT_UNKNOWN","Unknown variable in system@point().",0); return altair_num(0); }
    for (int i=0;i<g_point_n;i++) if (g_point_registry[i]==(void*)v) return altair_num((double)(uintptr_t)v);
    if (g_point_n<ALT_POINT_MAX) g_point_registry[g_point_n++]=(void*)v;
    return altair_num((double)(uintptr_t)v);
}

AltairVal *altair_unpoint(double addr) {
    uintptr_t target=(uintptr_t)addr;
    for (int i=0;i<g_point_n;i++) {
        if ((uintptr_t)g_point_registry[i]==target) {
            AltairVar *v=(AltairVar*)g_point_registry[i];
            return altair_val_copy(altair_var_get(v));
        }
    }
    altair_throw("ALT_UNPOINT_INVALID","Address was not produced by system@point().",0);
    return altair_num(0);
}

void altair_var_release(AltairVar **vp) {
    if (!vp || !*vp) return;
    AltairVar *v = *vp;
    altair_var_unregister(v->name);
    if (v->val) { altair_val_free(v->val); v->val = NULL; }
    if (v->storage == ALT_TEMP) {
        memset(v, 0, sizeof(AltairVar));
        free(v);
    } else if (v->storage == ALT_RAM) {
        ram_free(v, sizeof(AltairVar));
    } else {
        free(v);
    }
    *vp = NULL;
}

void altair_var_release_view(AltairVar **vp) {
    if (!vp || !*vp) return;
    AltairVar *v = *vp;
    altair_var_unregister(v->name);
    v->val = NULL;
    if (v->storage == ALT_TEMP) {
        memset(v, 0, sizeof(AltairVar));
        free(v);
    } else if (v->storage == ALT_RAM) {
        ram_free(v, sizeof(AltairVar));
    } else {
        free(v);
    }
    *vp = NULL;
}

void altair_migrate(AltairVar *v, int state_num) {
    if (!v || v->norbit==0) {
        altair_throw("ALT0012","Variable has no orbit states.",0); return;
    }
    for (int i=0; i<v->norbit; i++) {
        if (v->orbit[i].state_num == state_num) {
            AltStorage new_stor = v->orbit[i].storage;
            AltairVal *cur = altair_var_get(v);
            if (new_stor==ALT_DISK || new_stor==ALT_CACHE)
                altair_disk_save(v->name, cur, new_stor, v->expire_at);
            else if (v->storage==ALT_DISK || v->storage==ALT_CACHE)
                altair_disk_delete(v->name, v->storage);
            v->storage     = new_stor;
            v->orbit_state = i;
            return;
        }
    }
    char msg[320];
    snprintf(msg,sizeof(msg),"Orbit state %d not found in '%s'.",state_num,v->name);
    altair_throw("ALT0012", msg, 0);
}

void altair_migrate_name(AltairVar *v, const char *state_name) {
    if (!v || v->norbit==0) {
        altair_throw("ALT0012","Variable has no orbit states.",0); return;
    }
    for (int i=0; i<v->norbit; i++) {
        if (strcmp(v->orbit[i].state_name, state_name)==0) {
            altair_migrate(v, v->orbit[i].state_num); return;
        }
    }
    char msg[320];
    snprintf(msg,sizeof(msg),"Orbit state '%s' not found in '%s'.",state_name,v->name);
    altair_throw("ALT0012", msg, 0);
}

void altair_list_append(AltairVal *list, AltairVal *item) {
    if (!list || list->type!=ALT_LIST) return;
    if (list->list.len >= list->list.cap) {
        int nc = list->list.cap ? list->list.cap*2 : 8;
        list->list.items = (AltairVal**)realloc(list->list.items,sizeof(AltairVal*)*nc);
        list->list.cap   = nc;
    }
    list->list.items[list->list.len++] = altair_val_copy(item);
}
int altair_list_remove(AltairVal *list, int idx) {
    if (!list||list->type!=ALT_LIST) return 0;
    if (idx<0||idx>=list->list.len) return 0;
    altair_val_free(list->list.items[idx]);
    for (int i=idx; i<list->list.len-1; i++) list->list.items[i]=list->list.items[i+1];
    list->list.len--;
    return 1;
}
void altair_list_clear(AltairVal *list) {
    if (!list||list->type!=ALT_LIST) return;
    for (int i=0;i<list->list.len;i++) altair_val_free(list->list.items[i]);
    list->list.len = 0;
}
int altair_list_length(AltairVal *list) {
    if (!list) return 0;
    if (list->type==ALT_TEXT) return list->str ? (int)strlen(list->str) : 0;
    if (list->type!=ALT_LIST) return 0;
    return list->list.len;
}
AltairVal *altair_list_get(AltairVal *list, int idx, int line) {
    if (list && list->type==ALT_TEXT) {
        int len = list->str ? (int)strlen(list->str) : 0;
        if (idx<0||idx>=len) {
            char m[128]; snprintf(m,sizeof(m),"Character index %d out of bounds (length %d).",idx,len);
            altair_throw("ALT0013", m, line);
            return altair_str("");
        }
        char buf[2]; buf[0]=list->str[idx]; buf[1]='\0';
        return altair_str(buf);
    }
    if (!list||list->type!=ALT_LIST||idx<0||idx>=list->list.len) {
        char m[128]; snprintf(m,sizeof(m),"Index %d out of bounds (length %d).",
                              idx, list?list->list.len:0);
        altair_throw("ALT0013", m, line);
        return altair_num(0);
    }
    return list->list.items[idx];
}
void altair_list_set(AltairVal *list, int idx, AltairVal *val, int line) {
    if (!list||list->type!=ALT_LIST||idx<0||idx>=list->list.len) {
        char m[128]; snprintf(m,sizeof(m),"Index %d out of bounds.",idx);
        altair_throw("ALT0013", m, line);
        return;
    }
    altair_val_free(list->list.items[idx]);
    list->list.items[idx] = altair_val_copy(val);
}

#ifndef DISABLE_SB
AltairVal *altair_add(AltairVal *a, AltairVal *b, int line) {
    if (!a||!b) return altair_num(0);
    if (a->type==ALT_TEXT || b->type==ALT_TEXT) {
        AltairSB *sb = altair_sb_new();
        char *sa = altair_val_tostr(a);
        char *sb_str = altair_val_tostr(b);
        altair_sb_append(sb, sa);
        altair_sb_append(sb, sb_str);
        free(sa); free(sb_str);
        AltairVal *result = altair_sb_to_val(sb);
        altair_sb_free(sb);
        return result;
    }
    if (a->type==ALT_NUMERIC && b->type==ALT_NUMERIC)
        return altair_num(a->num + b->num);

    if (a->type==ALT_LIST && b->type==ALT_LIST) {
        AltairVal *r = altair_list_new();
        for (int i=0;i<a->list.len;i++) altair_list_append(r,a->list.items[i]);
        for (int i=0;i<b->list.len;i++) altair_list_append(r,b->list.items[i]);
        return r;
    }
    altair_throw("ALT0002","Cannot add values of incompatible types.",line);
    return altair_num(0);
}
#else
AltairVal *altair_add(AltairVal *a, AltairVal *b, int line) {
    if (!a||!b) return altair_num(0);
    if (a->type==ALT_TEXT || b->type==ALT_TEXT) {
        char *sa = altair_val_tostr(a);
        char *sb_str = altair_val_tostr(b);
        size_t la=strlen(sa), lb=strlen(sb_str);
        char  *r = (char*)malloc(la+lb+1);
        memcpy(r, sa, la);
        memcpy(r+la, sb_str, lb);
        r[la+lb] = '\0';
        free(sa); free(sb_str);
        return altair_str_own(r);
    }
    if (a->type==ALT_NUMERIC && b->type==ALT_NUMERIC)
        return altair_num(a->num + b->num);

    if (a->type==ALT_LIST && b->type==ALT_LIST) {
        AltairVal *r = altair_list_new();
        for (int i=0;i<a->list.len;i++) altair_list_append(r,a->list.items[i]);
        for (int i=0;i<b->list.len;i++) altair_list_append(r,b->list.items[i]);
        return r;
    }
    altair_throw("ALT0002","Cannot add values of incompatible types.",line);
    return altair_num(0);
}
#endif

AltairVal *altair_sub(AltairVal *a, AltairVal *b, int line) {
    if (!a||!b) return altair_num(0);
    if (a->type==ALT_NUMERIC && b->type==ALT_NUMERIC)
        return altair_num(a->num - b->num);
    altair_throw("ALT0002","Subtraction requires numeric operands.",line);
    return altair_num(0);
}
AltairVal *altair_mul(AltairVal *a, AltairVal *b, int line) {
    if (!a||!b) return altair_num(0);
    if (a->type==ALT_NUMERIC && b->type==ALT_NUMERIC)
        return altair_num(a->num * b->num);
    altair_throw("ALT0002","Multiplication requires numeric operands.",line);
    return altair_num(0);
}
AltairVal *altair_div(AltairVal *a, AltairVal *b, int line) {
    if (!a||!b) return altair_num(0);
    if (a->type==ALT_NUMERIC && b->type==ALT_NUMERIC) {
        if (b->num == 0) altair_throw("ALT0010","Division by zero.",line);
        return altair_num(a->num / b->num);
    }
    altair_throw("ALT0002","Division requires numeric operands.",line);
    return altair_num(0);
}
AltairVal *altair_mod(AltairVal *a, AltairVal *b, int line) {
    if (!a||!b) return altair_num(0);
    if (a->type==ALT_NUMERIC && b->type==ALT_NUMERIC) {
        if (b->num == 0) altair_throw("ALT0010","Modulo by zero.",line);
        return altair_num(fmod(a->num, b->num));
    }
    altair_throw("ALT0002","Modulo requires numeric operands.",line);
    return altair_num(0);
}
AltairVal *altair_neg(AltairVal *a) {
    if (!a) return altair_num(0);
    if (a->type==ALT_NUMERIC) return altair_num(-a->num);
    return altair_num(0);
}
AltairVal *altair_not(AltairVal *a) {
    if (!a) return altair_bool(1);
    int t=0;
    if (a->type==ALT_BOOL) t=!a->boolean;
    else if (a->type==ALT_NUMERIC) t=(a->num==0);
    else t=0;
    return altair_bool(t);
}
AltairVal *altair_eq(AltairVal *a, AltairVal *b) {
    if (!a||!b) return altair_bool(a==b);
    if (a->type==ALT_NUMERIC&&b->type==ALT_NUMERIC) return altair_bool(a->num==b->num);
    if (a->type==ALT_TEXT&&b->type==ALT_TEXT) return altair_bool(strcmp(a->str,b->str)==0);
    if (a->type==ALT_BOOL&&b->type==ALT_BOOL) return altair_bool(a->boolean==b->boolean);
    return altair_bool(0);
}
AltairVal *altair_neq(AltairVal *a, AltairVal *b) {
    AltairVal *eq=altair_eq(a,b);
    AltairVal *r=altair_bool(!eq->boolean);
    altair_val_free(eq); return r;
}
AltairVal *altair_lt(AltairVal *a, AltairVal *b, int line) {
    if (!a||!b) return altair_bool(0);
    if (a->type==ALT_NUMERIC&&b->type==ALT_NUMERIC) return altair_bool(a->num<b->num);
    if (a->type==ALT_TEXT&&b->type==ALT_TEXT) return altair_bool(strcmp(a->str,b->str)<0);
    altair_throw("ALT0002","Comparison requires compatible types.",line);
    return altair_bool(0);
}
AltairVal *altair_gt(AltairVal *a, AltairVal *b, int line) {
    if (!a||!b) return altair_bool(0);
    if (a->type==ALT_NUMERIC&&b->type==ALT_NUMERIC) return altair_bool(a->num>b->num);
    if (a->type==ALT_TEXT&&b->type==ALT_TEXT) return altair_bool(strcmp(a->str,b->str)>0);
    altair_throw("ALT0002","Comparison requires compatible types.",line);
    return altair_bool(0);
}
AltairVal *altair_lte(AltairVal *a, AltairVal *b, int line) {
    if (!a||!b) return altair_bool(0);
    if (a->type==ALT_NUMERIC&&b->type==ALT_NUMERIC) return altair_bool(a->num<=b->num);
    if (a->type==ALT_TEXT&&b->type==ALT_TEXT) return altair_bool(strcmp(a->str,b->str)<=0);
    altair_throw("ALT0002","Comparison requires compatible types.",line);
    return altair_bool(0);
}
AltairVal *altair_gte(AltairVal *a, AltairVal *b, int line) {
    if (!a||!b) return altair_bool(0);
    if (a->type==ALT_NUMERIC&&b->type==ALT_NUMERIC) return altair_bool(a->num>=b->num);
    if (a->type==ALT_TEXT&&b->type==ALT_TEXT) return altair_bool(strcmp(a->str,b->str)>=0);
    altair_throw("ALT0002","Comparison requires compatible types.",line);
    return altair_bool(0);
}
AltairVal *altair_and(AltairVal *a, AltairVal *b) {
    if (!a||!b) return altair_bool(0);
    int av = (a->type==ALT_BOOL) ? a->boolean : (a->type==ALT_NUMERIC ? a->num!=0 : 1);
    int bv = (b->type==ALT_BOOL) ? b->boolean : (b->type==ALT_NUMERIC ? b->num!=0 : 1);
    return altair_bool(av && bv);
}
AltairVal *altair_or(AltairVal *a, AltairVal *b) {
    if (!a||!b) return altair_bool(0);
    int av = (a->type==ALT_BOOL) ? a->boolean : (a->type==ALT_NUMERIC ? a->num!=0 : 1);
    int bv = (b->type==ALT_BOOL) ? b->boolean : (b->type==ALT_NUMERIC ? b->num!=0 : 1);
    return altair_bool(av || bv);
}

AltairVal *altair_coerce_num(AltairVal *v, int line) {
    if (!v) return altair_num(0);
    if (v->type==ALT_NUMERIC) return altair_val_copy(v);
    if (v->type==ALT_TEXT) { double d=atof(v->str); return altair_num(d); }
    if (v->type==ALT_BOOL) return altair_num(v->boolean);
    altair_throw("ALT0002","Cannot coerce value to numeric.",line);
    return altair_num(0);
}
AltairVal *altair_coerce_str(AltairVal *v) {
    if (!v) return altair_str("");
    char *s=altair_val_tostr(v);
    AltairVal *r=altair_str(s); free(s); return r;
}

AltairVal *altair_token_use(AltairVar *var, int line) {
    if (!var||!var->val||var->val->type!=ALT_TOKEN) {
        altair_throw("ALT0004","Expected a token variable.",line);
        return altair_num(0);
    }
    if (var->val->tok.consumed) {
        altair_throw("ALT0004","Token already consumed.",line);
        return altair_num(0);
    }
    AltairVal *inner = altair_val_copy(var->val->tok.inner);
    var->val->tok.consumed = 1;
    return inner;
}

AltairObj *altair_obj_new(const char *class_name, int nfields,
                           const char **field_names, AltairVal **field_defaults,
                           AltStorage *field_storage) {
    (void)field_storage;
    AltairObj *o = (AltairObj*)calloc(1, sizeof(AltairObj));
    strncpy(o->class_name, class_name, sizeof(o->class_name)-1);
    o->nfields     = nfields;
    o->field_names = nfields ? (char**)malloc(sizeof(char*)*nfields) : NULL;
    o->fields      = nfields ? (AltairVal**)malloc(sizeof(AltairVal*)*nfields) : NULL;
    for (int i=0; i<nfields; i++) {
        o->field_names[i] = strdup(field_names[i]);
        o->fields[i] = field_defaults[i] ? altair_val_copy(field_defaults[i])
                                         : altair_num(0);
    }
    o->nmethods     = 0;
    o->method_names = NULL;
    o->methods      = NULL;
    return o;
}

void altair_obj_free(AltairObj *obj) {
    if (!obj) return;
    for (int i=0; i<obj->nfields; i++) {
        free(obj->field_names[i]);
        altair_val_free(obj->fields[i]);
    }
    free(obj->field_names);
    free(obj->fields);
    for (int i=0; i<obj->nmethods; i++) free(obj->method_names[i]);
    free(obj->method_names);
    free(obj->methods);
    free(obj);
}

AltairVal *altair_obj_get(AltairObj *obj, const char *field, int line) {
    if (!obj) { altair_throw("ALT0001","Null object access.",line); return altair_num(0); }
    for (int i=0; i<obj->nfields; i++)
        if (strcmp(obj->field_names[i], field)==0)
            return obj->fields[i];
    char m[192]; snprintf(m,sizeof(m),"Field '%s' not found in class '%s'.",field,obj->class_name);
    altair_throw("ALT0014", m, line);
    return altair_num(0);
}

void altair_obj_set(AltairObj *obj, const char *field, AltairVal *val, int line) {
    if (!obj) { altair_throw("ALT0001","Null object assignment.",line); return; }
    for (int i=0; i<obj->nfields; i++) {
        if (strcmp(obj->field_names[i], field)==0) {
            altair_val_free(obj->fields[i]);
            obj->fields[i] = altair_val_copy(val);
            return;
        }
    }

    obj->nfields++;
    obj->field_names = (char**)realloc(obj->field_names, sizeof(char*)*obj->nfields);
    obj->fields      = (AltairVal**)realloc(obj->fields, sizeof(AltairVal*)*obj->nfields);
    obj->field_names[obj->nfields-1] = strdup(field);
    obj->fields[obj->nfields-1]      = altair_val_copy(val);
}

void altair_log(AltairVal *v) {
    char *s = altair_val_tostr(v);
    printf("%s\n", s);
    free(s);
}

AltairVal *altair_user_input(const char *prompt, AltVType expected_type) {
    printf("%s ", prompt);
    fflush(stdout);
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return altair_str("");
    size_t l = strlen(buf);
    while (l>0 && (buf[l-1]=='\n'||buf[l-1]=='\r')) buf[--l]='\0';
    if (expected_type==ALT_NUMERIC) return altair_num(atof(buf));
    if (expected_type==ALT_BOOL)    return altair_bool(strcmp(buf,"true")==0||strcmp(buf,"1")==0);
    return altair_str(buf);
}

int altair_choose(double *weights, int n) {
    double total=0; for(int i=0;i<n;i++) total+=weights[i];
    double r=(double)rand()/RAND_MAX*total;
    double acc=0;
    for(int i=0;i<n;i++){
        acc+=weights[i];
        if(r<=acc) return i;
    }
    return n-1;
}

void altair_wait(double seconds) {
#ifdef _WIN32
    Sleep((DWORD)(seconds*1000));
#else
    struct timespec ts;
    ts.tv_sec  = (time_t)seconds;
    ts.tv_nsec = (long)((seconds-(long)seconds)*1e9);
    nanosleep(&ts, NULL);
#endif
}

static void write_val(FILE *f, AltairVal *v) {
    if (!v) { uint8_t t=ALT_VOID; fwrite(&t,1,1,f); return; }
    uint8_t t=(uint8_t)v->type; fwrite(&t,1,1,f);
    switch(v->type) {
    case ALT_NUMERIC: fwrite(&v->num,sizeof(double),1,f); break;
    case ALT_BOOL:    fwrite(&v->boolean,sizeof(int),1,f); break;
    case ALT_TEXT: {
        uint32_t l=(uint32_t)strlen(v->str); fwrite(&l,4,1,f); fwrite(v->str,1,l,f); break;
    }
    case ALT_LIST: {
        uint32_t n=(uint32_t)v->list.len; fwrite(&n,4,1,f);
        for(int i=0;i<v->list.len;i++) write_val(f,v->list.items[i]); break;
    }
    default: break;
    }
}

static AltairVal *read_val(FILE *f) {
    uint8_t t; if(fread(&t,1,1,f)!=1) return altair_num(0);
    switch((AltVType)t) {
    case ALT_NUMERIC: { double n=0; if(fread(&n,sizeof(double),1,f)!=1) n=0; return altair_num(n); }
    case ALT_BOOL:    { int b=0; if(fread(&b,sizeof(int),1,f)!=1) b=0; return altair_bool(b); }
    case ALT_TEXT: {
        uint32_t l=0; if(fread(&l,4,1,f)!=1) l=0;
        char *s=(char*)malloc(l+1); if(fread(s,1,l,f)!=l){  } s[l]='\0';
        AltairVal *v=altair_str(s); free(s); return v;
    }
    case ALT_LIST: {
        uint32_t n=0; if(fread(&n,4,1,f)!=1) n=0;
        AltairVal *v=altair_list_new();
        for(uint32_t i=0;i<n;i++){
            AltairVal *item=read_val(f);
            altair_list_append(v,item); altair_val_free(item);
        }
        return v;
    }
    default: return altair_num(0);
    }
}

void altair_disk_save(const char *var_name, AltairVal *v,
                       AltStorage stor, time_t expire_at) {
    char path[600]; build_path(path, sizeof(path), stor, var_name);
    FILE *f=fopen(path,"wb"); if(!f) return;
    fwrite("ALTV",1,4,f); fwrite(&expire_at,sizeof(time_t),1,f);
    write_val(f, v);
    fclose(f);
    if (stor==ALT_CACHE && expire_at>0) {
        char ttl[600]; build_ttl_path(ttl, sizeof(ttl), var_name);
        FILE *ft=fopen(ttl,"wb"); if(ft){ fwrite(&expire_at,sizeof(time_t),1,ft); fclose(ft); }
    }
}

AltairVal *altair_disk_load(const char *var_name, AltStorage stor) {
    char path[600]; build_path(path, sizeof(path), stor, var_name);
    if (stor==ALT_CACHE) {
        char ttl[600]; build_ttl_path(ttl, sizeof(ttl), var_name);
        FILE *ft=fopen(ttl,"rb");
        if (ft) {
            time_t exp=0; if(fread(&exp,sizeof(time_t),1,ft)!=1) exp=0; fclose(ft);
            if (exp>0 && time(NULL)>exp) { remove(path); remove(ttl); return NULL; }
        }
    }
    FILE *f=fopen(path,"rb"); if(!f) return NULL;
    char magic[4]; if(fread(magic,1,4,f)!=4){ fclose(f); return NULL; }
    if (memcmp(magic,"ALTV",4)!=0) { fclose(f); return NULL; }
    time_t exp; if(fread(&exp,sizeof(time_t),1,f)!=1){ fclose(f); return NULL; }
    if (exp>0 && time(NULL)>exp) { fclose(f); remove(path); return NULL; }
    AltairVal *v = read_val(f);
    fclose(f);
    return v;
}

void altair_disk_delete(const char *var_name, AltStorage stor) {
    char path[600]; build_path(path, sizeof(path), stor, var_name);
    remove(path);
    char ttl[600]; build_ttl_path(ttl, sizeof(ttl), var_name);
    remove(ttl);
}

static uint32_t crc32_update(uint32_t crc, const void *buf, size_t len) {
    const uint8_t *p=(const uint8_t*)buf;
    crc=~crc;
    while(len--) {
        crc^=*p++;
        for(int i=0;i<8;i++) crc=(crc>>1)^(0xEDB88320&-(crc&1));
    }
    return ~crc;
}

static void snap_path(char *out, size_t sz, const char *snap_name) {
    snprintf(out, sz, "%ssnap/%s.alts", _altair_storage_dir, snap_name);
}

void altair_snapshot_create(const char *snap_name, int line) {
    char path[600]; snap_path(path, sizeof(path), snap_name);
    FILE *f=fopen(path,"wb");
    if (!f) {
        char m[256]; snprintf(m,sizeof(m),"Cannot create snapshot '%s': %s",snap_name,strerror(errno));
        altair_throw("ALT0005",m,line); return;
    }
    uint32_t crc=0;
#define CW(data,sz) do{ fwrite(data,1,sz,f); crc=crc32_update(crc,data,sz); }while(0)
    CW("ALTS",4);
    uint16_t spec=17; CW(&spec,2);
    uint32_t nl=(uint32_t)strlen(_altair_prog_name); CW(&nl,4); CW(_altair_prog_name,nl);
    uint32_t vl=(uint32_t)strlen(_altair_prog_version); CW(&vl,4); CW(_altair_prog_version,vl);
    int64_t ts=(int64_t)time(NULL); CW(&ts,8);
    int count=0;
    for(AltairVar *v=g_var_head; v; v=v->next) count++;
    uint32_t vc=(uint32_t)count; CW(&vc,4);
    for(AltairVar *v=g_var_head; v; v=v->next) {
        uint32_t nlen=(uint32_t)strlen(v->name); CW(&nlen,4); CW(v->name,nlen);
        uint8_t vt=v->vtype, stor=v->storage, ic=v->is_const;
        CW(&vt,1); CW(&stor,1); CW(&ic,1);
        int64_t exp=(int64_t)v->expire_at; CW(&exp,8);
        int32_t wt=v->weight; CW(&wt,4);

        char tmppath[620]; snprintf(tmppath,sizeof(tmppath),"%s.tmp",path);
        FILE *tmp=fopen(tmppath,"wb"); write_val(tmp, v->val); fclose(tmp);
        FILE *tmp2=fopen(tmppath,"rb"); fseek(tmp2,0,SEEK_END);
        uint32_t vsz=(uint32_t)ftell(tmp2); fseek(tmp2,0,SEEK_SET);
        CW(&vsz,4);
        char vbuf[65536]; uint32_t rem=vsz;
        while(rem>0){
            uint32_t chunk=rem<sizeof(vbuf)?rem:(uint32_t)sizeof(vbuf);
            if(fread(vbuf,1,chunk,tmp2)!=(size_t)chunk){  } CW(vbuf,chunk); rem-=chunk;
        }
        fclose(tmp2); remove(tmppath);
    }
    CW(&crc,4);
#undef CW
    fclose(f);
}

void altair_snapshot_restore(const char *snap_name, int line) {
    char path[600]; snap_path(path, sizeof(path), snap_name);
    FILE *f=fopen(path,"rb");
    if (!f) {
        char m[256]; snprintf(m,sizeof(m),"Snapshot '%s' not found.",snap_name);
        altair_throw("ALT0005",m,line); return;
    }
    fseek(f,0,SEEK_END); long fsz=ftell(f); fseek(f,0,SEEK_SET);
    if (fsz<8) { fclose(f); altair_throw("ALT0005","Snapshot file corrupt.",line); return; }
    char *buf=(char*)malloc(fsz);
    if(fread(buf,1,fsz,f)!=(size_t)fsz){  } fclose(f);
    uint32_t stored_crc; memcpy(&stored_crc, buf+fsz-4, 4);
    uint32_t calc_crc=crc32_update(0, buf, fsz-4);
    if (stored_crc!=calc_crc) { free(buf); altair_throw("ALT0005","Snapshot checksum mismatch.",line); return; }
    size_t pos=0;
#define RD(dst,sz) do{ memcpy(dst,buf+pos,sz); pos+=sz; }while(0)
    char magic[4]; RD(magic,4);
    if (memcmp(magic,"ALTS",4)!=0) { free(buf); altair_throw("ALT0005","Invalid snapshot magic.",line); return; }
    uint16_t spec; RD(&spec,2);
    uint32_t nl; RD(&nl,4); pos+=nl;
    uint32_t vl; RD(&vl,4); pos+=vl;
    int64_t ts; RD(&ts,8);
    uint32_t var_count; RD(&var_count,4);
    for(uint32_t i=0; i<var_count; i++) {
        uint32_t nlen; RD(&nlen,4);
        char vname[128]={0}; uint32_t rd=nlen<127?nlen:127;
        memcpy(vname,buf+pos,rd); pos+=nlen;
        uint8_t vt,stor,ic; RD(&vt,1); RD(&stor,1); RD(&ic,1);
        int64_t exp; RD(&exp,8);
        int32_t wt; RD(&wt,4);
        uint32_t vsz; RD(&vsz,4);
        AltairVar *existing = altair_var_lookup(vname);

        char tmppath[620]; snprintf(tmppath,sizeof(tmppath),"%s.rst.tmp",path);
        FILE *tmp=fopen(tmppath,"wb"); fwrite(buf+pos,1,vsz,tmp); fclose(tmp);
        pos+=vsz;
        FILE *tmp2=fopen(tmppath,"rb");
        AltairVal *v=read_val(tmp2); fclose(tmp2); remove(tmppath);
        if (existing) {
            altair_var_set(existing, v);
        }
        altair_val_free(v);
    }
#undef RD
    free(buf);
}

void altair_snapshot_delete(const char *snap_name, int line) {
    char path[600]; snap_path(path, sizeof(path), snap_name);
    if (remove(path) != 0) {
        char m[256]; snprintf(m,sizeof(m),"Cannot delete snapshot '%s'.",snap_name);
        altair_throw("ALT0005",m,line);
    }
}

AltairVal *altair_system(const char *key) {
    if(strcmp(key,"time")==0)    return altair_num((double)time(NULL));
    if(strcmp(key,"random")==0)  return altair_num((double)rand()/RAND_MAX);
    if(strcmp(key,"pid")==0)     {
#ifdef _WIN32
        return altair_num((double)GetCurrentProcessId());
#else
        return altair_num((double)getpid());
#endif
    }
    if(strcmp(key,"hostname")==0) {
#ifdef _WIN32
        char h[256]={0}; DWORD l=256; GetComputerNameA(h,&l); return altair_str(h);
#else
        char h[256]={0}; gethostname(h,sizeof(h)); return altair_str(h);
#endif
    }
    if(strcmp(key,"username")==0) {
#ifdef _WIN32
        const char *u=getenv("USERNAME"); return altair_str(u?u:"");
#else
        const char *u=getenv("USER"); return altair_str(u?u:"");
#endif
    }
    if(strcmp(key,"os")==0) {
#if defined(__linux__)
        return altair_str("linux");
#elif defined(__APPLE__)
        return altair_str("macos");
#elif defined(_WIN32)
        return altair_str("windows");
#else
        return altair_str("unknown");
#endif
    }
    if(strcmp(key,"memory")==0) {
#ifdef __linux__
        struct sysinfo si; sysinfo(&si);
        return altair_num((double)(si.totalram-si.freeram));
#else
        return altair_num(0);
#endif
    }
    if(strcmp(key,"diskfree")==0) {
#if defined(__linux__)||defined(__APPLE__)
        struct statvfs sv; statvfs(".", &sv);
        return altair_num((double)(sv.f_bavail*(uint64_t)sv.f_bsize));
#else
        return altair_num(0);
#endif
    }
    char m[128]; snprintf(m,sizeof(m),"Unknown system key '%s'.",key);
    altair_throw("ALT0011",m,0); return altair_num(0);
}

AltairVal *altair_compiler(const char *key) {
    if(strcmp(key,"version")==0)      return altair_str("1.8.5vB");
    if(strcmp(key,"name")==0)         return altair_str("altairc");
    if(strcmp(key,"build")==0)        return altair_str(__DATE__);
    if(strcmp(key,"architecture")==0){
#if defined(__x86_64__)||defined(_M_X64)
        return altair_str("x86_64");
#elif defined(__aarch64__)
        return altair_str("arm64");
#else
        return altair_str("unknown");
#endif
    }
    char m[128]; snprintf(m,sizeof(m),"Unknown compiler key '%s'.",key);
    altair_throw("ALT0011",m,0); return altair_num(0);
}

AltairVal *altair_program(const char *key) {
    if(strcmp(key,"name")==0)    return altair_str(_altair_prog_name);
    if(strcmp(key,"version")==0) return altair_str(_altair_prog_version);
    if(strcmp(key,"author")==0)  return altair_str(_altair_prog_author);
    char m[128]; snprintf(m,sizeof(m),"Unknown program key '%s'.",key);
    altair_throw("ALT0011",m,0); return altair_num(0);
}

AltairVal *altair_var_system(AltairVar *v, const char *key) {
    if(!v) { altair_throw("ALT0001","Unknown variable.",0); return altair_num(0); }
    static const char *stor_names[]={"ram","disk","cache","temp","auto"};
    if(strcmp(key,"storage")==0) return altair_str(stor_names[v->storage]);
    if(strcmp(key,"weight")==0)  return altair_num(v->weight);
    if(strcmp(key,"orbit")==0) {
        if(v->norbit>0) return altair_str(v->orbit[v->orbit_state].state_name);
        return altair_str("none");
    }
    if(strcmp(key,"type")==0){
        static const char *tnames[]={"numeric","text","bool","list","object","token","void"};
        return altair_str(tnames[v->vtype]);
    }
    if(strcmp(key,"size")==0){
        char *s=altair_val_tostr(v->val); size_t l=strlen(s); free(s);
        return altair_num((double)l);
    }
    if(strcmp(key,"value")==0) return v->val ? altair_val_copy(v->val) : altair_num(0);
    char m[128]; snprintf(m,sizeof(m),"Unknown var introspect key '%s'.",key);
    altair_throw("ALT0011",m,0); return altair_num(0);
}

void altair_server_init(int port) {
    _altair_server.port = port;
    _altair_server.routes = NULL;
    _altair_server.middlewares = NULL;
    _altair_server.shutdown_fn = NULL;
}

void altair_server_add_route(const char *method, const char *path,
                              AltairRouteHandler handler, int rate_limit) {
    AltairRoute *r = (AltairRoute*)calloc(1, sizeof(AltairRoute));
    strncpy(r->method, method, 15);
    strncpy(r->path, path, 511);
    r->handler    = handler;
    r->rate_limit = rate_limit;
    r->next       = NULL;

    if (!_altair_server.routes) { _altair_server.routes=r; }
    else {
        AltairRoute *cur=_altair_server.routes;
        while(cur->next) cur=cur->next;
        cur->next=r;
    }
}

void altair_server_add_middleware(AltairMiddlewareFn fn) {
    AltairMiddleware *m = (AltairMiddleware*)malloc(sizeof(AltairMiddleware));
    m->fn = fn; m->next = NULL;
    if (!_altair_server.middlewares) { _altair_server.middlewares=m; }
    else {
        AltairMiddleware *cur=_altair_server.middlewares;
        while(cur->next) cur=cur->next;
        cur->next=m;
    }
}

void altair_server_set_shutdown(void (*fn)(void)) {
    _altair_server.shutdown_fn = fn;
}

static void url_decode(char *dst, const char *src, size_t dstlen) {
    size_t j=0;
    for (size_t i=0; src[i] && j<dstlen-1; i++) {
        if (src[i]=='%' && isxdigit((unsigned char)src[i+1]) && isxdigit((unsigned char)src[i+2])) {
            char hex[3]={src[i+1],src[i+2],0};
            dst[j++]=(char)strtol(hex,NULL,16); i+=2;
        } else if (src[i]=='+') {
            dst[j++]=' ';
        } else {
            dst[j++]=src[i];
        }
    }
    dst[j]='\0';
}

static int path_match(const char *pattern, const char *actual, AltairRequest *req) {
    req->nparams=0;
    const char *p=pattern, *a=actual;
    while (*p && *a) {
        if (*p==':') {

            p++;
            char pname[64]={0}; int pi=0;
            while (*p && *p!='/' && pi<63) pname[pi++]=*p++;

            char pval[128]={0}; int vi=0;
            while (*a && *a!='/' && vi<127) pval[vi++]=*a++;
            if (req->nparams<32) {
                strncpy(req->params[req->nparams][0], pname, 127);
                strncpy(req->params[req->nparams][1], pval, 127);
                req->nparams++;
            }
        } else if (*p!=*a) { return 0; }
        else { p++; a++; }
    }

    while (*p=='/') p++;
    while (*a=='/') a++;
    return *p=='\0' && *a=='\0';
}

static void parse_request(int fd, AltairRequest *req) {
    char buf[65536]={0}; int total=0;
    while (total<(int)sizeof(buf)-1) {
        int n=recv(fd, buf+total, sizeof(buf)-1-total, 0);
        if (n<=0) break;
        total+=n;
        if (strstr(buf,"\r\n\r\n")) break;
    }

    char *line=buf, *end=strstr(buf,"\r\n");
    if (!end) return;
    *end='\0';
    char method[16]={0}, rawpath[512]={0};
    sscanf(line, "%15s %511s", method, rawpath);
    strncpy(req->method, method, 15);

    char *qs=strchr(rawpath,'?');
    if(qs) { *qs++='\0';  }
    url_decode(req->path, rawpath, sizeof(req->path));

    req->nheaders=0;
    char *h=end+2;
    while (*h && strncmp(h,"\r\n",2)!=0 && req->nheaders<64) {
        char *hend=strstr(h,"\r\n"); if(!hend) break;
        *hend='\0';
        char *colon=strchr(h,':');
        if (colon) {
            *colon='\0';
            char *v=colon+1; while(*v==' ')v++;
            strncpy(req->headers[req->nheaders][0], h, 255);
            strncpy(req->headers[req->nheaders][1], v, 255);
            req->nheaders++;
        }
        h=hend+2;
    }

    char *body=strstr(buf+total-(total>4?4:total),"\r\n\r\n");
    if (!body) body=strstr(buf,"\r\n\r\n");
    if (body) strncpy(req->body, body+4, sizeof(req->body)-1);
}

static void send_response(int fd, AltairResponse *res) {
    char header[1024];
    int hl=snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\nX-Powered-By: Altair/1.8.5vB\r\n\r\n",
        res->status,
        res->status==200?"OK":res->status==201?"Created":res->status==401?"Unauthorized":
        res->status==404?"Not Found":res->status==429?"Too Many Requests":"Error",
        res->content_type[0]?res->content_type:"text/plain",
        (int)strlen(res->body));
    send(fd, header, hl, 0);
    send(fd, res->body, (int)strlen(res->body), 0);
}

#define RATE_LIMIT_WINDOW 60
static struct { char path[512]; char method[16]; long calls; time_t window_start; } _rate_limits[256];
static int _n_rate_limits=0;

static int check_rate_limit(const char *method, const char *path, int max_per_minute) {
    if (!max_per_minute) return 1;
    time_t now=time(NULL);
    for (int i=0;i<_n_rate_limits;i++) {
        if (strcmp(_rate_limits[i].path,path)==0 && strcmp(_rate_limits[i].method,method)==0) {
            if (now - _rate_limits[i].window_start > RATE_LIMIT_WINDOW) {
                _rate_limits[i].calls=1; _rate_limits[i].window_start=now; return 1;
            }
            if (_rate_limits[i].calls >= max_per_minute) return 0;
            _rate_limits[i].calls++; return 1;
        }
    }
    if (_n_rate_limits<256) {
        strncpy(_rate_limits[_n_rate_limits].path, path, 511);
        strncpy(_rate_limits[_n_rate_limits].method, method, 15);
        _rate_limits[_n_rate_limits].calls=1;
        _rate_limits[_n_rate_limits].window_start=now;
        _n_rate_limits++;
    }
    return 1;
}

static void handle_connection(int fd) {
    AltairRequest req; memset(&req,0,sizeof(req));
    AltairResponse res; memset(&res,0,sizeof(res));
    res.status=200;
    strncpy(res.content_type,"text/plain",63);
    parse_request(fd, &req);

    for (AltairMiddleware *m=_altair_server.middlewares; m; m=m->next) {
        if (!m->fn(&req,&res)) { send_response(fd,&res); return; }
    }

    int matched=0;
    for (AltairRoute *r=_altair_server.routes; r; r=r->next) {
        if (strcmp(r->method, req.method)!=0 && strcmp(r->method,"*")!=0) continue;
        if (!path_match(r->path, req.path, &req)) continue;
        matched=1;
        if (!check_rate_limit(req.method, r->path, r->rate_limit)) {
            res.status=429;
            strncpy(res.body,"{\"error\":\"rate limit exceeded\"}",sizeof(res.body)-1);
            strncpy(res.content_type,"application/json",63);
        } else {
            _altair_req=&req; _altair_res=&res;
            r->handler(&req,&res);
        }
        break;
    }
    if (!matched) {
        res.status=404;
        snprintf(res.body,sizeof(res.body),"{\"error\":\"route not found: %s %s\"}",req.method,req.path);
        strncpy(res.content_type,"application/json",63);
    }
    send_response(fd, &res);
}

void altair_server_run(void) {
#if defined(_WIN32)||defined(__linux__)||defined(__APPLE__)
    int srv=socket(AF_INET,SOCK_STREAM,0);
    if(srv<0){ fprintf(stderr,"[altair] socket() failed\n"); return; }
    int opt=1;
#ifdef _WIN32
    setsockopt(srv,SOL_SOCKET,SO_REUSEADDR,(const char*)&opt,sizeof(opt));
#else
    setsockopt(srv,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    signal(SIGPIPE, SIG_IGN);
#endif
    struct sockaddr_in addr={0};
    addr.sin_family=AF_INET; addr.sin_port=htons(_altair_server.port);
    addr.sin_addr.s_addr=INADDR_ANY;
    if(bind(srv,(struct sockaddr*)&addr,sizeof(addr))<0){
        fprintf(stderr,"[altair] bind() failed on port %d: %s\n",_altair_server.port,strerror(errno));
        return;
    }
    listen(srv,64);
    _altair_server_running=1;
    fprintf(stderr,"[altair] Server listening on port %d\n",_altair_server.port);
    while(_altair_server_running){
        struct sockaddr_in cli; socklen_t clilen=sizeof(cli);
#ifdef _WIN32
        int fd=accept(srv,(struct sockaddr*)&cli,&clilen);
#else
        int fd=accept(srv,(struct sockaddr*)&cli,&clilen);
#endif
        if(fd<0) continue;
        altair_jobs_tick();
        handle_connection(fd);
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
    }
#ifdef _WIN32
    closesocket(srv);
#else
    close(srv);
#endif
#else
    fprintf(stderr,"[altair] HTTP server not supported on this platform\n");
#endif
}

void altair_server_stop(void) { _altair_server_running=0; }

void altair_respond_json(AltairResponse *res, AltairVal *v) {
    if (!res) return;
    char *s=altair_val_tostr(v);

    if (v && (v->type==ALT_TEXT)) {

        size_t l=strlen(s);
        char *js=(char*)malloc(l*2+4); int ji=0;
        js[ji++]='"';
        for(size_t i=0;i<l;i++){
            if(s[i]=='"'||s[i]=='\\') js[ji++]='\\';
            if(s[i]=='\n'){js[ji++]='\\';js[ji++]='n';}
            else js[ji++]=s[i];
        }
        js[ji++]='"'; js[ji]='\0';
        snprintf(res->body, sizeof(res->body), "%s", js);
        free(js);
    } else {
        snprintf(res->body, sizeof(res->body), "%s", s);
    }
    free(s);
    strncpy(res->content_type,"application/json",63);
}

void altair_respond_text(AltairResponse *res, AltairVal *v) {
    if (!res) return;
    char *s=altair_val_tostr(v);
    snprintf(res->body, sizeof(res->body), "%s", s);
    free(s);
    strncpy(res->content_type,"text/plain",63);
}

void altair_respond_status(AltairResponse *res, int status) {
    if (!res) return;
    res->status=status;
}

void altair_respond_stop(AltairResponse *res) {
    (void)res;
}

AltairVal *altair_req_param(AltairRequest *req, const char *name) {
    if (!req) return altair_str("");
    for (int i=0;i<req->nparams;i++)
        if (strcmp(req->params[i][0],name)==0) return altair_str(req->params[i][1]);
    return altair_str("");
}

AltairVal *altair_req_header(AltairRequest *req, const char *name) {
    if (!req) return altair_str("");
    for (int i=0;i<req->nheaders;i++)
        if (strcasecmp(req->headers[i][0],name)==0) return altair_str(req->headers[i][1]);
    return altair_str("");
}

AltairVal *altair_req_body(AltairRequest *req) {
    return req ? altair_str(req->body) : altair_str("");
}

void altair_health_add(const char *name, int (*fn)(void)) {
    AltairHealthCheck *h=(AltairHealthCheck*)malloc(sizeof(AltairHealthCheck));
    strncpy(h->name,name,63); h->fn=fn; h->next=(struct AltairHealthCheck*)_altair_health_checks;
    _altair_health_checks=h;
}

void altair_health_handler(AltairRequest *req, AltairResponse *res) {
    (void)req;
    char buf[4096]; int bi=0;
    bi+=snprintf(buf+bi,sizeof(buf)-bi,"{\"status\":\"ok\",\"checks\":{");
    int first=1;
    for(AltairHealthCheck *h=_altair_health_checks;h;h=(AltairHealthCheck*)h->next){
        int ok=h->fn();
        bi+=snprintf(buf+bi,sizeof(buf)-bi,"%s\"%s\":\"%s\"",first?"":",",h->name,ok?"ok":"fail");
        first=0;
        if(!ok) res->status=503;
    }
    bi+=snprintf(buf+bi,sizeof(buf)-bi,"}}");
    snprintf(res->body,sizeof(res->body),"%s",buf);
    strncpy(res->content_type,"application/json",63);
}

void altair_metric_inc(const char *name) {
    for(int i=0;i<_altair_nmetrics;i++)
        if(strcmp(_altair_metrics[i].name,name)==0){_altair_metrics[i].value++;return;}
    if(_altair_nmetrics<64){
        strncpy(_altair_metrics[_altair_nmetrics].name,name,63);
        _altair_metrics[_altair_nmetrics].value=1;
        _altair_metrics[_altair_nmetrics].is_histogram=0;
        _altair_nmetrics++;
    }
}

void altair_metric_observe(const char *name, double val) {
    (void)val;
    altair_metric_inc(name);
}

void altair_metrics_handler(AltairRequest *req, AltairResponse *res) {
    (void)req;
    char buf[8192]; int bi=0;
    for(int i=0;i<_altair_nmetrics;i++)
        bi+=snprintf(buf+bi,sizeof(buf)-bi,"# TYPE %s counter\n%s %lld\n",
                     _altair_metrics[i].name,_altair_metrics[i].name,_altair_metrics[i].value);
    snprintf(res->body,sizeof(res->body),"%s",buf);
    strncpy(res->content_type,"text/plain; version=0.0.4",63);
}

typedef struct { char id[64]; char key[64]; char value[512]; time_t expires; struct AltairSession *next; } AltairSessionNode;
static AltairSessionNode *_sessions=NULL;

AltairVal *altair_session_get(const char *session_id, const char *key) {
    time_t now=time(NULL);
    for(AltairSessionNode *s=_sessions;s;s=(AltairSessionNode*)s->next){
        if(s->expires&&s->expires<now) continue;
        if(strcmp(s->id,session_id)==0&&strcmp(s->key,key)==0) return altair_str(s->value);
    }
    return altair_str("");
}

void altair_session_set(const char *session_id, const char *key, AltairVal *val, int ttl_secs) {
    char *sv=altair_val_tostr(val);

    for(AltairSessionNode *s=_sessions;s;s=(AltairSessionNode*)s->next){
        if(strcmp(s->id,session_id)==0&&strcmp(s->key,key)==0){
            strncpy(s->value,sv,511); s->expires=ttl_secs?time(NULL)+ttl_secs:0;
            free(sv); return;
        }
    }
    AltairSessionNode *s=(AltairSessionNode*)malloc(sizeof(AltairSessionNode));
    strncpy(s->id,session_id,63); strncpy(s->key,key,63); strncpy(s->value,sv,511);
    s->expires=ttl_secs?time(NULL)+ttl_secs:0;
    s->next=(struct AltairSession*)_sessions; _sessions=s;
    free(sv);
}

AltairVal *altair_config_env(const char *key, const char *default_val, int required) {
    const char *v=getenv(key);
    if(!v){
        if(required){
            char m[256]; snprintf(m,sizeof(m),"Required env var '%s' is not set.",key);
            altair_throw("ALT0015",m,0);
        }
        return altair_str(default_val?default_val:"");
    }
    return altair_str(v);
}

void altair_job_register(const char *name, void (*fn)(void), long interval_secs) {
    AltairJob *j=(AltairJob*)malloc(sizeof(AltairJob));
    strncpy(j->name,name,63); j->fn=fn; j->interval_secs=interval_secs;
    j->next_run=time(NULL)+interval_secs;
    j->next=(struct AltairJob*)_altair_jobs; _altair_jobs=j;
}

void altair_jobs_tick(void) {
    time_t now=time(NULL);
    for(AltairJob *j=_altair_jobs;j;j=(AltairJob*)j->next){
        if(j->next_run<=now){ j->fn(); j->next_run=now+j->interval_secs; }
    }
}

static long long altair_as_int(AltairVal *v) {
    if (!v) return 0;
    if (v->type==ALT_NUMERIC) return v->num_is_int ? v->num_i64 : (long long)v->num;
    if (v->type==ALT_BOOL)    return v->boolean ? 1 : 0;
    return 0;
}

AltairVal *altair_band(AltairVal *a, AltairVal *b, int line) {
    if (a && b && a->type==ALT_NUMERIC && b->type==ALT_NUMERIC)
        return altair_num((double)(altair_as_int(a) & altair_as_int(b)));
    altair_throw("ALT0002","Bitwise '&' requires numeric operands.",line);
    return altair_num(0);
}
AltairVal *altair_bor(AltairVal *a, AltairVal *b, int line) {
    if (a && b && a->type==ALT_NUMERIC && b->type==ALT_NUMERIC)
        return altair_num((double)(altair_as_int(a) | altair_as_int(b)));
    altair_throw("ALT0002","Bitwise '|' requires numeric operands.",line);
    return altair_num(0);
}
AltairVal *altair_bxor(AltairVal *a, AltairVal *b, int line) {
    if (a && b && a->type==ALT_NUMERIC && b->type==ALT_NUMERIC)
        return altair_num((double)(altair_as_int(a) ^ altair_as_int(b)));
    altair_throw("ALT0002","Bitwise '^' requires numeric operands.",line);
    return altair_num(0);
}
AltairVal *altair_bnot(AltairVal *a, int line) {
    if (a && a->type==ALT_NUMERIC)
        return altair_num((double)(~altair_as_int(a)));
    altair_throw("ALT0002","Bitwise '~' requires a numeric operand.",line);
    return altair_num(0);
}
AltairVal *altair_shl(AltairVal *a, AltairVal *b, int line) {
    if (a && b && a->type==ALT_NUMERIC && b->type==ALT_NUMERIC)
        return altair_num((double)(altair_as_int(a) << altair_as_int(b)));
    altair_throw("ALT0002","Bitwise '<<' requires numeric operands.",line);
    return altair_num(0);
}
AltairVal *altair_shr(AltairVal *a, AltairVal *b, int line) {
    if (a && b && a->type==ALT_NUMERIC && b->type==ALT_NUMERIC)
        return altair_num((double)(altair_as_int(a) >> altair_as_int(b)));
    altair_throw("ALT0002","Bitwise '>>' requires numeric operands.",line);
    return altair_num(0);
}

AltairVal *altair_new_file(void *fp) {
    AltairVal *v=(AltairVal*)malloc(sizeof(AltairVal));
    v->type=ALT_FILE; v->ptr=fp;
    return v;
}
AltairVal *altair_new_ptr(void *p) {
    AltairVal *v=(AltairVal*)malloc(sizeof(AltairVal));
    v->type=ALT_POINTER; v->ptr=p;
    return v;
}

#define ALT_PTR_TABLE_BUCKETS 512
typedef struct AltPtrEntry {
    void *base;
    size_t size;
    struct AltPtrEntry *next;
} AltPtrEntry;
static AltPtrEntry *g_ptr_table[ALT_PTR_TABLE_BUCKETS];

static unsigned alt_ptr_hash(const void *p) {
    uintptr_t h = (uintptr_t)p;
    h ^= h >> 15;
    h *= 2654435761u;
    return (unsigned)(h & (ALT_PTR_TABLE_BUCKETS - 1));
}
static void alt_ptr_register(void *p, size_t size) {
    unsigned h = alt_ptr_hash(p);
    AltPtrEntry *e = (AltPtrEntry*)malloc(sizeof(AltPtrEntry));
    e->base = p; e->size = size; e->next = g_ptr_table[h];
    g_ptr_table[h] = e;
}
static size_t alt_ptr_valid_size(void *p) {
    if (!p) return 0;
    unsigned h = alt_ptr_hash(p);
    for (AltPtrEntry *e=g_ptr_table[h]; e; e=e->next)
        if (e->base==p) return e->size;
    return 0;
}
static int alt_ptr_release(void *p) {
    if (!p) return 0;
    unsigned h = alt_ptr_hash(p);
    AltPtrEntry **pp = &g_ptr_table[h];
    while (*pp) {
        if ((*pp)->base==p) {
            AltPtrEntry *dead=*pp;
            *pp = dead->next;
            free(dead);
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

static const char *altair_storage_path(const char *rel) {
    static char buf[1024];
    if (rel[0]=='/' || (isalpha((unsigned char)rel[0]) && rel[1]==':')) {
        snprintf(buf,sizeof(buf),"%s",rel);
    } else {
        snprintf(buf,sizeof(buf),"%s",rel);
    }
    return buf;
}

AltairVal *_fn_open(AltairVal *path) {
    if (!path || path->type!=ALT_TEXT) { altair_val_free(path); return altair_new_file(NULL); }
    FILE *fp=fopen(altair_storage_path(path->str),"r");
    altair_val_free(path);
    return altair_new_file(fp);
}
AltairVal *_fn_open_write(AltairVal *path) {
    if (!path || path->type!=ALT_TEXT) { altair_val_free(path); return altair_new_file(NULL); }
    FILE *fp=fopen(altair_storage_path(path->str),"w");
    altair_val_free(path);
    return altair_new_file(fp);
}
AltairVal *_fn_open_append(AltairVal *path) {
    if (!path || path->type!=ALT_TEXT) { altair_val_free(path); return altair_new_file(NULL); }
    FILE *fp=fopen(altair_storage_path(path->str),"a");
    altair_val_free(path);
    return altair_new_file(fp);
}
AltairVal *_fn_read(AltairVal *file) {
    if (!file || file->type!=ALT_FILE || !file->ptr) return altair_str("");
    FILE *fp=(FILE*)file->ptr;
    long start=ftell(fp);
    fseek(fp,0,SEEK_END);
    long end=ftell(fp);
    fseek(fp,start,SEEK_SET);
    long n = end>start ? end-start : 0;
    char *buf=(char*)malloc((size_t)n+1);
    size_t rd = n>0 ? fread(buf,1,(size_t)n,fp) : 0;
    buf[rd]='\0';
    return altair_str_own(buf);
}
AltairVal *_fn_read_line(AltairVal *file) {
    if (!file || file->type!=ALT_FILE || !file->ptr) return altair_str("");
    FILE *fp=(FILE*)file->ptr;
    char buf[65536];
    if (!fgets(buf,sizeof(buf),fp)) return altair_str("");
    size_t l=strlen(buf);
    while (l>0 && (buf[l-1]=='\n'||buf[l-1]=='\r')) buf[--l]='\0';
    return altair_str(buf);
}
AltairVal *_fn_write(AltairVal *file, AltairVal *text) {
    if (!file || file->type!=ALT_FILE || !file->ptr) { altair_val_free(text); return altair_bool(0); }
    char *s=altair_val_tostr(text);
    fputs(s,(FILE*)file->ptr);
    free(s);
    altair_val_free(text);
    return altair_bool(1);
}
AltairVal *_fn_close(AltairVal *file) {
    if (file && file->type==ALT_FILE && file->ptr) {
        fclose((FILE*)file->ptr);
        file->ptr=NULL;
    }
    return altair_bool(1);
}
AltairVal *_fn_create_file(AltairVal *path) {
    if (!path || path->type!=ALT_TEXT) { altair_val_free(path); return altair_bool(0); }
    FILE *fp=fopen(altair_storage_path(path->str),"a");
    altair_val_free(path);
    if (!fp) return altair_bool(0);
    fclose(fp);
    return altair_bool(1);
}
AltairVal *_fn_delete_file(AltairVal *path) {
    if (!path || path->type!=ALT_TEXT) { altair_val_free(path); return altair_bool(0); }
    int rc = remove(altair_storage_path(path->str));
    altair_val_free(path);
    return altair_bool(rc==0);
}
AltairVal *_fn_mkdir(AltairVal *path) {
    if (!path || path->type!=ALT_TEXT) { altair_val_free(path); return altair_bool(0); }
#ifdef _WIN32
    int rc = mkdir(altair_storage_path(path->str));
#else
    int rc = mkdir(altair_storage_path(path->str),0755);
#endif
    int ok = (rc==0 || errno==EEXIST);
    altair_val_free(path);
    return altair_bool(ok);
}
AltairVal *_fn_file_exists(AltairVal *path) {
    if (!path || path->type!=ALT_TEXT) { altair_val_free(path); return altair_bool(0); }
    struct stat st;
    int rc = stat(altair_storage_path(path->str),&st);
    altair_val_free(path);
    return altair_bool(rc==0);
}
AltairVal *_fn_list_dir(AltairVal *path) {
    AltairVal *list=altair_list_new();
    if (!path || path->type!=ALT_TEXT) { altair_val_free(path); return list; }
    DIR *d=opendir(altair_storage_path(path->str));
    altair_val_free(path);
    if (!d) return list;
    struct dirent *ent;
    while ((ent=readdir(d))!=NULL) {
        if (strcmp(ent->d_name,".")==0||strcmp(ent->d_name,"..")==0) continue;
        AltairVal *s=altair_str(ent->d_name);
        altair_list_append(list,s);
        altair_val_free(s);
    }
    closedir(d);
    return list;
}

AltairVal *_fn_exec(AltairVal *cmd) {
    if (!cmd || cmd->type!=ALT_TEXT) { altair_val_free(cmd); return altair_num(-1); }
    int rc=system(cmd->str);
    altair_val_free(cmd);
    return altair_num((double)rc);
}
AltairVal *_fn_exec_capture(AltairVal *cmd) {
    if (!cmd || cmd->type!=ALT_TEXT) { altair_val_free(cmd); return altair_str(""); }
    FILE *fp=popen(cmd->str,"r");
    altair_val_free(cmd);
    if (!fp) return altair_str("");
    size_t cap=4096, len=0;
    char *buf=(char*)malloc(cap);
    size_t rd;
    char chunk[1024];
    while ((rd=fread(chunk,1,sizeof(chunk),fp))>0) {
        if (len+rd+1>cap) { cap=(len+rd+1)*2; buf=(char*)realloc(buf,cap); }
        memcpy(buf+len,chunk,rd);
        len+=rd;
    }
    buf[len]='\0';
    pclose(fp);
    return altair_str_own(buf);
}

AltairVal *_fn_ptr_alloc(AltairVal *size) {
    long long n = size ? altair_as_int(size) : 0;
    altair_val_free(size);
    if (n<=0) n=1;
    void *p=calloc((size_t)n,1);
    if (!p) altair_throw("ALT0017","Pointer allocation failed.",0);
    alt_ptr_register(p,(size_t)n);
    return altair_new_ptr(p);
}
AltairVal *_fn_ptr_free(AltairVal *p) {
    if (!p || p->type!=ALT_POINTER || !p->ptr) { altair_val_free(p); return altair_bool(0); }
    if (!alt_ptr_release(p->ptr)) {
        altair_throw("ALT0018","Double free or invalid pointer.",0);
        altair_val_free(p);
        return altair_bool(0);
    }
    free(p->ptr);
    p->ptr=NULL;
    altair_val_free(p);
    return altair_bool(1);
}
AltairVal *_fn_ptr_is_null(AltairVal *p) {
    int r = !p || p->type!=ALT_POINTER || p->ptr==NULL || alt_ptr_valid_size(p->ptr)==0;
    altair_val_free(p);
    return altair_bool(r);
}

static int    _alt_argc = 0;
static char **_alt_argv = NULL;
void altair_set_args(int argc, char **argv) { _alt_argc=argc; _alt_argv=argv; }
AltairVal *_fn_argc(void) { return altair_num((double)_alt_argc); }
AltairVal *_fn_arg(AltairVal *idx) {
    long long i = idx ? altair_as_int(idx) : 0;
    altair_val_free(idx);
    if (i<0 || i>=_alt_argc) return altair_str("");
    return altair_str(_alt_argv[i]);
}

AltairVal *_fn_length(AltairVal *v) {
    return altair_num((double)altair_list_length(v));
}

typedef struct { char file[256]; AltairVar *v; } AltPersistEntry;
static AltPersistEntry _alt_persist_reg[256];
static int _alt_persist_n = 0;

static const char *altair_persist_full_path(const char *file) {
    static char buf[600];
    snprintf(buf,sizeof(buf),"variables/%s",file);
    return buf;
}

AltairVal *altair_persist_load(const char *file) {
    FILE *fp=fopen(altair_persist_full_path(file),"r");
    if (!fp) return NULL;
    char tag[4]={0};
    if (!fgets(tag,sizeof(tag),fp)) { fclose(fp); return NULL; }
    long start=ftell(fp);
    fseek(fp,0,SEEK_END); long end=ftell(fp); fseek(fp,start,SEEK_SET);
    long n = end>start ? end-start : 0;
    char *buf=(char*)malloc((size_t)n+1);
    size_t rd = n>0 ? fread(buf,1,(size_t)n,fp) : 0;
    buf[rd]='\0';
    fclose(fp);
    while (rd>0 && (buf[rd-1]=='\n'||buf[rd-1]=='\r')) buf[--rd]='\0';
    AltairVal *r;
    if (tag[0]=='N')      r=altair_num(atof(buf));
    else if (tag[0]=='B') r=altair_bool(strcmp(buf,"1")==0);
    else                  r=altair_str(buf);
    free(buf);
    return r;
}

void altair_persist_save(const char *file, AltairVal *v) {
#ifdef _WIN32
    _mkdir("variables");
#else
    mkdir("variables",0755);
#endif
    FILE *fp=fopen(altair_persist_full_path(file),"w");
    if (!fp || !v) { if(fp) fclose(fp); return; }
    if (v->type==ALT_NUMERIC)      { fprintf(fp,"N\n%g",v->num); }
    else if (v->type==ALT_BOOL)    { fprintf(fp,"B\n%d",v->boolean?1:0); }
    else if (v->type==ALT_TEXT)    { fprintf(fp,"T\n%s",v->str?v->str:""); }
    else                            { fprintf(fp,"T\n"); }
    fclose(fp);
}

void altair_persist_register(const char *file, AltairVar *v) {
    if (_alt_persist_n>=256) return;
    strncpy(_alt_persist_reg[_alt_persist_n].file,file,255);
    _alt_persist_reg[_alt_persist_n].v=v;
    _alt_persist_n++;
}

static void altair_persist_save_all(void) {
    for (int i=0;i<_alt_persist_n;i++) {
        AltairVar *v=_alt_persist_reg[i].v;
        if (v) altair_persist_save(_alt_persist_reg[i].file, altair_var_get(v));
    }
}

AltairVal *_fn_alloc(AltairVal *size) {
    long long n = size ? altair_as_int(size) : 0;
    altair_val_free(size);
    if (n<=0) n=8;
    void *raw = calloc(1,(size_t)n);
    if (!raw) { altair_throw("ALT0017","Pointer allocation failed.",0); return altair_new_ptr(NULL); }
    alt_ptr_register(raw,(size_t)n);
    return altair_new_ptr(raw);
}
AltairVal *_fn_p_bytes(AltairVal *p) {
    if (!p||p->type!=ALT_POINTER||!p->ptr) { altair_val_free(p); return altair_num(0); }
    size_t n=alt_ptr_valid_size(p->ptr);
    if (n==0) { altair_throw("ALT0018","Use of freed or invalid pointer.",0); altair_val_free(p); return altair_num(0); }
    altair_val_free(p);
    return altair_num((double)n);
}
AltairVal *_fn_p_null(AltairVal *p) {
    int r = !p||p->type!=ALT_POINTER||p->ptr==NULL||alt_ptr_valid_size(p->ptr)==0;
    altair_val_free(p);
    return altair_bool(r);
}
AltairVal *_fn_p_free(AltairVal *p) {
    if (!p||p->type!=ALT_POINTER||!p->ptr) return altair_bool(0);
    if (!alt_ptr_release(p->ptr)) {
        altair_throw("ALT0018","Double free or invalid pointer.",0);
        return altair_bool(0);
    }
    free(p->ptr);
    p->ptr=NULL;
    return altair_bool(1);
}
AltairVal *_fn_p_write(AltairVal *p, AltairVal *offset, AltairVal *val) {
    if (!p||p->type!=ALT_POINTER||!p->ptr) { altair_val_free(offset); altair_val_free(val); return altair_bool(0); }
    size_t n=alt_ptr_valid_size(p->ptr);
    if (n==0) { altair_throw("ALT0018","Use of freed or invalid pointer.",0); altair_val_free(offset); altair_val_free(val); return altair_bool(0); }
    long long off = offset?altair_as_int(offset):0;
    if (off<0 || (size_t)((off+1)*8)>n) { altair_throw("ALT0019","Pointer write out of bounds.",0); altair_val_free(offset); altair_val_free(val); return altair_bool(0); }
    double v = (val&&val->type==ALT_NUMERIC) ? val->num : 0.0;
    ((double*)p->ptr)[off]=v;
    altair_val_free(offset); altair_val_free(val);
    return altair_bool(1);
}
AltairVal *_fn_p_read(AltairVal *p, AltairVal *offset) {
    if (!p||p->type!=ALT_POINTER||!p->ptr) { altair_val_free(p); altair_val_free(offset); return altair_num(0); }
    size_t n=alt_ptr_valid_size(p->ptr);
    if (n==0) { altair_throw("ALT0018","Use of freed or invalid pointer.",0); altair_val_free(p); altair_val_free(offset); return altair_num(0); }
    long long off = offset?altair_as_int(offset):0;
    if (off<0 || (size_t)((off+1)*8)>n) { altair_throw("ALT0019","Pointer read out of bounds.",0); altair_val_free(p); altair_val_free(offset); return altair_num(0); }
    double r=((double*)p->ptr)[off];
    altair_val_free(p); altair_val_free(offset);
    return altair_num(r);
}


typedef struct AltLbaEntry {
    void *key;            
    FILE *fp;              
    int fd;                
    int is_raw;            
    size_t sector_size;    
    size_t bytes; char *path; int is_tmp;
    struct AltLbaEntry *next;
} AltLbaEntry;
static AltLbaEntry *g_lba_table[ALT_PTR_TABLE_BUCKETS];

static void alt_lba_register(void *key, FILE *fp, int fd, int is_raw, size_t sector_size,
                              size_t bytes, const char *path, int is_tmp) {
    unsigned h = alt_ptr_hash(key);
    AltLbaEntry *e = (AltLbaEntry*)malloc(sizeof(AltLbaEntry));
    e->key=key; e->fp=fp; e->fd=fd; e->is_raw=is_raw; e->sector_size=sector_size;
    e->bytes=bytes; e->path=path?strdup(path):NULL; e->is_tmp=is_tmp;
    e->next=g_lba_table[h]; g_lba_table[h]=e;
}
static AltLbaEntry *alt_lba_find(void *key) {
    if (!key) return NULL;
    unsigned h = alt_ptr_hash(key);
    for (AltLbaEntry *e=g_lba_table[h]; e; e=e->next) if (e->key==key) return e;
    return NULL;
}
static int alt_lba_release(void *key) {
    if (!key) return 0;
    unsigned h = alt_ptr_hash(key);
    AltLbaEntry **pp = &g_lba_table[h];
    while (*pp) {
        if ((*pp)->key==key) {
            AltLbaEntry *dead=*pp; *pp=dead->next;
            if (dead->is_tmp && dead->path) remove(dead->path);
            free(dead->path); free(dead);
            return 1;
        }
        pp=&(*pp)->next;
    }
    return 0;
}
static void alt_lba_grow_file(FILE *fp, size_t bytes) {
    
    fseek(fp,0,SEEK_END);
    long cur=ftell(fp);
    if (cur<0) cur=0;
    if ((size_t)cur<bytes) {
        static const char zero[4096]={0};
        size_t remain=bytes-(size_t)cur;
        while (remain>0) {
            size_t chunk = remain<sizeof(zero)?remain:sizeof(zero);
            fwrite(zero,1,chunk,fp);
            remain-=chunk;
        }
        fflush(fp);
    }
}

AltairVal *altair_new_lba(void *fp) {
    AltairVal *v=(AltairVal*)malloc(sizeof(AltairVal));
    v->type=ALT_LBA; v->ptr=fp;
    return v;
}
AltairVal *_fn_dalloc(AltairVal *size) {
    long long n = size ? altair_as_int(size) : 0;
    altair_val_free(size);
    if (n<=0) n=8;
    FILE *fp = tmpfile();
    if (!fp) { altair_throw("ALT0017","LBA allocation failed.",0); return altair_new_lba(NULL); }
    alt_lba_grow_file(fp,(size_t)n);
    alt_lba_register((void*)fp,fp,-1,0,0,(size_t)n,NULL,1);
    return altair_new_lba(fp);
}
AltairVal *_fn_dopen(AltairVal *path, AltairVal *size) {
    long long n = size ? altair_as_int(size) : 0;
    altair_val_free(size);
    if (n<=0) n=8;
    if (!path || path->type!=ALT_TEXT || !path->str) {
        altair_val_free(path);
        altair_throw("ALT0017","dopen() requires a text path.",0);
        return altair_new_lba(NULL);
    }
    FILE *fp = fopen(path->str,"r+b");
    if (!fp) fp = fopen(path->str,"w+b");
    if (!fp) {
        altair_throw("ALT0017","LBA disk allocation failed.",0);
        altair_val_free(path);
        return altair_new_lba(NULL);
    }
    alt_lba_grow_file(fp,(size_t)n);
    alt_lba_register((void*)fp,fp,-1,0,0,(size_t)n,path->str,0);
    altair_val_free(path);
    return altair_new_lba(fp);
}


AltairVal *_fn_draw(AltairVal *path) {
#ifdef ALT_LBA_RAW_SUPPORTED
    if (!path || path->type!=ALT_TEXT || !path->str) {
        altair_val_free(path);
        altair_throw("ALT0017","draw() requires a text device path.",0);
        return altair_new_lba(NULL);
    }
    if (strncmp(path->str,"/dev/",5)!=0) {
        altair_throw("ALT0017","draw() only accepts a raw block device path under /dev/.",0);
        altair_val_free(path);
        return altair_new_lba(NULL);
    }
    int fd = open(path->str, O_RDWR | O_DIRECT);
    if (fd<0) {
        altair_throw("ALT0017","Could not open block device (need permission / not a block device?).",0);
        altair_val_free(path);
        return altair_new_lba(NULL);
    }
    unsigned long ssz = 512;
    if (ioctl(fd, BLKSSZGET, &ssz)!=0 || ssz==0) ssz = 512;
    unsigned long long total = 0;
    if (ioctl(fd, BLKGETSIZE64, &total)!=0) {
        close(fd);
        altair_throw("ALT0017","Could not read block device size (BLKGETSIZE64).",0);
        altair_val_free(path);
        return altair_new_lba(NULL);
    }
    void *key = malloc(1);
    alt_lba_register(key, NULL, fd, 1, (size_t)ssz, (size_t)total, path->str, 0);
    altair_val_free(path);
    return altair_new_lba(key);
#else
    altair_val_free(path);
    altair_throw("ALT0017","draw() (raw LBA block device access) is only supported on Linux.",0);
    return altair_new_lba(NULL);
#endif
}

AltairVal *_fn_lba_bytes(AltairVal *p) {
    if (!p||p->type!=ALT_LBA||!p->ptr) { altair_val_free(p); return altair_num(0); }
    AltLbaEntry *e=alt_lba_find(p->ptr);
    if (!e) { altair_throw("ALT0018","Use of freed or invalid LBA node.",0); altair_val_free(p); return altair_num(0); }
    altair_val_free(p);
    return altair_num((double)e->bytes);
}
AltairVal *_fn_lba_null(AltairVal *p) {
    int r = !p||p->type!=ALT_LBA||p->ptr==NULL||alt_lba_find(p->ptr)==NULL;
    altair_val_free(p);
    return altair_bool(r);
}
AltairVal *_fn_lba_free(AltairVal *p) {
    if (!p||p->type!=ALT_LBA||!p->ptr) return altair_bool(0);
    void *key=p->ptr;
    AltLbaEntry *e=alt_lba_find(key);
    if (!e) { altair_throw("ALT0018","Double free or invalid LBA node.",0); return altair_bool(0); }
    int was_raw = e->is_raw;
#ifdef ALT_LBA_RAW_SUPPORTED
    int fd = e->fd;
#endif
    FILE *fp = e->fp;
    alt_lba_release(key);   
    if (was_raw) {
#ifdef ALT_LBA_RAW_SUPPORTED
        close(fd);
#endif
        free(key);
    } else {
        fclose(fp);
    }
    p->ptr=NULL;
    return altair_bool(1);
}
AltairVal *_fn_lba_write(AltairVal *p, AltairVal *offset, AltairVal *val) {
    if (!p||p->type!=ALT_LBA||!p->ptr) { altair_val_free(offset); altair_val_free(val); return altair_bool(0); }
    AltLbaEntry *e=alt_lba_find(p->ptr);
    if (!e) { altair_throw("ALT0018","Use of freed or invalid LBA node.",0); altair_val_free(offset); altair_val_free(val); return altair_bool(0); }
    long long off = offset?altair_as_int(offset):0;
    if (off<0 || (size_t)((off+1)*8)>e->bytes) { altair_throw("ALT0019","LBA write out of bounds.",0); altair_val_free(offset); altair_val_free(val); return altair_bool(0); }
    double v = (val&&val->type==ALT_NUMERIC) ? val->num : 0.0;

#ifdef ALT_LBA_RAW_SUPPORTED
    if (e->is_raw) {
        size_t ss = e->sector_size;
        long long sector = (off*8) / (long long)ss;
        size_t within = (size_t)((off*8) % (long long)ss);
        void *buf=NULL;
        if (posix_memalign(&buf, ss, ss)!=0 || !buf) {
            altair_throw("ALT0017","Aligned buffer allocation failed.",0);
            altair_val_free(offset); altair_val_free(val); return altair_bool(0);
        }
        off_t at = (off_t)sector * (off_t)ss;
        int ok=1;
        if (pread(e->fd, buf, ss, at) != (ssize_t)ss) ok=0;
        if (ok) {
            memcpy((char*)buf+within, &v, sizeof(double));
            if (pwrite(e->fd, buf, ss, at) != (ssize_t)ss) ok=0;
        }
        free(buf);
        altair_val_free(offset); altair_val_free(val);
        if (!ok) { altair_throw("ALT0019","Raw LBA sector I/O failed.",0); return altair_bool(0); }
        return altair_bool(1);
    }
#endif
    fseek(e->fp,(long)(off*8),SEEK_SET);
    fwrite(&v,sizeof(double),1,e->fp);
    fflush(e->fp);
    altair_val_free(offset); altair_val_free(val);
    return altair_bool(1);
}
AltairVal *_fn_lba_read(AltairVal *p, AltairVal *offset) {
    if (!p||p->type!=ALT_LBA||!p->ptr) { altair_val_free(p); altair_val_free(offset); return altair_num(0); }
    AltLbaEntry *e=alt_lba_find(p->ptr);
    if (!e) { altair_throw("ALT0018","Use of freed or invalid LBA node.",0); altair_val_free(p); altair_val_free(offset); return altair_num(0); }
    long long off = offset?altair_as_int(offset):0;
    if (off<0 || (size_t)((off+1)*8)>e->bytes) { altair_throw("ALT0019","LBA read out of bounds.",0); altair_val_free(p); altair_val_free(offset); return altair_num(0); }
    double r=0.0;

#ifdef ALT_LBA_RAW_SUPPORTED
    if (e->is_raw) {
        size_t ss = e->sector_size;
        long long sector = (off*8) / (long long)ss;
        size_t within = (size_t)((off*8) % (long long)ss);
        void *buf=NULL;
        if (posix_memalign(&buf, ss, ss)!=0 || !buf) {
            altair_throw("ALT0017","Aligned buffer allocation failed.",0);
            altair_val_free(p); altair_val_free(offset); return altair_num(0);
        }
        off_t at = (off_t)sector * (off_t)ss;
        if (pread(e->fd, buf, ss, at) == (ssize_t)ss)
            memcpy(&r, (char*)buf+within, sizeof(double));
        free(buf);
        altair_val_free(p); altair_val_free(offset);
        return altair_num(r);
    }
#endif
    fseek(e->fp,(long)(off*8),SEEK_SET);
    if (fread(&r,sizeof(double),1,e->fp)!=1) r=0.0;
    altair_val_free(p); altair_val_free(offset);
    return altair_num(r);
}

AltairVal *_fn_data_migrate(AltairVal *prefix, AltairVal *lugar) {
    if (!prefix||prefix->type!=ALT_TEXT||!lugar||lugar->type!=ALT_TEXT) {
        altair_val_free(prefix); altair_val_free(lugar); return altair_bool(0);
    }
    DIR *d=opendir("variables");
    if (!d) { altair_val_free(prefix); altair_val_free(lugar); return altair_bool(0); }
    char pfx[300]; snprintf(pfx,sizeof(pfx),"%s.",prefix->str);
    size_t pfxlen=strlen(pfx);
    struct dirent *ent;
    while ((ent=readdir(d))!=NULL) {
        if (strncmp(ent->d_name,pfx,pfxlen)!=0) continue;
        const char *rest=ent->d_name+pfxlen;
        const char *lastdot=strrchr(rest,'.');
        if (!lastdot) continue;
        char varname[128]={0};
        size_t vlen=(size_t)(lastdot-rest);
        if (vlen>=sizeof(varname)) vlen=sizeof(varname)-1;
        memcpy(varname,rest,vlen);
        char oldpath[600], newpath[600];
        snprintf(oldpath,sizeof(oldpath),"variables/%s",ent->d_name);
        snprintf(newpath,sizeof(newpath),"variables/%s.%s.%s",prefix->str,varname,lugar->str);
        rename(oldpath,newpath);
    }
    closedir(d);
    altair_val_free(prefix); altair_val_free(lugar);
    return altair_bool(1);
}
