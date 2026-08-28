
#define _POSIX_C_SOURCE 200809L
#include "sema.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SYMS 8192
typedef struct { char name[128]; VType type; int is_fun; int is_class;
                 VType ret_type; VType param_types[MAX_PARAMS]; int nparam; } Sym;
static Sym g_syms[MAX_SYMS];
static int g_nsyms=0;
static int g_scope_start[256];
static int g_scope_depth=0;

#define VTYPE_UNKNOWN ((VType)255)

static const char *vtype_name(VType t){
    switch(t){
    case VTYPE_NUMERIC: return "numeric";
    case VTYPE_TEXT:    return "text";
    case VTYPE_BOOL:    return "bool";
    case VTYPE_LIST:    return "list";
    case VTYPE_OBJECT:  return "object";
    case VTYPE_TOKEN:   return "token";
    case VTYPE_VOID:    return "void";
    case VTYPE_IMAGE:   return "image";
    case VTYPE_SOUND:   return "sound";
    case VTYPE_MUSIC:   return "music";
    case VTYPE_COLOR:   return "color";
    case VTYPE_FILE:    return "file";
    case VTYPE_POINTER: return "p#node";
    case VTYPE_LBA:      return "lba%node";
    default: return "?";
    }
}

static Sym *sym_find(const char *name){
    for(int i=g_nsyms-1;i>=0;i--)
        if(strcmp(g_syms[i].name,name)==0) return &g_syms[i];
    return NULL;
}

typedef struct { int occupied; char reg_name[16]; int bits; } RegFamily;
static RegFamily g_reg_fam[4];

static int reg_family_of(const char *name){
    if(!strcmp(name,"rax")||!strcmp(name,"eax")||!strcmp(name,"ax")||!strcmp(name,"al")) return 0;
    if(!strcmp(name,"rbx")||!strcmp(name,"ebx")||!strcmp(name,"bx")||!strcmp(name,"bl")) return 1;
    if(!strcmp(name,"rcx")||!strcmp(name,"ecx")||!strcmp(name,"cx")||!strcmp(name,"cl")) return 2;
    if(!strcmp(name,"rdx")||!strcmp(name,"edx")||!strcmp(name,"dx")||!strcmp(name,"dl")) return 3;
    return -1;
}
static int reg_bits_of(const char *name){
    if(!strcmp(name,"rax")||!strcmp(name,"rbx")||!strcmp(name,"rcx")||!strcmp(name,"rdx")) return 64;
    if(!strcmp(name,"eax")||!strcmp(name,"ebx")||!strcmp(name,"ecx")||!strcmp(name,"edx")) return 32;
    if(!strcmp(name,"ax")||!strcmp(name,"bx")||!strcmp(name,"cx")||!strcmp(name,"dx")) return 16;
    if(!strcmp(name,"al")||!strcmp(name,"bl")||!strcmp(name,"cl")||!strcmp(name,"dl")) return 8;
    return -1;
}

static int builtin_return_type(const char *name, VType *out){
    static const struct { const char *name; VType t; } tbl[] = {
        {"length",VTYPE_NUMERIC}, {"file_exists",VTYPE_BOOL},
        {"create_file",VTYPE_BOOL}, {"delete_file",VTYPE_BOOL},
        {"mkdir",VTYPE_BOOL}, {"exec",VTYPE_NUMERIC},
        {"exec_capture",VTYPE_TEXT}, {"argc",VTYPE_NUMERIC},
        {"arg",VTYPE_TEXT}, {"read",VTYPE_TEXT}, {"write",VTYPE_BOOL},
        {"close",VTYPE_BOOL}, {"p_null",VTYPE_BOOL}, {"p_bytes",VTYPE_NUMERIC},
        {"p_read",VTYPE_NUMERIC}, {"p_write",VTYPE_BOOL},
        {"alloc",VTYPE_POINTER}, {"ptr_alloc",VTYPE_POINTER},
        {"lba_null",VTYPE_BOOL}, {"lba_bytes",VTYPE_NUMERIC},
        {"lba_read",VTYPE_NUMERIC}, {"lba_write",VTYPE_BOOL},
        {"lba_free",VTYPE_BOOL},
        {"dalloc",VTYPE_LBA}, {"dopen",VTYPE_LBA}, {"draw",VTYPE_LBA},
        {NULL,VTYPE_VOID}
    };
    for(int i=0;tbl[i].name;i++) if(strcmp(tbl[i].name,name)==0){ *out=tbl[i].t; return 1; }
    return 0;
}

