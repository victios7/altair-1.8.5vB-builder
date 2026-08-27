
#include "parser.h"
#include "lexer.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct { Lexer lex; Token cur; } Parser;

static void parser_init(Parser *p, const char *src){
    lexer_init(&p->lex,src);
    p->cur=lexer_next(&p->lex);
}
static Token advance(Parser *p){
    Token t=p->cur; p->cur=lexer_next(&p->lex); return t;
}
static int check(Parser *p,TokenKind k){ return p->cur.kind==k; }
static Token expect(Parser *p,TokenKind k){
    if(p->cur.kind!=k){
        fprintf(stderr,"[ALT0003] Expected %s, got '%s' at line %d\n",
                tok_kind_str(k),p->cur.value,p->cur.line);
        exit(1);
    }
    return advance(p);
}
static void skip_semi(Parser *p){ if(check(p,TOK_SEMICOLON)) advance(p); }
static int check_type(Parser *p){
    return p->cur.kind==TOK_NUMERIC||p->cur.kind==TOK_TEXT||
           p->cur.kind==TOK_BOOL||p->cur.kind==TOK_LIST||
           p->cur.kind==TOK_OBJECT||p->cur.kind==TOK_TOKEN||
           p->cur.kind==TOK_CHAR||p->cur.kind==TOK_FILE;
}
static VType tok_to_vtype(TokenKind k){
    switch(k){
    case TOK_NUMERIC:return VTYPE_NUMERIC; case TOK_TEXT:return VTYPE_TEXT;
    case TOK_BOOL:return VTYPE_BOOL;       case TOK_LIST:return VTYPE_LIST;
    case TOK_OBJECT:return VTYPE_OBJECT;   case TOK_TOKEN:return VTYPE_TOKEN;
    case TOK_CHAR:return VTYPE_TEXT;
    case TOK_FILE:return VTYPE_FILE;
    default:return VTYPE_VOID;
    }
}
static StorKind tok_to_stor(TokenKind k){
    switch(k){
    case TOK_RAM:return STOR_RAM;   case TOK_DISK:return STOR_DISK;
    case TOK_CACHE:return STOR_CACHE; case TOK_TEMP:return STOR_TEMP;
    case TOK_AUTO:return STOR_AUTO; default:return STOR_AUTO;
    }
}
static int is_storage_tok(Parser *p){
    return p->cur.kind==TOK_RAM||p->cur.kind==TOK_DISK||
           p->cur.kind==TOK_CACHE||p->cur.kind==TOK_TEMP||p->cur.kind==TOK_AUTO||
           p->cur.kind==TOK_ORBIT||p->cur.kind==TOK_PREFER;
}

static int is_soft_keyword_kind(TokenKind k){
    switch(k){
    case TOK_USER: case TOK_HEALTH: case TOK_SESSION: case TOK_CONFIG:
    case TOK_JOB: case TOK_CHECK: case TOK_METRICS: case TOK_MAX:
    case TOK_DEFAULT_KW: case TOK_BODY_KW: case TOK_HEADER_KW: case TOK_PARAM:
    case TOK_STOP: case TOK_ROUTE: case TOK_LISTEN: case TOK_MIDDLEWARE:
    case TOK_SCHEDULE: case TOK_EVERY: case TOK_ON_SHUTDOWN: case TOK_EXPIRES:
    case TOK_REQUIRED_KW: case TOK_ENV_KW: case TOK_DB_POOL: case TOK_CONNECT:
    case TOK_PER_MINUTE: case TOK_PER_SECOND: case TOK_RATE_LIMIT:

    case TOK_LINK: case TOK_WINDOW: case TOK_LOOP: case TOK_DRAW:
    case TOK_IMAGE: case TOK_CLEAR: case TOK_COLOR_KW: case TOK_SOUND:
    case TOK_MUSIC: case TOK_PLAY: case TOK_PAUSE_KW: case TOK_TIMER_KW:
    case TOK_BUTTON: case TOK_LABEL_KW: case TOK_TEXTBOX: case TOK_CHECKBOX:
    case TOK_SLIDER_KW: case TOK_PROGRESS_KW: case TOK_LISTVIEW:
    case TOK_MENU_KW: case TOK_DIALOG: case TOK_SCENE_KW: case TOK_GOTO_KW:
    case TOK_CURSOR_KW: case TOK_ANIMATE: case TOK_POPUP: case TOK_CANVAS_GFX:
    case TOK_COLUMN: case TOK_ROW: case TOK_GRID: case TOK_KEY_KW:
        return 1;
    default: return 0;
    }
}
static int is_ident_like(Parser *p){
    if(check(p,TOK_IDENT)) return 1;
    return is_soft_keyword_kind(p->cur.kind);
}
static double parse_duration_secs(const char *s){
    double v=atof(s);
    char c=s[strlen(s)-1];
    if(c=='m') v*=60;
    else if(c=='h') v*=3600;
    return v;
}

static ASTNode *parse_expr(Parser *p);
static ASTNode *parse_stmt(Parser *p);
static ASTNode *parse_block_until_break(Parser *p);
static ASTNode *parse_if_branch_block(Parser *p);

static ASTNode *parse_postfix(Parser *p, ASTNode *base){
    while(check(p,TOK_DOT)||check(p,TOK_LBRACKET)){
        int line=p->cur.line;
        if(check(p,TOK_DOT)){
            advance(p);
            if(!is_ident_like(p)&&!check(p,TOK_RESPOND)){
                fprintf(stderr, "[ALT0003] Syntax error at line %d: Expected field or method name after '.', but found '%s'.\n", line, p->cur.value);
                exit(1);
            }
            char member[128]; strncpy(member,p->cur.value,127); advance(p);
            if(check(p,TOK_LPAREN)){
                advance(p);
                ASTNode *mc=ast_new(ND_METHOD_CALL,line);
                mc->left=base;
                strncpy(mc->fun_name,member,127);
                while(!check(p,TOK_RPAREN)&&!check(p,TOK_EOF)){
                    if(mc->nchildren>0) expect(p,TOK_COMMA);
                    if(mc->nchildren<MAX_CHILDREN) mc->children[mc->nchildren++]=parse_expr(p);
                }
                expect(p,TOK_RPAREN);
                base=mc;
            } else {
                ASTNode *ma=ast_new(ND_MEMBER_ACCESS,line);
                ma->left=base;
                strncpy(ma->str_val,member,sizeof(ma->str_val)-1);
                base=ma;
            }
        } else {

            advance(p);
            ASTNode *ia=ast_new(ND_INDEX_ACCESS,line);
            ia->idx_expr=parse_expr(p);

            if(base->kind==ND_IDENT) strncpy(ia->idx_list,base->str_val,127);
            else strncpy(ia->idx_list,"_anon",127);
            expect(p,TOK_RBRACKET);
            base=ia;
        }
    }
    return base;
}

