
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/stat.h>
#endif

#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "sema.h"
#include "codegen.h"

#ifndef ALTAIR_RT_H
#define ALTAIR_RT_H "runtime/altair_rt.h"
#endif
#ifndef ALTAIR_RT_C
#define ALTAIR_RT_C "runtime/altair_rt.c"
#endif

#ifdef _WIN32
static char s_exe_dir[MAX_PATH];
static char s_rt_h[MAX_PATH];
static char s_rt_c[MAX_PATH];
static char s_gcc[MAX_PATH];
static char s_guide[MAX_PATH];

static void resolve_windows_paths(void){
    GetModuleFileNameA(NULL, s_exe_dir, MAX_PATH);
    char *slash = strrchr(s_exe_dir, '\\');
    if(slash) *slash = '\0';

    snprintf(s_rt_h,   MAX_PATH, "%s\\runtime\\altair_rt.h", s_exe_dir);
    snprintf(s_rt_c,   MAX_PATH, "%s\\runtime\\altair_rt.c", s_exe_dir);
    snprintf(s_guide,  MAX_PATH, "%s\\ALTAIR_GUIDE.md",       s_exe_dir);

    char bundled[MAX_PATH];
    snprintf(bundled, MAX_PATH, "%s\\mingw64\\bin\\gcc.exe", s_exe_dir);
    if(GetFileAttributesA(bundled) != INVALID_FILE_ATTRIBUTES)
        snprintf(s_gcc, MAX_PATH, "%s\\mingw64\\bin\\gcc.exe", s_exe_dir);
    else
        strcpy(s_gcc, "gcc");
}
#else
static char s_rt_h[1024];
static char s_rt_c[1024];

static int file_exists(const char *p){
    struct stat st;
    return stat(p, &st) == 0;
}

static void resolve_unix_paths(const char *argv0){
    char exe_dir[900] = {0};
    int have_dir = 0;

#if defined(__linux__)
    char linkbuf[1024];
    ssize_t n = readlink("/proc/self/exe", linkbuf, sizeof(linkbuf)-1);
    if(n > 0){
        linkbuf[n] = '\0';
        char *slash = strrchr(linkbuf, '/');
        if(slash){
            int dl = (int)(slash - linkbuf);
            if(dl < (int)sizeof(exe_dir)){
                memcpy(exe_dir, linkbuf, dl);
                exe_dir[dl] = '\0';
                have_dir = 1;
            }
        }
    }
#endif
    if(!have_dir && argv0){
        char resolved[1024];
        if(realpath(argv0, resolved)){
            char *slash = strrchr(resolved, '/');
            if(slash){
                int dl = (int)(slash - resolved);
                if(dl < (int)sizeof(exe_dir)){
                    memcpy(exe_dir, resolved, dl);
                    exe_dir[dl] = '\0';
                    have_dir = 1;
                }
            }
        }
    }

    if(have_dir){
        char cand_h[1024], cand_c[1024];
        snprintf(cand_h, sizeof(cand_h), "%s/runtime/altair_rt.h", exe_dir);
        snprintf(cand_c, sizeof(cand_c), "%s/runtime/altair_rt.c", exe_dir);
        if(file_exists(cand_h) && file_exists(cand_c)){
            snprintf(s_rt_h, sizeof(s_rt_h), "%s", cand_h);
            snprintf(s_rt_c, sizeof(s_rt_c), "%s", cand_c);
            return;
        }
        snprintf(cand_h, sizeof(cand_h), "%s/../share/altair/runtime/altair_rt.h", exe_dir);
        snprintf(cand_c, sizeof(cand_c), "%s/../share/altair/runtime/altair_rt.c", exe_dir);
        if(file_exists(cand_h) && file_exists(cand_c)){
            snprintf(s_rt_h, sizeof(s_rt_h), "%s", cand_h);
            snprintf(s_rt_c, sizeof(s_rt_c), "%s", cand_c);
            return;
        }
    }

    if(file_exists("/usr/share/altair/runtime/altair_rt.h") &&
       file_exists("/usr/share/altair/runtime/altair_rt.c")){
        snprintf(s_rt_h, sizeof(s_rt_h), "/usr/share/altair/runtime/altair_rt.h");
        snprintf(s_rt_c, sizeof(s_rt_c), "/usr/share/altair/runtime/altair_rt.c");
        return;
    }

    if(file_exists("runtime/altair_rt.h") && file_exists("runtime/altair_rt.c")){
        snprintf(s_rt_h, sizeof(s_rt_h), "runtime/altair_rt.h");
        snprintf(s_rt_c, sizeof(s_rt_c), "runtime/altair_rt.c");
        return;
    }

    snprintf(s_rt_h, sizeof(s_rt_h), "%s", ALTAIR_RT_H);
    snprintf(s_rt_c, sizeof(s_rt_c), "%s", ALTAIR_RT_C);
}
#endif