static VType expr_type(ASTNode *n);

static VType binop_type(ASTNode *n){
    VType lt=expr_type(n->left), rt=expr_type(n->right);
    switch(n->op){
    case TOK_AND: case TOK_OR:
        return VTYPE_BOOL;
    case TOK_EQ: case TOK_NEQ: case TOK_LT: case TOK_GT: case TOK_LTE: case TOK_GTE:
        return VTYPE_BOOL;
    case TOK_PLUS:
        if(lt==VTYPE_UNKNOWN||rt==VTYPE_UNKNOWN) return VTYPE_UNKNOWN;
        if(lt==VTYPE_TEXT||rt==VTYPE_TEXT) return VTYPE_TEXT;
        if(lt==VTYPE_NUMERIC&&rt==VTYPE_NUMERIC) return VTYPE_NUMERIC;
        return VTYPE_UNKNOWN;
    case TOK_MINUS: case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: case TOK_PERCENT_LIT:
    case TOK_AMP: case TOK_PIPE: case TOK_CARET: case TOK_SHL: case TOK_SHR:
        return VTYPE_NUMERIC;
    default:
        return VTYPE_UNKNOWN;
    }
}

static VType expr_type(ASTNode *n){
    if(!n) return VTYPE_UNKNOWN;
    switch(n->kind){
    case ND_NUMBER: return VTYPE_NUMERIC;
    case ND_STRING: return VTYPE_TEXT;
    case ND_BOOL:   return VTYPE_BOOL;
    case ND_LIST_LIT: return VTYPE_LIST;
    case ND_IDENT: {
        Sym *s=sym_find(n->str_val);
        return s ? s->type : VTYPE_UNKNOWN;
    }
    case ND_BINOP: return binop_type(n);
    case ND_UNOP:
        if(n->op==TOK_BANG) return VTYPE_BOOL;
        return VTYPE_NUMERIC;
    case ND_FUNC_CALL: {
        Sym *s=sym_find(n->fun_name);
        if(s && s->is_fun) return s->ret_type;
        VType bt;
        if(builtin_return_type(n->fun_name,&bt)) return bt;
        return VTYPE_UNKNOWN;
    }
    case ND_METHOD_CALL: {
        if(strcmp(n->fun_name,"length")==0) return VTYPE_NUMERIC;
        if(strcmp(n->fun_name,"append")==0) return VTYPE_VOID;
        return VTYPE_UNKNOWN;
    }
    case ND_INDEX_ACCESS: return VTYPE_UNKNOWN;
    case ND_REG_READ: return VTYPE_NUMERIC;
    case ND_POINT:    return VTYPE_NUMERIC;
    case ND_UNPOINT:  return VTYPE_UNKNOWN;
    default: return VTYPE_UNKNOWN;
    }
}

static int types_compatible(VType want, VType got){
    if(want==got) return 1;
    if(got==VTYPE_UNKNOWN || want==VTYPE_UNKNOWN) return 1;
    return 0;
}

static void type_err(const char *code, const char *what, VType want, VType got, int line){
    char m[512];
    snprintf(m,sizeof(m),"Type mismatch in %s: expected '%s' but got '%s'.",
             what, vtype_name(want), vtype_name(got));
    fprintf(stderr,"\n%s\n\n%s\n\nLine: %d\n",code,m,line);
    exit(1);
}

static void check_expr_type(ASTNode *n, VType want, const char *what){
    if(!n) return;
    VType got=expr_type(n);
    if(!types_compatible(want,got))
        type_err("ALT0020",what,want,got,n->line);
}