static ASTNode *parse_primary(Parser *p){
    int line=p->cur.line;

    if(check(p,TOK_NUMBER)){
        Token t=advance(p);
        ASTNode *n=ast_new(ND_NUMBER,line);
        n->num_val=atof(t.value);
        return n;
    }
    if(check(p,TOK_DURATION)){
        Token t=advance(p);
        ASTNode *n=ast_new(ND_NUMBER,line);
        n->num_val=parse_duration_secs(t.value);
        return n;
    }
    if(check(p,TOK_STRING)){
        Token t=advance(p);
        ASTNode *n=ast_new(ND_STRING,line);
        strncpy(n->str_val,t.value,sizeof(n->str_val)-1);
        return n;
    }
    if(check(p,TOK_TRUE)||check(p,TOK_FALSE)){
        Token t=advance(p);
        ASTNode *n=ast_new(ND_BOOL,line);
        n->bool_val=(t.kind==TOK_TRUE)?1:0;
        strncpy(n->str_val,t.value,16);
        return n;
    }
    if(check(p,TOK_LBRACKET)){
        advance(p);
        ASTNode *n=ast_new(ND_LIST_LIT,line);
        while(!check(p,TOK_RBRACKET)&&!check(p,TOK_EOF)){
            if(n->nchildren>0) expect(p,TOK_COMMA);
            if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=parse_expr(p);
        }
        expect(p,TOK_RBRACKET);
        return n;
    }
    if(check(p,TOK_POINT)){
        advance(p);
        expect(p,TOK_HASH);
        char word[128]={0};
        if(is_ident_like(p)){ strncpy(word,p->cur.value,127); advance(p); }
        expect(p,TOK_LPAREN);
        ASTNode *n=ast_new(ND_FUNC_CALL,line);
        char fname[160]; snprintf(fname,159,"p_%s",word); strncpy(n->fun_name,fname,127);
        while(!check(p,TOK_RPAREN)&&!check(p,TOK_EOF)){
            if(n->nchildren>0) expect(p,TOK_COMMA);
            if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=parse_expr(p);
        }
        expect(p,TOK_RPAREN);
        return n;
    }
    if(check(p,TOK_LBA)){
        advance(p);
        expect(p,TOK_PERCENT_LIT);
        char word[128]={0};
        if(is_ident_like(p)){ strncpy(word,p->cur.value,127); advance(p); }
        expect(p,TOK_LPAREN);
        ASTNode *n=ast_new(ND_FUNC_CALL,line);
        char fname[160]; snprintf(fname,159,"lba_%s",word); strncpy(n->fun_name,fname,127);
        while(!check(p,TOK_RPAREN)&&!check(p,TOK_EOF)){
            if(n->nchildren>0) expect(p,TOK_COMMA);
            if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=parse_expr(p);
        }
        expect(p,TOK_RPAREN);
        return n;
    }
    if(check(p,TOK_LPAREN)){
        advance(p);
        ASTNode *e=parse_expr(p);
        expect(p,TOK_RPAREN);
        return e;
    }
    if(check(p,TOK_BANG)||check(p,TOK_MINUS)){
        Token op=advance(p);
        ASTNode *u=ast_new(ND_UNOP,line);
        u->op=op.kind;
        u->right=parse_primary(p);
        return u;
    }

    if(is_ident_like(p)&&strcmp(p->cur.value,"reg")==0){
        Lexer save_lex=p->lex; Token save_reg=p->cur;
        advance(p);
        if(check(p,TOK_AMP)){
            advance(p);
            if(is_ident_like(p)&&strcmp(p->cur.value,"read")==0){
                advance(p);
                expect(p,TOK_LPAREN);
                char rname[16]={0};
                if(is_ident_like(p)){ strncpy(rname,p->cur.value,15); advance(p); }
                expect(p,TOK_RPAREN);
                ASTNode *n=ast_new(ND_REG_READ,line);
                strncpy(n->reg_name,rname,15);
                return n;
            }
        }
        p->lex=save_lex; p->cur=save_reg;
    }

    if(is_ident_like(p)&&
       (strcmp(p->cur.value,"system")==0||strcmp(p->cur.value,"compiler")==0||
        strcmp(p->cur.value,"program")==0)){
        char ns[32]; strncpy(ns,p->cur.value,31); advance(p);
        if(check(p,TOK_AT)){
            advance(p);
            if(!is_ident_like(p)){
                fprintf(stderr,"[ALT0011] Expected key after '@' at line %d\n",line);
                exit(1);
            }
            char key[64]; strncpy(key,p->cur.value,63); advance(p);
            if(strcmp(ns,"system")==0&&(strcmp(key,"point")==0||strcmp(key,"unpoint")==0)&&
               check(p,TOK_LPAREN)){
                advance(p);
                if(strcmp(key,"point")==0){
                    ASTNode *n=ast_new(ND_POINT,line);
                    if(is_ident_like(p)){ strncpy(n->point_var,p->cur.value,127); advance(p); }
                    expect(p,TOK_RPAREN);
                    return n;
                } else {
                    ASTNode *n=ast_new(ND_UNPOINT,line);
                    if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=parse_expr(p);
                    expect(p,TOK_RPAREN);
                    return n;
                }
            }
            ASTNode *n=ast_new(ND_INTROSPECT,line);
            strncpy(n->introspect_ns,ns,31);
            strncpy(n->introspect_key,key,63);
            if(strcmp(ns,"system")==0&&check(p,TOK_LPAREN)){
                advance(p);
                if(is_ident_like(p)) strncpy(n->introspect_var,p->cur.value,127);
                advance(p); expect(p,TOK_RPAREN);
            }
            return n;
        }

        ASTNode *id=ast_new(ND_IDENT,line);
        strncpy(id->str_val,ns,sizeof(id->str_val)-1);
        return parse_postfix(p,id);
    }

    if(check(p,TOK_RESPOND)){
        advance(p);
        expect(p,TOK_DOT);
        char member[64]; strncpy(member,p->cur.value,63); advance(p);

        ASTNode *respond_id=ast_new(ND_IDENT,line);
        strncpy(respond_id->str_val,"respond",15);
        if(check(p,TOK_LPAREN)){
            advance(p);
            ASTNode *mc=ast_new(ND_METHOD_CALL,line);
            mc->left=respond_id;
            strncpy(mc->fun_name,member,63);
            while(!check(p,TOK_RPAREN)&&!check(p,TOK_EOF)){
                if(mc->nchildren>0) expect(p,TOK_COMMA);
                if(mc->nchildren<MAX_CHILDREN) mc->children[mc->nchildren++]=parse_expr(p);
            }
            expect(p,TOK_RPAREN);
            return mc;
        }
        return respond_id;
    }

    if(check(p,TOK_USER)){
        Lexer save_lex=p->lex; Token save_tok=p->cur;
        advance(p);
        if(check(p,TOK_INPUT)){
            advance(p);
            ASTNode *n=ast_new(ND_USER_INPUT,line);
            n->input_type=VTYPE_TEXT;
            if(is_ident_like(p)&&strcmp(p->cur.value,"prompt")==0) advance(p);
            if(check(p,TOK_STRING)){
                strncpy(n->input_prompt,p->cur.value,sizeof(n->input_prompt)-1);
                advance(p);
            }
            if(check(p,TOK_AS)){
                advance(p);
                if(check_type(p)){ n->input_type=tok_to_vtype(p->cur.kind); advance(p); }
            }
            return n;
        }

        p->lex=save_lex; p->cur=save_tok;
    }

    if(is_ident_like(p)&&!check(p,TOK_INPUT)){
        Token t=advance(p);
        char name[128]; strncpy(name,t.value,127);

        if(check(p,TOK_LPAREN)){
            advance(p);
            ASTNode *n=ast_new(ND_FUNC_CALL,line);
            strncpy(n->fun_name,name,127);
            while(!check(p,TOK_RPAREN)&&!check(p,TOK_EOF)){
                if(n->nchildren>0) expect(p,TOK_COMMA);
                if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=parse_expr(p);
            }
            expect(p,TOK_RPAREN);
            return parse_postfix(p,n);
        }

        ASTNode *id=ast_new(ND_IDENT,line);
        strncpy(id->str_val,name,sizeof(id->str_val)-1);
        return parse_postfix(p,id);
    }

    if(check(p,TOK_INPUT)){
        advance(p);
        ASTNode *n=ast_new(ND_USER_INPUT,line);
        n->input_type=VTYPE_TEXT;
        if(is_ident_like(p)&&strcmp(p->cur.value,"prompt")==0) advance(p);
        if(check(p,TOK_STRING)){
            strncpy(n->input_prompt,p->cur.value,sizeof(n->input_prompt)-1);
            advance(p);
        }
        if(check(p,TOK_AS)){
            advance(p);
            if(check_type(p)){ n->input_type=tok_to_vtype(p->cur.kind); advance(p); }
        }
        return n;
    }

    if(check(p,TOK_KEY_KW)){
        advance(p);
        ASTNode *n=ast_new(ND_KEY_EXPR,line);
        if(check(p,TOK_STRING)||is_ident_like(p)){
            strncpy(n->str_val,p->cur.value,sizeof(n->str_val)-1); advance(p);
        }
        return n;
    }

    fprintf(stderr,"[ALT0003] Unexpected token '%s' in expression at line %d\n",
            p->cur.value, p->cur.line);
    advance(p);
    return ast_new(ND_NUMBER,line);
}

static ASTNode *parse_unary(Parser *p){
    int line=p->cur.line;
    if(check(p,TOK_BANG)||check(p,TOK_MINUS)||check(p,TOK_TILDE)){
        Token op=advance(p);
        ASTNode *u=ast_new(ND_UNOP,line);
        u->op=op.kind;
        u->right=parse_unary(p);
        return u;
    }
    return parse_primary(p);
}

static ASTNode *parse_mul(Parser *p){
    ASTNode *left=parse_unary(p);
    while(check(p,TOK_STAR)||check(p,TOK_SLASH)||check(p,TOK_PERCENT_LIT)){
        int line=p->cur.line;
        Token op=advance(p);
        ASTNode *b=ast_new(ND_BINOP,line);
        b->op=op.kind; b->left=left; b->right=parse_unary(p);
        left=b;
    }
    return left;
}

static ASTNode *parse_add(Parser *p){
    ASTNode *left=parse_mul(p);
    while(check(p,TOK_PLUS)||check(p,TOK_MINUS)){
        int line=p->cur.line;
        Token op=advance(p);
        ASTNode *b=ast_new(ND_BINOP,line);
        b->op=op.kind; b->left=left; b->right=parse_mul(p);
        left=b;
    }
    return left;
}

static ASTNode *parse_shift(Parser *p){
    ASTNode *left=parse_add(p);
    while(check(p,TOK_SHL)||check(p,TOK_SHR)){
        int line=p->cur.line;
        Token op=advance(p);
        ASTNode *b=ast_new(ND_BINOP,line);
        b->op=op.kind; b->left=left; b->right=parse_add(p);
        left=b;
    }
    return left;
}

