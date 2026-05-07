%option noyywrap
%%
[a-z]+      printf("(%s)", yytext);
[ \t\n]+    /* skip */
.           printf("[%c]", *yytext);
%%
int main(void) {
    yy_scan_string("hello world 42");
    yylex();
    printf("\n");
    return 0;
}
