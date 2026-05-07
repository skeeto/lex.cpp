%option noyywrap
%%
[\x00-\xff]    printf("[%02x]", (unsigned char)yytext[0]);
%%
int main(void){ yylex(); printf("\n"); return 0; }