static ASTNode *parse_bitand(Parser *p){
    ASTNode *left=parse_shift(p);
    while(check(p,TOK_AMP)){
        int line=p->cur.line;
        Token op=advance(p);
        ASTNode *b=ast_new(ND_BINOP,line);
        b->op=op.kind; b->left=left; b->right=parse_shift(p);
        left=b;
    }
    return left;
}

static ASTNode *parse_bitxor(Parser *p){
    ASTNode *left=parse_bitand(p);
    while(check(p,TOK_CARET)){
        int line=p->cur.line;
        Token op=advance(p);
        ASTNode *b=ast_new(ND_BINOP,line);
        b->op=op.kind; b->left=left; b->right=parse_bitand(p);
        left=b;
    }
    return left;
}

static ASTNode *parse_bitor(Parser *p){
    ASTNode *left=parse_bitxor(p);
    while(check(p,TOK_PIPE)){
        int line=p->cur.line;
        Token op=advance(p);
        ASTNode *b=ast_new(ND_BINOP,line);
        b->op=op.kind; b->left=left; b->right=parse_bitxor(p);
        left=b;
    }
    return left;
}

static ASTNode *parse_cmp(Parser *p){
    ASTNode *left=parse_bitor(p);
    while(check(p,TOK_LT)||check(p,TOK_GT)||check(p,TOK_LTE)||check(p,TOK_GTE)){
        int line=p->cur.line;
        Token op=advance(p);
        ASTNode *b=ast_new(ND_BINOP,line);
        b->op=op.kind; b->left=left; b->right=parse_bitor(p);
        left=b;
    }
    return left;
}

static ASTNode *parse_eq(Parser *p){
    ASTNode *left=parse_cmp(p);
    while(check(p,TOK_EQ)||check(p,TOK_NEQ)){
        int line=p->cur.line;
        Token op=advance(p);
        ASTNode *b=ast_new(ND_BINOP,line);
        b->op=op.kind; b->left=left; b->right=parse_cmp(p);
        left=b;
    }
    return left;
}

static ASTNode *parse_logic(Parser *p){
    ASTNode *left=parse_eq(p);
    while(check(p,TOK_AND)||check(p,TOK_OR)){
        int line=p->cur.line;
        Token op=advance(p);
        ASTNode *b=ast_new(ND_BINOP,line);
        b->op=op.kind; b->left=left; b->right=parse_eq(p);
        left=b;
    }
    return left;
}

static ASTNode *parse_expr(Parser *p){ return parse_logic(p); }

static ASTNode *parse_block_until_break(Parser *p){
    ASTNode *blk=ast_new(ND_BLOCK,p->cur.line);
    while(!check(p,TOK_BREAK)&&!check(p,TOK_EOF)){
        ASTNode *s=parse_stmt(p);
        if(s&&blk->nchildren<MAX_CHILDREN) blk->children[blk->nchildren++]=s;
    }
    if(check(p,TOK_BREAK)) advance(p);
    skip_semi(p);
    return blk;
}

static ASTNode *parse_if_branch_block(Parser *p){
    ASTNode *blk=ast_new(ND_BLOCK,p->cur.line);
    while(!check(p,TOK_BREAK)&&!check(p,TOK_EOF)&&
          !check(p,TOK_ELIF)&&!check(p,TOK_ELSE)){
        ASTNode *s=parse_stmt(p);
        if(s&&blk->nchildren<MAX_CHILDREN) blk->children[blk->nchildren++]=s;
    }
    return blk;
}

static void parse_storage_quals(Parser *p, ASTNode *n){
    n->storage=STOR_AUTO; n->is_const=0; n->weight=0; n->expire_secs=0;
    while(is_storage_tok(p)||check(p,TOK_CONST)||check(p,TOK_EXPIRE)||check(p,TOK_WEIGHT)){
        if(check(p,TOK_CONST)){   advance(p); n->is_const=1; }
        else if(check(p,TOK_WEIGHT)){
            advance(p); expect(p,TOK_ASSIGN);
            n->weight=(int)atof(p->cur.value); advance(p);
        }
        else if(check(p,TOK_EXPIRE)){
            advance(p); expect(p,TOK_ASSIGN);
            const char *s=p->cur.value;
            n->expire_secs=parse_duration_secs(s); advance(p);
        }
        else if(check(p,TOK_ORBIT)){
            advance(p); n->storage=STOR_ORBIT;
            while(is_ident_like(p)||check(p,TOK_NUMBER)){
                OrbitEntry *e=&n->orbit[n->norbit];
                if(check(p,TOK_NUMBER)){
                    e->state_num=(int)atof(p->cur.value); advance(p);
                    if(is_ident_like(p)||check(p,TOK_STRING)){
                        strncpy(e->state_name,p->cur.value,63); advance(p);
                    }
                } else {
                    strncpy(e->state_name,p->cur.value,63); advance(p);
                }

                if(is_storage_tok(p)&&p->cur.kind!=TOK_ORBIT&&p->cur.kind!=TOK_PREFER){
                    e->storage=tok_to_stor(p->cur.kind); advance(p);
                } else { e->storage=STOR_RAM; }
                if(n->norbit<MAX_ORBIT) n->norbit++;
                if(check(p,TOK_COMMA)) advance(p);
                else break;
            }
        }
        else if(check(p,TOK_PREFER)){
            advance(p); n->storage=STOR_PREFER;
            while(is_storage_tok(p)&&p->cur.kind!=TOK_ORBIT&&p->cur.kind!=TOK_PREFER){
                PreferEntry *e=&n->prefer[n->nprefer];
                e->storage=tok_to_stor(p->cur.kind); advance(p);
                if(n->nprefer<MAX_PREFER) n->nprefer++;
                if(check(p,TOK_COMMA)) advance(p); else break;
            }
        }
        else {
            n->storage=tok_to_stor(p->cur.kind); advance(p);
        }
    }
}

static ASTNode *parse_ident_led_stmt(Parser *p, char *name, int line){

        if(check(p,TOK_MIGRATE)){
            advance(p);
            ASTNode *n=ast_new(ND_MIGRATE,line);
            strncpy(n->migrate_var,name,127);
            if(check(p,TOK_AS)) advance(p);
            if(check(p,TOK_NUMBER)){
                n->migrate_num=(int)atof(p->cur.value); advance(p);
            } else if(check(p,TOK_STRING)||is_ident_like(p)){
                strncpy(n->migrate_state,p->cur.value,63); advance(p);
                n->migrate_by_name=1;
            }
            skip_semi(p); return n;
        }

        if(check(p,TOK_ASSIGN)){
            advance(p);
            ASTNode *n=ast_new(ND_ASSIGN,line);
            n->left=ast_new(ND_IDENT,line);
            strncpy(n->left->str_val,name,127);
            n->right=parse_expr(p);
            skip_semi(p); return n;
        }

        if(check(p,TOK_PLUS_ASSIGN)||check(p,TOK_MINUS_ASSIGN)||
           check(p,TOK_STAR_ASSIGN)||check(p,TOK_SLASH_ASSIGN)||
           check(p,TOK_PERCENT_ASSIGN)){
            Token op=advance(p);
            ASTNode *n=ast_new(ND_COMPOUND_ASSIGN,line);
            strncpy(n->var_name,name,127);
            n->compound_op=op.kind;
            n->right=parse_expr(p);
            skip_semi(p); return n;
        }

        if(check(p,TOK_LBRACKET)){
            advance(p);
            ASTNode *idx=parse_expr(p);
            expect(p,TOK_RBRACKET);
            if(check(p,TOK_ASSIGN)){
                advance(p);
                ASTNode *n=ast_new(ND_INDEX_ASSIGN,line);
                strncpy(n->idx_list,name,127);
                n->idx_expr=idx;
                n->idx_val=parse_expr(p);
                skip_semi(p); return n;
            }

            ASTNode *ia=ast_new(ND_INDEX_ACCESS,line);
            strncpy(ia->idx_list,name,127); ia->idx_expr=idx;
            ASTNode *full=parse_postfix(p,ia);
            ASTNode *s=ast_new(ND_EXPR_STMT,line);
            s->children[s->nchildren++]=full;
            skip_semi(p); return s;
        }

        if(check(p,TOK_DOT)){
            ASTNode *id=ast_new(ND_IDENT,line);
            strncpy(id->str_val,name,127);
            ASTNode *chain=parse_postfix(p,id);

            if(check(p,TOK_ASSIGN)){
                advance(p);
                ASTNode *n=ast_new(ND_ASSIGN,line);
                n->left=chain; n->right=parse_expr(p);
                skip_semi(p); return n;
            }
            ASTNode *s=ast_new(ND_EXPR_STMT,line);
            s->children[s->nchildren++]=chain;
            skip_semi(p); return s;
        }

        if(check(p,TOK_LPAREN)){

            advance(p);
            ASTNode *n=ast_new(ND_FUNC_CALL,line);
            strncpy(n->fun_name,name,127);
            while(!check(p,TOK_RPAREN)&&!check(p,TOK_EOF)){
                if(n->nchildren>0) expect(p,TOK_COMMA);
                if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=parse_expr(p);
            }
            expect(p,TOK_RPAREN);
            ASTNode *s=ast_new(ND_EXPR_STMT,line);
            s->children[s->nchildren++]=parse_postfix(p,n);
            skip_semi(p); return s;
        }

        if(check(p,TOK_NUMBER)||check(p,TOK_STRING)||check(p,TOK_TRUE)||
           check(p,TOK_FALSE)||check(p,TOK_LBRACKET)||
           is_ident_like(p)){
            ASTNode *n=ast_new(ND_FUNC_CALL,line);
            strncpy(n->fun_name,name,127);
            n->children[n->nchildren++]=parse_expr(p);
            while(check(p,TOK_COMMA)){
                advance(p);
                if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=parse_expr(p);
            }
            ASTNode *s=ast_new(ND_EXPR_STMT,line);
            s->children[s->nchildren++]=n;
            skip_semi(p); return s;
        }

        ASTNode *id=ast_new(ND_IDENT,line);
        strncpy(id->str_val,name,127);
        ASTNode *chain=parse_postfix(p,id);
        ASTNode *s=ast_new(ND_EXPR_STMT,line);
        s->children[s->nchildren++]=chain;
        skip_semi(p); return s;
}

