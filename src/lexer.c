#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const struct { const char *word; TokenKind kind; } KEYWORDS[] = {
    {"numeric",TOK_NUMERIC},{"text",TOK_TEXT},{"bool",TOK_BOOL},
    {"list",TOK_LIST},{"object",TOK_OBJECT},{"token",TOK_TOKEN},
    {"ram",TOK_RAM},{"disk",TOK_DISK},{"cache",TOK_CACHE},
    {"temp",TOK_TEMP},{"auto",TOK_AUTO},
    {"const",TOK_CONST},{"expire",TOK_EXPIRE},
    {"if",TOK_IF},{"elif",TOK_ELIF},{"else",TOK_ELSE},
    {"repeat",TOK_REPEAT},{"while",TOK_WHILE},{"forever",TOK_FOREVER},
    {"exit",TOK_EXIT},{"wait",TOK_WAIT},
    {"orbit",TOK_ORBIT},{"prefer",TOK_PREFER},
    {"migrate",TOK_MIGRATE},{"weight",TOK_WEIGHT},
    {"snapshot",TOK_SNAPSHOT},
    {"fun",TOK_FUN},{"class",TOK_CLASS},{"return",TOK_RETURN},
    {"create",TOK_CREATE},{"break",TOK_BREAK},{"define",TOK_DEFINE},
    {"foreach",TOK_FOREACH},{"in",TOK_IN},
    {"try",TOK_TRY},{"catch",TOK_CATCH},
    {"import",TOK_IMPORT},{"module",TOK_MODULE},{"as",TOK_AS},
    {"true",TOK_TRUE},{"false",TOK_FALSE},
    {"log",TOK_LOG},{"call",TOK_CALL},
    {"user",TOK_USER},{"input",TOK_INPUT},
    {"choose",TOK_CHOOSE},
    {"release",TOK_RELEASE},

    {"listen",TOK_LISTEN},
    {"route",TOK_ROUTE},
    {"middleware",TOK_MIDDLEWARE},
    {"respond",TOK_RESPOND},
    {"stop",TOK_STOP},
    {"param",TOK_PARAM},
    {"header",TOK_HEADER_KW},
    {"body",TOK_BODY_KW},
    {"rate_limit",TOK_RATE_LIMIT},
    {"per_minute",TOK_PER_MINUTE},
    {"per_second",TOK_PER_SECOND},
    {"job",TOK_JOB},
    {"schedule",TOK_SCHEDULE},
    {"every",TOK_EVERY},
    {"health",TOK_HEALTH},
    {"check",TOK_CHECK},
    {"metrics",TOK_METRICS},
    {"on_shutdown",TOK_ON_SHUTDOWN},
    {"session",TOK_SESSION},
    {"expires",TOK_EXPIRES},
    {"config",TOK_CONFIG},
    {"default",TOK_DEFAULT_KW},
    {"required",TOK_REQUIRED_KW},
    {"env",TOK_ENV_KW},
    {"db_pool",TOK_DB_POOL},
    {"max",TOK_MAX},
    {"connect",TOK_CONNECT},

    {"link",TOK_LINK},
    {"window",TOK_WINDOW},
    {"loop",TOK_LOOP},
    {"draw",TOK_DRAW},
    {"image",TOK_IMAGE},
    {"clear",TOK_CLEAR},
    {"color",TOK_COLOR_KW},
    {"sound",TOK_SOUND},
    {"music",TOK_MUSIC},
    {"play",TOK_PLAY},
    {"pause",TOK_PAUSE_KW},
    {"timer",TOK_TIMER_KW},
    {"button",TOK_BUTTON},
    {"label",TOK_LABEL_KW},
    {"textbox",TOK_TEXTBOX},
    {"checkbox",TOK_CHECKBOX},
    {"slider",TOK_SLIDER_KW},
    {"progress",TOK_PROGRESS_KW},
    {"listview",TOK_LISTVIEW},
    {"menu",TOK_MENU_KW},
    {"dialog",TOK_DIALOG},
    {"scene",TOK_SCENE_KW},
    {"goto",TOK_GOTO_KW},
    {"cursor",TOK_CURSOR_KW},
    {"animate",TOK_ANIMATE},
    {"popup",TOK_POPUP},
    {"canvas",TOK_CANVAS_GFX},
    {"column",TOK_COLUMN},
    {"row",TOK_ROW},
    {"grid",TOK_GRID},
    {"key",TOK_KEY_KW},

    {"char",TOK_CHAR},{"file",TOK_FILE},{"p",TOK_POINT},{"lba",TOK_LBA},{"data",TOK_DATA_KW},

    {NULL,TOK_EOF}
};

