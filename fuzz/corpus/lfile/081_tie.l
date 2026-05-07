%option noyywrap
%%
"if"        printf("[FIRST]");
"if"        printf("[SECOND]");
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
