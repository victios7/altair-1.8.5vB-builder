#ifndef ALTAIR_CODEGEN_H
#define ALTAIR_CODEGEN_H
#include "ast.h"
#include <stdio.h>
void codegen_emit(ASTNode *program, FILE *fp,
                  const char *runtime_h_path,
                  const char *runtime_c_path,
                  const char *source_file);
#endif