void lexer_init(Lexer *l, const char *source) {
    l->src=source; l->pos=0; l->line=1; l->col=1; l->prev_was_value=0;
}
static char lpeek(Lexer *l,int off) __attribute__((unused));
static char lpeek(Lexer *l,int off){ return l->src[l->pos+off]; }
static char ladvance(Lexer *l){
    char c=l->src[l->pos++];
    if(c=='\n'){l->line++;l->col=1;}else{l->col++;}
    return c;
}
static void skip_ws_and_comments(Lexer *l){
    for(;;){

        while(l->src[l->pos]&&(l->src[l->pos]==' '||l->src[l->pos]=='\t'||
              l->src[l->pos]=='\r'||l->src[l->pos]=='\n')){
            if(l->src[l->pos]=='\n') l->prev_was_value=0;
            ladvance(l);
        }

        if(l->src[l->pos]=='/' && !l->prev_was_value &&
           (l->src[l->pos+1]==' '||l->src[l->pos+1]=='\t'||
            l->src[l->pos+1]=='\n'||l->src[l->pos+1]=='\r'||
            l->src[l->pos+1]=='\0'||isalpha((unsigned char)l->src[l->pos+1]))){

            while(l->src[l->pos]&&l->src[l->pos]!='\n') ladvance(l);
            continue;
        }
        break;
    }
}
static Token make_tok(TokenKind k,const char *val,int line,int col){
    Token t; t.kind=k; t.line=line; t.col=col;
    strncpy(t.value,val,sizeof(t.value)-1); t.value[sizeof(t.value)-1]='\0';
    return t;
}
static int is_value_tok(TokenKind k) __attribute__((unused));
static int is_value_tok(TokenKind k){
    return k==TOK_NUMBER||k==TOK_STRING||k==TOK_TRUE||k==TOK_FALSE||
           k==TOK_IDENT||k==TOK_RBRACKET||k==TOK_RPAREN||
           k==TOK_DURATION;
}