static ASTNode *parse_stmt(Parser *p){
    int line=p->cur.line;

    if(is_ident_like(p)&&strcmp(p->cur.value,"reg")==0){
        Lexer save_lex=p->lex; Token save_reg=p->cur;
        advance(p);
        if(check(p,TOK_AMP)){
            advance(p);
            if(check(p,TOK_NUMBER)){
                int bits=(int)atof(p->cur.value); advance(p);
                char rname[16]={0};
                if(is_ident_like(p)){ strncpy(rname,p->cur.value,15); advance(p); }
                ASTNode *n=ast_new(ND_REG_DECL,line);
                n->reg_bits=bits;
                strncpy(n->reg_name,rname,15);
                expect(p,TOK_ASSIGN);
                if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=parse_expr(p);
                skip_semi(p);
                return n;
            }
            if(is_ident_like(p)&&strcmp(p->cur.value,"write")==0){
                advance(p);
                expect(p,TOK_LPAREN);
                char rname[16]={0};
                if(is_ident_like(p)){ strncpy(rname,p->cur.value,15); advance(p); }
                expect(p,TOK_RPAREN);
                ASTNode *n=ast_new(ND_REG_WRITE,line);
                strncpy(n->reg_name,rname,15);
                expect(p,TOK_ASSIGN);
                if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=parse_expr(p);
                skip_semi(p);
                return n;
            }
            if(is_ident_like(p)&&strcmp(p->cur.value,"free")==0){
                advance(p);
                expect(p,TOK_LPAREN);
                char rname[16]={0};
                if(is_ident_like(p)){ strncpy(rname,p->cur.value,15); advance(p); }
                expect(p,TOK_RPAREN);
                ASTNode *n=ast_new(ND_REG_FREE,line);
                strncpy(n->reg_name,rname,15);
                skip_semi(p);
                return n;
            }
        }
        p->lex=save_lex; p->cur=save_reg;
    }

    if(is_soft_keyword_kind(p->cur.kind)){
        TokenKind nk;
        { Lexer sl=p->lex; Token sc=p->cur; advance(p); nk=p->cur.kind; p->lex=sl; p->cur=sc; }
        if(nk==TOK_ASSIGN||nk==TOK_PLUS_ASSIGN||nk==TOK_MINUS_ASSIGN||
           nk==TOK_STAR_ASSIGN||nk==TOK_SLASH_ASSIGN||nk==TOK_PERCENT_ASSIGN||
           nk==TOK_DOT||nk==TOK_LBRACKET){
            char name[128]; strncpy(name,p->cur.value,127); name[127]='\0';
            advance(p);
            return parse_ident_led_stmt(p,name,line);
        }
    }

    if(check(p,TOK_DATA_KW)){
        advance(p);
        char dname[128]={0};
        if(is_ident_like(p)){ strncpy(dname,p->cur.value,127); advance(p); }
        if(check(p,TOK_MIGRATE)){
            advance(p);
            char lugar[64]={0};
            if(is_storage_tok(p)||is_ident_like(p)){ strncpy(lugar,p->cur.value,63); advance(p); }
            ASTNode *n=ast_new(ND_FUNC_CALL,line);
            strncpy(n->fun_name,"data_migrate",127);
            ASTNode *a1=ast_new(ND_STRING,line); strncpy(a1->str_val,dname,127); n->children[n->nchildren++]=a1;
            ASTNode *a2=ast_new(ND_STRING,line); strncpy(a2->str_val,lugar,63); n->children[n->nchildren++]=a2;
            ASTNode *s=ast_new(ND_EXPR_STMT,line);
            s->children[s->nchildren++]=n;
            skip_semi(p); return s;
        }
        char lugar[64]={0};
        if(is_storage_tok(p)||is_ident_like(p)){ strncpy(lugar,p->cur.value,63); advance(p); }
        skip_semi(p);
        ASTNode *blk=ast_new(ND_BLOCK,line);
        while(!check(p,TOK_DATA_KW)&&!check(p,TOK_EOF)){
            ASTNode *s=parse_stmt(p);
            if(s){
                if(s->kind==ND_VAR_DECL){
                    char pf[256]; snprintf(pf,255,"%s.%s.%s",dname,s->var_name,lugar);
                    strncpy(s->persist_file,pf,255);
                }
                if(blk->nchildren<MAX_CHILDREN) blk->children[blk->nchildren++]=s;
            }
        }
        if(check(p,TOK_DATA_KW)){ advance(p); if(check(p,TOK_CREATE)) advance(p); }
        skip_semi(p);
        return blk;
    }

    if(check(p,TOK_DEFINE)){
        advance(p);
        VType vtype=VTYPE_TEXT;
        if(check_type(p)){ vtype=tok_to_vtype(p->cur.kind); advance(p); }
        char vname[128]={0};
        if(is_ident_like(p)){ strncpy(vname,p->cur.value,127); advance(p); }
        ASTNode *n=ast_new(ND_VAR_DECL,line);
        n->var_type=vtype;
        strncpy(n->var_name,vname,127);
        parse_storage_quals(p,n);
        if(check(p,TOK_ASSIGN)){
            advance(p);
            if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=parse_expr(p);
        }
        skip_semi(p); return n;
    }

    if(check(p,TOK_CREATE)){
        advance(p);

        if(check(p,TOK_OBJECT)) advance(p);
        char class_name[128]={0};
        if(is_ident_like(p)){ strncpy(class_name,p->cur.value,127); advance(p); }
        char vname[128]={0};
        if(check(p,TOK_AS)){ advance(p); }
        if(is_ident_like(p)){ strncpy(vname,p->cur.value,127); advance(p); }
        ASTNode *n=ast_new(ND_VAR_DECL,line);
        n->var_type=VTYPE_OBJECT;
        strncpy(n->var_name,vname,127);
        n->storage=STOR_RAM;
        ASTNode *init=ast_new(ND_FUNC_CALL,line);
        strncpy(init->fun_name,class_name,127);
        n->children[n->nchildren++]=init;
        skip_semi(p); return n;
    }

    if(check(p,TOK_IF)){
        advance(p);
        ASTNode *n=ast_new(ND_IF,line);
        n->children[n->nchildren++]=parse_expr(p);
        skip_semi(p);
        n->children[n->nchildren++]=parse_if_branch_block(p);
        while(check(p,TOK_ELIF)){
            advance(p);
            n->nelif++;
            n->children[n->nchildren++]=parse_expr(p); skip_semi(p);
            n->children[n->nchildren++]=parse_if_branch_block(p);
        }
        if(check(p,TOK_ELSE)){
            advance(p); skip_semi(p);
            n->has_else=1;
            n->children[n->nchildren++]=parse_if_branch_block(p);
        }
        if(check(p,TOK_BREAK)) advance(p);
        skip_semi(p);
        return n;
    }

    if(check(p,TOK_WHILE)){
        advance(p);
        ASTNode *n=ast_new(ND_WHILE,line);
        n->count_expr=parse_expr(p); skip_semi(p);
        n->body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_REPEAT)){
        advance(p);
        ASTNode *n=ast_new(ND_REPEAT,line);
        n->count_expr=parse_expr(p);
        if(is_ident_like(p)&&strcmp(p->cur.value,"times")==0) advance(p);
        skip_semi(p);
        n->body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_FOREVER)){
        advance(p); skip_semi(p);
        ASTNode *n=ast_new(ND_FOREVER,line);
        n->body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_FOREACH)){
        advance(p);
        ASTNode *n=ast_new(ND_FOREACH,line);
        if(is_ident_like(p)){ strncpy(n->iter_var,p->cur.value,127); advance(p); }
        expect(p,TOK_IN);
        n->iter_list_expr=parse_expr(p);
        skip_semi(p);
        n->body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_TRY)){
        advance(p); skip_semi(p);
        ASTNode *n=ast_new(ND_TRY_CATCH,line);
        n->try_body=parse_block_until_break(p);
        expect(p,TOK_CATCH);
        if(check(p,TOK_AS)) advance(p);
        if(is_ident_like(p)){ strncpy(n->catch_ident,p->cur.value,63); advance(p); }
        else strncpy(n->catch_ident,"err",63);
        skip_semi(p);
        n->catch_body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_FUN)){
        advance(p);
        ASTNode *n=ast_new(ND_FUN_DECL,line);
        n->return_type=VTYPE_VOID;
        if(is_ident_like(p)){ strncpy(n->fun_name,p->cur.value,127); advance(p); }

        if(check(p,TOK_ARROW)){
            advance(p);
            if(check_type(p)){ n->return_type=tok_to_vtype(p->cur.kind); advance(p); }
        }

        while(check_type(p)){
            VType pt=tok_to_vtype(p->cur.kind); advance(p);
            char pname[128]={0};
            if(is_ident_like(p)){ strncpy(pname,p->cur.value,127); advance(p); }
            if(n->nparam<MAX_PARAMS){
                strncpy(n->param_names[n->nparam],pname,127);
                n->param_types[n->nparam]=pt;
                n->nparam++;
            }
            if(check(p,TOK_COMMA)) advance(p); else break;
        }
        skip_semi(p);
        n->fun_body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_CLASS)){
        advance(p);
        ASTNode *n=ast_new(ND_CLASS_DECL,line);
        if(is_ident_like(p)){ strncpy(n->class_name,p->cur.value,127); advance(p); }
        skip_semi(p);

        while(!check(p,TOK_BREAK)&&!check(p,TOK_EOF)){
            if(check(p,TOK_CREATE)){
                Lexer save_lex=p->lex; Token save_tok=p->cur;
                advance(p);
                if(check(p,TOK_CLASS)){ advance(p); break; }
                p->lex=save_lex; p->cur=save_tok;
            }
            ASTNode *s=parse_stmt(p);
            if(s&&n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=s;
        }
        if(check(p,TOK_BREAK)) advance(p);
        skip_semi(p);
        return n;
    }

    if(check(p,TOK_LOG)){
        advance(p);
        ASTNode *n=ast_new(ND_LOG,line);
        if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=parse_expr(p);
        skip_semi(p); return n;
    }

    if(check(p,TOK_RETURN)){
        advance(p);
        ASTNode *n=ast_new(ND_RETURN,line);
        if(!check(p,TOK_SEMICOLON)&&!check(p,TOK_BREAK)&&!check(p,TOK_EOF))
            n->children[n->nchildren++]=parse_expr(p);
        skip_semi(p); return n;
    }

    if(check(p,TOK_EXIT)){
        advance(p); skip_semi(p);
        return ast_new(ND_EXIT,line);
    }

    if(check(p,TOK_WAIT)){
        advance(p);
        ASTNode *n=ast_new(ND_WAIT,line);
        if(check(p,TOK_DURATION)||check(p,TOK_NUMBER)){
            n->wait_secs=parse_duration_secs(p->cur.value); advance(p);
        }
        skip_semi(p); return n;
    }

    if(check(p,TOK_RELEASE)){
        advance(p);
        ASTNode *n=ast_new(ND_RELEASE,line);
        if(is_ident_like(p)){ strncpy(n->release_var,p->cur.value,127); advance(p); }
        skip_semi(p); return n;
    }

    if(check(p,TOK_MIGRATE)){
        advance(p);
        ASTNode *n=ast_new(ND_MIGRATE,line);
        if(is_ident_like(p)){ strncpy(n->migrate_var,p->cur.value,127); advance(p); }
        if(check(p,TOK_AS)) advance(p);
        if(check(p,TOK_NUMBER)){
            n->migrate_num=(int)atof(p->cur.value); advance(p);
        } else if(check(p,TOK_STRING)||is_ident_like(p)){
            strncpy(n->migrate_state,p->cur.value,63); advance(p);
            n->migrate_by_name=1;
        }
        skip_semi(p); return n;
    }

    if(check(p,TOK_SNAPSHOT)){
        advance(p);
        ASTNode *n=ast_new(ND_SNAPSHOT,line);
        if(is_ident_like(p)||check(p,TOK_CREATE)){
            strncpy(n->snap_op,p->cur.value,15); advance(p);
        }
        if(check(p,TOK_STRING)||is_ident_like(p)||check(p,TOK_CREATE)){
            strncpy(n->snap_name,p->cur.value,255); advance(p);
        }
        skip_semi(p); return n;
    }

    if(check(p,TOK_CHOOSE)){
        advance(p);
        ASTNode *n=ast_new(ND_CHOOSE,line);
        if(is_ident_like(p)){ strncpy(n->choose_name,p->cur.value,127); advance(p); }
        skip_semi(p);
        while(!check(p,TOK_DEFINE)&&!check(p,TOK_EOF)&&n->nchoose<MAX_CHOOSE_OPT){

            double w=1.0;
            char opt[128]={0};
            if(check(p,TOK_NUMBER)){
                w=atof(p->cur.value); advance(p);
                if(check(p,TOK_PERCENT_LIT)) advance(p);
            }
            if(check(p,TOK_ASSIGN)) advance(p);
            if(check(p,TOK_STRING)||is_ident_like(p)){
                strncpy(opt,p->cur.value,127); advance(p);
            }
            strncpy(n->choose_opts[n->nchoose],opt,127);
            n->choose_weights[n->nchoose]=w;
            n->nchoose++;
            skip_semi(p);
        }
        if(check(p,TOK_DEFINE)) advance(p);
        skip_semi(p); return n;
    }

    if(check(p,TOK_IMPORT)){
        advance(p);

        if(check(p,TOK_MODULE)) advance(p);
        if(check(p,TOK_STRING)||is_ident_like(p)) advance(p);
        if(check(p,TOK_AS)) { advance(p); if(is_ident_like(p)) advance(p); }
        skip_semi(p);
        return NULL;
    }

    if(check(p,TOK_CALL)){
        advance(p);
        ASTNode *n=ast_new(ND_CALL_GALAXY,line);
        if(is_ident_like(p)){ strncpy(n->galaxy_name,p->cur.value,127); advance(p); }
        skip_semi(p); return n;
    }

    if(check(p,TOK_LISTEN)){
        advance(p);
        ASTNode *n=ast_new(ND_LISTEN,line);
        if(check(p,TOK_NUMBER)){ n->server_port=(int)atof(p->cur.value); advance(p); }
        else n->server_port=8080;
        skip_semi(p);
        n->body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_ROUTE)){
        advance(p);
        ASTNode *n=ast_new(ND_ROUTE,line);
        strncpy(n->route_method,"GET",15);
        if(check(p,TOK_STRING)){ strncpy(n->route_method,p->cur.value,15); advance(p); }
        if(check(p,TOK_STRING)){ strncpy(n->route_path,p->cur.value,511); advance(p); }
        if(check(p,TOK_RATE_LIMIT)){
            advance(p);
            if(check(p,TOK_NUMBER)){ n->rate_limit=(int)atof(p->cur.value); advance(p); }
            if(check(p,TOK_PER_MINUTE)||check(p,TOK_PER_SECOND)) advance(p);
        }

        {
            char hname[128]={0}; int hi=0;
            const char *path=n->route_path;
            for(int i=0;path[i]&&hi<126;i++){
                char c=path[i];
                if(c=='/'||c==':'||c=='-') { if(hi>0&&hname[hi-1]!='_') hname[hi++]='_'; }
                else if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')) hname[hi++]=c;
            }
            if(hi==0||hname[0]=='_'){ strcpy(hname,"root"); }

            char full[140]={0};
            char m[10]={0}; int mi=0;
            for(int i=0;n->route_method[i]&&mi<8;i++) m[mi++]=(char)tolower((unsigned char)n->route_method[i]);
            snprintf(full,sizeof(full),"%s_%s",m,hname);
            strncpy(n->handler_name,full,127);
        }
        skip_semi(p);
        n->body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_MIDDLEWARE)){
        advance(p);
        ASTNode *n=ast_new(ND_MIDDLEWARE,line);
        if(is_ident_like(p)){ strncpy(n->handler_name,p->cur.value,127); advance(p); }
        else { snprintf(n->handler_name,127,"mw%d",line); }
        skip_semi(p);
        n->body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_JOB)||check(p,TOK_SCHEDULE)){
        advance(p);
        ASTNode *n=ast_new(ND_JOB,line);
        if(is_ident_like(p)){ strncpy(n->handler_name,p->cur.value,127); advance(p); }
        if(check(p,TOK_EVERY)) advance(p);
        if(check(p,TOK_DURATION)||check(p,TOK_STRING)){
            n->job_interval_secs=parse_duration_secs(p->cur.value); advance(p);
        } else if(check(p,TOK_NUMBER)){
            n->job_interval_secs=atof(p->cur.value); advance(p);
            if(check(p,TOK_PER_MINUTE)) n->job_interval_secs=60.0/n->job_interval_secs;
            if(check(p,TOK_PER_SECOND)) n->job_interval_secs=1.0/n->job_interval_secs;
            advance(p);
        }
        skip_semi(p);
        n->body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_HEALTH)){
        advance(p);
        ASTNode *n=ast_new(ND_HEALTH,line);
        strncpy(n->health_path,"/health",511);
        if(check(p,TOK_STRING)){ strncpy(n->health_path,p->cur.value,511); advance(p); }
        skip_semi(p);

        while(!check(p,TOK_BREAK)&&!check(p,TOK_EOF)){
            if(check(p,TOK_CHECK)){
                advance(p);
                char chk_name[64]={0};
                if(check(p,TOK_STRING)||is_ident_like(p)){
                    strncpy(chk_name,p->cur.value,63); advance(p);
                }
                if(check(p,TOK_ARROW)) advance(p);
                ASTNode *chk=ast_new(ND_EXPR_STMT,line);
                chk->children[chk->nchildren++]=parse_expr(p);
                strncpy(chk->str_val,chk_name,127);
                if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=chk;
                skip_semi(p);
            } else {
                ASTNode *s=parse_stmt(p);
                if(s&&n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=s;
            }
        }
        if(check(p,TOK_BREAK)) advance(p);
        skip_semi(p);
        return n;
    }

    if(check(p,TOK_METRICS)){
        advance(p);
        ASTNode *n=ast_new(ND_METRICS,line);
        strncpy(n->metrics_path,"/metrics",511);
        if(check(p,TOK_STRING)){ strncpy(n->metrics_path,p->cur.value,511); advance(p); }
        skip_semi(p);
        return n;
    }

    if(check(p,TOK_ON_SHUTDOWN)){
        advance(p); skip_semi(p);
        ASTNode *n=ast_new(ND_ON_SHUTDOWN,line);
        n->body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_SESSION)){
        advance(p);
        ASTNode *n=ast_new(ND_SESSION_DECL,line);
        if(is_ident_like(p)){ strncpy(n->session_var,p->cur.value,127); advance(p); }
        if(check(p,TOK_EXPIRES)||check(p,TOK_EXPIRE)) {
            advance(p);
            if(check(p,TOK_DURATION)||check(p,TOK_NUMBER)||check(p,TOK_STRING)){
                n->session_ttl_secs=parse_duration_secs(p->cur.value); advance(p);
            }
        }
        skip_semi(p); return n;
    }

    if(check(p,TOK_CONFIG)){
        advance(p); skip_semi(p);
        ASTNode *n=ast_new(ND_CONFIG_DECL,line);
        n->body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_LINK)){
        advance(p);
        ASTNode *n=ast_new(ND_LINK,line);

        if(is_ident_like(p)){ strncpy(n->str_val,p->cur.value,127); advance(p); }

        if(is_ident_like(p)){ strncpy(n->gfx_link_lib,p->cur.value,63); advance(p); }
        skip_semi(p); return n;
    }

    if(check(p,TOK_WINDOW)){
        advance(p); skip_semi(p);
        ASTNode *n=ast_new(ND_WINDOW_DECL,line);
        while(!check(p,TOK_EOF)){
            if(check(p,TOK_CREATE)){
                Lexer sl=p->lex; Token sc=p->cur; advance(p);
                if(check(p,TOK_WINDOW)){ advance(p); break; }
                p->lex=sl; p->cur=sc; break;
            }
            if(check(p,TOK_BREAK)){ advance(p); break; }

            if(is_ident_like(p) && n->ngfx_props < MAX_GFX_PROPS){
                char k[32]; strncpy(k,p->cur.value,31); advance(p);
                strncpy(n->gfx_props[n->ngfx_props],k,31);
                if(check(p,TOK_ASSIGN)) advance(p);
                n->gfx_vals[n->ngfx_props]=parse_expr(p);
                n->ngfx_props++;
            } else { advance(p); }
            skip_semi(p);
        }
        return n;
    }

    if(check(p,TOK_LOOP)){
        advance(p); skip_semi(p);
        ASTNode *n=ast_new(ND_LOOP,line);
        n->body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_DRAW)){
        advance(p);
        ASTNode *n=ast_new(ND_DRAW_CMD,line);

        if(check(p,TOK_TEXT)||check(p,TOK_NUMERIC)||check(p,TOK_BOOL)||
           check(p,TOK_LIST)||check(p,TOK_OBJECT)||check(p,TOK_TOKEN)){
            strncpy(n->gfx_kind,p->cur.value,31); advance(p);
        } else if(is_ident_like(p)){
            strncpy(n->gfx_kind,p->cur.value,31); advance(p);
        } else {
            strncpy(n->gfx_kind,"rect",31);
        }
        skip_semi(p);
        while(!check(p,TOK_EOF)){
            if(check(p,TOK_CREATE)){
                Lexer sl=p->lex; Token sc=p->cur; advance(p);
                if(check(p,TOK_DRAW)){ advance(p); break; }
                p->lex=sl; p->cur=sc; break;
            }
            if(check(p,TOK_BREAK)){ advance(p); break; }
            if(is_ident_like(p) && n->ngfx_props < MAX_GFX_PROPS){
                char k[32]; strncpy(k,p->cur.value,31); advance(p);
                strncpy(n->gfx_props[n->ngfx_props],k,31);
                if(check(p,TOK_ASSIGN)) advance(p);
                n->gfx_vals[n->ngfx_props]=parse_expr(p);
                n->ngfx_props++;
            } else { advance(p); }
            skip_semi(p);
        }
        return n;
    }

    if(check(p,TOK_CLEAR)){
        advance(p);
        ASTNode *n=ast_new(ND_CLEAR_STMT,line);

        if(is_ident_like(p)){ strncpy(n->gfx_kind,p->cur.value,31); advance(p); }
        else if(!check(p,TOK_SEMICOLON)&&!check(p,TOK_EOF)){
            n->left=parse_expr(p);
        }
        skip_semi(p); return n;
    }

    if(check(p,TOK_COLOR_KW)){
        advance(p);
        ASTNode *n=ast_new(ND_COLOR_DECL,line);
        if(is_ident_like(p)){ strncpy(n->var_name,p->cur.value,127); advance(p); }
        if(check(p,TOK_ASSIGN)) advance(p);

        if(!check(p,TOK_SEMICOLON)&&!check(p,TOK_EOF))
            n->children[n->nchildren++]=parse_expr(p);
        skip_semi(p); return n;
    }

    if(check(p,TOK_IMAGE)){
        advance(p);
        ASTNode *n=ast_new(ND_IMAGE_DECL,line);
        n->var_type=VTYPE_IMAGE;
        if(is_ident_like(p)){ strncpy(n->var_name,p->cur.value,127); advance(p); }
        if(check(p,TOK_ASSIGN)) advance(p);
        if(check(p,TOK_STRING)){ strncpy(n->str_val,p->cur.value,sizeof(n->str_val)-1); advance(p); }
        if(is_storage_tok(p)){ n->storage=tok_to_stor(p->cur.kind); advance(p); }
        skip_semi(p); return n;
    }

    if(check(p,TOK_SOUND)){
        advance(p);
        ASTNode *n=ast_new(ND_SOUND_DECL,line);
        n->var_type=VTYPE_SOUND;
        if(is_ident_like(p)){ strncpy(n->var_name,p->cur.value,127); advance(p); }
        if(check(p,TOK_ASSIGN)) advance(p);
        if(check(p,TOK_STRING)){ strncpy(n->str_val,p->cur.value,sizeof(n->str_val)-1); advance(p); }
        skip_semi(p); return n;
    }

    if(check(p,TOK_MUSIC)){
        advance(p);
        ASTNode *n=ast_new(ND_MUSIC_DECL,line);
        n->var_type=VTYPE_MUSIC;
        if(is_ident_like(p)){ strncpy(n->var_name,p->cur.value,127); advance(p); }
        if(check(p,TOK_ASSIGN)) advance(p);
        if(check(p,TOK_STRING)){ strncpy(n->str_val,p->cur.value,sizeof(n->str_val)-1); advance(p); }
        skip_semi(p); return n;
    }

    if(check(p,TOK_PLAY)){
        advance(p);
        ASTNode *n=ast_new(ND_PLAY_STMT,line);
        if(is_ident_like(p)){ strncpy(n->var_name,p->cur.value,127); advance(p); }
        skip_semi(p); return n;
    }

    if(check(p,TOK_STOP)){
        advance(p);
        ASTNode *n=ast_new(ND_STOP_STMT,line);
        if(is_ident_like(p)){ strncpy(n->var_name,p->cur.value,127); advance(p); }
        skip_semi(p); return n;
    }

    if(check(p,TOK_PAUSE_KW)){
        advance(p);
        ASTNode *n=ast_new(ND_PAUSE_STMT,line);
        if(is_ident_like(p)){ strncpy(n->var_name,p->cur.value,127); advance(p); }
        skip_semi(p); return n;
    }

    if(check(p,TOK_TIMER_KW)){
        advance(p);
        ASTNode *n=ast_new(ND_TIMER_DECL,line);
        if(is_ident_like(p)){ strncpy(n->var_name,p->cur.value,127); advance(p); }
        if(check(p,TOK_ASSIGN)) advance(p);
        if(!check(p,TOK_SEMICOLON)&&!check(p,TOK_EOF))
            n->children[n->nchildren++]=parse_expr(p);
        skip_semi(p); return n;
    }

    if(check(p,TOK_BUTTON)||check(p,TOK_LABEL_KW)||check(p,TOK_TEXTBOX)||
       check(p,TOK_CHECKBOX)||check(p,TOK_SLIDER_KW)||check(p,TOK_PROGRESS_KW)||
       check(p,TOK_LISTVIEW)){
        char widget_kind[32]; strncpy(widget_kind,p->cur.value,31); advance(p);
        ASTNode *n=ast_new(ND_WIDGET_DECL,line);
        strncpy(n->gfx_kind,widget_kind,31);
        if(is_ident_like(p)){ strncpy(n->var_name,p->cur.value,127); advance(p); }
        skip_semi(p);
        while(!check(p,TOK_EOF)){
            if(check(p,TOK_CREATE)){
                Lexer sl=p->lex; Token sc=p->cur; advance(p);
                if(is_ident_like(p)){ advance(p); break; }
                p->lex=sl; p->cur=sc; break;
            }
            if(check(p,TOK_BREAK)){ advance(p); break; }
            if(is_ident_like(p) && n->ngfx_props < MAX_GFX_PROPS){
                char k[32]; strncpy(k,p->cur.value,31); advance(p);
                strncpy(n->gfx_props[n->ngfx_props],k,31);
                if(check(p,TOK_ASSIGN)) advance(p);
                n->gfx_vals[n->ngfx_props]=parse_expr(p);
                n->ngfx_props++;
            } else { advance(p); }
            skip_semi(p);
        }
        return n;
    }

    if(check(p,TOK_MENU_KW)){
        advance(p);
        ASTNode *n=ast_new(ND_MENU_DECL,line);
        if(is_ident_like(p)){ strncpy(n->var_name,p->cur.value,127); advance(p); }
        skip_semi(p);
        while(!check(p,TOK_EOF)){
            if(check(p,TOK_CREATE)){
                Lexer sl=p->lex; Token sc=p->cur; advance(p);
                if(check(p,TOK_MENU_KW)||is_ident_like(p)){ advance(p); break; }
                p->lex=sl; p->cur=sc; break;
            }
            if(check(p,TOK_BREAK)){ advance(p); break; }
            if(is_ident_like(p) && n->ngfx_props < MAX_GFX_PROPS){
                char k[32]; strncpy(k,p->cur.value,31); advance(p);
                strncpy(n->gfx_props[n->ngfx_props],k,31);
                if(check(p,TOK_ASSIGN)) advance(p);
                n->gfx_vals[n->ngfx_props]=parse_expr(p);
                n->ngfx_props++;
            } else { advance(p); }
            skip_semi(p);
        }
        return n;
    }

    if(check(p,TOK_DIALOG)){
        advance(p);
        ASTNode *n=ast_new(ND_DIALOG_DECL,line);
        if(is_ident_like(p)){ strncpy(n->var_name,p->cur.value,127); advance(p); }
        skip_semi(p);
        n->body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_SCENE_KW)){
        advance(p);
        ASTNode *n=ast_new(ND_SCENE_DECL,line);
        if(is_ident_like(p)){ strncpy(n->var_name,p->cur.value,127); advance(p); }
        skip_semi(p); return n;
    }

    if(check(p,TOK_GOTO_KW)){
        advance(p);
        ASTNode *n=ast_new(ND_GOTO_STMT,line);
        if(is_ident_like(p)){ strncpy(n->var_name,p->cur.value,127); advance(p); }
        skip_semi(p); return n;
    }

    if(check(p,TOK_CURSOR_KW)){
        advance(p);
        ASTNode *n=ast_new(ND_CURSOR_STMT,line);
        if(is_ident_like(p)){ strncpy(n->gfx_kind,p->cur.value,31); advance(p); }
        skip_semi(p); return n;
    }

    if(check(p,TOK_ANIMATE)){
        advance(p);
        ASTNode *n=ast_new(ND_ANIMATE_DECL,line);
        if(is_ident_like(p)){ strncpy(n->var_name,p->cur.value,127); advance(p); }
        if(check(p,TOK_DOT)){ advance(p);
            if(is_ident_like(p)){ strncpy(n->gfx_kind,p->cur.value,31); advance(p); }
        }
        skip_semi(p);
        while(!check(p,TOK_EOF)){
            if(check(p,TOK_CREATE)){
                Lexer sl=p->lex; Token sc=p->cur; advance(p);
                if(is_ident_like(p)){ advance(p); break; }
                p->lex=sl; p->cur=sc; break;
            }
            if(check(p,TOK_BREAK)){ advance(p); break; }
            if(is_ident_like(p) && n->ngfx_props < MAX_GFX_PROPS){
                char k[32]; strncpy(k,p->cur.value,31); advance(p);
                strncpy(n->gfx_props[n->ngfx_props],k,31);
                if(check(p,TOK_ASSIGN)) advance(p);
                n->gfx_vals[n->ngfx_props]=parse_expr(p);
                n->ngfx_props++;
            } else { advance(p); }
            skip_semi(p);
        }
        return n;
    }

    if(check(p,TOK_POPUP)){
        advance(p);
        ASTNode *n=ast_new(ND_POPUP_DECL,line);
        if(is_ident_like(p)){ strncpy(n->var_name,p->cur.value,127); advance(p); }
        skip_semi(p);
        n->body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_CANVAS_GFX)){
        advance(p);
        ASTNode *n=ast_new(ND_CANVAS_GFX,line);
        if(is_ident_like(p)){ strncpy(n->var_name,p->cur.value,127); advance(p); }
        skip_semi(p);
        n->body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_COLUMN)||check(p,TOK_ROW)||check(p,TOK_GRID)){
        char lk[32]; strncpy(lk,p->cur.value,31); advance(p);
        ASTNode *n=ast_new(ND_LAYOUT,line);
        strncpy(n->gfx_kind,lk,31);
        skip_semi(p);
        n->body=parse_block_until_break(p);
        return n;
    }

    if(check(p,TOK_DB_POOL)){
        advance(p);
        ASTNode *n=ast_new(ND_DB_POOL,line);
        n->db_max=10;
        if(is_ident_like(p)){ strncpy(n->db_var,p->cur.value,127); advance(p); }
        if(check(p,TOK_ASSIGN)) advance(p);
        if(check(p,TOK_CONNECT)) advance(p);
        if(check(p,TOK_LPAREN)){ advance(p);
            if(check(p,TOK_STRING)){ strncpy(n->db_url,p->cur.value,511); advance(p); }
            expect(p,TOK_RPAREN);
        }
        if(check(p,TOK_MAX)){ advance(p);
            if(check(p,TOK_NUMBER)){ n->db_max=(int)atof(p->cur.value); advance(p); }
        }
        skip_semi(p); return n;
    }

    if(check(p,TOK_ENV_KW)){
        advance(p);
        char env_key[128]={0};
        if(check(p,TOK_LPAREN)){ advance(p);
            if(check(p,TOK_STRING)){ strncpy(env_key,p->cur.value,127); advance(p); }
            expect(p,TOK_RPAREN);
        }
        char def_val[256]=""; int is_req=0;
        if(check(p,TOK_DEFAULT_KW)){ advance(p);
            if(check(p,TOK_STRING)||check(p,TOK_NUMBER)){ strncpy(def_val,p->cur.value,255); advance(p); }
        }
        if(check(p,TOK_REQUIRED_KW)){ advance(p); is_req=1; }

        ASTNode *n=ast_new(ND_VAR_DECL,line);
        n->var_type=VTYPE_TEXT;
        strncpy(n->var_name,env_key,127);
        n->storage=STOR_RAM;

        ASTNode *init=ast_new(ND_FUNC_CALL,line);
        strncpy(init->fun_name,"altair_config_env_inline",127);
        ASTNode *ka=ast_new(ND_STRING,line); strncpy(ka->str_val,env_key,127); init->children[init->nchildren++]=ka;
        ASTNode *dv=ast_new(ND_STRING,line); strncpy(dv->str_val,def_val,255); init->children[init->nchildren++]=dv;
        ASTNode *rq=ast_new(ND_NUMBER,line); rq->num_val=is_req; init->children[init->nchildren++]=rq;
        n->children[n->nchildren++]=init;
        skip_semi(p); return n;
    }

    if(check(p,TOK_POINT)){
        advance(p);
        expect(p,TOK_HASH);
        char word[128]={0};
        if(is_ident_like(p)||check_type(p)){ strncpy(word,p->cur.value,127); advance(p); }
        if(check(p,TOK_LPAREN)){
            advance(p);
            ASTNode *n=ast_new(ND_FUNC_CALL,line);
            char fname[160]; snprintf(fname,159,"p_%s",word); strncpy(n->fun_name,fname,127);
            while(!check(p,TOK_RPAREN)&&!check(p,TOK_EOF)){
                if(n->nchildren>0) expect(p,TOK_COMMA);
                if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=parse_expr(p);
            }
            expect(p,TOK_RPAREN);
            ASTNode *s=ast_new(ND_EXPR_STMT,line);
            s->children[s->nchildren++]=n;
            skip_semi(p); return s;
        }
        char vname[128]={0};
        if(is_ident_like(p)){ strncpy(vname,p->cur.value,127); advance(p); }
        ASTNode *n=ast_new(ND_VAR_DECL,line);
        n->var_type=VTYPE_POINTER;
        strncpy(n->var_name,vname,127);
        strncpy(n->class_name,word,127);
        n->storage=STOR_AUTO;
        if(check(p,TOK_ASSIGN)){
            advance(p);
            if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=parse_expr(p);
        }
        skip_semi(p); return n;
    }
    if(check(p,TOK_LBA)){
        advance(p);
        expect(p,TOK_PERCENT_LIT);
        char word[128]={0};
        if(is_ident_like(p)||check_type(p)){ strncpy(word,p->cur.value,127); advance(p); }
        if(check(p,TOK_LPAREN)){
            advance(p);
            ASTNode *n=ast_new(ND_FUNC_CALL,line);
            char fname[160]; snprintf(fname,159,"lba_%s",word); strncpy(n->fun_name,fname,127);
            while(!check(p,TOK_RPAREN)&&!check(p,TOK_EOF)){
                if(n->nchildren>0) expect(p,TOK_COMMA);
                if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=parse_expr(p);
            }
            expect(p,TOK_RPAREN);
            ASTNode *s=ast_new(ND_EXPR_STMT,line);
            s->children[s->nchildren++]=n;
            skip_semi(p); return s;
        }
        char vname[128]={0};
        if(is_ident_like(p)){ strncpy(vname,p->cur.value,127); advance(p); }
        ASTNode *n=ast_new(ND_VAR_DECL,line);
        n->var_type=VTYPE_LBA;
        strncpy(n->var_name,vname,127);
        strncpy(n->class_name,word,127);
        n->storage=STOR_AUTO;
        if(check(p,TOK_ASSIGN)){
            advance(p);
            if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=parse_expr(p);
        }
        skip_semi(p); return n;
    }

    if(check_type(p)){
        VType vtype=tok_to_vtype(p->cur.kind); advance(p);
        char vname[128]={0};
        if(is_ident_like(p)){ strncpy(vname,p->cur.value,127); advance(p); }
        ASTNode *n=ast_new(ND_VAR_DECL,line);
        n->var_type=vtype;
        strncpy(n->var_name,vname,127);
        n->storage=STOR_AUTO;

        if(check(p,TOK_ASSIGN)){
            advance(p);
            if(n->nchildren<MAX_CHILDREN) n->children[n->nchildren++]=parse_expr(p);
        }

        if(is_storage_tok(p)) n->storage=tok_to_stor(p->cur.kind);
        while(is_storage_tok(p)||check(p,TOK_CONST)||check(p,TOK_EXPIRE)||check(p,TOK_WEIGHT)){
            if(check(p,TOK_CONST)){   advance(p); n->is_const=1; }
            else if(check(p,TOK_WEIGHT)){
                advance(p);
                if(check(p,TOK_ASSIGN)) advance(p);
                n->weight=(int)atof(p->cur.value); advance(p);
            }
            else if(check(p,TOK_EXPIRE)){
                advance(p);
                if(check(p,TOK_ASSIGN)) advance(p);
                n->expire_secs=parse_duration_secs(p->cur.value); advance(p);
            }
            else if(check(p,TOK_ORBIT)){
                advance(p); n->storage=STOR_ORBIT;
                skip_semi(p);
                while(check(p,TOK_NUMBER)){
                    OrbitEntry *e=&n->orbit[n->norbit];
                    e->state_num=(int)atof(p->cur.value); advance(p);
                    if(check(p,TOK_ASSIGN)) advance(p);
                    if(is_ident_like(p)||check(p,TOK_STRING)||check(p,TOK_CREATE)){
                        strncpy(e->state_name,p->cur.value,63); advance(p);
                    }
                    if(is_storage_tok(p)&&p->cur.kind!=TOK_ORBIT&&p->cur.kind!=TOK_PREFER){
                        e->storage=tok_to_stor(p->cur.kind); advance(p);
                    } else { e->storage=STOR_RAM; }
                    if(n->norbit<MAX_ORBIT) n->norbit++;
                    skip_semi(p);
                }
                if(check(p,TOK_BREAK)) advance(p);
            }
            else { n->storage=tok_to_stor(p->cur.kind); advance(p); }
        }
        if(is_ident_like(p) && p->cur.line==n->line){
            Lexer lex_save=p->lex; Token cur_save=p->cur;
            char part1[128]; strncpy(part1,p->cur.value,127);
            advance(p);
            if(check(p,TOK_DOT) && p->cur.line==n->line){
                advance(p);
                if(is_ident_like(p) && p->cur.line==n->line){
                    char part2[128]; strncpy(part2,p->cur.value,127);
                    advance(p);
                    snprintf(n->persist_file,255,"%s.%s",part1,part2);
                } else {
                    p->lex=lex_save; p->cur=cur_save;
                }
            } else {
                p->lex=lex_save; p->cur=cur_save;
            }
        }
        skip_semi(p); return n;
    }

    if(is_ident_like(p)){
        char name[128]; strncpy(name,p->cur.value,127);
        Token name_tok=advance(p); (void)name_tok;
        return parse_ident_led_stmt(p,name,line);
    }

    if(check(p,TOK_RESPOND)){
        ASTNode *e=parse_primary(p);
        ASTNode *s=ast_new(ND_EXPR_STMT,line);
        s->children[s->nchildren++]=e;
        skip_semi(p); return s;
    }

    if(check(p,TOK_USER)){
        advance(p);
        if(check(p,TOK_INPUT)){
            ASTNode *e=parse_primary(p);
            ASTNode *s=ast_new(ND_EXPR_STMT,line);
            s->children[s->nchildren++]=e;
            skip_semi(p); return s;
        }
    }

    if(check(p,TOK_BREAK)){ advance(p); skip_semi(p); return NULL; }

    if(check(p,TOK_SEMICOLON)){ advance(p); return NULL; }

    advance(p);
    return NULL;
}

