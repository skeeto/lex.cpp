%option noyywrap
%%
^foo$       printf("[FOO]");
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
