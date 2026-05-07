%option reentrant bison-bridge noyywrap
%{
typedef union { int n; int len; } YYSTYPE;
#define YYSTYPE_IS_DECLARED 1
%}
%%
[0-9]+      { yylval->n   = atoi(yytext); return 1; }
[a-z]+      { yylval->len = (int)yyleng;  return 2; }
[ \n]+      /* skip */
.           { return 3; }
%%
int main(void){
    yyscan_t s;
    YYSTYPE val;
    int t;
    yylex_init(&s);
    yy_scan_string("foo 42 hello 7", s);
    while ((t = yylex(&val, s)) != 0) {
        if (t == 1) printf("[N%d]", val.n);
        else if (t == 2) printf("[W%d]", val.len);
    }
    yylex_destroy(s);
    printf("\n");
    return 0;
}
