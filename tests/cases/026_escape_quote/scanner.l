%option noyywrap
%%
"\""        printf("[QUOTE]");
\"          printf("[BACKSLASH-QUOTE]");
.|\n        ECHO;
%%
int main(void){ yylex(); return 0; }
