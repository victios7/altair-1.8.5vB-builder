
#ifndef ALTAIR_RT_H
#define ALTAIR_RT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <setjmp.h>

typedef enum {
    ALT_NUMERIC = 0,
    ALT_TEXT,
    ALT_BOOL,
    ALT_LIST,
    ALT_OBJECT,
    ALT_TOKEN,
    ALT_VOID,
    ALT_FILE,
    ALT_POINTER,
    ALT_LBA
} AltVType;

typedef enum {
    ALT_RAM = 0,
    ALT_DISK,
    ALT_CACHE,
    ALT_TEMP,
    ALT_AUTO
} AltStorage;

typedef struct AltairVal AltairVal;
typedef struct AltairObj AltairObj;

struct AltairVal {
    AltVType type;
    /* A2: when num_is_int is set, num_i64 holds the exact integer value
       produced by the fast (long long) codegen path. num still mirrors
       it as a double for existing double-based call sites, but display
       and integer-widening code should prefer num_i64 to avoid losing
       precision above 2^53. */
    int        num_is_int;
    long long  num_i64;
    /* C2: capacity of str's heap buffer (ALT_TEXT only). 0 means "unknown/
       exact fit" (e.g. just strdup'd). Lets altair_var_plus_assign grow the
       buffer with amortized doubling instead of reallocating+copying the
       whole string on every "+=" in a loop. */
    size_t     str_cap;
    union {
        double      num;
        char       *str;
        int         boolean;
        struct { AltairVal **items; int len; int cap; } list;
        AltairObj  *obj;
        struct { AltairVal *inner; int consumed; } tok;
        void       *ptr;
    };
};

typedef struct {
    int        state_num;
    char       state_name[64];
    AltStorage storage;
} AltOrbitEntry;

typedef struct AltairVar AltairVar;
struct AltairVar {
    char        name[128];
    AltVType    vtype;
    AltStorage  storage;
    int         is_const;
    int         weight;
    time_t      expire_at;
    AltairVal  *val;

    AltOrbitEntry orbit[16];
    int           norbit;
    int           orbit_state;

    AltStorage    prefer[8];
    int           nprefer;

    AltairVar  *next;
    AltairVar  *prev;
    AltairVar  *hnext;
    AltairVar  *hprev;
};

struct AltairObj {
    char         class_name[64];
    char       **field_names;
    AltairVal  **fields;
    int          nfields;
    char       **method_names;
    void       **methods;
    int          nmethods;
};

typedef struct {
    char    code[16];
    char    message[256];
    int     line;
    int     active;
} AltairError;

typedef struct {
    double *items;
    int     len;
    int     cap;
} AltairFNumList;

AltairFNumList *altair_fnumlist_new(void);
void altair_fnumlist_free(AltairFNumList *list);
void altair_fnumlist_append(AltairFNumList *list, double value);
AltairVal *altair_fnumlist_to_val(AltairFNumList *list);

typedef struct {
    char   *buf;
    size_t  len;
    size_t  cap;
} AltairSB;

AltairSB *altair_sb_new(void);
void altair_sb_free(AltairSB *sb);
void altair_sb_append(AltairSB *sb, const char *str);
void altair_sb_append_val(AltairSB *sb, AltairVal *val);
AltairVal *altair_sb_to_val(AltairSB *sb);

#define ALT_TRY_MAX 128
extern jmp_buf     _altair_jmp_stack[ALT_TRY_MAX];
extern AltairError _altair_err_stack[ALT_TRY_MAX];
extern int         _altair_try_depth;

extern char _altair_prog_name[128];
extern char _altair_prog_version[64];
extern char _altair_prog_author[128];
extern char _altair_storage_dir[512];

void altair_init(const char *name, const char *version, const char *author);
void altair_shutdown(void);
#ifdef _WIN32
void altair_pause_if_own_console(void);
#endif

AltairVal *altair_num(double n);
AltairVal *altair_num_i64(long long n);
AltairVal *altair_text_append_owned(AltairVal *dst, AltairVal *addend);
void       altair_var_plus_assign(AltairVar *v, AltairVal *addend, int line);
AltairVal *altair_str(const char *s);
AltairVal *altair_str_own(char *s);
AltairVal *altair_bool(int b);
AltairVal *altair_list_new(void);
AltairVal *altair_token_new(AltairVal *inner);
AltairVal *altair_val_copy(const AltairVal *v);
void       altair_val_free(AltairVal *v);
char      *altair_val_tostr(const AltairVal *v);

AltairVar *altair_var_new(const char *name, AltVType t, AltStorage stor,
                           int is_const, int weight, double expire_secs);
AltairVar *altair_var_orbit(const char *name, AltVType t,
                             AltOrbitEntry *entries, int n);
AltairVar *altair_var_prefer(const char *name, AltVType t,
                              AltStorage *prefs, int n);

void       altair_var_set(AltairVar *v, AltairVal *val);
void       altair_var_set_own(AltairVar *v, AltairVal *val);
AltairVal *altair_var_get(AltairVar *v);

