%option noyywrap
%%
foo$        printf("[E:foo]");
foo         printf("(foo)");
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
