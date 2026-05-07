%option noyywrap
%{
static const char *yy_src = "alpha beta gamma";
static int yy_src_pos = 0;
#define YY_INPUT(buf, result, max_size) do {                       \
    int yy_n_local = 0;                                             \
    while (yy_n_local < (int)(max_size) && yy_src[yy_src_pos] != 0) \
        (buf)[yy_n_local++] = yy_src[yy_src_pos++];                 \
    (result) = (size_t)yy_n_local;                                  \
} while (0)
%}
%%
[a-z]+      printf("(%s)", yytext);
.|\n        /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
