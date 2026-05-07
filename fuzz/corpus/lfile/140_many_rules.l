%option noyywrap
%%
a    printf("[a]");
b    printf("[b]");
c    printf("[c]");
d    printf("[d]");
e    printf("[e]");
f    printf("[f]");
g    printf("[g]");
h    printf("[h]");
i    printf("[i]");
j    printf("[j]");
k    printf("[k]");
l    printf("[l]");
m    printf("[m]");
n    printf("[n]");
o    printf("[o]");
p    printf("[p]");
q    printf("[q]");
r    printf("[r]");
s    printf("[s]");
t    printf("[t]");
u    printf("[u]");
v    printf("[v]");
w    printf("[w]");
x    printf("[x]");
y    printf("[y]");
z    printf("[z]");
.|\\n        ECHO;
%%
int main(void){ yylex(); return 0; }