void       altair_var_register(AltairVar *v);
void       altair_var_unregister(const char *name);
AltairVar *altair_var_lookup(const char *name);

void       altair_var_release(AltairVar **vp);
void       altair_var_release_view(AltairVar **vp);

AltairVal *altair_point(AltairVar *v);
AltairVal *altair_unpoint(double addr);

void altair_migrate(AltairVar *v, int state_num);
void altair_migrate_name(AltairVar *v, const char *state_name);

void altair_list_append(AltairVal *list, AltairVal *item);
int  altair_list_remove(AltairVal *list, int idx);
void altair_list_clear(AltairVal *list);
int  altair_list_length(AltairVal *list);
AltairVal *altair_list_get(AltairVal *list, int idx, int line);
void       altair_list_set(AltairVal *list, int idx, AltairVal *val, int line);

AltairVal *altair_add(AltairVal *a, AltairVal *b, int line);
AltairVal *altair_sub(AltairVal *a, AltairVal *b, int line);
AltairVal *altair_mul(AltairVal *a, AltairVal *b, int line);
AltairVal *altair_div(AltairVal *a, AltairVal *b, int line);
AltairVal *altair_mod(AltairVal *a, AltairVal *b, int line);
AltairVal *altair_neg(AltairVal *a);
AltairVal *altair_not(AltairVal *a);
AltairVal *altair_eq(AltairVal *a, AltairVal *b);
AltairVal *altair_neq(AltairVal *a, AltairVal *b);
AltairVal *altair_lt(AltairVal *a, AltairVal *b, int line);
AltairVal *altair_gt(AltairVal *a, AltairVal *b, int line);
AltairVal *altair_lte(AltairVal *a, AltairVal *b, int line);
AltairVal *altair_gte(AltairVal *a, AltairVal *b, int line);
AltairVal *altair_and(AltairVal *a, AltairVal *b);
AltairVal *altair_or(AltairVal *a, AltairVal *b);

AltairVal *altair_coerce_num(AltairVal *v, int line);
AltairVal *altair_coerce_str(AltairVal *v);

AltairVal *altair_token_use(AltairVar *var, int line);

AltairObj *altair_obj_new(const char *class_name, int nfields,
                           const char **field_names, AltairVal **field_defaults,
                           AltStorage *field_storage);
void       altair_obj_free(AltairObj *obj);
AltairVal *altair_obj_get(AltairObj *obj, const char *field, int line);
void       altair_obj_set(AltairObj *obj, const char *field, AltairVal *val, int line);

void       altair_log(AltairVal *v);
AltairVal *altair_user_input(const char *prompt, AltVType expected_type);

int        altair_choose(double *weights, int n);

void altair_snapshot_create(const char *snap_name, int line);
void altair_snapshot_restore(const char *snap_name, int line);
void altair_snapshot_delete(const char *snap_name, int line);

AltairVal *altair_system(const char *key);
AltairVal *altair_compiler(const char *key);
AltairVal *altair_program(const char *key);
AltairVal *altair_var_system(AltairVar *v, const char *key);

void       altair_disk_save(const char *var_name, AltairVal *v,
                             AltStorage stor, time_t expire_at);
AltairVal *altair_disk_load(const char *var_name, AltStorage stor);
void       altair_disk_delete(const char *var_name, AltStorage stor);

void altair_wait(double seconds);
void altair_throw(const char *code, const char *msg, int line);

AltairVal *altair_band(AltairVal *a, AltairVal *b, int line);
AltairVal *altair_bor(AltairVal *a, AltairVal *b, int line);
AltairVal *altair_bxor(AltairVal *a, AltairVal *b, int line);
AltairVal *altair_bnot(AltairVal *a, int line);
AltairVal *altair_shl(AltairVal *a, AltairVal *b, int line);
AltairVal *altair_shr(AltairVal *a, AltairVal *b, int line);

AltairVal *altair_new_file(void *fp);
AltairVal *altair_new_ptr(void *p);

AltairVal *_fn_open(AltairVal *path);
AltairVal *_fn_open_write(AltairVal *path);
AltairVal *_fn_open_append(AltairVal *path);
AltairVal *_fn_read(AltairVal *file);
AltairVal *_fn_read_line(AltairVal *file);
AltairVal *_fn_write(AltairVal *file, AltairVal *text);
AltairVal *_fn_close(AltairVal *file);
AltairVal *_fn_create_file(AltairVal *path);
AltairVal *_fn_delete_file(AltairVal *path);
AltairVal *_fn_mkdir(AltairVal *path);
AltairVal *_fn_file_exists(AltairVal *path);
AltairVal *_fn_list_dir(AltairVal *path);

AltairVal *_fn_exec(AltairVal *cmd);
AltairVal *_fn_exec_capture(AltairVal *cmd);