static void scope_push(void){ g_scope_start[g_scope_depth++]=g_nsyms; }
static void scope_pop(void) { if(g_scope_depth>0) g_nsyms=g_scope_start[--g_scope_depth]; }
static void sym_push(const char *name,VType t,int fun,int cls){
    if(g_nsyms>=MAX_SYMS) return;
    strncpy(g_syms[g_nsyms].name,name,127); g_syms[g_nsyms].name[127]='\0';
    g_syms[g_nsyms].type=t; g_syms[g_nsyms].is_fun=fun; g_syms[g_nsyms].is_class=cls;
    g_syms[g_nsyms].ret_type=t; g_syms[g_nsyms].nparam=0;
    g_nsyms++;
}
static void sym_push_fun(ASTNode *n){
    if(g_nsyms>=MAX_SYMS) return;
    strncpy(g_syms[g_nsyms].name,n->fun_name,127); g_syms[g_nsyms].name[127]='\0';
    g_syms[g_nsyms].type=n->return_type; g_syms[g_nsyms].is_fun=1; g_syms[g_nsyms].is_class=0;
    g_syms[g_nsyms].ret_type=n->return_type;
    g_syms[g_nsyms].nparam=n->nparam;
    for(int i=0;i<n->nparam && i<MAX_PARAMS;i++) g_syms[g_nsyms].param_types[i]=n->param_types[i];
    g_nsyms++;
}

static VType g_ret_stack[64]; static int g_ret_depth=0;
static int sym_exists(const char *name){
    for(int i=g_nsyms-1;i>=0;i--)
        if(strcmp(g_syms[i].name,name)==0) return 1;
    return 0;
}
static void sema_err(const char *code,const char *msg,int line){
    fprintf(stderr,"\n%s\n\n%s\n\nLine: %d\n",code,msg,line);
    exit(1);
}
static void sema_warn(const char *name, int line){
    fprintf(stderr,"[WARN] Line %d: variable '%s' used but not declared.\n",line,name);
}

static void check_node(ASTNode *n, int in_fun, int in_class);
static void check_block(ASTNode *blk, int in_fun, int in_class){
    if(!blk) return;
    if(blk->kind==ND_BLOCK){
        for(int i=0;i<blk->nchildren;i++) check_node(blk->children[i],in_fun,in_class);
    } else {
        check_node(blk,in_fun,in_class);
    }
}

