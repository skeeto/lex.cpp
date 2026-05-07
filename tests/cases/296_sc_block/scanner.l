%option noyywrap

WS  [[:space:]]+

%x in_struct

%%
^"struct"/[^a-zA-Z]    { BEGIN(in_struct); printf("[STRUCT]"); }

<in_struct>{
{WS}       { printf("[WS:%d]", yyleng); }
.          { printf("[X:%c]", yytext[0]); }
}
.|\n                  /* skip */
%%
int main(void){ yylex(); printf("\n"); return 0; }
