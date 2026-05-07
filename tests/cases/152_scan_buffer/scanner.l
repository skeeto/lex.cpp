%option noyywrap
%%
[a-z]+      printf("(%s)", yytext);
.|\n        /* skip */
%%
int main(void) {
    /* yy_scan_buffer requires two trailing NULs in the input. */
    char buf[14] = {'h','e','l','l','o',' ','w','o','r','l','d',0,0,0};
    if (yy_scan_buffer(buf, 13) == 0) {
        printf("scan_buffer FAILED\n");
        return 1;
    }
    yylex();
    printf("\n");
    return 0;
}