Token lexer_next(Lexer *l){
    skip_ws_and_comments(l);
    if(!l->src[l->pos]) return make_tok(TOK_EOF,"",l->line,l->col);

    int sline=l->line, scol=l->col;
    char ch=ladvance(l);

    if(ch=='/'){
        if(l->src[l->pos]=='='){
            ladvance(l); l->prev_was_value=0;
            return make_tok(TOK_SLASH_ASSIGN,"/=",sline,scol);
        }

        if(!l->prev_was_value){

            while(l->src[l->pos]&&l->src[l->pos]!='\n') ladvance(l);
            l->prev_was_value=0;
            return lexer_next(l);
        }
        l->prev_was_value=0;
        return make_tok(TOK_SLASH,"/",sline,scol);
    }

    if(ch=='-'&&l->src[l->pos]=='>'){
        ladvance(l);
        l->prev_was_value=0;
        return make_tok(TOK_ARROW,"->",sline,scol);
    }
    if(ch=='='&&l->src[l->pos]=='='){
        ladvance(l);
        l->prev_was_value=0;
        return make_tok(TOK_EQ,"==",sline,scol);
    }
    if(ch=='!'&&l->src[l->pos]=='='){
        ladvance(l);
        l->prev_was_value=0;
        return make_tok(TOK_NEQ,"!=",sline,scol);
    }
    if(ch=='<'&&l->src[l->pos]=='='){
        ladvance(l);
        l->prev_was_value=0;
        return make_tok(TOK_LTE,"<=",sline,scol);
    }
    if(ch=='>'&&l->src[l->pos]=='='){
        ladvance(l);
        l->prev_was_value=0;
        return make_tok(TOK_GTE,">=",sline,scol);
    }
    if(ch=='<'&&l->src[l->pos]=='<'){
        ladvance(l);
        l->prev_was_value=0;
        return make_tok(TOK_SHL,"<<",sline,scol);
    }
    if(ch=='>'&&l->src[l->pos]=='>'){
        ladvance(l);
        l->prev_was_value=0;
        return make_tok(TOK_SHR,">>",sline,scol);
    }
    if(ch=='&'&&l->src[l->pos]=='&'){
        ladvance(l);
        l->prev_was_value=0;
        return make_tok(TOK_AND,"and",sline,scol);
    }
    if(ch=='|'&&l->src[l->pos]=='|'){
        ladvance(l);
        l->prev_was_value=0;
        return make_tok(TOK_OR,"or",sline,scol);
    }
    if(ch=='&'){ l->prev_was_value=0; return make_tok(TOK_AMP,"&",sline,scol); }
    if(ch=='|'){ l->prev_was_value=0; return make_tok(TOK_PIPE,"|",sline,scol); }
    if(ch=='^'){ l->prev_was_value=0; return make_tok(TOK_CARET,"^",sline,scol); }
    if(ch=='~'){ l->prev_was_value=0; return make_tok(TOK_TILDE,"~",sline,scol); }
    if(ch=='#'){ l->prev_was_value=0; return make_tok(TOK_HASH,"#",sline,scol); }

    if(ch=='+'&&l->src[l->pos]=='='){ladvance(l);l->prev_was_value=0;return make_tok(TOK_PLUS_ASSIGN,"+=",sline,scol);}
    if(ch=='-'&&l->src[l->pos]=='='){ladvance(l);l->prev_was_value=0;return make_tok(TOK_MINUS_ASSIGN,"-=",sline,scol);}
    if(ch=='*'&&l->src[l->pos]=='='){ladvance(l);l->prev_was_value=0;return make_tok(TOK_STAR_ASSIGN,"*=",sline,scol);}
    if(ch=='%'&&l->src[l->pos]=='='){ladvance(l);l->prev_was_value=0;return make_tok(TOK_PERCENT_ASSIGN,"%=",sline,scol);}

    if(isdigit((unsigned char)ch)||(ch=='.'&&isdigit((unsigned char)l->src[l->pos]))){
        char buf[64]; int bi=0;
        int has_dot=(ch=='.');
        buf[bi++]=ch;
        while(l->src[l->pos]&&(isdigit((unsigned char)l->src[l->pos])||l->src[l->pos]=='.')&&bi<62){
            if(l->src[l->pos]=='.'){
                if(has_dot){
                    char msg[96];
                    snprintf(msg,sizeof(msg),"ALT0003 malformed number literal at line %d",sline);
                    fprintf(stderr,"\nALT0003\n\n%s\n\nLine: %d\n",msg,sline);
                    exit(1);
                }
                has_dot=1;
            }
            buf[bi++]=ladvance(l);
        }
        buf[bi]='\0';

        if(l->src[l->pos]=='s'||l->src[l->pos]=='m'||l->src[l->pos]=='h'){
            buf[bi++]=ladvance(l); buf[bi]='\0';
            l->prev_was_value=1;
            return make_tok(TOK_DURATION,buf,sline,scol);
        }
        l->prev_was_value=1;
        return make_tok(TOK_NUMBER,buf,sline,scol);
    }

    if(isdigit((unsigned char)ch)){

    }

    if(ch=='"'){
        char buf[512]; int bi=0;
        while(l->src[l->pos]&&l->src[l->pos]!='"'&&bi<510){
            char c=ladvance(l);
            if(c=='\\'){
                char esc=ladvance(l);
                if(esc=='n')buf[bi++]='\n';
                else if(esc=='t')buf[bi++]='\t';
                else if(esc=='\\')buf[bi++]='\\';
                else if(esc=='"')buf[bi++]='"';
                else{buf[bi++]='\\';buf[bi++]=esc;}
            } else buf[bi++]=c;
        }
        if(l->src[l->pos]!='"'){
            fprintf(stderr,"\nALT0003\n\nUnterminated string literal.\n\nLine: %d\n",sline);
            exit(1);
        }
        ladvance(l);
        buf[bi]='\0';
        l->prev_was_value=1;
        return make_tok(TOK_STRING,buf,sline,scol);
    }

    if(ch=='a'&&strncmp(l->src+l->pos-1,"altair.doc",10)==0){
        for(int i=0;i<9;i++) ladvance(l);
        l->prev_was_value=0;
        return make_tok(TOK_ALTAIR_DOC,"altair.doc",sline,scol);
    }

    if(isalpha((unsigned char)ch)||ch=='_'){
        char buf[512]; int bi=0;
        buf[bi++]=ch;
        while(l->src[l->pos]&&(isalnum((unsigned char)l->src[l->pos])||l->src[l->pos]=='_')&&bi<510)
            buf[bi++]=ladvance(l);
        buf[bi]='\0';

        if(l->src[l->pos]=='%'&&isdigit((unsigned char)buf[0])){
             }
        for(int i=0;KEYWORDS[i].word;i++){
            if(strcmp(buf,KEYWORDS[i].word)==0){
                l->prev_was_value=0;
                return make_tok(KEYWORDS[i].kind,buf,sline,scol);
            }
        }
        l->prev_was_value=1;
        return make_tok(TOK_IDENT,buf,sline,scol);
    }

    if(ch=='%'&&l->prev_was_value){
        l->prev_was_value=1;
        return make_tok(TOK_PERCENT_LIT,"%",sline,scol);
    }

    l->prev_was_value=0;
    switch(ch){
    case '+': return make_tok(TOK_PLUS,"+",sline,scol);
    case '-': return make_tok(TOK_MINUS,"-",sline,scol);
    case '*': return make_tok(TOK_STAR,"*",sline,scol);
    case '%': l->prev_was_value=1; return make_tok(TOK_PERCENT_LIT,"%",sline,scol);
    case '!': return make_tok(TOK_BANG,"!",sline,scol);
    case '>': return make_tok(TOK_GT,">",sline,scol);
    case '<': return make_tok(TOK_LT,"<",sline,scol);
    case '=': return make_tok(TOK_ASSIGN,"=",sline,scol);
    case ';': return make_tok(TOK_SEMICOLON,";",sline,scol);
    case ',': return make_tok(TOK_COMMA,",",sline,scol);
    case '.': return make_tok(TOK_DOT,".",sline,scol);
    case '[': return make_tok(TOK_LBRACKET,"[",sline,scol);
    case ']': l->prev_was_value=1; return make_tok(TOK_RBRACKET,"]",sline,scol);
    case '(': return make_tok(TOK_LPAREN,"(",sline,scol);
    case ')': l->prev_was_value=1; return make_tok(TOK_RPAREN,")",sline,scol);
    case '@': return make_tok(TOK_AT,"@",sline,scol);
    default: break;
    }
    char msg[64]; snprintf(msg,sizeof(msg),"ALT0003 unexpected char '%c' at line %d",ch,sline);
    fprintf(stderr,"%s\n",msg); exit(1);
}

