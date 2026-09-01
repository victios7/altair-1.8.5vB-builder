#ifndef ALTAIR_AST_H
#define ALTAIR_AST_H
#include "lexer.h"

typedef enum {
    STOR_RAM=0, STOR_DISK, STOR_CACHE, STOR_TEMP, STOR_AUTO,
    STOR_PREFER, STOR_ORBIT
} StorKind;

typedef struct { int state_num; char state_name[64]; StorKind storage; } OrbitEntry;
typedef struct { int priority; StorKind storage; } PreferEntry;

typedef enum {
    ND_PROGRAM, ND_HEADER, ND_BLOCK,
    ND_VAR_DECL, ND_FUN_DECL, ND_CLASS_DECL,
    ND_ASSIGN, ND_COMPOUND_ASSIGN,
    ND_IF, ND_REPEAT, ND_WHILE, ND_FOREVER, ND_FOREACH,
    ND_CHOOSE, ND_SNAPSHOT, ND_MIGRATE, ND_LOG, ND_CALL_GALAXY,
    ND_TRY_CATCH, ND_RETURN, ND_EXIT, ND_WAIT, ND_EXPR_STMT,
    ND_BINOP, ND_UNOP,
    ND_NUMBER, ND_STRING, ND_BOOL, ND_IDENT,
    ND_LIST_LIT, ND_FUNC_CALL, ND_OBJECT_CREATE,
    ND_MEMBER_ACCESS, ND_METHOD_CALL,
    ND_INDEX_ACCESS, ND_INDEX_ASSIGN,
    ND_INTROSPECT,
    ND_USER_INPUT,
    ND_RELEASE,

    ND_LISTEN,
    ND_ROUTE,
    ND_MIDDLEWARE,
    ND_JOB,
    ND_HEALTH,
    ND_METRICS,
    ND_ON_SHUTDOWN,
    ND_SESSION_DECL,
    ND_CONFIG_DECL,
    ND_DB_POOL,
    ND_RESPOND_JSON,
    ND_RESPOND_TEXT,
    ND_RESPOND_STATUS,

    ND_LINK,
    ND_WINDOW_DECL,
    ND_LOOP,
    ND_DRAW_CMD,
    ND_CLEAR_STMT,
    ND_COLOR_DECL,
    ND_IMAGE_DECL,
    ND_SOUND_DECL,
    ND_MUSIC_DECL,
    ND_PLAY_STMT,
    ND_STOP_STMT,
    ND_PAUSE_STMT,
    ND_TIMER_DECL,
    ND_WIDGET_DECL,
    ND_MENU_DECL,
    ND_DIALOG_DECL,
    ND_SCENE_DECL,
    ND_GOTO_STMT,
    ND_CURSOR_STMT,
    ND_ANIMATE_DECL,
    ND_POPUP_DECL,
    ND_CANVAS_GFX,
    ND_LAYOUT,
    ND_KEY_EXPR,

    ND_REG_DECL,
    ND_REG_READ,
    ND_REG_WRITE,
    ND_REG_FREE,
    ND_POINT,
    ND_UNPOINT,
} NodeKind;

typedef enum {
    VTYPE_NUMERIC=0, VTYPE_TEXT, VTYPE_BOOL, VTYPE_LIST,
    VTYPE_OBJECT, VTYPE_TOKEN, VTYPE_VOID,
    VTYPE_IMAGE,
    VTYPE_SOUND,
    VTYPE_MUSIC,
    VTYPE_COLOR,
    VTYPE_FILE,
    VTYPE_POINTER,
    VTYPE_LBA,
} VType;

#define MAX_CHILDREN  1280
#define MAX_PARAMS     320
#define MAX_ORBIT      160
#define MAX_PREFER      80
#define MAX_CHOOSE_OPT 640
#define MAX_GFX_PROPS  100

typedef struct ASTNode ASTNode;
struct ASTNode {
    NodeKind  kind;
    int       line;
    double    num_val;
    char      str_val[1024];
    int       bool_val;
    TokenKind op;
    ASTNode  *left;
    ASTNode  *right;
    ASTNode  *children[MAX_CHILDREN];
    int       nchildren;

    char      var_name[128];
    VType     var_type;
    int       is_const;
    double    expire_secs;
    StorKind  storage;
    int       weight;
    OrbitEntry orbit[MAX_ORBIT]; int norbit;
    PreferEntry prefer[MAX_PREFER]; int nprefer;

    char      fun_name[128];
    VType     return_type;
    VType     param_types[MAX_PARAMS];
    char      param_names[MAX_PARAMS][128];
    int       nparam;
    ASTNode  *fun_body;
    int       is_method;

    char      class_name[128];

    int       nelif;
    int       has_else;

    ASTNode  *count_expr;
    double    wait_secs;

    char      iter_var[128];
    ASTNode  *iter_list_expr;
    ASTNode  *body;

    char      choose_name[128];
    char      choose_opts[MAX_CHOOSE_OPT][128];
    double    choose_weights[MAX_CHOOSE_OPT];
    int       nchoose;

    char      snap_op[16];
    char      snap_name[256];

    char      migrate_var[128];
    int       migrate_num;
    char      migrate_state[64];
    int       migrate_by_name;

    char      introspect_ns[32];
    char      introspect_key[64];
    char      introspect_var[128];

    ASTNode  *try_body;
    char      catch_ident[64];
    ASTNode  *catch_body;

    TokenKind compound_op;
    char      idx_list[128];
    ASTNode  *idx_expr;
    ASTNode  *idx_val;

    char      input_prompt[512];
    VType     input_type;

    char      galaxy_name[128];

    char      release_var[128];

    int       server_port;
    char      route_method[16];
    char      route_path[512];
    int       rate_limit;
    char      handler_name[128];
    char      health_path[512];
    char      metrics_path[512];
    double    job_interval_secs;
    char      session_var[128];
    double    session_ttl_secs;
    char      db_var[128];
    char      db_url[512];
    int       db_max;

    char      gfx_kind[32];
    char      gfx_props[MAX_GFX_PROPS][32];
    ASTNode  *gfx_vals[MAX_GFX_PROPS];
    int       ngfx_props;
    char      gfx_link_lib[64];

    char      persist_file[256];

    char      reg_name[16];
    int       reg_bits;
    char      point_var[128];
};

ASTNode *ast_new(NodeKind kind, int line);
void     ast_free(ASTNode *n);
#endif