static void check_node(ASTNode *n, int in_fun, int in_class){
    if(!n) return;
    switch(n->kind){
    case ND_HEADER: break;

    case ND_VAR_DECL:
        if(n->weight<0)
            sema_err("ALT0008","Weight must be a non-negative integer.",n->line);
        if(n->storage==STOR_PREFER && n->nprefer==0)
            sema_err("ALT0006","Prefer block must have at least one storage entry.",n->line);
        if(n->storage==STOR_ORBIT){
            for(int i=0;i<n->norbit;i++)
                for(int j=i+1;j<n->norbit;j++)
                    if(n->orbit[i].state_num==n->orbit[j].state_num)
                        sema_err("ALT0009","Orbit has duplicate state numbers.",n->line);
        }
        if(n->nchildren>0){
            check_node(n->children[0],in_fun,in_class);
            char what[192]; snprintf(what,sizeof(what),"initializer of '%s'",n->var_name);
            check_expr_type(n->children[0], n->var_type, what);
        }
        sym_push(n->var_name, n->var_type, 0, 0);
        break;

    case ND_FUN_DECL:
        sym_push_fun(n);
        scope_push();
        for(int i=0;i<n->nparam;i++) sym_push(n->param_names[i],n->param_types[i],0,0);
        g_ret_stack[g_ret_depth++]=n->return_type;
        check_block(n->fun_body,1,in_class);
        g_ret_depth--;
        scope_pop();
        break;

    case ND_CLASS_DECL:
        sym_push(n->class_name,VTYPE_OBJECT,0,1);
        scope_push();
        for(int i=0;i<n->nchildren;i++) check_node(n->children[i],in_fun,1);
        scope_pop();
        break;

    case ND_CHOOSE:
        sym_push(n->choose_name,VTYPE_TEXT,0,0);
        break;

    case ND_FOREACH:
        sym_push(n->iter_var,VTYPE_TEXT,0,0);
        check_node(n->iter_list_expr,in_fun,in_class);
        check_block(n->body,in_fun,in_class);
        break;

    case ND_TRY_CATCH:
        check_block(n->try_body,in_fun,in_class);
        sym_push(n->catch_ident,VTYPE_TEXT,0,0);
        { char tmp[192];
          snprintf(tmp,sizeof(tmp),"%s_code",n->catch_ident); sym_push(tmp,VTYPE_TEXT,0,0);
          snprintf(tmp,sizeof(tmp),"%s_message",n->catch_ident); sym_push(tmp,VTYPE_TEXT,0,0);
          snprintf(tmp,sizeof(tmp),"%s_line",n->catch_ident); sym_push(tmp,VTYPE_NUMERIC,0,0);
        }
        check_block(n->catch_body,in_fun,in_class);
        break;

    case ND_MIGRATE:
        if(n->migrate_var[0] && !sym_exists(n->migrate_var))
            sema_warn(n->migrate_var, n->line);
        break;

    case ND_RELEASE:
        if(n->release_var[0] && !sym_exists(n->release_var))
            sema_warn(n->release_var, n->line);
        break;

    case ND_BINOP:
        check_node(n->left,in_fun,in_class); check_node(n->right,in_fun,in_class); break;
    case ND_UNOP:
        check_node(n->right,in_fun,in_class); break;

    case ND_RETURN:
        for(int i=0;i<n->nchildren;i++) check_node(n->children[i],in_fun,in_class);
        check_node(n->left,in_fun,in_class);
        check_node(n->right,in_fun,in_class);
        if(in_fun && g_ret_depth>0 && n->nchildren>0)
            check_expr_type(n->children[0], g_ret_stack[g_ret_depth-1], "return statement");
        break;

    case ND_LOG: case ND_EXPR_STMT:
        for(int i=0;i<n->nchildren;i++) check_node(n->children[i],in_fun,in_class);
        check_node(n->left,in_fun,in_class);
        check_node(n->right,in_fun,in_class);
        break;

    case ND_IF: {
        for(int i=0;i<n->nchildren;i++) check_node(n->children[i],in_fun,in_class);
        int idx=0;
        check_expr_type(n->children[idx],VTYPE_BOOL,"if condition"); idx+=2;
        for(int i=0;i<n->nelif;i++){
            check_expr_type(n->children[idx],VTYPE_BOOL,"elif condition"); idx+=2;
        }
        break;
    }

    case ND_WHILE: case ND_REPEAT: case ND_FOREVER:
        check_node(n->count_expr,in_fun,in_class);
        if(n->kind==ND_WHILE)
            check_expr_type(n->count_expr,VTYPE_BOOL,"while condition");
        check_block(n->body,in_fun,in_class);
        break;

    case ND_FUNC_CALL: {
        for(int i=0;i<n->nchildren;i++) check_node(n->children[i],in_fun,in_class);
        Sym *fs=sym_find(n->fun_name);
        if(fs && fs->is_fun && fs->nparam==n->nchildren){
            for(int i=0;i<n->nchildren;i++){
                char what[192];
                snprintf(what,sizeof(what),"argument %d of call to '%s'",i+1,n->fun_name);
                check_expr_type(n->children[i], fs->param_types[i], what);
            }
        }
        break;
    }

    case ND_METHOD_CALL: case ND_OBJECT_CREATE:
        for(int i=0;i<n->nchildren;i++) check_node(n->children[i],in_fun,in_class);
        break;

    case ND_ASSIGN:
        check_node(n->left,in_fun,in_class);
        check_node(n->right,in_fun,in_class);
        check_node(n->idx_expr,in_fun,in_class);
        check_node(n->idx_val,in_fun,in_class);
        if(n->left && n->left->kind==ND_IDENT){
            Sym *s=sym_find(n->left->str_val);
            if(s && !s->is_fun){
                char what[192]; snprintf(what,sizeof(what),"assignment to '%s'",n->left->str_val);
                check_expr_type(n->right, s->type, what);
            }
        }
        break;

    case ND_COMPOUND_ASSIGN: case ND_INDEX_ASSIGN:
        check_node(n->left,in_fun,in_class);
        check_node(n->right,in_fun,in_class);
        check_node(n->idx_expr,in_fun,in_class);
        check_node(n->idx_val,in_fun,in_class);
        if(n->kind==ND_COMPOUND_ASSIGN && n->var_name[0]){
            Sym *s=sym_find(n->var_name);
            if(s && !s->is_fun && s->type==VTYPE_NUMERIC){
                char what[192]; snprintf(what,sizeof(what),"compound assignment to '%s'",n->var_name);
                check_expr_type(n->right, VTYPE_NUMERIC, what);
            }
        }
        break;

    case ND_IDENT:
        if(n->str_val[0] && !sym_exists(n->str_val) &&
           strcmp(n->str_val,"system")!=0 &&
           strcmp(n->str_val,"compiler")!=0 &&
           strcmp(n->str_val,"program")!=0)
        {   char _m[1200]; snprintf(_m,sizeof(_m),"Variable '%s' is used but was never declared.",n->str_val);
            sema_err("ALT0016",_m,n->line); }
        break;

    case ND_INTROSPECT:
        if(strcmp(n->introspect_ns,"system")!=0&&
           strcmp(n->introspect_ns,"compiler")!=0&&
           strcmp(n->introspect_ns,"program")!=0){
            char m[128]; snprintf(m,sizeof(m),"Unknown namespace '%s'.",n->introspect_ns);
            sema_err("ALT0011",m,n->line);
        }
        break;

    case ND_SNAPSHOT:
        if(n->snap_op[0]=='\0')
            sema_err("ALT0012","Snapshot requires create/restore/delete.",n->line);
        break;

    case ND_LISTEN:
        check_block(n->body,0,0);
        break;

    case ND_ROUTE:
        sym_push(n->handler_name,VTYPE_VOID,1,0);
        check_block(n->body,1,0);
        break;

    case ND_MIDDLEWARE:
        sym_push(n->handler_name,VTYPE_VOID,1,0);
        check_block(n->body,1,0);
        break;

    case ND_JOB:
        sym_push(n->handler_name,VTYPE_VOID,1,0);
        check_block(n->body,1,0);
        break;

    case ND_HEALTH:
        for(int i=0;i<n->nchildren;i++) check_node(n->children[i],in_fun,in_class);
        break;

    case ND_METRICS:
        break;

    case ND_ON_SHUTDOWN:
        check_block(n->body,1,0);
        break;

    case ND_SESSION_DECL:
        sym_push(n->session_var,VTYPE_TEXT,0,0);
        break;

    case ND_CONFIG_DECL:
        check_block(n->body,in_fun,in_class);
        break;

    case ND_DB_POOL:
        sym_push(n->db_var,VTYPE_TEXT,0,0);
        break;

    case ND_RESPOND_JSON: case ND_RESPOND_TEXT: case ND_RESPOND_STATUS:
        for(int i=0;i<n->nchildren;i++) check_node(n->children[i],in_fun,in_class);
        break;

    case ND_LINK: break;

    case ND_WINDOW_DECL:
        for(int i=0;i<n->ngfx_props;i++) check_node(n->gfx_vals[i],in_fun,in_class);
        break;

    case ND_LOOP:
        check_block(n->body,in_fun,in_class);
        break;

    case ND_DRAW_CMD:
        for(int i=0;i<n->ngfx_props;i++) check_node(n->gfx_vals[i],in_fun,in_class);
        break;

    case ND_CLEAR_STMT:
        check_node(n->left,in_fun,in_class);
        break;

    case ND_COLOR_DECL:
        sym_push(n->var_name,VTYPE_COLOR,0,0);
        for(int i=0;i<n->nchildren;i++) check_node(n->children[i],in_fun,in_class);
        break;

    case ND_IMAGE_DECL:
        sym_push(n->var_name,VTYPE_IMAGE,0,0);
        break;

    case ND_SOUND_DECL:
        sym_push(n->var_name,VTYPE_SOUND,0,0);
        break;

    case ND_MUSIC_DECL:
        sym_push(n->var_name,VTYPE_MUSIC,0,0);
        break;

    case ND_PLAY_STMT: case ND_STOP_STMT: case ND_PAUSE_STMT:
        break;

    case ND_TIMER_DECL:
        sym_push(n->var_name,VTYPE_NUMERIC,0,0);
        for(int i=0;i<n->nchildren;i++) check_node(n->children[i],in_fun,in_class);
        break;

    case ND_WIDGET_DECL:
        if(n->var_name[0]) sym_push(n->var_name,VTYPE_OBJECT,0,0);
        for(int i=0;i<n->ngfx_props;i++) check_node(n->gfx_vals[i],in_fun,in_class);
        break;

    case ND_MENU_DECL:
        if(n->var_name[0]) sym_push(n->var_name,VTYPE_OBJECT,0,0);
        for(int i=0;i<n->ngfx_props;i++) check_node(n->gfx_vals[i],in_fun,in_class);
        break;

    case ND_DIALOG_DECL: case ND_POPUP_DECL: case ND_CANVAS_GFX:
        if(n->var_name[0]) sym_push(n->var_name,VTYPE_OBJECT,0,0);
        check_block(n->body,in_fun,in_class);
        break;

    case ND_SCENE_DECL:
        if(n->var_name[0]) sym_push(n->var_name,VTYPE_TEXT,0,0);
        break;

    case ND_GOTO_STMT: break;
    case ND_CURSOR_STMT: break;

    case ND_ANIMATE_DECL:
        for(int i=0;i<n->ngfx_props;i++) check_node(n->gfx_vals[i],in_fun,in_class);
        break;

    case ND_LAYOUT:
        check_block(n->body,in_fun,in_class);
        break;

    case ND_KEY_EXPR: break;

    case ND_REG_DECL: {
        if(n->nchildren>0) check_node(n->children[0],in_fun,in_class);
        int fam=reg_family_of(n->reg_name);
        if(fam<0){
            char m[192]; snprintf(m,sizeof(m),"'%s' is not a valid register name.",n->reg_name);
            sema_err("ALT_REG_FORBIDDEN",m,n->line);
        }
        if(n->reg_bits!=8&&n->reg_bits!=16&&n->reg_bits!=32&&n->reg_bits!=64)
            sema_err("ALT_REG_FORBIDDEN","Register size must be 8, 16, 32 or 64.",n->line);
        if(n->reg_bits!=reg_bits_of(n->reg_name)){
            char m[192]; snprintf(m,sizeof(m),
                "'%s' is a %d-bit register, not %d-bit.",n->reg_name,reg_bits_of(n->reg_name),n->reg_bits);
            sema_err("ALT_REG_FORBIDDEN",m,n->line);
        }
        if(g_reg_fam[fam].occupied){
            char m[192]; snprintf(m,sizeof(m),
                "Register family of '%s' is already occupied by '%s'.",n->reg_name,g_reg_fam[fam].reg_name);
            sema_err("ALT_REG_OCCUPIED",m,n->line);
        }
        if(n->nchildren>0 && n->children[0]->kind==ND_POINT && n->reg_bits!=64)
            sema_err("ALT_REG_WIDTH","Storing a system@point() address requires reg& 64.",n->line);
        g_reg_fam[fam].occupied=1;
        strncpy(g_reg_fam[fam].reg_name,n->reg_name,15);
        g_reg_fam[fam].bits=n->reg_bits;
        break;
    }

    case ND_REG_READ: {
        int fam=reg_family_of(n->reg_name);
        if(fam<0){
            char m[192]; snprintf(m,sizeof(m),"'%s' is not a valid register name.",n->reg_name);
            sema_err("ALT_REG_FORBIDDEN",m,n->line);
        }
        if(!g_reg_fam[fam].occupied){
            char m[192]; snprintf(m,sizeof(m),"Register '%s' is not declared.",n->reg_name);
            sema_err("ALT_REG_UNDECLARED",m,n->line);
        }
        break;
    }

    case ND_REG_WRITE: {
        if(n->nchildren>0) check_node(n->children[0],in_fun,in_class);
        int fam=reg_family_of(n->reg_name);
        if(fam<0){
            char m[192]; snprintf(m,sizeof(m),"'%s' is not a valid register name.",n->reg_name);
            sema_err("ALT_REG_FORBIDDEN",m,n->line);
        }
        if(!g_reg_fam[fam].occupied){
            char m[192]; snprintf(m,sizeof(m),"Register '%s' is not declared.",n->reg_name);
            sema_err("ALT_REG_UNDECLARED",m,n->line);
        }
        if(n->nchildren>0 && n->children[0]->kind==ND_POINT && g_reg_fam[fam].bits!=64)
            sema_err("ALT_REG_WIDTH","Storing a system@point() address requires a 64-bit register.",n->line);
        break;
    }

    case ND_REG_FREE: {
        int fam=reg_family_of(n->reg_name);
        if(fam<0){
            char m[192]; snprintf(m,sizeof(m),"'%s' is not a valid register name.",n->reg_name);
            sema_err("ALT_REG_FORBIDDEN",m,n->line);
        }
        if(!g_reg_fam[fam].occupied){
            char m[192]; snprintf(m,sizeof(m),"Register '%s' is not declared.",n->reg_name);
            sema_err("ALT_REG_UNDECLARED",m,n->line);
        }
        g_reg_fam[fam].occupied=0;
        break;
    }

    case ND_POINT:
        if(!n->point_var[0] || !sym_exists(n->point_var)){
            char m[192]; snprintf(m,sizeof(m),"Unknown variable '%s' in system@point().",n->point_var);
            sema_err("ALT_POINT_UNKNOWN",m,n->line);
        }
        break;

    case ND_UNPOINT:
        if(n->nchildren>0) check_node(n->children[0],in_fun,in_class);
        break;

    case ND_BLOCK:
        for(int i=0;i<n->nchildren;i++) check_node(n->children[i],in_fun,in_class);
        break;

    case ND_NUMBER: case ND_STRING: case ND_BOOL:
    case ND_LIST_LIT: case ND_INDEX_ACCESS: case ND_MEMBER_ACCESS:
    case ND_WAIT: case ND_EXIT: case ND_CALL_GALAXY:
    case ND_USER_INPUT: case ND_PROGRAM:
    default:
        break;
    }
}

void sema_check(ASTNode *program){
    g_nsyms=0; g_scope_depth=0;
    memset(g_reg_fam,0,sizeof(g_reg_fam));
    if(!program) return;

    static const char *color_names[]={
        "white","black","red","green","blue","yellow","orange",
        "purple","pink","gray","lightgray","darkgray","brown",
        "skyblue","darkblue","maroon","darkgreen","lime","gold",
        "beige","magenta","violet","darkpurple","darkbrown",
        "raywhite","transparent",NULL
    };
    for(int i=0;color_names[i];i++) sym_push(color_names[i],VTYPE_NUMERIC,0,0);

    sym_push("MOUSE_LEFT",  VTYPE_NUMERIC,0,0);
    sym_push("MOUSE_RIGHT", VTYPE_NUMERIC,0,0);
    sym_push("MOUSE_MIDDLE",VTYPE_NUMERIC,0,0);

    for(int i=0;i<program->nchildren;i++) check_node(program->children[i],0,0);
}