static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");
    if(!f){ fprintf(stderr,"altairc: cannot open '%s'\n",path); exit(1); }
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *buf=(char*)malloc(sz+1);
    if(!buf){ fclose(f); fprintf(stderr,"altairc: out of memory\n"); exit(1); }
    size_t rd=fread(buf,1,sz,f); buf[rd]='\0'; fclose(f);
    return buf;
}

static void print_usage(void){
    fprintf(stderr,
        "Altair Compiler v1.8.5\n\n"
        "Usage:\n"
        "  altairc <source.at> [options]\n"
        "  altairc guide              Write ALTAIR_GUIDE.md to current directory\n"
        "  altairc guide --stdout     Print guide to stdout\n\n"
        "Options:\n"
        "  -o <output>     Output binary name (default: a.out / a.exe)\n"
        "  -icon <file.ico> Embed a .ico icon into the output .exe (Windows only)\n"
        "  --emit-c        Print generated C source to stdout\n"
        "  --emit-ast      Print AST node count\n"
        "  --no-sema       Skip semantic analysis\n"
        "  -v, --version   Print compiler version\n"
        "  -h, --help      Show this help\n\n"
        "Examples:\n"
        "  altairc hello.at -o hello\n"
        "  altairc server.at -o server\n"
        "  altairc app.at -o app -icon app.ico\n"
        "  altairc guide\n"
    );
}

static void print_ast(ASTNode *n, int depth) __attribute__((unused));
static void print_ast(ASTNode *n, int depth){
    if(!n||depth>20) return;
    for(int i=0;i<depth;i++) printf("  ");
    printf("[%d] kind=%d", n->line, (int)n->kind);
    if(n->var_name[0]) printf(" var=%s",n->var_name);
    if(n->fun_name[0]) printf(" fun=%s",n->fun_name);
    if(n->class_name[0]) printf(" class=%s",n->class_name);
    if(n->str_val[0]&&n->kind==ND_IDENT) printf(" ident=%s",n->str_val);
    if(n->kind==ND_NUMBER) printf(" num=%g",n->num_val);
    printf("\n");
    for(int i=0;i<n->nchildren;i++) print_ast(n->children[i],depth+1);
    print_ast(n->left,depth+1);
    print_ast(n->right,depth+1);
    print_ast(n->fun_body,depth+1);
    print_ast(n->body,depth+1);
    print_ast(n->try_body,depth+1);
    print_ast(n->catch_body,depth+1);
}

static void cmd_guide(int to_stdout, const char *guide_search_path){

    const char *paths_to_try[] = {
        guide_search_path,
        "ALTAIR_GUIDE.md",
        "../ALTAIR_GUIDE.md",
        NULL
    };

    FILE *gf = NULL;
    for(int i=0; paths_to_try[i]; i++){
        if(!paths_to_try[i]) continue;
        gf = fopen(paths_to_try[i], "r");
        if(gf) break;
    }

    if(!gf){
        fprintf(stderr,"[altairc] ALTAIR_GUIDE.md not found next to the binary.\n");
        fprintf(stderr,"          Run: altairc guide  from the altair install directory.\n");
        exit(1);
    }

    if(to_stdout){
        char buf[4096];
        while(fgets(buf,sizeof(buf),gf)) fputs(buf,stdout);
        fclose(gf);
    } else {

        FILE *out = fopen("ALTAIR_GUIDE.md","w");
        if(!out){
            fprintf(stderr,"[altairc] Cannot create ALTAIR_GUIDE.md in current directory.\n");
            fclose(gf);
            exit(1);
        }
        char buf[4096];
        while(fgets(buf,sizeof(buf),gf)) fputs(buf,out);
        fclose(gf); fclose(out);
        fprintf(stderr,"[altairc] Guide written to: ALTAIR_GUIDE.md\n");
    }
}

