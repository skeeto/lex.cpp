%option noyywrap
%%
foo$        { printf("[FOOEOL]"); REJECT; }
foo         { printf("[FOO]"); }
.|\n        printf("[%c]", yytext[0]);
%%
int main(void){ yylex(); return 0; }
