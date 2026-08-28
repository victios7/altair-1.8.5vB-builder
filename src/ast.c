
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ASTNode *ast_new(NodeKind kind, int line){
    ASTNode *n=(ASTNode*)calloc(1,sizeof(ASTNode));
    if(!n){ fprintf(stderr,"ast_new: out of memory\n"); exit(1); }
    n->kind=kind; n->line=line;
    return n;
}

void ast_free(ASTNode *n){
    if(!n) return;
    for(int i=0;i<n->nchildren;i++) ast_free(n->children[i]);
    ast_free(n->left);
    ast_free(n->right);
    ast_free(n->fun_body);
    ast_free(n->body);
    ast_free(n->try_body);
    ast_free(n->catch_body);
    ast_free(n->count_expr);
    ast_free(n->iter_list_expr);
    ast_free(n->idx_expr);
    ast_free(n->idx_val);
    free(n);
}