Token lexer_peek(Lexer *l){
    size_t sp=l->pos; int sl=l->line,sc=l->col,pv=l->prev_was_value;
    Token t=lexer_next(l);
    l->pos=sp; l->line=sl; l->col=sc; l->prev_was_value=pv;
    return t;
}

const char *tok_kind_str(TokenKind k){
    switch(k){
    case TOK_NUMBER:return "NUMBER"; case TOK_STRING:return "STRING";
    case TOK_IDENT:return "IDENT";  case TOK_DURATION:return "DURATION";
    case TOK_ALTAIR_DOC:return "altair.doc"; case TOK_EOF:return "EOF";
    case TOK_ASSIGN:return "'='";   case TOK_SEMICOLON:return "';'";
    case TOK_LPAREN:return "'('";   case TOK_RPAREN:return "')'";
    case TOK_LBRACKET:return "'['"; case TOK_RBRACKET:return "']'";
    case TOK_COMMA:return "','";    case TOK_ARROW:return "'->'";
    case TOK_AT:return "'@'";
    case TOK_BREAK:return "break";
    case TOK_DEFINE:return "define";case TOK_CREATE:return "create";
    case TOK_RELEASE:return "release";
    case TOK_LISTEN:return "listen"; case TOK_ROUTE:return "route";
    case TOK_MIDDLEWARE:return "middleware"; case TOK_HEALTH:return "health";
    case TOK_METRICS:return "metrics"; case TOK_ON_SHUTDOWN:return "on_shutdown";
    case TOK_SESSION:return "session"; case TOK_CONFIG:return "config";
    case TOK_JOB:return "job"; case TOK_EVERY:return "every";
    case TOK_DB_POOL:return "db_pool";

    case TOK_LINK:return "link"; case TOK_WINDOW:return "window";
    case TOK_LOOP:return "loop"; case TOK_DRAW:return "draw";
    case TOK_IMAGE:return "image"; case TOK_CLEAR:return "clear";
    case TOK_COLOR_KW:return "color"; case TOK_SOUND:return "sound";
    case TOK_MUSIC:return "music"; case TOK_PLAY:return "play";
    case TOK_PAUSE_KW:return "pause"; case TOK_TIMER_KW:return "timer";
    case TOK_BUTTON:return "button"; case TOK_LABEL_KW:return "label";
    case TOK_TEXTBOX:return "textbox"; case TOK_CHECKBOX:return "checkbox";
    case TOK_SLIDER_KW:return "slider"; case TOK_PROGRESS_KW:return "progress";
    case TOK_LISTVIEW:return "listview"; case TOK_MENU_KW:return "menu";
    case TOK_DIALOG:return "dialog"; case TOK_SCENE_KW:return "scene";
    case TOK_GOTO_KW:return "goto"; case TOK_CURSOR_KW:return "cursor";
    case TOK_ANIMATE:return "animate"; case TOK_POPUP:return "popup";
    case TOK_CANVAS_GFX:return "canvas"; case TOK_COLUMN:return "column";
    case TOK_ROW:return "row"; case TOK_GRID:return "grid";
    case TOK_KEY_KW:return "key";
    default:return "TOKEN";
    }
}