AltairVal *_fn_ptr_alloc(AltairVal *size);
AltairVal *_fn_ptr_free(AltairVal *p);
AltairVal *_fn_ptr_is_null(AltairVal *p);
AltairVal *_fn_alloc(AltairVal *size);
AltairVal *_fn_p_write(AltairVal *p, AltairVal *offset, AltairVal *val);
AltairVal *_fn_p_read(AltairVal *p, AltairVal *offset);
AltairVal *_fn_p_bytes(AltairVal *p);
AltairVal *_fn_p_null(AltairVal *p);
AltairVal *_fn_p_free(AltairVal *p);

/* lba%: same read/write/bytes/null/free shape as p#, but disk-backed via
   FILE* instead of a malloc'd buffer. dalloc() makes an anonymous node
   (deleted when freed); dopen() makes a named, persistent node whose file
   survives past lba_free(). */
AltairVal *altair_new_lba(void *fp);
AltairVal *_fn_dalloc(AltairVal *size);
AltairVal *_fn_dopen(AltairVal *path, AltairVal *size);
AltairVal *_fn_draw(AltairVal *path);
AltairVal *_fn_lba_write(AltairVal *p, AltairVal *offset, AltairVal *val);
AltairVal *_fn_lba_read(AltairVal *p, AltairVal *offset);
AltairVal *_fn_lba_bytes(AltairVal *p);
AltairVal *_fn_lba_null(AltairVal *p);
AltairVal *_fn_lba_free(AltairVal *p);
AltairVal *_fn_data_migrate(AltairVal *prefix, AltairVal *lugar);

void altair_set_args(int argc, char **argv);
AltairVal *_fn_argc(void);
AltairVal *_fn_arg(AltairVal *idx);
AltairVal *_fn_length(AltairVal *v);

AltairVal *altair_persist_load(const char *file);
void altair_persist_save(const char *file, AltairVal *v);
void altair_persist_register(const char *file, AltairVar *v);

typedef struct {
    char method[16];
    char path[512];
    char body[65536];
    char headers[64][2][256];
    int  nheaders;
    char params[32][2][128];
    int  nparams;
} AltairRequest;

typedef struct {
    int   status;
    char  content_type[64];
    char  body[65536];
    int   sent;
} AltairResponse;

extern AltairRequest  *_altair_req;
extern AltairResponse *_altair_res;
extern volatile int    _altair_server_running;

typedef void (*AltairRouteHandler)(AltairRequest *req, AltairResponse *res);
typedef int  (*AltairMiddlewareFn)(AltairRequest *req, AltairResponse *res);

typedef struct AltairRoute {
    char              method[16];
    char              path[512];
    AltairRouteHandler handler;
    int               rate_limit;
    struct AltairRoute *next;
} AltairRoute;

typedef struct AltairMiddleware {
    AltairMiddlewareFn fn;
    struct AltairMiddleware *next;
} AltairMiddleware;

typedef struct {
    int              port;
    AltairRoute     *routes;
    AltairMiddleware *middlewares;
    void           (*shutdown_fn)(void);
} AltairServer;

extern AltairServer _altair_server;

void altair_server_init(int port);
void altair_server_add_route(const char *method, const char *path,
                              AltairRouteHandler handler, int rate_limit);
void altair_server_add_middleware(AltairMiddlewareFn fn);
void altair_server_run(void);
void altair_server_stop(void);
void altair_server_set_shutdown(void (*fn)(void));

void altair_respond_json(AltairResponse *res, AltairVal *v);
void altair_respond_text(AltairResponse *res, AltairVal *v);
void altair_respond_status(AltairResponse *res, int status);
void altair_respond_stop(AltairResponse *res);

AltairVal *altair_req_param(AltairRequest *req, const char *name);
AltairVal *altair_req_header(AltairRequest *req, const char *name);
AltairVal *altair_req_body(AltairRequest *req);

typedef struct AltairHealthCheck { char name[64]; int (*fn)(void); struct AltairHealthCheck *next; } AltairHealthCheck;
extern AltairHealthCheck *_altair_health_checks;
void altair_health_add(const char *name, int (*fn)(void));
void altair_health_handler(AltairRequest *req, AltairResponse *res);

typedef struct { char name[64]; long long value; int is_histogram; } AltairMetric;
extern AltairMetric _altair_metrics[64];
extern int          _altair_nmetrics;
void altair_metric_inc(const char *name);
void altair_metric_observe(const char *name, double val);
void altair_metrics_handler(AltairRequest *req, AltairResponse *res);

typedef struct { char id[64]; char key[64]; char value[512]; time_t expires; struct AltairSession *next; } AltairSession;
AltairVal *altair_session_get(const char *session_id, const char *key);
void       altair_session_set(const char *session_id, const char *key, AltairVal *val, int ttl_secs);

AltairVal *altair_config_env(const char *key, const char *default_val, int required);

typedef struct AltairJob { char name[64]; void (*fn)(void); long interval_secs; time_t next_run; struct AltairJob *next; } AltairJob;
extern AltairJob *_altair_jobs;
void altair_job_register(const char *name, void (*fn)(void), long interval_secs);
void altair_jobs_tick(void);

#endif