int main(int argc, char **argv){
    if(argc<2){ print_usage(); return 1; }

#ifdef _WIN32
    resolve_windows_paths();
    const char *rt_h    = s_rt_h;
    const char *rt_c    = s_rt_c;
    const char *gcc_bin = s_gcc;
    const char *guide_path = s_guide;
#else
    resolve_unix_paths(argv[0]);
    const char *rt_h    = s_rt_h;
    const char *rt_c    = s_rt_c;
    const char *gcc_bin = "gcc";

    char guide_path[1024] = "ALTAIR_GUIDE.md";

    {
        char *slash = strrchr(argv[0],'/');
        if(slash){
            int dl = (int)(slash-argv[0]+1);
            if(dl < 900){
                char tmp[1024];
                memcpy(tmp,argv[0],dl);
                snprintf(tmp+dl, sizeof(tmp)-dl, "ALTAIR_GUIDE.md");
                snprintf(guide_path, sizeof(guide_path), "%s", tmp);
            }
        }
    }
    const char *guide_search_path = guide_path;
#endif

    if(strcmp(argv[1],"guide")==0){
        int to_stdout = (argc>=3 && strcmp(argv[2],"--stdout")==0);
#ifdef _WIN32
        cmd_guide(to_stdout, guide_path);
#else
        cmd_guide(to_stdout, guide_search_path);
#endif
        return 0;
    }

    if(strcmp(argv[1],"-v")==0||strcmp(argv[1],"--version")==0){
        printf("altairc 1.8.5\n"); return 0;
    }
    if(strcmp(argv[1],"-h")==0||strcmp(argv[1],"--help")==0){
        print_usage(); return 0;
    }

    const char *source_file = NULL;
    const char *output_file =
#ifdef _WIN32
        "a.exe";
#else
        "a.out";
#endif
    int emit_c=0, emit_ast=0, skip_sema=0;
    const char *icon_file = NULL;

    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--emit-c")==0)    { emit_c=1; continue; }
        if(strcmp(argv[i],"--emit-ast")==0)   { emit_ast=1; continue; }
        if(strcmp(argv[i],"--no-sema")==0)    { skip_sema=1; continue; }
        if(strcmp(argv[i],"-o")==0&&i+1<argc) { output_file=argv[++i]; continue; }
        if(strcmp(argv[i],"-icon")==0&&i+1<argc) { icon_file=argv[++i]; continue; }
        if(argv[i][0]!='-') { source_file=argv[i]; continue; }
    }

    if(!source_file){ print_usage(); return 1; }

    char *source=read_file(source_file);

    ASTNode *program=parse_program(source);
    free(source);
    if(!program){ fprintf(stderr,"altairc: parse failed\n"); return 1; }

    if(!skip_sema) sema_check(program);

    if(emit_ast){
        int count=0;

        printf("AST parsed (top-level nodes counted)\n");
        for(int i=0;i<program->nchildren;i++) count++;
        printf("Top-level children: %d\n",count);
        if(program->nchildren>1){
            ASTNode *body=program->children[program->nchildren-1];
            printf("Body statements: %d\n",body?body->nchildren:0);
        }
        ast_free(program);
        return 0;
    }

    if(emit_c){
        codegen_emit(program, stdout, rt_h, rt_c, source_file);
        ast_free(program);
        return 0;
    }

    int uses_raylib = 0;
    {
        ASTNode *body = NULL;
        for(int i=0;i<program->nchildren;i++){
            if(program->children[i] && program->children[i]->kind==ND_BLOCK)
                body = program->children[i];
        }
        if(body){
            for(int i=0;i<body->nchildren;i++){
                ASTNode *s = body->children[i];
                if(s && s->kind==ND_LINK && strcmp(s->gfx_link_lib,"raylib")==0)
                    uses_raylib = 1;
            }
        }
    }

    char tmp_c[512];
#ifdef _WIN32
    const char *tmpdir = getenv("TEMP");
    if(!tmpdir) tmpdir = getenv("TMP");
    if(!tmpdir) tmpdir = s_exe_dir;
    snprintf(tmp_c,sizeof(tmp_c),"%s\\altair_%lu_gen.c",
             tmpdir, (unsigned long)GetCurrentProcessId());
#else
    snprintf(tmp_c,sizeof(tmp_c),"/tmp/altair_%d_gen.c",(int)getpid());
#endif

    FILE *fp=fopen(tmp_c,"w");
    if(!fp){ fprintf(stderr,"altairc: cannot create temp file %s\n",tmp_c); return 1; }
    codegen_emit(program, fp, rt_h, rt_c, source_file);
    fclose(fp);
    ast_free(program);

    fprintf(stderr,"[altairc] Compiling %s ...\n", source_file);

    int ret;
