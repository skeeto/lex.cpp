%option noyywrap
%%
.|\n        ECHO;
<<EOF>>     { printf("[end]"); yyterminate(); }
%%
int main(void){ yylex(); return 0; }