static ASTNode *parse_header(Parser *p){
    int line=p->cur.line;
    advance(p);
    expect(p,TOK_SEMICOLON);
    ASTNode *hdr=ast_new(ND_HEADER,line);
    while(!check(p,TOK_EOF)){
        if(check(p,TOK_CREATE)){
            advance(p);
            if(check(p,TOK_ALTAIR_DOC)){ advance(p); break; }
            fprintf(stderr,"[ALT0003] Expected 'altair.doc' after 'create' at line %d\n",p->cur.line);
            exit(1);
        }
        if(!check(p,TOK_IDENT)){ advance(p); continue; }
        ASTNode *kv=ast_new(ND_ASSIGN,p->cur.line);
        Token key=advance(p);
        strncpy(kv->var_name,key.value,127);
        if(!check(p,TOK_ASSIGN)){
            fprintf(stderr,
                "[ALT0003] Encabezado 'altair.doc' sin cerrar: falta 'create altair.doc' "
                "antes de la linea %d.\n",p->cur.line);
            exit(1);
        }
        advance(p);
        ASTNode *val=ast_new(ND_STRING,p->cur.line);
        if(check(p,TOK_STRING)||check(p,TOK_NUMBER)||check(p,TOK_IDENT)){
            strncpy(val->str_val,p->cur.value,sizeof(val->str_val)-1); advance(p);
        }
        kv->right=val;
        if(hdr->nchildren<MAX_CHILDREN) hdr->children[hdr->nchildren++]=kv;
    }
    if(check(p,TOK_EOF)){
        fprintf(stderr,
            "[ALT0003] Encabezado 'altair.doc' sin cerrar: falta 'create altair.doc'.\n");
        exit(1);
    }
    return hdr;
}

ASTNode *parse_program(const char *source){
    Parser p; parser_init(&p,source);
    ASTNode *prog=ast_new(ND_PROGRAM,1);
    if(check(&p,TOK_ALTAIR_DOC))
        prog->children[prog->nchildren++]=parse_header(&p);
    ASTNode *body=ast_new(ND_BLOCK,1);
    while(!check(&p,TOK_EOF)){
        ASTNode *s=parse_stmt(&p);
        if(s&&body->nchildren<MAX_CHILDREN) body->children[body->nchildren++]=s;
    }
    prog->children[prog->nchildren++]=body;
    return prog;
}
