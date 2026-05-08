%option noyywrap
%%
[[:cntrl:]]     printf("[C:%02x]", (unsigned char)yytext[0]);
[[:print:]]+    printf("[P:%s]", yytext);
.|\n            /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