#ifdef _WIN32
    char res_obj[512] = "";
    if(icon_file){
        if(GetFileAttributesA(icon_file) == INVALID_FILE_ATTRIBUTES){
            fprintf(stderr,"[altairc] warning: icon file not found: %s (building without icon)\n", icon_file);
            icon_file = NULL;
        }
    }
    if(icon_file){
        char rc_path[512], windres_bin[MAX_PATH];
        const char *tmpdir = getenv("TEMP"); if(!tmpdir) tmpdir = getenv("TMP"); if(!tmpdir) tmpdir = s_exe_dir;
        snprintf(rc_path, sizeof(rc_path), "%s\\altair_%lu_res.rc", tmpdir, (unsigned long)GetCurrentProcessId());
        snprintf(res_obj, sizeof(res_obj), "%s\\altair_%lu_res.o",  tmpdir, (unsigned long)GetCurrentProcessId());

        FILE *rf = fopen(rc_path, "w");
        if(rf){
            fprintf(rf, "IDI_ICON1 ICON \"%s\"\n", icon_file);
            fclose(rf);

            snprintf(windres_bin, MAX_PATH, "%s\\mingw64\\bin\\windres.exe", s_exe_dir);
            if(GetFileAttributesA(windres_bin) == INVALID_FILE_ATTRIBUTES)
                strcpy(windres_bin, "windres");

            char rc_cmd[2048];
            snprintf(rc_cmd, sizeof(rc_cmd), "cmd /c \"\"%s\" \"%s\" -O coff -o \"%s\"\"",
                     windres_bin, rc_path, res_obj);
            if(system(rc_cmd) != 0){
                fprintf(stderr,"[altairc] warning: windres failed, building without icon.\n");
                res_obj[0] = '\0';
            }
        }
    }

    char gcc_cmd[2048];
    snprintf(gcc_cmd, sizeof(gcc_cmd),
             "cmd /c \"\"%s\" -O3 -flto -fomit-frame-pointer -o \"%s\" \"%s\" %s%s -lm -lws2_32%s\"",
             gcc_bin, output_file, tmp_c,
             res_obj[0] ? res_obj : "",
             uses_raylib ? " -I\"libs\\raylib\\windows\\include\" -L\"libs\\raylib\\windows\\lib\"" : "",
             uses_raylib ? " -lraylib -lglfw3 -lopengl32 -lgdi32 -lwinmm" : "");
    ret = system(gcc_cmd);

    if(res_obj[0]) remove(res_obj);
#else
    char gcc_cmd[2048];
    if(uses_raylib){
#if defined(__APPLE__)
        snprintf(gcc_cmd, sizeof(gcc_cmd),
                 "%s -O3 -flto -fomit-frame-pointer -o \"%s\" \"%s\" -I\"libs/raylib/macos/include\" -L\"libs/raylib/macos/lib\""
                 " -lm -lpthread -lraylib"
                 " -framework CoreVideo -framework IOKit -framework Cocoa"
                 " -framework GLUT -framework OpenGL 2>&1",
                 gcc_bin, output_file, tmp_c);
#else
        snprintf(gcc_cmd, sizeof(gcc_cmd),
                 "%s -O3 -flto -fomit-frame-pointer -o \"%s\" \"%s\" -I\"libs/raylib/linux/include\" -L\"libs/raylib/linux/lib\""
                 " -lm -lpthread -lraylib -lX11 -lXrandr -lXinerama -lXi -lXcursor -lGL -ldl -lrt 2>&1",
                 gcc_bin, output_file, tmp_c);
#endif
    } else {
        snprintf(gcc_cmd, sizeof(gcc_cmd),
                 "%s -O3 -flto -fomit-frame-pointer -o \"%s\" \"%s\" -lm -lpthread 2>&1",
                 gcc_bin, output_file, tmp_c);
    }
    ret = system(gcc_cmd);
    (void)icon_file;
#endif

    remove(tmp_c);

    if(ret!=0){

        fprintf(stderr,"\n[altairc] Compilation failed (gcc exit %d).\n",ret);
        fprintf(stderr,"  Hint: run  altairc %s --emit-c | head -100  to inspect the generated C.\n", source_file);
        fprintf(stderr,"  Common causes: using a variable before declaring it, or type mismatch.\n");
        return 1;
    }

    fprintf(stderr,"[altairc] Done. Output: %s\n", output_file);
    return 0;
}
