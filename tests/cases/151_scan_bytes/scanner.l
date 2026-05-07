%option noyywrap
%%
[a-z]+      printf("(%s)", yytext);
\x00        printf("<NUL>");
.|\n        ECHO;
%%
int main(void) {
    yy_scan_bytes("ab\0cd", 5);
    yylex();
    printf("\n");
    return 0;
}
