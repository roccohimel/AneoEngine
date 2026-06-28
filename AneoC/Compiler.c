#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <elf.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define VERSION "AneoC independent 2.7"
#define ARR_GROW(a, n, cap, type)                                  \
	do                                                         \
	{                                                          \
		if((n) >= (cap))                                   \
		{                                                  \
			(cap) = (cap) ? (cap) * 2 : 16;            \
			(a) = xrealloc((a), (cap) * sizeof(type)); \
		}                                                  \
	} while(0)

typedef struct
{
	char *s;
	size_t n;
	size_t cap;
} Buf;

//init functions
static void fatal(const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "\033[37;41mERROR:\033[0m Compilation failed: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(0);
}

static void *xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);
	if(!p)
		fatal("out of memory");
	return p;
}

static void *xcalloc(size_t n, size_t z)
{
	void *p = calloc(n ? n : 1, z ? z : 1);
	if(!p)
		fatal("out of memory");
	return p;
}

static void *xrealloc(void *p, size_t n)
{
	p = realloc(p, n ? n : 1);
	if(!p)
		fatal("out of memory");
	return p;
}

static char *xstrdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = xmalloc(n);
	memcpy(p, s, n);
	return p;
}

static char *xstrndup2(const char *s, size_t n)
{
	char *p = xmalloc(n + 1);
	memcpy(p, s, n);
	p[n] = 0;
	return p;
}

static void bneed(Buf *b, size_t extra)
{
	size_t need = b->n + extra + 1;
	if(need <= b->cap)
		return;
	if(!b->cap)
		b->cap = 4096;
	while(b->cap < need)
		b->cap *= 2;
	b->s = xrealloc(b->s, b->cap);
}

static void bputn(Buf *b, const char *s, size_t n)
{
	bneed(b, n);
	memcpy(b->s + b->n, s, n);
	b->n += n;
	b->s[b->n] = 0;
}

static void bputs(Buf *b, const char *s)
{
	bputn(b, s, strlen(s));
}

static void bprintf(Buf *b, const char *fmt, ...)
{
	va_list ap, aq;
	int n;
	va_start(ap, fmt);
	va_copy(aq, ap);
	n = vsnprintf(NULL, 0, fmt, aq);
	va_end(aq);
	if(n < 0)
		fatal("formatting failed");
	bneed(b, (size_t)n);
	vsnprintf(b->s + b->n, b->cap - b->n, fmt, ap);
	va_end(ap);
	b->n += (size_t)n;
}

static char *read_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	Buf b = {0};
	char tmp[8192];
	size_t n;
	if(!f)
		fatal("cannot open %s: %s", path, strerror(errno));
	while((n = fread(tmp, 1, sizeof(tmp), f)) != 0)
		bputn(&b, tmp, n);
	if(ferror(f))
		fatal("cannot read %s", path);
	fclose(f);
	if(!b.s)
		b.s = xstrdup("");
	return b.s;
}

static void write_file(const char *path, const char *s, size_t n)
{
	FILE *f = fopen(path, "wb");
	if(!f)
		fatal("cannot create %s: %s", path, strerror(errno));
	if(n && fwrite(s, 1, n, f) != n)
		fatal("cannot write %s", path);
	if(fclose(f))
		fatal("cannot close %s", path);
}

typedef struct
{
	char *name;
	char *value;
} Macro;

static Macro *find_macro(Macro *macros, size_t nmacros, const char *name, size_t length)
{
	size_t i;

	for(i = 0; i < nmacros; i++)
		if(strlen(macros[i].name) == length &&
		   !memcmp(macros[i].name, name, length))
			return &macros[i];
	return NULL;
}

static char *preprocess_source(const char *text)
{
	Macro *macros = NULL;
	size_t nmacros = 0, capmacros = 0;
	Buf body = {0};
	Buf out = {0};
	const char *p = text;
	size_t i, n;
	enum
	{
		PP_NORMAL,
		PP_STRING,
		PP_CHAR,
		PP_LINE_COMMENT,
		PP_BLOCK_COMMENT
	} state = PP_NORMAL;
	bool escaped = false;

	while(*p)
	{
		const char *line = p;
		const char *end = strchr(p, '\n');
		const char *q;

		if(!end)
			end = p + strlen(p);
		q = line;
		while(q < end && (*q == ' ' || *q == '\t' || *q == '\r'))
			q++;
		if(q < end && *q == '#')
		{
			q++;
			while(q < end && isspace((unsigned char)*q))
				q++;
			if(end - q >= 6 && !memcmp(q, "define", 6) &&
			   (q + 6 == end || isspace((unsigned char)q[6])))
			{
				const char *name;
				const char *value;
				const char *value_end;
				Macro *old;

				q += 6;
				while(q < end && isspace((unsigned char)*q))
					q++;
				name = q;
				if(q < end && (isalpha((unsigned char)*q) || *q == '_'))
				{
					q++;
					while(q < end && (isalnum((unsigned char)*q) || *q == '_'))
						q++;
					if(q < end && *q != '(')
					{
						value = q;
						while(value < end && isspace((unsigned char)*value))
							value++;
						value_end = end;
						while(value_end > value &&
						      isspace((unsigned char)value_end[-1]))
							value_end--;
						old = find_macro(macros, nmacros, name, (size_t)(q - name));
						if(!old)
						{
							ARR_GROW(macros, nmacros, capmacros, Macro);
							old = &macros[nmacros++];
							old->name = xstrndup2(name, (size_t)(q - name));
						} else {
							free(old->value);
						}
						old->value = xstrndup2(value,
								       (size_t)(value_end - value));
					}
				}
			}
			bputn(&body, "\n", 1);
		} else {
			bputn(&body, line, (size_t)(end - line));
			if(*end == '\n')
				bputn(&body, "\n", 1);
		}
		p = *end ? end + 1 : end;
	}
	if(!body.s)
		body.s = xstrdup("");
	n = body.n;
	for(i = 0; i < n;)
	{
		char c = body.s[i];
		char next = i + 1 < n ? body.s[i + 1] : 0;

		if(state == PP_NORMAL)
		{
			if(c == '/' && next == '/')
			{
				bputn(&out, "//", 2);
				i += 2;
				state = PP_LINE_COMMENT;
				continue;
			}
			if(c == '/' && next == '*')
			{
				bputn(&out, "/*", 2);
				i += 2;
				state = PP_BLOCK_COMMENT;
				continue;
			}
			if(c == '"' || c == '\'')
			{
				bputn(&out, &c, 1);
				i++;
				state = c == '"' ? PP_STRING : PP_CHAR;
				escaped = false;
				continue;
			}
			if(isalpha((unsigned char)c) || c == '_')
			{
				size_t start = i;
				Macro *macro;

				i++;
				while(i < n && (isalnum((unsigned char)body.s[i]) ||
						body.s[i] == '_'))
					i++;
				macro = find_macro(macros, nmacros, body.s + start, i - start);
				if(macro)
				{
					bputn(&out, "(", 1);
					bputs(&out, macro->value);
					bputn(&out, ")", 1);
				} else {
					bputn(&out, body.s + start, i - start);
				}
				continue;
			}
			bputn(&out, &c, 1);
			i++;
			continue;
		}
		bputn(&out, &c, 1);
		i++;
		if(state == PP_LINE_COMMENT)
		{
			if(c == '\n')
				state = PP_NORMAL;
		} else if(state == PP_BLOCK_COMMENT)
		{
			if(c == '*' && next == '/')
			{
				bputn(&out, "/", 1);
				i++;
				state = PP_NORMAL;
			}
		} else if(escaped)
		{
			escaped = false;
		} else if(c == '\\')
		{
			escaped = true;
		} else if((state == PP_STRING && c == '"') ||
			  (state == PP_CHAR && c == '\''))
		{
			state = PP_NORMAL;
		}
	}
	if(!out.s)
		out.s = xstrdup("");
	return out.s;
}

*/

//declaration types
typedef enum
{
	TY_VOID,
	TY_CHAR,
	TY_SHORT,
	TY_INT,
	TY_U8,
	TY_U16,
	TY_U32,
	TY_U64,
	TY_DOUBLE,
	TY_PTR,
	TY_ARRAY,
	TY_STRUCT,
	TY_XEVENT,
	TY_VALIST,
	TY_OPAQUE
} TypeKind;

typedef struct CType CType;
typedef struct StructMember StructMember;

struct StructMember
{
	char *name;
	CType *type;
	long offset;
};

struct CType
{
	TypeKind kind;
	CType *base;
	long count;
	const char *name;
	StructMember *members;
	size_t nmembers;
	size_t capmembers;
	long size;
	long align;
	bool packed;
};

static CType T_VOID = {.kind = TY_VOID, .name = "void"};
static CType T_CHAR = {.kind = TY_CHAR, .name = "char"};
static CType T_SHORT = {.kind = TY_SHORT, .name = "short"};
static CType T_INT = {.kind = TY_INT, .name = "int"};
static CType T_U8 = {.kind = TY_U8, .name = "unsigned char"};
static CType T_U16 = {.kind = TY_U16, .name = "unsigned short"};
static CType T_U32 = {.kind = TY_U32, .name = "unsigned int"};
static CType T_U64 = {.kind = TY_U64, .name = "unsigned long long"};
static CType T_DOUBLE = {.kind = TY_DOUBLE, .name = "double"};
static CType T_XEVENT = {.kind = TY_XEVENT, .name = "XEvent"};
static CType T_VALIST = {.kind = TY_VALIST, .name = "va_list"};
static CType T_DISPLAY = {.kind = TY_OPAQUE, .name = "Display"};
static CType T_FILE = {.kind = TY_OPAQUE, .name = "FILE"};

static CType *new_type(TypeKind k, CType *base, long count, const char *name)
{
	CType *t = xcalloc(1, sizeof(*t));
	t->kind = k;
	t->base = base;
	t->count = count;
	t->name = name;
	return t;
}

static CType *ptr_to(CType *base)
{
	return new_type(TY_PTR, base, 0, NULL);
}
static CType *array_of(CType *base, long n)
{
	return new_type(TY_ARRAY, base, n, NULL);
}
static CType *vla_of(CType *base, const char *bound)
{
	return new_type(TY_ARRAY, base, -1, xstrdup(bound));
}

static bool is_vla(CType *type)
{
	return type && type->kind == TY_ARRAY && type->count < 0;
}

static long type_size(CType *t)
{
	switch(t->kind)
	{
	case TY_VOID:
		return 0;
	case TY_CHAR:
	case TY_U8:
		return 1;
	case TY_SHORT:
	case TY_U16:
		return 2;
	case TY_INT:
	case TY_U32:
		return 4;
	case TY_U64:
	case TY_DOUBLE:
	case TY_PTR:
	case TY_OPAQUE:
		return 8;
	case TY_STRUCT:
		return t->size;
	case TY_ARRAY:
		if(is_vla(t))
			return 8;
		return type_size(t->base) * t->count;
	case TY_XEVENT:
		return 192;
	case TY_VALIST:
		return 24;
	}
	fatal("internal: unknown type");
	return 0;
}

static long type_align(CType *t)
{
	if(t->kind == TY_CHAR || t->kind == TY_U8)
		return 1;
	if(t->kind == TY_SHORT || t->kind == TY_U16)
		return 2;
	if(t->kind == TY_INT || t->kind == TY_U32)
		return 4;
	if(t->kind == TY_ARRAY)
		return type_align(t->base);
	if(t->kind == TY_STRUCT)
		return t->align ? t->align : 1;
	return 8;
}

/* ---------- Lexer ---------- */

typedef enum
{
	TK_ID,
	TK_NUM,
	TK_STR,
	TK_CHAR,
	TK_OP,
	TK_EOF
} TokKind;

typedef struct
{
	TokKind kind;
	char *v;
	int line;
	int col;
} Token;

typedef struct
{
	Token *a;
	size_t n;
	size_t cap;
	const char *file;
} Tokens;

static void tok_push(Tokens *ts, TokKind k, const char *s, size_t n, int line, int col)
{
	ARR_GROW(ts->a, ts->n, ts->cap, Token);
	ts->a[ts->n].kind = k;
	ts->a[ts->n].v = xstrndup2(s, n);
	ts->a[ts->n].line = line;
	ts->a[ts->n].col = col;
	ts->n++;
}

static bool starts(const char *s, size_t i, const char *x)
{
	return !strncmp(s + i, x, strlen(x));
}

static Tokens lex_source(const char *text, const char *file)
{
	Tokens ts = {0};
	size_t i = 0, n = strlen(text);
	int line = 1, col = 1;
	bool bol = true;

	while(i < n)
	{
		char c = text[i];

		if(bol)
		{
			size_t j = i;
			int ccol = col;
			while(j < n && (text[j] == ' ' || text[j] == '\t' || text[j] == '\r'))
			{
				j++;
				ccol++;
			}
			if(j < n && text[j] == '#')
			{
				bool cont;
				do
				{
					cont = false;
					while(i < n && text[i] != '\n')
					{
						if(text[i] == '\\')
							cont = true;
						else if(text[i] != '\r' && !isspace((unsigned char)text[i]))
							cont = false;
						i++;
						col++;
					}
					if(i < n && text[i] == '\n')
					{
						i++;
						line++;
						col = 1;
						bol = true;
					}
				} while(cont && i < n);
				continue;
			}
		}

		if(isspace((unsigned char)c))
		{
			if(c == '\n')
			{
				line++;
				col = 1;
				bol = true;
			} else
				col++;
			i++;
			continue;
		}
		bol = false;

		if(i + 1 < n && text[i] == '/' && text[i + 1] == '/')
		{
			i += 2;
			col += 2;
			while(i < n && text[i] != '\n')
			{
				i++;
				col++;
			}
			continue;
		}
		if(i + 1 < n && text[i] == '/' && text[i + 1] == '*')
		{
			i += 2;
			col += 2;
			while(i + 1 < n && !(text[i] == '*' && text[i + 1] == '/'))
			{
				if(text[i] == '\n')
				{
					line++;
					col = 1;
					bol = true;
					i++;
				} else {
					i++;
					col++;
				}
			}
			if(i + 1 >= n)
				fatal("%s:%d:%d: unterminated comment", file, line, col);
			i += 2;
			col += 2;
			continue;
		}

		{
			int sl = line, sc = col;
			size_t start = i;
			if(isalpha((unsigned char)c) || c == '_')
			{
				i++;
				col++;
				while(i < n && (isalnum((unsigned char)text[i]) || text[i] == '_'))
				{
					i++;
					col++;
				}
				tok_push(&ts, TK_ID, text + start, i - start, sl, sc);
				continue;
			}
			if(isdigit((unsigned char)c))
			{
				bool floating = false;

				i++;
				col++;
				if(c == '0' && i < n && (text[i] == 'x' || text[i] == 'X'))
				{
					i++;
					col++;
					while(i < n && isxdigit((unsigned char)text[i]))
					{
						i++;
						col++;
					}
				} else {
					while(i < n && isdigit((unsigned char)text[i]))
					{
						i++;
						col++;
					}
					if(i < n && text[i] == '.')
					{
						floating = true;
						i++;
						col++;
						while(i < n && isdigit((unsigned char)text[i]))
						{
							i++;
							col++;
						}
					}
					if(i < n && (text[i] == 'e' || text[i] == 'E'))
					{
						floating = true;
						i++;
						col++;
						if(i < n && (text[i] == '+' || text[i] == '-'))
						{
							i++;
							col++;
						}
						while(i < n && isdigit((unsigned char)text[i]))
						{
							i++;
							col++;
						}
					}
				}
				if(floating)
				{
					if(i < n && (text[i] == 'f' || text[i] == 'F' || text[i] == 'l' || text[i] == 'L'))
					{
						i++;
						col++;
					}
				} else {
					while(i < n && strchr("uUlL", text[i]))
					{
						i++;
						col++;
					}
				}
				tok_push(&ts, TK_NUM, text + start, i - start, sl, sc);
				continue;
			}
			if(c == '"' || c == '\'')
			{
				char q = c;
				bool esc = false;
				i++;
				col++;
				while(i < n)
				{
					char d = text[i++];
					col++;
					if(d == '\n')
					{
						line++;
						col = 1;
					}
					if(esc)
						esc = false;
					else if(d == '\\')
						esc = true;
					else if(d == q)
						break;
				}
				if(text[i - 1] != q)
					fatal("%s:%d:%d: unterminated literal", file, sl, sc);
				tok_push(&ts, q == '"' ? TK_STR : TK_CHAR, text + start, i - start, sl, sc);
				continue;
			}
			{
				static const char *ops[] = {
					"<<=", ">>=", "...", "==", "!=", "<=", ">=", "&&", "||", "++", "--", "->", "<<", ">>", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", NULL};
				int k;
				for(k = 0; ops[k]; k++)
				{
					size_t z = strlen(ops[k]);
					if(i + z <= n && starts(text, i, ops[k]))
					{
						tok_push(&ts, TK_OP, text + i, z, sl, sc);
						i += z;
						col += (int)z;
						goto token_done;
					}
				}
			}
			if(strchr("{}[]();,.*&+-/%!~<>=|^?:", c))
			{
				tok_push(&ts, TK_OP, text + i, 1, sl, sc);
				i++;
				col++;
				continue;
			}
			fatal("%s:%d:%d: unexpected character '%c'", file, line, col, c);
		}
	token_done:;
	}
	tok_push(&ts, TK_EOF, "<eof>", 5, line, col);
	ts.file = file;
	return ts;
}

/* ---------- AST ---------- */

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Decl Decl;

typedef struct
{
	char *name;
	CType *type;
} Param;
typedef struct
{
	char *name;
	CType *type;
} TypeAlias;
typedef struct
{
	char *name;
	CType *type;
} StructTag;

typedef struct
{
	char *name;
	CType *type;
	Param *params;
	size_t nparams, capparams;
	bool variadic, function;
} Declarator;

typedef enum
{
	EX_NUM,
	EX_STR,
	EX_ID,
	EX_UNARY,
	EX_BINARY,
	EX_CALL,
	EX_INDEX,
	EX_MEMBER,
	EX_PTRMEMBER,
	EX_SIZEOF,
	EX_INITLIST
} ExprKind;
struct Expr
{
	ExprKind kind;
	char *op;
	unsigned long long num;
	double fnum;
	char *str;
	Expr *left, *right;
	Expr **args;
	size_t nargs, capargs;
	CType *type;
	CType *sizeof_type;
};

typedef enum
{
	ST_BLOCK,
	ST_DECL,
	ST_EXPR,
	ST_IF,
	ST_WHILE,
	ST_FOR,
	ST_SWITCH,
	ST_CASE,
	ST_DEFAULT,
	ST_RETURN,
	ST_BREAK,
	ST_CONTINUE,
	ST_ASM,
	ST_EMPTY
} StmtKind;
struct Stmt
{
	StmtKind kind;
	Decl *decl;
	Expr *expr;
	Expr *cond;
	Expr *post;
	Stmt *init;
	Stmt *yes, *no, *body;
	char *label;
	char *asm_text;
	char **asm_constraints;
	Expr **asm_outputs;
	size_t nasm_outputs, capasm_constraints, capasm_outputs;
	Stmt **children;
	size_t nchildren, capchildren;
};

struct Decl
{
	char *name;
	CType *type;
	Expr *init;
	Param *params;
	size_t nparams, capparams;
	bool variadic;
	bool prototype;
	bool is_extern;
	bool is_static;
	bool is_inline;
	Stmt *body;
};

typedef struct
{
	Decl **a;
	size_t n, cap;
	TypeAlias *aliases;
	size_t naliases, capaliases;
	StructTag *tags;
	size_t ntags, captags;
} Program;

static Expr *new_expr(ExprKind k)
{
	Expr *e = xcalloc(1, sizeof(*e));
	e->kind = k;
	return e;
}
static Stmt *new_stmt(StmtKind k)
{
	Stmt *s = xcalloc(1, sizeof(*s));
	s->kind = k;
	return s;
}
static Decl *new_decl(void)
{
	return xcalloc(1, sizeof(Decl));
}

/* ---------- Parser ---------- */

typedef struct
{
	Tokens *ts;
	size_t p;
	Program *prog;
} Parser;

static Token *ptok(Parser *p)
{
	return &p->ts->a[p->p];
}
static bool peq(Parser *p, const char *s)
{
	return !strcmp(ptok(p)->v, s);
}
static bool paccept(Parser *p, const char *s)
{
	if(peq(p, s))
	{
		p->p++;
		return true;
	}
	return false;
}
static void perr(Parser *p, const char *fmt, ...)
{
	va_list ap;
	Token *t = ptok(p);
	fprintf(stderr, "aneoc: %s:%d:%d: ", p->ts->file, t->line, t->col);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "; got '%s'\n", t->v);
	exit(1);
}
static void pexpect(Parser *p, const char *s)
{
	if(!paccept(p, s))
		perr(p, "expected '%s'", s);
}
static char *pexpect_id(Parser *p, bool optional)
{
	if(ptok(p)->kind == TK_ID)
		return p->ts->a[p->p++].v;
	if(optional)
		return xstrdup("");
	perr(p, "expected identifier");
	return NULL;
}

static CType *find_alias(Program *prog, const char *name)
{
	size_t i;

	for(i = 0; i < prog->naliases; i++)
		if(!strcmp(prog->aliases[i].name, name))
			return prog->aliases[i].type;
	return NULL;
}

static bool same_type(CType *a, CType *b)
{
	if(a == b)
		return true;
	if(!a || !b || a->kind != b->kind || a->count != b->count)
		return false;
	if(a->kind == TY_PTR || a->kind == TY_ARRAY)
		return same_type(a->base, b->base);
	return true;
}

static void add_alias(Parser *p, char *name, CType *type)
{
	Program *prog = p->prog;
	CType *old = find_alias(prog, name);

	if(old)
	{
		if(same_type(old, type))
			return;
		perr(p, "conflicting typedef '%s'", name);
	}
	ARR_GROW(prog->aliases, prog->naliases, prog->capaliases, TypeAlias);
	prog->aliases[prog->naliases].name = name;
	prog->aliases[prog->naliases].type = type;
	prog->naliases++;
}

static CType *find_struct_tag(Program *prog, const char *name)
{
	size_t i;

	for(i = 0; i < prog->ntags; i++)
		if(!strcmp(prog->tags[i].name, name))
			return prog->tags[i].type;
	return NULL;
}

static void add_struct_tag(Parser *p, char *name, CType *type)
{
	CType *old;

	if(!name || !*name)
		return;
	old = find_struct_tag(p->prog, name);
	if(old && old != type)
		perr(p, "conflicting struct tag '%s'", name);
	if(old)
		return;
	ARR_GROW(p->prog->tags, p->prog->ntags, p->prog->captags, StructTag);
	p->prog->tags[p->prog->ntags].name = name;
	p->prog->tags[p->prog->ntags].type = type;
	p->prog->ntags++;
}

static StructMember *find_struct_member(CType *type, const char *name)
{
	static StructMember xevent_type_member = {"type", &T_INT, 0};
	size_t i;

	if(!type)
		return NULL;
	if(type->kind == TY_XEVENT)
		return !strcmp(name, "type") ? &xevent_type_member : NULL;
	if(type->kind != TY_STRUCT)
		return NULL;
	for(i = 0; i < type->nmembers; i++)
		if(!strcmp(type->members[i].name, name))
			return &type->members[i];
	return NULL;
}

static Declarator parse_declarator(Parser *p, CType *base, bool unnamed);
static Expr *parse_expr(Parser *p, int minprec);
static bool eval_const_expr(Expr *e, long *value);

static bool type_start(Parser *p)
{
	const char *s = ptok(p)->v;

	if(find_alias(p->prog, s))
		return true;
	return !strcmp(s, "VD") || !strcmp(s, "C") || !strcmp(s, "CC") ||
	       !strcmp(s, "INT") || !strcmp(s, "ULL") || !strcmp(s, "DB") || !strcmp(s, "L") ||
	       !strcmp(s, "S") || !strcmp(s, "U") || !strcmp(s, "void") ||
	       !strcmp(s, "char") || !strcmp(s, "short") || !strcmp(s, "int") ||
	       !strcmp(s, "double") ||
	       !strcmp(s, "FILE") || !strcmp(s, "Display") || !strcmp(s, "Window") ||
	       !strcmp(s, "GC") || !strcmp(s, "Font") || !strcmp(s, "XEvent") ||
	       !strcmp(s, "va_list") || !strcmp(s, "size_t") ||
	       !strcmp(s, "uint8_t") || !strcmp(s, "uint16_t") ||
	       !strcmp(s, "uint32_t") || !strcmp(s, "uint64_t") ||
	       !strcmp(s, "int8_t") || !strcmp(s, "int16_t") ||
	       !strcmp(s, "int32_t") || !strcmp(s, "int64_t") ||
	       !strcmp(s, "const") || !strcmp(s, "unsigned") ||
	       !strcmp(s, "long") || !strcmp(s, "signed") ||
	       !strcmp(s, "struct") || !strcmp(s, "ST");
}

static bool parse_gnu_attributes(Parser *p)
{
	bool packed = false;

	while(peq(p, "__attribute__") || peq(p, "__attribute"))
	{
		int depth = 0;

		p->p++;
		pexpect(p, "(");
		depth = 1;
		while(depth > 0)
		{
			if(ptok(p)->kind == TK_EOF)
				perr(p, "unterminated __attribute__");
			if(peq(p, "packed") || peq(p, "__packed__"))
				packed = true;
			if(peq(p, "("))
				depth++;
			else if(peq(p, ")"))
				depth--;
			p->p++;
		}
	}
	return packed;
}

static void finish_struct_layout(CType *type, bool packed)
{
	long offset = 0;
	long maximum = 1;
	size_t i;

	type->packed = packed;
	for(i = 0; i < type->nmembers; i++)
	{
		long alignment = packed ? 1 : type_align(type->members[i].type);
		long size = type_size(type->members[i].type);

		if(alignment < 1)
			alignment = 1;
		offset = (offset + alignment - 1) / alignment * alignment;
		type->members[i].offset = offset;
		offset += size;
		if(alignment > maximum)
			maximum = alignment;
	}
	type->align = packed ? 1 : maximum;
	type->size = packed ? offset : (offset + maximum - 1) / maximum * maximum;
}

static CType *parse_type(Parser *p)
{
	const char *s;
	CType *alias;
	bool is_unsigned = false;

	paccept(p, "const");
	if(paccept(p, "struct") || paccept(p, "ST"))
	{
		char *tag = NULL;
		CType *type;

		if(ptok(p)->kind == TK_ID && !peq(p, "{"))
			tag = pexpect_id(p, false);
		if(!paccept(p, "{"))
		{
			type = find_struct_tag(p->prog, tag ? tag : "");
			if(!type)
				perr(p, "unknown struct '%s'", tag ? tag : "");
			return type;
		}
		type = new_type(TY_STRUCT, NULL, 0, tag ? tag : "<anonymous>");
		add_struct_tag(p, tag, type);
		while(!paccept(p, "}"))
		{
			CType *member_base = parse_type(p);
			Declarator member = parse_declarator(p, member_base, false);
			if(member.function)
				perr(p, "struct member cannot be a function");
			pexpect(p, ";");
			ARR_GROW(type->members, type->nmembers, type->capmembers, StructMember);
			type->members[type->nmembers].name = member.name;
			type->members[type->nmembers].type = member.type;
			type->members[type->nmembers].offset = 0;
			type->nmembers++;
		}
		finish_struct_layout(type, parse_gnu_attributes(p));
		return type;
	}
	if(paccept(p, "unsigned") || paccept(p, "U"))
		is_unsigned = true;
	else
		paccept(p, "signed");

	if(is_unsigned)
	{
		if(paccept(p, "char") || paccept(p, "C"))
			return &T_U8;
		if(paccept(p, "short") || paccept(p, "S"))
		{
			paccept(p, "int");
			paccept(p, "INT");
			return &T_U16;
		}
		if(paccept(p, "long") || paccept(p, "L"))
		{
			paccept(p, "long");
			paccept(p, "L");
			return &T_U64;
		}
		paccept(p, "int");
		paccept(p, "INT");
		return &T_U32;
	}

	if(paccept(p, "long") || paccept(p, "L"))
	{
		paccept(p, "long");
		paccept(p, "L");
		return &T_U64;
	}
	if(paccept(p, "short") || paccept(p, "S"))
	{
		paccept(p, "int");
		paccept(p, "INT");
		return &T_SHORT;
	}

	s = ptok(p)->v;
	alias = find_alias(p->prog, s);
	if(alias)
	{
		p->p++;
		return alias;
	}
	if(!strcmp(s, "VD") || !strcmp(s, "void"))
	{
		p->p++;
		return &T_VOID;
	}
	if(!strcmp(s, "C") || !strcmp(s, "CC") || !strcmp(s, "char") ||
	   !strcmp(s, "int8_t"))
	{
		p->p++;
		return &T_CHAR;
	}
	if(!strcmp(s, "INT") || !strcmp(s, "int") || !strcmp(s, "int32_t"))
	{
		p->p++;
		return &T_INT;
	}
	if(!strcmp(s, "DB") || !strcmp(s, "double"))
	{
		p->p++;
		return &T_DOUBLE;
	}
	if(!strcmp(s, "uint8_t"))
	{
		p->p++;
		return &T_U8;
	}
	if(!strcmp(s, "uint16_t"))
	{
		p->p++;
		return &T_U16;
	}
	if(!strcmp(s, "uint32_t"))
	{
		p->p++;
		return &T_U32;
	}
	if(!strcmp(s, "ULL") || !strcmp(s, "uint64_t") ||
	   !strcmp(s, "int64_t") || !strcmp(s, "Window") ||
	   !strcmp(s, "Font") || !strcmp(s, "size_t"))
	{
		p->p++;
		return &T_U64;
	}
	if(!strcmp(s, "int16_t"))
	{
		p->p++;
		return &T_SHORT;
	}
	if(!strcmp(s, "FILE"))
	{
		p->p++;
		return &T_FILE;
	}
	if(!strcmp(s, "Display"))
	{
		p->p++;
		return &T_DISPLAY;
	}
	if(!strcmp(s, "GC"))
	{
		p->p++;
		return ptr_to(&T_VOID);
	}
	if(!strcmp(s, "XEvent"))
	{
		p->p++;
		return &T_XEVENT;
	}
	if(!strcmp(s, "va_list"))
	{
		p->p++;
		return &T_VALIST;
	}
	perr(p, "expected type");
	return NULL;
}

static Declarator parse_declarator(Parser *p, CType *base, bool unnamed)
{
	Declarator d = {0};
	CType *t = base;
	while(paccept(p, "*"))
		t = ptr_to(t);
	d.name = pexpect_id(p, unnamed);
	d.type = t;
	if(paccept(p, "("))
	{
		d.function = true;
		if(paccept(p, ")"))
			return d;
		if((peq(p, "VD") || peq(p, "void")) &&
		   !strcmp(p->ts->a[p->p + 1].v, ")"))
		{
			p->p += 2;
			return d;
		}
		for(;;)
		{
			CType *pt;
			char *pn;
			if(paccept(p, "..."))
			{
				d.variadic = true;
				pexpect(p, ")");
				break;
			}
			pt = parse_type(p);
			while(paccept(p, "*"))
				pt = ptr_to(pt);
			pn = pexpect_id(p, true);
			if(!*pn)
			{
				char z[32];
				snprintf(z, sizeof(z), "__arg%zu", d.nparams);
				pn = xstrdup(z);
			}
			if(paccept(p, "["))
			{
				if(ptok(p)->kind == TK_NUM)
					p->p++;
				pexpect(p, "]");
				pt = ptr_to(pt);
			}
			ARR_GROW(d.params, d.nparams, d.capparams, Param);
			d.params[d.nparams].name = pn;
			d.params[d.nparams].type = pt;
			d.nparams++;
			if(paccept(p, ")"))
				break;
			pexpect(p, ",");
		}
		return d;
	}
	while(paccept(p, "["))
	{
		Expr *bound;
		long count;

		if(paccept(p, "]"))
		{
			t = array_of(t, 0);
			d.type = t;
			continue;
		}
		bound = parse_expr(p, 1);
		pexpect(p, "]");
		if(eval_const_expr(bound, &count))
		{
			if(count < 0)
				perr(p, "array length cannot be negative");
			t = array_of(t, count);
		} else if(bound->kind == EX_ID)
		{
			t = vla_of(t, bound->str);
		} else {
			perr(p, "array length must be a constant expression or identifier");
		}
		d.type = t;
	}
	return d;
}

static char decode_escape(const char **pp)
{
	const char *p = *pp;
	char c = *p++;
	if(c != '\\')
	{
		*pp = p;
		return c;
	}
	c = *p++;
	switch(c)
	{
	case 'a':
		c = '\a';
		break;
	case 'b':
		c = '\b';
		break;
	case 'f':
		c = '\f';
		break;
	case 'n':
		c = '\n';
		break;
	case 'r':
		c = '\r';
		break;
	case 't':
		c = '\t';
		break;
	case 'v':
		c = '\v';
		break;
	case '0':
		c = '\0';
		break;
	case '\\':
		c = '\\';
		break;
	case '\'':
		c = '\'';
		break;
	case '"':
		c = '"';
		break;
	default:
		break;
	}
	*pp = p;
	return c;
}

static char *decode_string(const char *raw)
{
	size_t n = strlen(raw), i = 1;
	Buf b = {0};
	while(i + 1 < n)
	{
		const char *p = raw + i;
		char c = decode_escape(&p);
		bputn(&b, &c, 1);
		i = (size_t)(p - raw);
	}
	if(!b.s)
		b.s = xstrdup("");
	return b.s;
}

static Expr *parse_primary(Parser *p)
{
	Token *t = ptok(p);
	Expr *e;
	if(paccept(p, "("))
	{
		e = parse_expr(p, 1);
		pexpect(p, ")");
		return e;
	}
	if(t->kind == TK_NUM)
	{
		bool floating = strchr(t->v, '.') != NULL;
		const char *number = t->v;

		if(!floating && !(number[0] == '0' && (number[1] == 'x' || number[1] == 'X')) &&
		   (strchr(number, 'e') || strchr(number, 'E')))
			floating = true;
		if(floating)
		{
			e = new_expr(EX_NUM);
			e->fnum = strtod(t->v, NULL);
			e->type = &T_DOUBLE;
			p->p++;
			return e;
		}
		char *q = xstrdup(t->v), *x = q;
		char *suffix = NULL;
		unsigned long long v;
		bool wide = false;
		bool uns = false;

		while(*x)
		{
			if(strchr("uUlL", *x))
			{
				suffix = x;
				*x = 0;
				break;
			}
			x++;
		}
		if(suffix)
		{
			const char *r = t->v + (suffix - q);

			while(*r)
			{
				if(*r == 'u' || *r == 'U')
					uns = true;
				if((*r == 'l' || *r == 'L') &&
				   (r[1] == 'l' || r[1] == 'L'))
					wide = true;
				r++;
			}
		}
		v = strtoull(q, NULL, 0);
		free(q);
		p->p++;
		e = new_expr(EX_NUM);
		e->num = v;
		e->type = (wide || v > 0xffffffffULL) ? &T_U64 : (uns ? &T_U32 : &T_INT);
		return e;
	}
	if(t->kind == TK_STR)
	{
		Buf joined = {0};
		while(ptok(p)->kind == TK_STR)
		{
			char *part = decode_string(ptok(p)->v);
			bputs(&joined, part);
			free(part);
			p->p++;
		}
		e = new_expr(EX_STR);
		e->str = joined.s ? joined.s : xstrdup("");
		e->type = ptr_to(&T_CHAR);
		return e;
	}
	if(t->kind == TK_CHAR)
	{
		const char *q = t->v + 1;
		char c = decode_escape(&q);
		p->p++;
		e = new_expr(EX_NUM);
		e->num = (unsigned char)c;
		e->type = &T_INT;
		return e;
	}
	if(t->kind == TK_ID)
	{
		p->p++;
		e = new_expr(EX_ID);
		e->str = t->v;
		return e;
	}
	perr(p, "expected expression");
	return NULL;
}

static Expr *parse_postfix(Parser *p)
{
	Expr *e = parse_primary(p);
	for(;;)
	{
		if(paccept(p, "("))
		{
			Expr *c = new_expr(EX_CALL);
			c->left = e;
			if(!paccept(p, ")"))
				for(;;)
				{
					Expr *a = parse_expr(p, 1);
					ARR_GROW(c->args, c->nargs, c->capargs, Expr *);
					c->args[c->nargs++] = a;
					if(paccept(p, ")"))
						break;
					pexpect(p, ",");
				}
			e = c;
			continue;
		}
		if(paccept(p, "["))
		{
			Expr *x = new_expr(EX_INDEX);
			x->left = e;
			x->right = parse_expr(p, 1);
			pexpect(p, "]");
			e = x;
			continue;
		}
		if(paccept(p, "."))
		{
			Expr *x = new_expr(EX_MEMBER);
			x->left = e;
			x->str = pexpect_id(p, false);
			e = x;
			continue;
		}
		if(paccept(p, "->"))
		{
			Expr *x = new_expr(EX_PTRMEMBER);
			x->left = e;
			x->str = pexpect_id(p, false);
			e = x;
			continue;
		}
		if(paccept(p, "++"))
		{
			Expr *x = new_expr(EX_UNARY);
			x->op = "post++";
			x->left = e;
			e = x;
			continue;
		}
		if(paccept(p, "--"))
		{
			Expr *x = new_expr(EX_UNARY);
			x->op = "post--";
			x->left = e;
			e = x;
			continue;
		}
		break;
	}
	return e;
}

static Expr *parse_unary(Parser *p)
{
	if(peq(p, "("))
	{
		size_t save = p->p;
		CType *cast_type;
		Expr *e;

		p->p++;
		if(type_start(p))
		{
			cast_type = parse_type(p);
			while(paccept(p, "*"))
				cast_type = ptr_to(cast_type);
			if(paccept(p, ")"))
			{
				e = new_expr(EX_UNARY);
				e->op = "cast";
				e->type = cast_type;
				e->left = parse_unary(p);
				return e;
			}
		}
		p->p = save;
	}
	if(peq(p, "++") || peq(p, "--"))
	{
		Expr *e = new_expr(EX_UNARY);
		e->op = ptok(p)->v;
		p->p++;
		e->left = parse_unary(p);
		return e;
	}
	if(peq(p, "!") || peq(p, "~") || peq(p, "-") || peq(p, "+") || peq(p, "&") || peq(p, "*"))
	{
		Expr *e = new_expr(EX_UNARY);
		e->op = ptok(p)->v;
		p->p++;
		e->left = parse_unary(p);
		return e;
	}
	if(paccept(p, "sizeof"))
	{
		Expr *e = new_expr(EX_SIZEOF);

		pexpect(p, "(");
		if(type_start(p))
		{
			e->sizeof_type = parse_type(p);
			while(paccept(p, "*"))
				e->sizeof_type = ptr_to(e->sizeof_type);
		} else {
			e->left = parse_expr(p, 1);
		}
		pexpect(p, ")");
		return e;
	}
	return parse_postfix(p);
}

static int prec(const char *s)
{
	if(!strcmp(s, "=") || !strcmp(s, "+=") || !strcmp(s, "-=") ||
	   !strcmp(s, "*=") || !strcmp(s, "/=") || !strcmp(s, "%=") ||
	   !strcmp(s, "&=") || !strcmp(s, "|=") || !strcmp(s, "^=") ||
	   !strcmp(s, "<<=") || !strcmp(s, ">>="))
		return 1;
	if(!strcmp(s, "||"))
		return 2;
	if(!strcmp(s, "&&"))
		return 3;
	if(!strcmp(s, "|"))
		return 4;
	if(!strcmp(s, "^"))
		return 5;
	if(!strcmp(s, "&"))
		return 6;
	if(!strcmp(s, "==") || !strcmp(s, "!="))
		return 7;
	if(!strcmp(s, "<") || !strcmp(s, "<=") || !strcmp(s, ">") || !strcmp(s, ">="))
		return 8;
	if(!strcmp(s, "<<") || !strcmp(s, ">>"))
		return 9;
	if(!strcmp(s, "+") || !strcmp(s, "-"))
		return 10;
	if(!strcmp(s, "*") || !strcmp(s, "/") || !strcmp(s, "%"))
		return 11;
	return 0;
}

static Expr *parse_expr(Parser *p, int minprec)
{
	Expr *lhs = parse_unary(p);
	for(;;)
	{
		int pr = prec(ptok(p)->v);
		char *op;
		Expr *rhs, *e;
		bool right;
		if(pr < minprec)
			break;
		op = ptok(p)->v;
		p->p++;
		right = (!strcmp(op, "=") || !strcmp(op, "+=") || !strcmp(op, "-=") ||
			 !strcmp(op, "*=") || !strcmp(op, "/=") || !strcmp(op, "%=") ||
			 !strcmp(op, "&=") || !strcmp(op, "|=") || !strcmp(op, "^=") ||
			 !strcmp(op, "<<=") || !strcmp(op, ">>="));
		rhs = parse_expr(p, right ? pr : pr + 1);
		e = new_expr(EX_BINARY);
		e->op = op;
		e->left = lhs;
		e->right = rhs;
		lhs = e;
	}
	return lhs;
}

static Expr *parse_initializer(Parser *p)
{
	Expr *e;

	if(!paccept(p, "{"))
		return parse_expr(p, 1);
	e = new_expr(EX_INITLIST);
	if(paccept(p, "}"))
		return e;
	for(;;)
	{
		Expr *item = parse_initializer(p);

		ARR_GROW(e->args, e->nargs, e->capargs, Expr *);
		e->args[e->nargs++] = item;
		if(paccept(p, "}"))
			break;
		pexpect(p, ",");
		if(paccept(p, "}"))
			break;
	}
	return e;
}

static bool eval_const_expr(Expr *e, long *value)
{
	long a, b;

	if(!e)
		return false;
	if(e->kind == EX_NUM)
	{
		*value = (long)e->num;
		return true;
	}
	if(e->kind == EX_ID && !strcmp(e->str, "NULL"))
	{
		*value = 0;
		return true;
	}
	if(e->kind == EX_UNARY && eval_const_expr(e->left, &a))
	{
		if(!strcmp(e->op, "+") || !strcmp(e->op, "cast"))
			*value = a;
		else if(!strcmp(e->op, "-"))
			*value = -a;
		else if(!strcmp(e->op, "~"))
			*value = ~a;
		else if(!strcmp(e->op, "!"))
			*value = !a;
		else
			return false;
		return true;
	}
	if(e->kind != EX_BINARY || !eval_const_expr(e->left, &a) ||
	   !eval_const_expr(e->right, &b))
		return false;
	if(!strcmp(e->op, "+"))
		*value = a + b;
	else if(!strcmp(e->op, "-"))
		*value = a - b;
	else if(!strcmp(e->op, "*"))
		*value = a * b;
	else if(!strcmp(e->op, "/"))
	{
		if(!b)
			return false;
		*value = a / b;
	} else if(!strcmp(e->op, "%"))
	{
		if(!b)
			return false;
		*value = a % b;
	} else if(!strcmp(e->op, "<<"))
		*value = a << b;
	else if(!strcmp(e->op, ">>"))
		*value = a >> b;
	else if(!strcmp(e->op, "&"))
		*value = a & b;
	else if(!strcmp(e->op, "|"))
		*value = a | b;
	else if(!strcmp(e->op, "^"))
		*value = a ^ b;
	else
		return false;
	return true;
}

static void infer_array_bound(Parser *p, CType *type, Expr *initializer)
{
	if(!type || type->kind != TY_ARRAY || type->count != 0)
		return;
	if(!initializer)
		return;
	if(initializer->kind == EX_INITLIST)
		type->count = (long)initializer->nargs;
	else if(initializer->kind == EX_STR &&
		(type->base->kind == TY_CHAR || type->base->kind == TY_U8))
		type->count = (long)strlen(initializer->str) + 1;
	else
		perr(p, "array with omitted length requires an initializer");
}

static Stmt *parse_stmt(Parser *p);

static Stmt *parse_block(Parser *p)
{
	Stmt *s = new_stmt(ST_BLOCK);
	pexpect(p, "{");
	while(!paccept(p, "}"))
	{
		Stmt *x;

		if(type_start(p))
		{
			CType *b = parse_type(p);

			for(;;)
			{
				Declarator q = parse_declarator(p, b, false);
				Decl *d = new_decl();

				if(q.function)
					perr(p, "nested function unsupported");
				d->name = q.name;
				d->type = q.type;
				if(paccept(p, "="))
					d->init = parse_initializer(p);
				infer_array_bound(p, d->type, d->init);
				x = new_stmt(ST_DECL);
				x->decl = d;
				ARR_GROW(s->children, s->nchildren, s->capchildren, Stmt *);
				s->children[s->nchildren++] = x;
				if(!paccept(p, ","))
					break;
			}
			pexpect(p, ";");
			continue;
		}
		x = parse_stmt(p);
		ARR_GROW(s->children, s->nchildren, s->capchildren, Stmt *);
		s->children[s->nchildren++] = x;
	}
	return s;
}

static Stmt *parse_stmt(Parser *p)
{
	Stmt *s;
	if(peq(p, "{"))
		return parse_block(p);
	if(paccept(p, "if"))
	{
		s = new_stmt(ST_IF);
		pexpect(p, "(");
		s->cond = parse_expr(p, 1);
		pexpect(p, ")");
		s->yes = parse_stmt(p);
		if(paccept(p, "else"))
			s->no = parse_stmt(p);
		return s;
	}
	if(paccept(p, "while"))
	{
		s = new_stmt(ST_WHILE);
		pexpect(p, "(");
		s->cond = parse_expr(p, 1);
		pexpect(p, ")");
		s->body = parse_stmt(p);
		return s;
	}
	if(paccept(p, "for"))
	{
		s = new_stmt(ST_FOR);
		pexpect(p, "(");
		if(!paccept(p, ";"))
		{
			if(type_start(p))
			{
				CType *b = parse_type(p);
				Declarator q = parse_declarator(p, b, false);
				Decl *d = new_decl();
				if(q.function)
					perr(p, "function declaration in for initializer unsupported");
				d->name = q.name;
				d->type = q.type;
				if(paccept(p, "="))
					d->init = parse_initializer(p);
				infer_array_bound(p, d->type, d->init);
				pexpect(p, ";");
				s->init = new_stmt(ST_DECL);
				s->init->decl = d;
			} else {
				s->init = new_stmt(ST_EXPR);
				s->init->expr = parse_expr(p, 1);
				pexpect(p, ";");
			}
		}
		if(!paccept(p, ";"))
		{
			s->cond = parse_expr(p, 1);
			pexpect(p, ";");
		}
		if(!paccept(p, ")"))
		{
			s->post = parse_expr(p, 1);
			pexpect(p, ")");
		}
		s->body = parse_stmt(p);
		return s;
	}
	if(paccept(p, "switch"))
	{
		s = new_stmt(ST_SWITCH);
		pexpect(p, "(");
		s->cond = parse_expr(p, 1);
		pexpect(p, ")");
		s->body = parse_stmt(p);
		return s;
	}
	if(paccept(p, "case"))
	{
		s = new_stmt(ST_CASE);
		s->expr = parse_expr(p, 1);
		pexpect(p, ":");
		return s;
	}
	if(paccept(p, "default"))
	{
		s = new_stmt(ST_DEFAULT);
		pexpect(p, ":");
		return s;
	}
	if(paccept(p, "return"))
	{
		s = new_stmt(ST_RETURN);
		if(!paccept(p, ";"))
		{
			s->expr = parse_expr(p, 1);
			pexpect(p, ";");
		}
		return s;
	}
	if(paccept(p, "break"))
	{
		pexpect(p, ";");
		return new_stmt(ST_BREAK);
	}
	if(paccept(p, "continue"))
	{
		pexpect(p, ";");
		return new_stmt(ST_CONTINUE);
	}
	if(paccept(p, "asm") || paccept(p, "__asm__"))
	{
		Buf text = {0};

		s = new_stmt(ST_ASM);
		paccept(p, "volatile");
		paccept(p, "__volatile__");
		pexpect(p, "(");
		if(ptok(p)->kind != TK_STR)
			perr(p, "inline asm requires a string literal");
		while(ptok(p)->kind == TK_STR)
		{
			char *part = decode_string(ptok(p)->v);

			bputs(&text, part);
			free(part);
			p->p++;
		}
		if(paccept(p, ":"))
		{
			if(!peq(p, ")") && !peq(p, ":"))
			{
				for(;;)
				{
					char *constraint;
					Expr *output;

					if(ptok(p)->kind != TK_STR)
						perr(p, "asm output constraint must be a string");
					constraint = decode_string(ptok(p)->v);
					p->p++;
					pexpect(p, "(");
					output = parse_expr(p, 1);
					pexpect(p, ")");
					ARR_GROW(s->asm_constraints, s->nasm_outputs, s->capasm_constraints, char *);
					s->asm_constraints[s->nasm_outputs] = constraint;
					ARR_GROW(s->asm_outputs, s->nasm_outputs, s->capasm_outputs, Expr *);
					s->asm_outputs[s->nasm_outputs] = output;
					s->nasm_outputs++;
					if(!paccept(p, ","))
						break;
				}
			}
			/* Inputs and clobbers are parsed only when empty for now. */
			if(paccept(p, ":"))
			{
				if(!peq(p, ")") && !peq(p, ":"))
					perr(p, "asm inputs are not supported yet");
				paccept(p, ":");
			}
		}
		pexpect(p, ")");
		pexpect(p, ";");
		s->asm_text = text.s ? text.s : xstrdup("");
		return s;
	}
	if(paccept(p, ";"))
		return new_stmt(ST_EMPTY);
	s = new_stmt(ST_EXPR);
	s->expr = parse_expr(p, 1);
	pexpect(p, ";");
	return s;
}

static void parse_program(Tokens *ts, Program *prog)
{
	Parser p = {ts, 0, prog};

	while(ptok(&p)->kind != TK_EOF)
	{
		bool is_typedef = false;
		bool is_extern = false;
		bool is_static = false;
		bool is_inline = false;
		CType *b;
		Declarator q;
		Decl *d;

		for(;;)
		{
			if(paccept(&p, "typedef") || paccept(&p, "TD"))
			{
				if(is_typedef)
					perr(&p, "duplicate typedef specifier");
				is_typedef = true;
				continue;
			}
			if(paccept(&p, "extern"))
			{
				if(is_extern)
					perr(&p, "duplicate extern specifier");
				is_extern = true;
				continue;
			}
			if(paccept(&p, "static") || paccept(&p, "SC"))
			{
				if(is_static)
					perr(&p, "duplicate static specifier");
				is_static = true;
				continue;
			}
			if(paccept(&p, "inline") || paccept(&p, "IL"))
			{
				if(is_inline)
					perr(&p, "duplicate inline specifier");
				is_inline = true;
				continue;
			}
			break;
		}
		if(is_extern && is_static)
			perr(&p, "declaration cannot be both extern and static");
		if(is_typedef && (is_extern || is_static || is_inline))
			perr(&p, "typedef cannot be combined with extern, static, or inline");
		b = parse_type(&p);
		if(paccept(&p, ";"))
		{
			if(is_typedef || is_extern || is_static || is_inline)
				perr(&p, "declaration specifier requires a declarator");
			continue;
		}
		q = parse_declarator(&p, b, false);
		if(is_typedef)
		{
			if(q.function)
				perr(&p, "function typedefs are not supported yet");
			pexpect(&p, ";");
			add_alias(&p, q.name, q.type);
			continue;
		}
		if(is_inline && !q.function)
			perr(&p, "inline can only be used on a function");
		d = new_decl();
		d->name = q.name;
		d->type = q.type;
		d->params = q.params;
		d->nparams = q.nparams;
		d->capparams = q.capparams;
		d->variadic = q.variadic;
		d->is_extern = is_extern;
		d->is_static = is_static;
		d->is_inline = is_inline;
		if(q.function)
		{
			if(paccept(&p, ";"))
				d->prototype = true;
			else
			{
				if(is_extern)
					perr(&p, "extern function cannot have a body");
				d->body = parse_block(&p);
			}
		} else {
			if(paccept(&p, "="))
			{
				if(is_extern)
					perr(&p, "extern object cannot have an initializer");
				d->init = parse_initializer(&p);
			}
			infer_array_bound(&p, d->type, d->init);
			pexpect(&p, ";");
		}
		ARR_GROW(prog->a, prog->n, prog->cap, Decl *);
		prog->a[prog->n++] = d;
	}
}

/* ---------- Code generation ---------- */

typedef enum
{
	SY_LOCAL,
	SY_GLOBAL,
	SY_CONST,
	SY_EXTERN_GLOBAL,
	SY_FUNCTION,
	SY_EXTERN_FUNCTION
} SymKind;
typedef struct
{
	char *name;
	CType *type;
	SymKind kind;
	long off;
	Decl *fn;
} Symbol;
typedef struct
{
	char *value;
	char label[32];
} StringLit;

typedef struct
{
	Program *prog;
	Buf out;
	Buf ro;
	Symbol *globals;
	size_t nglobals, capglobals;
	Decl **funcs;
	size_t nfuncs, capfuncs;
	Symbol *locals;
	size_t nlocals, caplocals;
	StringLit *strings;
	size_t nstrings, capstrings;
	Decl *current;
	long frame, vaoff, tempdepth;
	long label;
	char retlabel[128];
	char **breaks;
	size_t nbreaks, capbreaks;
	char **continues;
	size_t ncontinues, capcontinues;
} Gen;

static long align_up(long v, long a)
{
	return (v + a - 1) / a * a;
}
static void emit(Gen *g, const char *fmt, ...)
{
	va_list ap, aq;
	int n;
	va_start(ap, fmt);
	va_copy(aq, ap);
	n = vsnprintf(NULL, 0, fmt, aq);
	va_end(aq);
	bneed(&g->out, (size_t)n + 1);
	vsnprintf(g->out.s + g->out.n, g->out.cap - g->out.n, fmt, ap);
	va_end(ap);
	g->out.n += (size_t)n;
	bputs(&g->out, "\n");
}
static char *new_label(Gen *g, const char *prefix)
{
	char z[128];
	snprintf(z, sizeof(z), "%s%ld", prefix, ++g->label);
	return xstrdup(z);
}
static Symbol *find_local(Gen *g, const char *n)
{
	size_t i;
	for(i = 0; i < g->nlocals; i++)
		if(!strcmp(g->locals[i].name, n))
			return &g->locals[i];
	return NULL;
}
static Symbol *find_global(Gen *g, const char *n)
{
	size_t i;
	Symbol *external = NULL;

	for(i = 0; i < g->nglobals; i++)
	{
		if(strcmp(g->globals[i].name, n))
			continue;
		if(g->globals[i].kind == SY_GLOBAL)
			return &g->globals[i];
		external = &g->globals[i];
	}
	return external;
}
static Decl *find_func(Gen *g, const char *n)
{
	size_t i;
	for(i = 0; i < g->nfuncs; i++)
		if(!strcmp(g->funcs[i]->name, n))
			return g->funcs[i];
	return NULL;
}

static Symbol lookup(Gen *g, const char *n)
{
	Symbol *s = find_local(g, n);
	Decl *f;
	if(s)
		return *s;
	s = find_global(g, n);
	if(s)
		return *s;
	if(!strcmp(n, "NULL"))
		return (Symbol){(char *)n, &T_U64, SY_CONST, 0, NULL};
	if(!strcmp(n, "ExposureMask"))
		return (Symbol){(char *)n, &T_U64, SY_CONST, 1L << 15, NULL};
	if(!strcmp(n, "KeyPressMask"))
		return (Symbol){(char *)n, &T_U64, SY_CONST, 1L << 0, NULL};
	if(!strcmp(n, "KeyPress"))
		return (Symbol){(char *)n, &T_U64, SY_CONST, 2, NULL};
	if(!strcmp(n, "stderr"))
		return (Symbol){(char *)n, ptr_to(&T_VOID), SY_EXTERN_GLOBAL, 0, NULL};
	f = find_func(g, n);
	if(f)
		return (Symbol){(char *)n, f->type, SY_FUNCTION, 0, f};
	return (Symbol){(char *)n, &T_U64, SY_EXTERN_FUNCTION, 0, NULL};
}

static CType *expr_type(Gen *g, Expr *e)
{
	CType *t;
	if(e->type)
		return e->type;
	switch(e->kind)
	{
	case EX_ID:
	{
		Symbol s = lookup(g, e->str);
		return s.type;
	}
	case EX_STR:
		return ptr_to(&T_CHAR);
	case EX_NUM:
		return &T_U64;
	case EX_UNARY:
		if(!strcmp(e->op, "&"))
			return ptr_to(expr_type(g, e->left));
		if(!strcmp(e->op, "*"))
		{
			t = expr_type(g, e->left);
			return t->base ? t->base : &T_U64;
		}
		return expr_type(g, e->left);
	case EX_INDEX:
		t = expr_type(g, e->left);
		return t->base ? t->base : &T_U64;
	case EX_MEMBER:
	case EX_PTRMEMBER:
	{
		StructMember *member;
		t = expr_type(g, e->left);
		if(e->kind == EX_PTRMEMBER)
			t = t && t->kind == TY_PTR ? t->base : NULL;
		member = find_struct_member(t, e->str);
		if(!member)
			fatal("unknown struct member %s", e->str);
		return member->type;
	}
	case EX_SIZEOF:
		return &T_U64;
	case EX_BINARY:
		if(!strcmp(e->op, "==") || !strcmp(e->op, "!=") || !strcmp(e->op, "<") || !strcmp(e->op, "<=") || !strcmp(e->op, ">") || !strcmp(e->op, ">=") || !strcmp(e->op, "&&") || !strcmp(e->op, "||"))
			return &T_INT;
		if(!strcmp(e->op, "=") || !strcmp(e->op, "+=") || !strcmp(e->op, "-=") ||
		   !strcmp(e->op, "*=") || !strcmp(e->op, "/="))
			return expr_type(g, e->left);
		if(expr_type(g, e->left)->kind == TY_DOUBLE ||
		   expr_type(g, e->right)->kind == TY_DOUBLE)
			return &T_DOUBLE;
		return expr_type(g, e->left);
	case EX_CALL:
		if(e->left->kind == EX_ID)
		{
			Decl *f = find_func(g, e->left->str);
			if(f)
				return f->type;
		}
		return &T_U64;
	case EX_INITLIST:
		return &T_VOID;
	}
	return &T_U64;
}

static char *intern_string(Gen *g, const char *v)
{
	size_t i, j, n;
	for(i = 0; i < g->nstrings; i++)
		if(!strcmp(g->strings[i].value, v))
			return g->strings[i].label;
	ARR_GROW(g->strings, g->nstrings, g->capstrings, StringLit);
	g->strings[g->nstrings].value = xstrdup(v);
	snprintf(g->strings[g->nstrings].label, sizeof(g->strings[g->nstrings].label), ".LC%zu", g->nstrings);
	bprintf(&g->ro, "%s:\n    .byte ", g->strings[g->nstrings].label);
	n = strlen(v);
	for(j = 0; j < n; j++)
		bprintf(&g->ro, "%u, ", (unsigned char)v[j]);
	bputs(&g->ro, "0\n");
	return g->strings[g->nstrings++].label;
}

static void gen_expr(Gen *, Expr *);
static CType *gen_addr(Gen *, Expr *);

static void pushreg(Gen *g, const char *r)
{
	emit(g, "    push %s", r);
	g->tempdepth += 8;
}
static void popreg(Gen *g, const char *r)
{
	emit(g, "    pop %s", r);
	g->tempdepth -= 8;
}
static void load_rax(Gen *g, CType *t)
{
	if(t->kind == TY_ARRAY || t->kind == TY_STRUCT ||
	   t->kind == TY_XEVENT || t->kind == TY_VALIST)
		return;
	if(t->kind == TY_CHAR || t->kind == TY_U8)
		emit(g, "    movzx eax, byte ptr [rax]");
	else if(t->kind == TY_SHORT || t->kind == TY_U16)
		emit(g, "    movzx eax, word ptr [rax]");
	else if(t->kind == TY_INT)
		emit(g, "    movsxd rax, dword ptr [rax]");
	else if(t->kind == TY_U32)
		emit(g, "    mov eax, dword ptr [rax]");
	else
		emit(g, "    mov rax, qword ptr [rax]");
}
static void store_rcx(Gen *g, CType *t)
{
	if(t->kind == TY_CHAR || t->kind == TY_U8)
		emit(g, "    mov byte ptr [rcx], al");
	else if(t->kind == TY_SHORT || t->kind == TY_U16)
		emit(g, "    mov word ptr [rcx], ax");
	else if(t->kind == TY_INT || t->kind == TY_U32)
		emit(g, "    mov dword ptr [rcx], eax");
	else
		emit(g, "    mov qword ptr [rcx], rax");
}

static bool is_double_type(CType *type)
{
	return type && type->kind == TY_DOUBLE;
}

static void integer_bits_to_double(Gen *g)
{
	emit(g, "    cvtsi2sd xmm0, rax");
	emit(g, "    movq rax, xmm0");
}

static void gen_expr_as_double(Gen *g, Expr *expression)
{
	CType *source = expr_type(g, expression);

	gen_expr(g, expression);
	if(!is_double_type(source))
		integer_bits_to_double(g);
}

static void load_extern_global(Gen *g, const char *name, CType *t)
{
	if(t->kind == TY_CHAR || t->kind == TY_U8)
		emit(g, "    movzx eax, byte ptr [rip+%s]", name);
	else if(t->kind == TY_SHORT || t->kind == TY_U16)
		emit(g, "    movzx eax, word ptr [rip+%s]", name);
	else if(t->kind == TY_INT)
		emit(g, "    movsxd rax, dword ptr [rip+%s]", name);
	else if(t->kind == TY_U32)
		emit(g, "    mov eax, dword ptr [rip+%s]", name);
	else
		emit(g, "    mov rax, qword ptr [rip+%s]", name);
}

static CType *gen_addr(Gen *g, Expr *e)
{
	CType *t;
	if(e->kind == EX_ID)
	{
		Symbol s = lookup(g, e->str);
		if(s.kind == SY_LOCAL)
		{
			emit(g, "    lea rax, [rbp-%ld]", s.off);
			return s.type;
		}
		if(s.kind == SY_GLOBAL)
		{
			emit(g, "    lea rax, [rip+%s]", s.name);
			return s.type;
		}
		fatal("%s is not assignable", e->str);
	}
	if(e->kind == EX_UNARY && !strcmp(e->op, "*"))
	{
		gen_expr(g, e->left);
		t = expr_type(g, e->left);
		return t->base ? t->base : &T_U64;
	}
	if(e->kind == EX_INDEX)
	{
		gen_expr(g, e->left);
		pushreg(g, "rax");
		gen_expr(g, e->right);
		t = expr_type(g, e->left);
		t = t->base ? t->base : &T_U64;
		if(type_size(t) != 1)
			emit(g, "    imul rax, %ld", type_size(t));
		popreg(g, "rcx");
		emit(g, "    add rax, rcx");
		return t;
	}
	if(e->kind == EX_MEMBER || e->kind == EX_PTRMEMBER)
	{
		StructMember *member;
		CType *owner;
		if(e->kind == EX_MEMBER)
		{
			gen_addr(g, e->left);
			owner = expr_type(g, e->left);
		} else {
			gen_expr(g, e->left);
			owner = expr_type(g, e->left);
			owner = owner && owner->kind == TY_PTR ? owner->base : NULL;
		}
		member = find_struct_member(owner, e->str);
		if(!member)
			fatal("unknown struct member %s", e->str);
		if(member->offset)
			emit(g, "    add rax, %ld", member->offset);
		return member->type;
	}
	fatal("expression is not an lvalue");
	return &T_U64;
}

static void gen_binary(Gen *g, Expr *e)
{
	const char *op = e->op;
	CType *t;
	bool floating = is_double_type(expr_type(g, e->left)) ||
			is_double_type(expr_type(g, e->right));

	if(!strcmp(op, "="))
	{
		t = gen_addr(g, e->left);
		pushreg(g, "rax");
		if(is_double_type(t))
			gen_expr_as_double(g, e->right);
		else
		{
			gen_expr(g, e->right);
			if(is_double_type(expr_type(g, e->right)))
			{
				emit(g, "    movq xmm0, rax");
				emit(g, "    cvttsd2si rax, xmm0");
			}
		}
		popreg(g, "rcx");
		store_rcx(g, t);
		return;
	}
	if(!strcmp(op, "+=") || !strcmp(op, "-=") || !strcmp(op, "*=") || !strcmp(op, "/="))
	{
		t = gen_addr(g, e->left);
		pushreg(g, "rax");
		load_rax(g, t);
		if(is_double_type(t))
		{
			pushreg(g, "rax");
			gen_expr_as_double(g, e->right);
			popreg(g, "rcx");
			emit(g, "    movq xmm0, rcx");
			emit(g, "    movq xmm1, rax");
			if(!strcmp(op, "+="))
				emit(g, "    addsd xmm0, xmm1");
			else if(!strcmp(op, "-="))
				emit(g, "    subsd xmm0, xmm1");
			else if(!strcmp(op, "*="))
				emit(g, "    mulsd xmm0, xmm1");
			else
				emit(g, "    divsd xmm0, xmm1");
			emit(g, "    movq rax, xmm0");
		} else {
			pushreg(g, "rax");
			gen_expr(g, e->right);
			popreg(g, "rcx");
			if(!strcmp(op, "+="))
				emit(g, "    add rax, rcx");
			else if(!strcmp(op, "-="))
			{
				emit(g, "    sub rcx, rax");
				emit(g, "    mov rax, rcx");
			} else if(!strcmp(op, "*="))
				emit(g, "    imul rax, rcx");
			else
			{
				emit(g, "    mov r10, rax");
				emit(g, "    mov rax, rcx");
				emit(g, "    cqo");
				emit(g, "    idiv r10");
			}
		}
		popreg(g, "rcx");
		store_rcx(g, t);
		return;
	}
	if(!strcmp(op, "&&") || !strcmp(op, "||"))
	{
		char *a = new_label(g, ".Llogic"), *d = new_label(g, ".Llogicdone");
		gen_expr(g, e->left);
		emit(g, "    test rax, rax");
		if(!strcmp(op, "&&"))
			emit(g, "    jz %s", a);
		else
			emit(g, "    jnz %s", a);
		gen_expr(g, e->right);
		emit(g, "    test rax, rax");
		emit(g, "    setne al");
		emit(g, "    movzx rax, al");
		emit(g, "    jmp %s", d);
		emit(g, "%s:", a);
		if(!strcmp(op, "&&"))
			emit(g, "    xor eax, eax");
		else
			emit(g, "    mov eax, 1");
		emit(g, "%s:", d);
		return;
	}
	if(floating && (!strcmp(op, "+") || !strcmp(op, "-") || !strcmp(op, "*") || !strcmp(op, "/")))
	{
		gen_expr_as_double(g, e->left);
		pushreg(g, "rax");
		gen_expr_as_double(g, e->right);
		popreg(g, "rcx");
		emit(g, "    movq xmm0, rcx");
		emit(g, "    movq xmm1, rax");
		if(!strcmp(op, "+"))
			emit(g, "    addsd xmm0, xmm1");
		else if(!strcmp(op, "-"))
			emit(g, "    subsd xmm0, xmm1");
		else if(!strcmp(op, "*"))
			emit(g, "    mulsd xmm0, xmm1");
		else
			emit(g, "    divsd xmm0, xmm1");
		emit(g, "    movq rax, xmm0");
		return;
	}
	gen_expr(g, e->left);
	pushreg(g, "rax");
	gen_expr(g, e->right);
	popreg(g, "rcx");
	if(!strcmp(op, "+"))
		emit(g, "    add rax, rcx");
	else if(!strcmp(op, "-"))
	{
		emit(g, "    sub rcx, rax");
		emit(g, "    mov rax, rcx");
	} else if(!strcmp(op, "*"))
		emit(g, "    imul rax, rcx");
	else if(!strcmp(op, "/") || !strcmp(op, "%"))
	{
		emit(g, "    mov r10, rax");
		emit(g, "    mov rax, rcx");
		emit(g, "    cqo");
		emit(g, "    idiv r10");
		if(!strcmp(op, "%"))
			emit(g, "    mov rax, rdx");
	} else if(!strcmp(op, "&"))
		emit(g, "    and rax, rcx");
	else if(!strcmp(op, "|"))
		emit(g, "    or rax, rcx");
	else if(!strcmp(op, "^"))
		emit(g, "    xor rax, rcx");
	else if(!strcmp(op, "<<") || !strcmp(op, ">>"))
	{
		emit(g, "    mov rdx, rax");
		emit(g, "    mov rax, rcx");
		emit(g, "    mov rcx, rdx");
		emit(g, !strcmp(op, "<<") ? "    shl rax, cl" : "    sar rax, cl");
	} else if(!strcmp(op, "==") || !strcmp(op, "!=") || !strcmp(op, "<") || !strcmp(op, "<=") || !strcmp(op, ">") || !strcmp(op, ">="))
	{
		const char *cc = !strcmp(op, "==") ? "e" : !strcmp(op, "!=") ? "ne"
						   : !strcmp(op, "<")	     ? "l"
						   : !strcmp(op, "<=")	     ? "le"
						   : !strcmp(op, ">")	     ? "g"
									     : "ge";
		emit(g, "    cmp rcx, rax");
		emit(g, "    set%s al", cc);
		emit(g, "    movzx rax, al");
	} else
		fatal("unsupported operator %s", op);
}

static const char *call_name(const char *n)
{
	if(!strcmp(n, "DefaultScreen"))
		return "XDefaultScreen";
	if(!strcmp(n, "RootWindow"))
		return "XRootWindow";
	if(!strcmp(n, "BlackPixel"))
		return "XBlackPixel";
	if(!strcmp(n, "WhitePixel"))
		return "XWhitePixel";
	return n;
}

static void gen_call(Gen *g, Expr *e)
{
	size_t i, n = e->nargs;
	long nstack, pad, cleanup;
	const char *name;
	static const char *regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
	bool has_double = false;

	if(e->left->kind != EX_ID)
		fatal("only direct calls supported");
	name = e->left->str;
	if(!strcmp(name, "va_start"))
	{
		Symbol ap;
		long named;
		if(!g->current || !g->current->variadic || n != 2 || e->args[0]->kind != EX_ID)
			fatal("bad va_start");
		ap = lookup(g, e->args[0]->str);
		named = (long)(g->current->nparams < 6 ? g->current->nparams : 6);
		emit(g, "    lea rax, [rbp-%ld]", ap.off);
		emit(g, "    mov dword ptr [rax], %ld", named * 8);
		emit(g, "    mov dword ptr [rax+4], 48");
		emit(g, "    lea rcx, [rbp+16]");
		emit(g, "    mov qword ptr [rax+8], rcx");
		emit(g, "    lea rcx, [rbp-%ld]", g->vaoff);
		emit(g, "    mov qword ptr [rax+16], rcx");
		emit(g, "    xor eax, eax");
		return;
	}
	if(!strcmp(name, "va_end"))
	{
		emit(g, "    xor eax, eax");
		return;
	}
	for(i = 0; i < n; i++)
		if(is_double_type(expr_type(g, e->args[i])))
			has_double = true;
	name = call_name(name);
	if(has_double)
	{
		size_t integer_count = 0, double_count = 0;
		for(i = 0; i < n; i++)
		{
			if(is_double_type(expr_type(g, e->args[i])))
				double_count++;
			else
				integer_count++;
		}
		if(integer_count > 6 || double_count > 8)
			fatal("native calls with floating arguments currently support 6 integer and 8 double register arguments");
		pad = (16 - ((g->tempdepth + (long)n * 8) % 16)) % 16;
		if(pad)
		{
			emit(g, "    sub rsp, %ld", pad);
			g->tempdepth += pad;
		}
		for(i = n; i > 0; i--)
		{
			if(is_double_type(expr_type(g, e->args[i - 1])))
				gen_expr_as_double(g, e->args[i - 1]);
			else
				gen_expr(g, e->args[i - 1]);
			pushreg(g, "rax");
		}
		integer_count = double_count = 0;
		for(i = 0; i < n; i++)
		{
			if(is_double_type(expr_type(g, e->args[i])))
			{
				popreg(g, "rax");
				emit(g, "    movq xmm%zu, rax", double_count++);
			} else {
				popreg(g, regs[integer_count++]);
			}
		}
		emit(g, "    mov eax, %zu", double_count);
		emit(g, "    call %s@PLT", name);
		if(pad)
		{
			emit(g, "    add rsp, %ld", pad);
			g->tempdepth -= pad;
		}
		if(is_double_type(expr_type(g, e)))
			emit(g, "    movq rax, xmm0");
		return;
	}
	nstack = (long)(n > 6 ? n - 6 : 0);
	pad = (16 - ((g->tempdepth + nstack * 8) % 16)) % 16;
	if(pad)
	{
		emit(g, "    sub rsp, %ld", pad);
		g->tempdepth += pad;
	}
	for(i = n; i > 0; i--)
	{
		gen_expr(g, e->args[i - 1]);
		pushreg(g, "rax");
	}
	for(i = 0; i < n && i < 6; i++)
		popreg(g, regs[i]);
	emit(g, "    xor eax, eax");
	emit(g, "    call %s@PLT", name);
	cleanup = nstack * 8 + pad;
	if(cleanup)
	{
		emit(g, "    add rsp, %ld", cleanup);
		g->tempdepth -= cleanup;
	}
	if(is_double_type(expr_type(g, e)))
		emit(g, "    movq rax, xmm0");
}

static void gen_incdec(Gen *g, Expr *e)
{
	CType *t = gen_addr(g, e->left);
	long step = 1;
	bool postfix = !strcmp(e->op, "post++") ||
		       !strcmp(e->op, "post--");
	bool decrement = !strcmp(e->op, "--") ||
			 !strcmp(e->op, "post--");

	if(t->kind == TY_PTR && t->base)
	{
		step = type_size(t->base);
		if(step <= 0)
			step = 1;
	}

	pushreg(g, "rax");
	load_rax(g, t);
	if(postfix)
		pushreg(g, "rax");
	if(decrement)
		emit(g, "    sub rax, %ld", step);
	else
		emit(g, "    add rax, %ld", step);
	if(postfix)
	{
		popreg(g, "rdx");
		popreg(g, "rcx");
		store_rcx(g, t);
		emit(g, "    mov rax, rdx");
	} else {
		popreg(g, "rcx");
		store_rcx(g, t);
	}
}

static void gen_expr(Gen *g, Expr *e)
{
	CType *t;
	char *lab;
	Symbol s;
	switch(e->kind)
	{
	case EX_NUM:
		if(is_double_type(e->type))
		{
			union
			{
				double d;
				uint64_t u;
			} bits;
			bits.d = e->fnum;
			emit(g, "    mov rax, %llu", (unsigned long long)bits.u);
		} else
			emit(g, "    mov rax, %llu", e->num);
		return;
	case EX_STR:
		lab = intern_string(g, e->str);
		emit(g, "    lea rax, [rip+%s]", lab);
		return;
	case EX_ID:
		s = lookup(g, e->str);
		if(s.kind == SY_CONST)
		{
			emit(g, "    mov rax, %ld", s.off);
			return;
		}
		if(s.kind == SY_EXTERN_GLOBAL)
		{
			load_extern_global(g, s.name, s.type);
			return;
		}
		if(s.kind == SY_FUNCTION || s.kind == SY_EXTERN_FUNCTION)
		{
			emit(g, "    lea rax, [rip+%s]", s.name);
			return;
		}
		t = gen_addr(g, e);
		load_rax(g, t);
		return;
	case EX_SIZEOF:
		emit(g, "    mov rax, %ld", type_size(e->sizeof_type ? e->sizeof_type : expr_type(g, e->left)));
		return;
	case EX_UNARY:
		if(!strcmp(e->op, "++") || !strcmp(e->op, "--") ||
		   !strcmp(e->op, "post++") || !strcmp(e->op, "post--"))
		{
			gen_incdec(g, e);
		} else if(!strcmp(e->op, "cast"))
		{
			CType *source = expr_type(g, e->left);
			gen_expr(g, e->left);
			if(e->type->kind == TY_DOUBLE)
			{
				if(!is_double_type(source))
					integer_bits_to_double(g);
			} else {
				if(is_double_type(source))
				{
					emit(g, "    movq xmm0, rax");
					emit(g, "    cvttsd2si rax, xmm0");
				}
				if(e->type->kind == TY_CHAR || e->type->kind == TY_U8)
					emit(g, "    movzx eax, al");
				else if(e->type->kind == TY_SHORT || e->type->kind == TY_U16)
					emit(g, "    movzx eax, ax");
				else if(e->type->kind == TY_INT)
					emit(g, "    movsxd rax, eax");
				else if(e->type->kind == TY_U32)
					emit(g, "    mov eax, eax");
			}
		} else if(!strcmp(e->op, "&"))
			gen_addr(g, e->left);
		else if(!strcmp(e->op, "*"))
		{
			gen_expr(g, e->left);
			load_rax(g, expr_type(g, e));
		} else {
			gen_expr(g, e->left);
			if(!strcmp(e->op, "!"))
			{
				emit(g, "    test rax, rax");
				emit(g, "    sete al");
				emit(g, "    movzx rax, al");
			} else if(!strcmp(e->op, "~"))
				emit(g, "    not rax");
			else if(!strcmp(e->op, "-"))
				emit(g, "    neg rax");
		}
		return;
	case EX_INDEX:
	case EX_MEMBER:
	case EX_PTRMEMBER:
		t = gen_addr(g, e);
		load_rax(g, t);
		return;
	case EX_BINARY:
		gen_binary(g, e);
		return;
	case EX_CALL:
		gen_call(g, e);
		return;
	case EX_INITLIST:
		fatal("initializer list used as an expression");
		return;
	}
	fatal("unsupported expression");
}

static long collect_locals(Gen *g, Stmt *s, long off)
{
	size_t i;
	Decl *d;
	if(!s)
		return off;
	if(s->kind == ST_DECL)
	{
		d = s->decl;
		if(find_local(g, d->name))
			fatal("duplicate local %s", d->name);
		off = align_up(off, type_align(d->type));
		off += type_size(d->type) > 0 ? type_size(d->type) : 1;
		ARR_GROW(g->locals, g->nlocals, g->caplocals, Symbol);
		g->locals[g->nlocals++] = (Symbol){d->name, d->type, SY_LOCAL, off, NULL};
	} else if(s->kind == ST_BLOCK)
		for(i = 0; i < s->nchildren; i++)
			off = collect_locals(g, s->children[i], off);
	else if(s->kind == ST_IF)
	{
		off = collect_locals(g, s->yes, off);
		off = collect_locals(g, s->no, off);
	} else if(s->kind == ST_WHILE)
		off = collect_locals(g, s->body, off);
	else if(s->kind == ST_FOR)
	{
		off = collect_locals(g, s->init, off);
		off = collect_locals(g, s->body, off);
	}
	return off;
}

static void gen_stmt(Gen *g, Stmt *s)
{
	size_t i;
	Symbol *x;
	char *a, *d;
	if(!s)
		return;
	switch(s->kind)
	{
	case ST_BLOCK:
		for(i = 0; i < s->nchildren; i++)
			gen_stmt(g, s->children[i]);
		return;
	case ST_DECL:
		if(s->decl->init)
		{
			x = find_local(g, s->decl->name);
			if(is_double_type(x->type))
				gen_expr_as_double(g, s->decl->init);
			else
				gen_expr(g, s->decl->init);
			emit(g, "    lea rcx, [rbp-%ld]", x->off);
			store_rcx(g, x->type);
		}
		return;
	case ST_EXPR:
		gen_expr(g, s->expr);
		return;
	case ST_EMPTY:
		return;
	case ST_RETURN:
		if(s->expr)
		{
			if(g->current && is_double_type(g->current->type))
			{
				gen_expr_as_double(g, s->expr);
				emit(g, "    movq xmm0, rax");
			} else
				gen_expr(g, s->expr);
		} else
			emit(g, "    xor eax, eax");
		emit(g, "    jmp %s", g->retlabel);
		return;
	case ST_IF:
		a = new_label(g, ".Lelse");
		d = new_label(g, ".Lifend");
		gen_expr(g, s->cond);
		emit(g, "    test rax, rax");
		emit(g, "    jz %s", a);
		gen_stmt(g, s->yes);
		emit(g, "    jmp %s", d);
		emit(g, "%s:", a);
		gen_stmt(g, s->no);
		emit(g, "%s:", d);
		return;
	case ST_WHILE:
		a = new_label(g, ".Lwhile");
		d = new_label(g, ".Lwend");
		ARR_GROW(g->breaks, g->nbreaks, g->capbreaks, char *);
		g->breaks[g->nbreaks++] = d;
		ARR_GROW(g->continues, g->ncontinues, g->capcontinues, char *);
		g->continues[g->ncontinues++] = a;
		emit(g, "%s:", a);
		gen_expr(g, s->cond);
		emit(g, "    test rax, rax");
		emit(g, "    jz %s", d);
		gen_stmt(g, s->body);
		emit(g, "    jmp %s", a);
		emit(g, "%s:", d);
		g->nbreaks--;
		g->ncontinues--;
		return;
	case ST_FOR:
	{
		char *c = new_label(g, ".Lforcond");
		char *n = new_label(g, ".Lfornext");
		d = new_label(g, ".Lforend");
		gen_stmt(g, s->init);
		ARR_GROW(g->breaks, g->nbreaks, g->capbreaks, char *);
		g->breaks[g->nbreaks++] = d;
		ARR_GROW(g->continues, g->ncontinues, g->capcontinues, char *);
		g->continues[g->ncontinues++] = n;
		emit(g, "%s:", c);
		if(s->cond)
		{
			gen_expr(g, s->cond);
			emit(g, "    test rax, rax");
			emit(g, "    jz %s", d);
		}
		gen_stmt(g, s->body);
		emit(g, "%s:", n);
		if(s->post)
			gen_expr(g, s->post);
		emit(g, "    jmp %s", c);
		emit(g, "%s:", d);
		g->nbreaks--;
		g->ncontinues--;
		return;
	}
	case ST_SWITCH:
	case ST_CASE:
	case ST_DEFAULT:
		fatal("switch is supported by the i386 object backend only");
		return;
	case ST_BREAK:
		if(!g->nbreaks)
			fatal("break outside loop");
		emit(g, "    jmp %s", g->breaks[g->nbreaks - 1]);
		return;
	case ST_CONTINUE:
		if(!g->ncontinues)
			fatal("continue outside loop");
		emit(g, "    jmp %s", g->continues[g->ncontinues - 1]);
		return;
	case ST_ASM:
		if(!strcmp(s->asm_text, "hlt") || !strcmp(s->asm_text, "cli") ||
		   !strcmp(s->asm_text, "sti") || !strcmp(s->asm_text, "nop") ||
		   !strcmp(s->asm_text, "cld") || !strcmp(s->asm_text, "std") ||
		   !strcmp(s->asm_text, "int3") || !strcmp(s->asm_text, "pause") ||
		   !strcmp(s->asm_text, "ud2"))
			emit(g, "    %s", s->asm_text);
		else
			fatal("unsupported inline asm instruction: %s", s->asm_text);
		return;
	}
}

static void gen_function(Gen *g, Decl *d)
{
	size_t i;
	long off = 0;
	static const char *regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
	Symbol *s;
	g->current = d;
	g->nlocals = 0;
	g->tempdepth = 0;
	g->vaoff = 0;
	for(i = 0; i < d->nparams; i++)
	{
		off = align_up(off, 8) + 8;
		ARR_GROW(g->locals, g->nlocals, g->caplocals, Symbol);
		g->locals[g->nlocals++] = (Symbol){d->params[i].name, d->params[i].type, SY_LOCAL, off, NULL};
	}
	off = collect_locals(g, d->body, off);
	if(d->variadic)
	{
		off = align_up(off, 16) + 176;
		g->vaoff = off;
	}
	g->frame = align_up(off, 16);
	snprintf(g->retlabel, sizeof(g->retlabel), ".Lreturn_%s_%ld", d->name, ++g->label);
	emit(g, ".text");
	if(!d->is_static)
		emit(g, ".globl %s", d->name);
	emit(g, ".type %s, @function", d->name);
	emit(g, "%s:", d->name);
	emit(g, "    push rbp");
	emit(g, "    mov rbp, rsp");
	if(g->frame)
		emit(g, "    sub rsp, %ld", g->frame);
	{
		size_t integer_parameter = 0;
		size_t double_parameter = 0;

		for(i = 0; i < d->nparams; i++)
		{
			s = find_local(g, d->params[i].name);
			if(is_double_type(d->params[i].type))
			{
				if(double_parameter >= 8)
					fatal("native functions currently support up to 8 double register parameters");
				emit(g, "    movq rax, xmm%zu", double_parameter++);
				emit(g, "    mov qword ptr [rbp-%ld], rax", s->off);
			} else if(integer_parameter < 6)
			{
				emit(g, "    mov qword ptr [rbp-%ld], %s", s->off, regs[integer_parameter++]);
			} else {
				emit(g, "    mov rax, qword ptr [rbp+%zu]", 16 + (integer_parameter - 6) * 8);
				emit(g, "    mov qword ptr [rbp-%ld], rax", s->off);
				integer_parameter++;
			}
		}
	}
	if(d->variadic)
		for(i = 0; i < 6; i++)
			emit(g, "    mov qword ptr [rbp-%ld], %s", g->vaoff - (long)i * 8, regs[i]);
	gen_stmt(g, d->body);
	emit(g, "    xor eax, eax");
	emit(g, "%s:", g->retlabel);
	emit(g, "    leave");
	emit(g, "    ret");
	emit(g, ".size %s, .-%s", d->name, d->name);
}

static char *generate(Program *p)
{
	Gen g = {0};
	size_t i;
	Decl *d;

	g.prog = p;
	for(i = 0; i < p->n; i++)
	{
		d = p->a[i];
		if(d->body || d->prototype)
		{
			ARR_GROW(g.funcs, g.nfuncs, g.capfuncs, Decl *);
			g.funcs[g.nfuncs++] = d;
		} else {
			ARR_GROW(g.globals, g.nglobals, g.capglobals, Symbol);
			g.globals[g.nglobals++] = (Symbol){
				d->name, d->type, d->is_extern ? SY_EXTERN_GLOBAL : SY_GLOBAL, 0, NULL};
		}
	}
	emit(&g, ".intel_syntax noprefix");
	emit(&g, ".text");
	for(i = 0; i < p->n; i++)
		if(p->a[i]->body)
			gen_function(&g, p->a[i]);
	if(g.nglobals)
	{
		bool emitted_bss = false;

		for(i = 0; i < g.nglobals; i++)
		{
			Symbol *sym = &g.globals[i];

			if(sym->kind != SY_GLOBAL)
				continue;
			if(!emitted_bss)
			{
				emit(&g, ".bss");
				emitted_bss = true;
			}
			emit(&g, ".globl %s", sym->name);
			emit(&g, ".align %ld", type_align(sym->type));
			emit(&g, "%s:", sym->name);
			emit(&g, "    .zero %ld", type_size(sym->type) > 0 ? type_size(sym->type) : 1);
		}
	}
	if(g.ro.n)
	{
		emit(&g, ".section .rodata");
		bputn(&g.out, g.ro.s, g.ro.n);
	}
	emit(&g, ".section .note.GNU-stack,\"\",@progbits");
	return g.out.s;
}

/* ---------- Internal x86-64 assembler and ELF writer ---------- */

typedef enum
{
	ASEC_TEXT,
	ASEC_RODATA,
	ASEC_BSS
} ASection;

typedef struct
{
	char *name;
	ASection section;
	uint64_t offset;
} ALabel;

typedef enum
{
	FIX_REL32_SYMBOL,
	FIX_RIP32_SYMBOL,
	FIX_RIP32_OBJECT,
	FIX_RIP32_GOT
} FixKind;

typedef struct
{
	FixKind kind;
	uint64_t offset;
	char *name;
	long aux;
} Fixup;

typedef struct
{
	char *name;
	bool object;
	uint32_t symbol_index;
	uint32_t got_index;
	uint32_t plt_reloc_index;
	uint64_t plt_offset;
	uint32_t string_offset;
} Import;

typedef struct
{
	Buf text;
	Buf rodata;
	uint64_t bss_size;
	ASection section;
	ALabel *labels;
	size_t nlabels;
	size_t caplabels;
	Fixup *fixups;
	size_t nfixups;
	size_t capfixups;
	Import *imports;
	size_t nimports;
	size_t capimports;
	uint64_t plt0_offset;
} AsmImage;

static uint64_t ualign(uint64_t value, uint64_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

static void put_u8(Buf *b, uint8_t value)
{
	bputn(b, (char *)&value, 1);
}

static void put_u32(Buf *b, uint32_t value)
{
	uint8_t bytes[4];

	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
	bputn(b, (char *)bytes, sizeof(bytes));
}

static void put_u64(Buf *b, uint64_t value)
{
	uint8_t bytes[8];
	int i;

	for(i = 0; i < 8; i++)
		bytes[i] = (uint8_t)(value >> (i * 8));
	bputn(b, (char *)bytes, sizeof(bytes));
}

static void patch_u32(Buf *b, uint64_t offset, uint32_t value)
{
	if(offset + 4 > b->n)
		fatal("internal: patch outside code buffer");
	b->s[offset + 0] = (char)value;
	b->s[offset + 1] = (char)(value >> 8);
	b->s[offset + 2] = (char)(value >> 16);
	b->s[offset + 3] = (char)(value >> 24);
}

static Buf *current_buffer(AsmImage *a)
{
	if(a->section == ASEC_TEXT)
		return &a->text;
	if(a->section == ASEC_RODATA)
		return &a->rodata;
	fatal("internal: attempted to emit bytes into BSS");
	return NULL;
}

static uint64_t current_offset(AsmImage *a)
{
	if(a->section == ASEC_TEXT)
		return a->text.n;
	if(a->section == ASEC_RODATA)
		return a->rodata.n;
	return a->bss_size;
}

static void add_label(AsmImage *a, const char *name)
{
	size_t i;

	for(i = 0; i < a->nlabels; i++)
		if(!strcmp(a->labels[i].name, name))
			fatal("duplicate assembly label %s", name);
	ARR_GROW(a->labels, a->nlabels, a->caplabels, ALabel);
	a->labels[a->nlabels].name = xstrdup(name);
	a->labels[a->nlabels].section = a->section;
	a->labels[a->nlabels].offset = current_offset(a);
	a->nlabels++;
}

static ALabel *find_label(AsmImage *a, const char *name)
{
	size_t i;

	for(i = 0; i < a->nlabels; i++)
		if(!strcmp(a->labels[i].name, name))
			return &a->labels[i];
	return NULL;
}

static Import *find_import(AsmImage *a, const char *name)
{
	size_t i;

	for(i = 0; i < a->nimports; i++)
		if(!strcmp(a->imports[i].name, name))
			return &a->imports[i];
	return NULL;
}

static Import *add_import(AsmImage *a, const char *name, bool object)
{
	Import *import = find_import(a, name);

	if(import)
	{
		if(object)
			import->object = true;
		return import;
	}
	ARR_GROW(a->imports, a->nimports, a->capimports, Import);
	import = &a->imports[a->nimports++];
	memset(import, 0, sizeof(*import));
	import->name = xstrdup(name);
	import->object = object;
	return import;
}

static void add_fixup(AsmImage *a, FixKind kind, uint64_t offset, const char *name, long aux)
{
	ARR_GROW(a->fixups, a->nfixups, a->capfixups, Fixup);
	a->fixups[a->nfixups].kind = kind;
	a->fixups[a->nfixups].offset = offset;
	a->fixups[a->nfixups].name = name ? xstrdup(name) : NULL;
	a->fixups[a->nfixups].aux = aux;
	a->nfixups++;
}

static int register_number(const char *name)
{
	static const char *names[] = {
		"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
	int i;

	for(i = 0; i < 16; i++)
		if(!strcmp(name, names[i]))
			return i;
	return -1;
}

static int xmm_number(const char *name)
{
	char *end;
	long number;

	if(strncmp(name, "xmm", 3))
		return -1;
	number = strtol(name + 3, &end, 10);
	if(*end || number < 0 || number > 15)
		return -1;
	return (int)number;
}

static void emit_rex(Buf *b, bool w, int reg, int index, int base)
{
	uint8_t rex = 0x40;

	if(w)
		rex |= 8;
	if(reg & 8)
		rex |= 4;
	if(index & 8)
		rex |= 2;
	if(base & 8)
		rex |= 1;
	if(rex != 0x40)
		put_u8(b, rex);
}

static void emit_modrm(Buf *b, int mod, int reg, int rm)
{
	put_u8(b, (uint8_t)((mod << 6) | ((reg & 7) << 3) | (rm & 7)));
}

static void emit_reg_reg(Buf *b, uint8_t opcode, int destination, int source)
{
	emit_rex(b, true, source, 0, destination);
	put_u8(b, opcode);
	emit_modrm(b, 3, source, destination);
}

static void emit_memory_operand(Buf *b, int reg, int base, long displacement)
{
	int mod;

	if(displacement == 0 && (base & 7) != 5)
		mod = 0;
	else if(displacement >= -128 && displacement <= 127)
		mod = 1;
	else
		mod = 2;
	emit_modrm(b, mod, reg, base);
	if((base & 7) == 4)
		put_u8(b, 0x24);
	if(mod == 1)
		put_u8(b, (uint8_t)displacement);
	else if(mod == 2 || (mod == 0 && (base & 7) == 5))
		put_u32(b, (uint32_t)displacement);
}

static void emit_mov_memory_register(Buf *b, int base, long displacement, int source, int size)
{
	if(size == 8)
	{
		emit_rex(b, true, source, 0, base);
		put_u8(b, 0x89);
	} else if(size == 4)
	{
		emit_rex(b, false, source, 0, base);
		put_u8(b, 0x89);
	} else {
		emit_rex(b, false, source, 0, base);
		put_u8(b, 0x88);
	}
	emit_memory_operand(b, source, base, displacement);
}

static void emit_mov_register_memory(Buf *b, int destination, int base, long displacement, int size)
{
	if(size == 8)
	{
		emit_rex(b, true, destination, 0, base);
		put_u8(b, 0x8b);
	} else {
		emit_rex(b, false, destination, 0, base);
		put_u8(b, 0x8b);
	}
	emit_memory_operand(b, destination, base, displacement);
}

static void emit_lea_memory(Buf *b, int destination, int base, long displacement)
{
	emit_rex(b, true, destination, 0, base);
	put_u8(b, 0x8d);
	emit_memory_operand(b, destination, base, displacement);
}

static char *trim(char *line)
{
	char *end;

	while(*line && isspace((unsigned char)*line))
		line++;
	end = line + strlen(line);
	while(end > line && isspace((unsigned char)end[-1]))
		*--end = 0;
	return line;
}

static void emit_relative_fixup(AsmImage *a, uint8_t opcode, const char *name, bool conditional, uint8_t condition)
{
	Buf *b = &a->text;

	if(conditional)
	{
		put_u8(b, 0x0f);
		put_u8(b, condition);
	} else {
		put_u8(b, opcode);
	}
	add_fixup(a, FIX_REL32_SYMBOL, b->n, name, 0);
	put_u32(b, 0);
}

static void assemble_instruction(AsmImage *a, char *line)
{
	Buf *b = current_buffer(a);
	char left[128], right[128], name[256], reg1[32], reg2[32];
	long long signed_value;
	unsigned long long unsigned_value;
	long displacement;
	int r1, r2;

	if(!strcmp(line, "push rbp"))
	{
		put_u8(b, 0x55);
		return;
	}
	if(!strcmp(line, "leave"))
	{
		put_u8(b, 0xc9);
		return;
	}
	if(!strcmp(line, "ret"))
	{
		put_u8(b, 0xc3);
		return;
	}
	if(!strcmp(line, "hlt"))
	{
		put_u8(b, 0xf4);
		return;
	}
	if(!strcmp(line, "cli"))
	{
		put_u8(b, 0xfa);
		return;
	}
	if(!strcmp(line, "sti"))
	{
		put_u8(b, 0xfb);
		return;
	}
	if(!strcmp(line, "nop"))
	{
		put_u8(b, 0x90);
		return;
	}
	if(!strcmp(line, "cld"))
	{
		put_u8(b, 0xfc);
		return;
	}
	if(!strcmp(line, "std"))
	{
		put_u8(b, 0xfd);
		return;
	}
	if(!strcmp(line, "int3"))
	{
		put_u8(b, 0xcc);
		return;
	}
	if(!strcmp(line, "pause"))
	{
		put_u8(b, 0xf3);
		put_u8(b, 0x90);
		return;
	}
	if(!strcmp(line, "ud2"))
	{
		put_u8(b, 0x0f);
		put_u8(b, 0x0b);
		return;
	}
	if(!strcmp(line, "cqo"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0x99);
		return;
	}
	if(!strcmp(line, "xor eax, eax"))
	{
		put_u8(b, 0x31);
		put_u8(b, 0xc0);
		return;
	}
	if(!strcmp(line, "test rax, rax"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0x85);
		put_u8(b, 0xc0);
		return;
	}
	if(!strcmp(line, "cmp rcx, rax"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0x39);
		put_u8(b, 0xc1);
		return;
	}
	if(!strcmp(line, "movzx eax, byte ptr [rax]"))
	{
		put_u8(b, 0x0f);
		put_u8(b, 0xb6);
		put_u8(b, 0x00);
		return;
	}
	if(!strcmp(line, "movzx eax, word ptr [rax]"))
	{
		put_u8(b, 0x0f);
		put_u8(b, 0xb7);
		put_u8(b, 0x00);
		return;
	}
	if(!strcmp(line, "mov eax, dword ptr [rax]"))
	{
		put_u8(b, 0x8b);
		put_u8(b, 0x00);
		return;
	}
	if(!strcmp(line, "movsxd rax, dword ptr [rax]"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0x63);
		put_u8(b, 0x00);
		return;
	}
	if(!strcmp(line, "mov rax, qword ptr [rax]"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0x8b);
		put_u8(b, 0x00);
		return;
	}
	if(!strcmp(line, "mov byte ptr [rcx], al"))
	{
		put_u8(b, 0x88);
		put_u8(b, 0x01);
		return;
	}
	if(!strcmp(line, "mov word ptr [rcx], ax"))
	{
		put_u8(b, 0x66);
		put_u8(b, 0x89);
		put_u8(b, 0x01);
		return;
	}
	if(!strcmp(line, "mov dword ptr [rcx], eax"))
	{
		put_u8(b, 0x89);
		put_u8(b, 0x01);
		return;
	}
	if(!strcmp(line, "mov qword ptr [rcx], rax"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0x89);
		put_u8(b, 0x01);
		return;
	}
	if(!strcmp(line, "movzx rax, al"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0x0f);
		put_u8(b, 0xb6);
		put_u8(b, 0xc0);
		return;
	}
	if(!strcmp(line, "movzx eax, al"))
	{
		put_u8(b, 0x0f);
		put_u8(b, 0xb6);
		put_u8(b, 0xc0);
		return;
	}
	if(!strcmp(line, "movzx eax, ax"))
	{
		put_u8(b, 0x0f);
		put_u8(b, 0xb7);
		put_u8(b, 0xc0);
		return;
	}
	if(!strcmp(line, "movsxd rax, eax"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0x63);
		put_u8(b, 0xc0);
		return;
	}
	if(!strcmp(line, "mov eax, eax"))
	{
		put_u8(b, 0x89);
		put_u8(b, 0xc0);
		return;
	}
	if(sscanf(line, "movq %31[^,], %31s", reg1, reg2) == 2)
	{
		int xd = xmm_number(reg1);
		int xs = xmm_number(reg2);
		r1 = register_number(reg1);
		r2 = register_number(reg2);
		if(xd >= 0 && r2 >= 0)
		{
			put_u8(b, 0x66);
			emit_rex(b, true, xd, 0, r2);
			put_u8(b, 0x0f);
			put_u8(b, 0x6e);
			emit_modrm(b, 3, xd, r2);
			return;
		}
		if(r1 >= 0 && xs >= 0)
		{
			put_u8(b, 0x66);
			emit_rex(b, true, xs, 0, r1);
			put_u8(b, 0x0f);
			put_u8(b, 0x7e);
			emit_modrm(b, 3, xs, r1);
			return;
		}
	}
	if(sscanf(line, "cvtsi2sd %31[^,], %31s", reg1, reg2) == 2)
	{
		int xd = xmm_number(reg1);
		r2 = register_number(reg2);
		if(xd >= 0 && r2 >= 0)
		{
			put_u8(b, 0xf2);
			emit_rex(b, true, xd, 0, r2);
			put_u8(b, 0x0f);
			put_u8(b, 0x2a);
			emit_modrm(b, 3, xd, r2);
			return;
		}
	}
	if(sscanf(line, "cvttsd2si %31[^,], %31s", reg1, reg2) == 2)
	{
		r1 = register_number(reg1);
		int xs = xmm_number(reg2);
		if(r1 >= 0 && xs >= 0)
		{
			put_u8(b, 0xf2);
			emit_rex(b, true, r1, 0, xs);
			put_u8(b, 0x0f);
			put_u8(b, 0x2c);
			emit_modrm(b, 3, r1, xs);
			return;
		}
	}
	{
		char mnemonic[16];
		if(sscanf(line, "%15s %31[^,], %31s", mnemonic, reg1, reg2) == 3)
		{
			int xd = xmm_number(reg1);
			int xs = xmm_number(reg2);
			uint8_t opcode = 0;
			if(!strcmp(mnemonic, "addsd"))
				opcode = 0x58;
			else if(!strcmp(mnemonic, "subsd"))
				opcode = 0x5c;
			else if(!strcmp(mnemonic, "mulsd"))
				opcode = 0x59;
			else if(!strcmp(mnemonic, "divsd"))
				opcode = 0x5e;
			if(opcode && xd >= 0 && xs >= 0)
			{
				put_u8(b, 0xf2);
				emit_rex(b, false, xd, 0, xs);
				put_u8(b, 0x0f);
				put_u8(b, opcode);
				emit_modrm(b, 3, xd, xs);
				return;
			}
		}
	}
	if(!strcmp(line, "not rax"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0xf7);
		put_u8(b, 0xd0);
		return;
	}
	if(!strcmp(line, "neg rax"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0xf7);
		put_u8(b, 0xd8);
		return;
	}
	if(!strcmp(line, "idiv r10"))
	{
		put_u8(b, 0x49);
		put_u8(b, 0xf7);
		put_u8(b, 0xfa);
		return;
	}
	if(!strcmp(line, "shl rax, cl"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0xd3);
		put_u8(b, 0xe0);
		return;
	}
	if(!strcmp(line, "sar rax, cl"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0xd3);
		put_u8(b, 0xf8);
		return;
	}
	if(!strncmp(line, "set", 3) && strstr(line, " al"))
	{
		uint8_t condition;
		char cc[8];

		if(sscanf(line, "set%7s al", cc) != 1)
			fatal("internal assembler: %s", line);
		if(!strcmp(cc, "e"))
			condition = 0x94;
		else if(!strcmp(cc, "ne"))
			condition = 0x95;
		else if(!strcmp(cc, "l"))
			condition = 0x9c;
		else if(!strcmp(cc, "le"))
			condition = 0x9e;
		else if(!strcmp(cc, "g"))
			condition = 0x9f;
		else if(!strcmp(cc, "ge"))
			condition = 0x9d;
		else
			fatal("unsupported condition code %s", cc);
		put_u8(b, 0x0f);
		put_u8(b, condition);
		put_u8(b, 0xc0);
		return;
	}
	if(sscanf(line, "push %31s", reg1) == 1 &&
	   register_number(reg1) >= 0)
	{
		r1 = register_number(reg1);
		if(r1 >= 8)
			put_u8(b, 0x41);
		put_u8(b, (uint8_t)(0x50 + (r1 & 7)));
		return;
	}
	if(sscanf(line, "pop %31s", reg1) == 1 &&
	   register_number(reg1) >= 0)
	{
		r1 = register_number(reg1);
		if(r1 >= 8)
			put_u8(b, 0x41);
		put_u8(b, (uint8_t)(0x58 + (r1 & 7)));
		return;
	}
	if(sscanf(line, "jmp %255s", name) == 1)
	{
		emit_relative_fixup(a, 0xe9, name, false, 0);
		return;
	}
	if(sscanf(line, "jz %255s", name) == 1)
	{
		emit_relative_fixup(a, 0, name, true, 0x84);
		return;
	}
	if(sscanf(line, "jnz %255s", name) == 1)
	{
		emit_relative_fixup(a, 0, name, true, 0x85);
		return;
	}
	if(sscanf(line, "call %255s", name) == 1)
	{
		char *suffix = strstr(name, "@PLT");

		if(suffix)
			*suffix = 0;
		put_u8(b, 0xe8);
		add_fixup(a, FIX_REL32_SYMBOL, b->n, name, 0);
		put_u32(b, 0);
		add_import(a, name, false);
		return;
	}
	if(sscanf(line, "mov %31[^,], %31s", reg1, reg2) == 2)
	{
		r1 = register_number(reg1);
		r2 = register_number(reg2);
		if(r1 >= 0 && r2 >= 0)
		{
			emit_reg_reg(b, 0x89, r1, r2);
			return;
		}
	}
	if(sscanf(line, "mov rax, %llu", &unsigned_value) == 1)
	{
		put_u8(b, 0x48);
		put_u8(b, 0xb8);
		put_u64(b, (uint64_t)unsigned_value);
		return;
	}
	if(sscanf(line, "mov eax, %lld", &signed_value) == 1)
	{
		put_u8(b, 0xb8);
		put_u32(b, (uint32_t)signed_value);
		return;
	}
	if(sscanf(line, "lea %31[^,], [rbp-%ld]", reg1, &displacement) == 2)
	{
		r1 = register_number(reg1);
		if(r1 < 0)
			fatal("bad register in %s", line);
		emit_lea_memory(b, r1, 5, -displacement);
		return;
	}
	if(sscanf(line, "lea %31[^,], [rbp+%ld]", reg1, &displacement) == 2)
	{
		r1 = register_number(reg1);
		if(r1 < 0)
			fatal("bad register in %s", line);
		emit_lea_memory(b, r1, 5, displacement);
		return;
	}
	if(sscanf(line, "lea rax, [rip+%255[^]]]", name) == 1)
	{
		put_u8(b, 0x48);
		put_u8(b, 0x8d);
		put_u8(b, 0x05);
		add_fixup(a, FIX_RIP32_SYMBOL, b->n, name, 0);
		put_u32(b, 0);
		return;
	}
	if(sscanf(line, "movzx eax, byte ptr [rip+%255[^]]]", name) == 1)
	{
		put_u8(b, 0x48);
		put_u8(b, 0x8b);
		put_u8(b, 0x05);
		add_fixup(a, FIX_RIP32_OBJECT, b->n, name, 0);
		put_u32(b, 0);
		put_u8(b, 0x0f);
		put_u8(b, 0xb6);
		put_u8(b, 0x00);
		add_import(a, name, true);
		return;
	}
	if(sscanf(line, "movzx eax, word ptr [rip+%255[^]]]", name) == 1)
	{
		put_u8(b, 0x48);
		put_u8(b, 0x8b);
		put_u8(b, 0x05);
		add_fixup(a, FIX_RIP32_OBJECT, b->n, name, 0);
		put_u32(b, 0);
		put_u8(b, 0x0f);
		put_u8(b, 0xb7);
		put_u8(b, 0x00);
		add_import(a, name, true);
		return;
	}
	if(sscanf(line, "movsxd rax, dword ptr [rip+%255[^]]]", name) == 1)
	{
		put_u8(b, 0x48);
		put_u8(b, 0x8b);
		put_u8(b, 0x05);
		add_fixup(a, FIX_RIP32_OBJECT, b->n, name, 0);
		put_u32(b, 0);
		put_u8(b, 0x48);
		put_u8(b, 0x63);
		put_u8(b, 0x00);
		add_import(a, name, true);
		return;
	}
	if(sscanf(line, "mov eax, dword ptr [rip+%255[^]]]", name) == 1)
	{
		put_u8(b, 0x48);
		put_u8(b, 0x8b);
		put_u8(b, 0x05);
		add_fixup(a, FIX_RIP32_OBJECT, b->n, name, 0);
		put_u32(b, 0);
		put_u8(b, 0x8b);
		put_u8(b, 0x00);
		add_import(a, name, true);
		return;
	}
	if(sscanf(line, "mov rax, qword ptr [rip+%255[^]]]", name) == 1)
	{
		put_u8(b, 0x48);
		put_u8(b, 0x8b);
		put_u8(b, 0x05);
		add_fixup(a, FIX_RIP32_OBJECT, b->n, name, 0);
		put_u32(b, 0);
		put_u8(b, 0x48);
		put_u8(b, 0x8b);
		put_u8(b, 0x00);
		add_import(a, name, true);
		return;
	}
	if(sscanf(line, "mov rax, qword ptr [rbp+%ld]", &displacement) == 1)
	{
		emit_mov_register_memory(b, 0, 5, displacement, 8);
		return;
	}
	if(sscanf(line, "mov qword ptr [rbp-%ld], %31s", &displacement, reg1) == 2)
	{
		r1 = register_number(reg1);
		if(r1 < 0)
			fatal("bad register in %s", line);
		emit_mov_memory_register(b, 5, -displacement, r1, 8);
		return;
	}
	if(sscanf(line, "mov qword ptr [rax+%ld], %31s", &displacement, reg1) == 2)
	{
		r1 = register_number(reg1);
		if(r1 < 0)
			fatal("bad register in %s", line);
		emit_mov_memory_register(b, 0, displacement, r1, 8);
		return;
	}
	if(sscanf(line, "mov qword ptr [rax], %31s", reg1) == 1)
	{
		r1 = register_number(reg1);
		if(r1 < 0)
			fatal("bad register in %s", line);
		emit_mov_memory_register(b, 0, 0, r1, 8);
		return;
	}
	if(sscanf(line, "mov dword ptr [rax+%ld], %lld", &displacement, &signed_value) == 2)
	{
		put_u8(b, 0xc7);
		emit_memory_operand(b, 0, 0, displacement);
		put_u32(b, (uint32_t)signed_value);
		return;
	}
	if(sscanf(line, "mov dword ptr [rax], %lld", &signed_value) == 1)
	{
		put_u8(b, 0xc7);
		emit_memory_operand(b, 0, 0, 0);
		put_u32(b, (uint32_t)signed_value);
		return;
	}
	if(sscanf(line, "sub rsp, %lld", &signed_value) == 1 ||
	   sscanf(line, "add rsp, %lld", &signed_value) == 1)
	{
		bool add = !strncmp(line, "add", 3);

		put_u8(b, 0x48);
		if(signed_value >= -128 && signed_value <= 127)
		{
			put_u8(b, 0x83);
			put_u8(b, add ? 0xc4 : 0xec);
			put_u8(b, (uint8_t)signed_value);
		} else {
			put_u8(b, 0x81);
			put_u8(b, add ? 0xc4 : 0xec);
			put_u32(b, (uint32_t)signed_value);
		}
		return;
	}
	if(sscanf(line, "imul rax, %lld", &signed_value) == 1)
	{
		put_u8(b, 0x48);
		put_u8(b, 0x69);
		put_u8(b, 0xc0);
		put_u32(b, (uint32_t)signed_value);
		return;
	}
	if(sscanf(line, "add rax, %lld", &signed_value) == 1 ||
	   sscanf(line, "sub rax, %lld", &signed_value) == 1)
	{
		bool add = !strncmp(line, "add", 3);

		put_u8(b, 0x48);
		if(signed_value >= -128 && signed_value <= 127)
		{
			put_u8(b, 0x83);
			put_u8(b, add ? 0xc0 : 0xe8);
			put_u8(b, (uint8_t)signed_value);
		} else {
			put_u8(b, add ? 0x05 : 0x2d);
			put_u32(b, (uint32_t)signed_value);
		}
		return;
	}
	if(!strcmp(line, "add rax, rcx"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0x01);
		put_u8(b, 0xc8);
		return;
	}
	if(!strcmp(line, "sub rcx, rax"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0x29);
		put_u8(b, 0xc1);
		return;
	}
	if(!strcmp(line, "imul rax, rcx"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0x0f);
		put_u8(b, 0xaf);
		put_u8(b, 0xc1);
		return;
	}
	if(!strcmp(line, "and rax, rcx"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0x21);
		put_u8(b, 0xc8);
		return;
	}
	if(!strcmp(line, "or rax, rcx"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0x09);
		put_u8(b, 0xc8);
		return;
	}
	if(!strcmp(line, "xor rax, rcx"))
	{
		put_u8(b, 0x48);
		put_u8(b, 0x31);
		put_u8(b, 0xc8);
		return;
	}

	if(sscanf(line, "%127[^,], %127s", left, right) == 2)
		fatal("unsupported internal assembly instruction: %s, %s", left, right);
	fatal("unsupported internal assembly instruction: %s", line);
}

static void align_assembly_section(AsmImage *a, uint64_t alignment)
{
	if(a->section == ASEC_BSS)
	{
		a->bss_size = ualign(a->bss_size, alignment);
		return;
	}
	while(current_buffer(a)->n % alignment)
		put_u8(current_buffer(a), 0);
}

static void parse_byte_directive(Buf *b, const char *line)
{
	const char *p = line + 5;

	while(*p)
	{
		char *end;
		unsigned long value;

		while(*p == ' ' || *p == '\t' || *p == ',')
			p++;
		if(!*p)
			break;
		value = strtoul(p, &end, 0);
		if(end == p || value > 255)
			fatal("bad .byte directive");
		put_u8(b, (uint8_t)value);
		p = end;
	}
}

static void add_startup(AsmImage *a)
{
	Buf *b = &a->text;

	a->section = ASEC_TEXT;
	add_label(a, "_start");
	put_u8(b, 0x31);
	put_u8(b, 0xed);
	put_u8(b, 0x48);
	put_u8(b, 0x8b);
	put_u8(b, 0x3c);
	put_u8(b, 0x24);
	put_u8(b, 0x48);
	put_u8(b, 0x8d);
	put_u8(b, 0x74);
	put_u8(b, 0x24);
	put_u8(b, 0x08);
	put_u8(b, 0x48);
	put_u8(b, 0x83);
	put_u8(b, 0xe4);
	put_u8(b, 0xf0);
	put_u8(b, 0xe8);
	add_fixup(a, FIX_REL32_SYMBOL, b->n, "main", 0);
	put_u32(b, 0);
	put_u8(b, 0x89);
	put_u8(b, 0xc7);
	put_u8(b, 0xb8);
	put_u32(b, 60);
	put_u8(b, 0x0f);
	put_u8(b, 0x05);
}

static void internal_assemble(const char *assembly, AsmImage *a)
{
	char *copy = xstrdup(assembly);
	char *save = NULL;
	char *raw;

	memset(a, 0, sizeof(*a));
	add_startup(a);
	for(raw = strtok_r(copy, "\n", &save); raw;
	    raw = strtok_r(NULL, "\n", &save))
	{
		char *line = trim(raw);
		size_t length;

		if(!*line)
			continue;
		if(!strncmp(line, ".intel_syntax", 13) ||
		   !strncmp(line, ".globl", 6) ||
		   !strncmp(line, ".type", 5) ||
		   !strncmp(line, ".size", 5) ||
		   !strncmp(line, ".section .note", 14))
			continue;
		if(!strcmp(line, ".text"))
		{
			a->section = ASEC_TEXT;
			continue;
		}
		if(!strcmp(line, ".bss"))
		{
			a->section = ASEC_BSS;
			continue;
		}
		if(!strcmp(line, ".section .rodata"))
		{
			a->section = ASEC_RODATA;
			continue;
		}
		if(!strncmp(line, ".align ", 7))
		{
			align_assembly_section(a, strtoull(line + 7, NULL, 0));
			continue;
		}
		if(!strncmp(line, ".zero ", 6))
		{
			uint64_t count = strtoull(line + 6, NULL, 0);

			if(a->section == ASEC_BSS)
				a->bss_size += count;
			else
				while(count--)
					put_u8(current_buffer(a), 0);
			continue;
		}
		if(!strncmp(line, ".byte", 5))
		{
			parse_byte_directive(current_buffer(a), line);
			continue;
		}
		length = strlen(line);
		if(length && line[length - 1] == ':')
		{
			line[length - 1] = 0;
			add_label(a, line);
			continue;
		}
		if(a->section != ASEC_TEXT)
			fatal("instruction outside text section: %s", line);
		assemble_instruction(a, line);
	}
	free(copy);
}

static void append_plt(AsmImage *a)
{
	Buf *b = &a->text;
	size_t i;
	uint32_t relocation_index = 0;

	while(b->n & 15)
		put_u8(b, 0x90);
	a->plt0_offset = b->n;
	put_u8(b, 0xff);
	put_u8(b, 0x35);
	add_fixup(a, FIX_RIP32_GOT, b->n, NULL, 1);
	put_u32(b, 0);
	put_u8(b, 0xff);
	put_u8(b, 0x25);
	add_fixup(a, FIX_RIP32_GOT, b->n, NULL, 2);
	put_u32(b, 0);
	put_u8(b, 0x0f);
	put_u8(b, 0x1f);
	put_u8(b, 0x40);
	put_u8(b, 0x00);
	for(i = 0; i < a->nimports; i++)
	{
		Import *import = &a->imports[i];

		if(import->object)
			continue;
		import->plt_offset = b->n;
		import->plt_reloc_index = relocation_index++;
		put_u8(b, 0xff);
		put_u8(b, 0x25);
		add_fixup(a, FIX_RIP32_GOT, b->n, NULL, import->got_index);
		put_u32(b, 0);
		put_u8(b, 0x68);
		put_u32(b, import->plt_reloc_index);
		put_u8(b, 0xe9);
		add_fixup(a, FIX_REL32_SYMBOL, b->n, "@plt0", 0);
		put_u32(b, 0);
	}
}

static void prune_local_imports(AsmImage *a)
{
	size_t read_index;
	size_t write_index = 0;

	for(read_index = 0; read_index < a->nimports; read_index++)
	{
		Import *import = &a->imports[read_index];

		if(find_label(a, import->name))
			continue;
		if(write_index != read_index)
			a->imports[write_index] = *import;
		write_index++;
	}
	a->nimports = write_index;
}

static uint32_t sysv_hash(const unsigned char *name)
{
	uint32_t hash = 0;

	while(*name)
	{
		uint32_t high;

		hash = (hash << 4) + *name++;
		high = hash & 0xf0000000;
		if(high)
			hash ^= high >> 24;
		hash &= ~high;
	}
	return hash;
}

static uint64_t symbol_address(AsmImage *a, ALabel *label, uint64_t text_va, uint64_t rodata_va, uint64_t bss_va)
{
	(void)a;
	if(label->section == ASEC_TEXT)
		return text_va + label->offset;
	if(label->section == ASEC_RODATA)
		return rodata_va + label->offset;
	return bss_va + label->offset;
}

static Import *require_import(AsmImage *a, const char *name, bool object)
{
	Import *import = add_import(a, name, object);

	if(object)
		import->object = true;
	return import;
}

static void patch_code(AsmImage *a, uint64_t text_va, uint64_t rodata_va, uint64_t bss_va, uint64_t got_va)
{
	size_t i;

	for(i = 0; i < a->nfixups; i++)
	{
		Fixup *fix = &a->fixups[i];
		uint64_t place = text_va + fix->offset;
		uint64_t target = 0;
		int64_t relative;

		if(fix->kind == FIX_RIP32_GOT)
		{
			target = got_va + (uint64_t)fix->aux * 8;
		} else if(fix->name && !strcmp(fix->name, "@plt0"))
		{
			target = text_va + a->plt0_offset;
		} else {
			ALabel *label = fix->name ? find_label(a, fix->name) : NULL;

			if(label)
				target = symbol_address(a, label, text_va, rodata_va, bss_va);
			else
			{
				Import *import;

				if(fix->kind == FIX_RIP32_OBJECT)
					import = require_import(a, fix->name, true);
				else
					import = require_import(a, fix->name, false);
				if(fix->kind == FIX_RIP32_OBJECT)
					target = got_va + (uint64_t)import->got_index * 8;
				else
					target = text_va + import->plt_offset;
			}
		}
		relative = (int64_t)target - (int64_t)(place + 4);
		if(relative < INT32_MIN || relative > INT32_MAX)
			fatal("internal: relative relocation overflow");
		patch_u32(&a->text, fix->offset, (uint32_t)(int32_t)relative);
	}
}

static void copy_into(uint8_t *file, uint64_t offset, const void *data, size_t size)
{
	memcpy(file + offset, data, size);
}

static void write_independent_elf(const char *path, AsmImage *a, bool needs_x11, char **libraries, size_t library_count)
{
	const uint64_t base = 0x400000;
	const char interpreter[] = "/lib64/ld-linux-x86-64.so.2";
	const uint16_t phnum = 5;
	uint64_t cursor;
	uint64_t interp_off, text_off, rodata_off, dynstr_off, dynsym_off;
	uint64_t hash_off, rela_dyn_off, rela_plt_off, got_off, dynamic_off;
	uint64_t bss_off, file_size, memory_size;
	uint64_t text_va, rodata_va, bss_va, got_va, dynamic_va;
	Buf dynstr = {0};
	Elf64_Sym *dynsym;
	Elf64_Rela *rela_dyn;
	Elf64_Rela *rela_plt;
	Elf64_Dyn *dynamic;
	uint64_t *got;
	uint32_t *hash;
	uint32_t *buckets;
	uint32_t *chains;
	uint32_t symbol_count;
	uint32_t function_count = 0;
	uint32_t object_count = 0;
	uint32_t bucket_count;
	uint32_t dynamic_count;
	uint32_t needed_count = 1 + (needs_x11 ? 1 : 0) + (uint32_t)library_count;
	uint32_t *needed_offsets;
	uint8_t *file;
	Elf64_Ehdr *ehdr;
	Elf64_Phdr *phdr;
	size_t i;
	uint32_t next_got = 3;
	uint32_t next_symbol = 1;
	uint32_t next_function_relocation = 0;
	uint32_t next_object_relocation = 0;

	prune_local_imports(a);
	put_u8(&dynstr, 0);
	needed_offsets = xcalloc(needed_count, sizeof(*needed_offsets));
	needed_offsets[0] = dynstr.n;
	bputs(&dynstr, "libc.so.6");
	put_u8(&dynstr, 0);
	if(needs_x11)
	{
		needed_offsets[1] = dynstr.n;
		bputs(&dynstr, "libX11.so.6");
		put_u8(&dynstr, 0);
	}
	for(i = 0; i < library_count; i++)
	{
		char library_name[256];
		uint32_t index = 1 + (needs_x11 ? 1 : 0) + (uint32_t)i;
		const char *argument = libraries[i];

		if(!strncmp(argument, "-l", 2))
			argument += 2;
		if(strstr(argument, ".so"))
			snprintf(library_name, sizeof(library_name), "%s", argument);
		else
			snprintf(library_name, sizeof(library_name), "lib%s.so", argument);
		needed_offsets[index] = dynstr.n;
		bputs(&dynstr, library_name);
		put_u8(&dynstr, 0);
	}
	for(i = 0; i < a->nimports; i++)
	{
		Import *import = &a->imports[i];

		import->symbol_index = next_symbol++;
		import->got_index = next_got++;
		import->string_offset = dynstr.n;
		bputs(&dynstr, import->name);
		put_u8(&dynstr, 0);
		if(import->object)
			object_count++;
		else
			function_count++;
	}
	append_plt(a);

	symbol_count = (uint32_t)a->nimports + 1;
	dynsym = xcalloc(symbol_count, sizeof(*dynsym));
	for(i = 0; i < a->nimports; i++)
	{
		Import *import = &a->imports[i];
		Elf64_Sym *symbol = &dynsym[import->symbol_index];

		symbol->st_name = import->string_offset;
		symbol->st_info = ELF64_ST_INFO(STB_GLOBAL,
						import->object ? STT_OBJECT : STT_FUNC);
		symbol->st_other = STV_DEFAULT;
		symbol->st_shndx = SHN_UNDEF;
	}
	bucket_count = symbol_count ? symbol_count : 1;
	hash = xcalloc(2 + bucket_count + symbol_count, sizeof(*hash));
	hash[0] = bucket_count;
	hash[1] = symbol_count;
	buckets = hash + 2;
	chains = buckets + bucket_count;
	for(i = 1; i < symbol_count; i++)
	{
		uint32_t bucket = sysv_hash((unsigned char *)a->imports[i - 1].name) % bucket_count;

		if(!buckets[bucket])
			buckets[bucket] = (uint32_t)i;
		else
		{
			uint32_t link = buckets[bucket];

			while(chains[link])
				link = chains[link];
			chains[link] = (uint32_t)i;
		}
	}
	rela_dyn = xcalloc(object_count ? object_count : 1, sizeof(*rela_dyn));
	rela_plt = xcalloc(function_count ? function_count : 1, sizeof(*rela_plt));
	got = xcalloc(3 + a->nimports, sizeof(*got));

	cursor = ualign(sizeof(Elf64_Ehdr) + phnum * sizeof(Elf64_Phdr), 16);
	interp_off = cursor;
	cursor += sizeof(interpreter);
	text_off = ualign(cursor, 16);
	cursor = text_off + a->text.n;
	rodata_off = ualign(cursor, 16);
	cursor = rodata_off + a->rodata.n;
	dynstr_off = ualign(cursor, 8);
	cursor = dynstr_off + dynstr.n;
	dynsym_off = ualign(cursor, 8);
	cursor = dynsym_off + symbol_count * sizeof(Elf64_Sym);
	hash_off = ualign(cursor, 8);
	cursor = hash_off + (2 + bucket_count + symbol_count) * sizeof(uint32_t);
	rela_dyn_off = ualign(cursor, 8);
	cursor = rela_dyn_off + object_count * sizeof(Elf64_Rela);
	rela_plt_off = ualign(cursor, 8);
	cursor = rela_plt_off + function_count * sizeof(Elf64_Rela);
	got_off = ualign(cursor, 8);
	cursor = got_off + (3 + a->nimports) * sizeof(uint64_t);
	dynamic_count = needed_count + 12 + 1;
	dynamic_off = ualign(cursor, 8);
	cursor = dynamic_off + dynamic_count * sizeof(Elf64_Dyn);
	file_size = ualign(cursor, 16);
	bss_off = file_size;
	memory_size = bss_off + a->bss_size;

	text_va = base + text_off;
	rodata_va = base + rodata_off;
	bss_va = base + bss_off;
	got_va = base + got_off;
	dynamic_va = base + dynamic_off;
	got[0] = dynamic_va;
	for(i = 0; i < a->nimports; i++)
	{
		Import *import = &a->imports[i];

		if(import->object)
		{
			Elf64_Rela *relocation = &rela_dyn[next_object_relocation++];

			relocation->r_offset = got_va + (uint64_t)import->got_index * 8;
			relocation->r_info = ELF64_R_INFO(import->symbol_index,
							  R_X86_64_GLOB_DAT);
			relocation->r_addend = 0;
		} else {
			Elf64_Rela *relocation = &rela_plt[next_function_relocation++];

			relocation->r_offset = got_va + (uint64_t)import->got_index * 8;
			relocation->r_info = ELF64_R_INFO(import->symbol_index,
							  R_X86_64_JUMP_SLOT);
			relocation->r_addend = 0;
			got[import->got_index] = text_va + import->plt_offset + 6;
		}
	}
	patch_code(a, text_va, rodata_va, bss_va, got_va);

	dynamic = xcalloc(dynamic_count, sizeof(*dynamic));
	{
		uint32_t d = 0;

		for(i = 0; i < needed_count; i++)
		{
			dynamic[d].d_tag = DT_NEEDED;
			dynamic[d++].d_un.d_val = needed_offsets[i];
		}
		dynamic[d].d_tag = DT_HASH;
		dynamic[d++].d_un.d_ptr = base + hash_off;
		dynamic[d].d_tag = DT_STRTAB;
		dynamic[d++].d_un.d_ptr = base + dynstr_off;
		dynamic[d].d_tag = DT_SYMTAB;
		dynamic[d++].d_un.d_ptr = base + dynsym_off;
		dynamic[d].d_tag = DT_STRSZ;
		dynamic[d++].d_un.d_val = dynstr.n;
		dynamic[d].d_tag = DT_SYMENT;
		dynamic[d++].d_un.d_val = sizeof(Elf64_Sym);
		dynamic[d].d_tag = DT_PLTGOT;
		dynamic[d++].d_un.d_ptr = got_va;
		dynamic[d].d_tag = DT_PLTRELSZ;
		dynamic[d++].d_un.d_val = function_count * sizeof(Elf64_Rela);
		dynamic[d].d_tag = DT_PLTREL;
		dynamic[d++].d_un.d_val = DT_RELA;
		dynamic[d].d_tag = DT_JMPREL;
		dynamic[d++].d_un.d_ptr = base + rela_plt_off;
		dynamic[d].d_tag = DT_RELA;
		dynamic[d++].d_un.d_ptr = base + rela_dyn_off;
		dynamic[d].d_tag = DT_RELASZ;
		dynamic[d++].d_un.d_val = object_count * sizeof(Elf64_Rela);
		dynamic[d].d_tag = DT_RELAENT;
		dynamic[d++].d_un.d_val = sizeof(Elf64_Rela);
		dynamic[d].d_tag = DT_NULL;
	}

	file = xcalloc(file_size, 1);
	ehdr = (Elf64_Ehdr *)file;
	memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
	ehdr->e_ident[EI_CLASS] = ELFCLASS64;
	ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_ident[EI_OSABI] = ELFOSABI_SYSV;
	ehdr->e_type = ET_EXEC;
	ehdr->e_machine = EM_X86_64;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_entry = text_va;
	ehdr->e_phoff = sizeof(Elf64_Ehdr);
	ehdr->e_ehsize = sizeof(Elf64_Ehdr);
	ehdr->e_phentsize = sizeof(Elf64_Phdr);
	ehdr->e_phnum = phnum;
	phdr = (Elf64_Phdr *)(file + ehdr->e_phoff);
	phdr[0].p_type = PT_PHDR;
	phdr[0].p_flags = PF_R;
	phdr[0].p_offset = ehdr->e_phoff;
	phdr[0].p_vaddr = base + ehdr->e_phoff;
	phdr[0].p_paddr = phdr[0].p_vaddr;
	phdr[0].p_filesz = phnum * sizeof(Elf64_Phdr);
	phdr[0].p_memsz = phdr[0].p_filesz;
	phdr[0].p_align = 8;
	phdr[1].p_type = PT_INTERP;
	phdr[1].p_flags = PF_R;
	phdr[1].p_offset = interp_off;
	phdr[1].p_vaddr = base + interp_off;
	phdr[1].p_paddr = phdr[1].p_vaddr;
	phdr[1].p_filesz = sizeof(interpreter);
	phdr[1].p_memsz = sizeof(interpreter);
	phdr[1].p_align = 1;
	phdr[2].p_type = PT_LOAD;
	phdr[2].p_flags = PF_R | PF_W | PF_X;
	phdr[2].p_offset = 0;
	phdr[2].p_vaddr = base;
	phdr[2].p_paddr = base;
	phdr[2].p_filesz = file_size;
	phdr[2].p_memsz = memory_size;
	phdr[2].p_align = 0x1000;
	phdr[3].p_type = PT_DYNAMIC;
	phdr[3].p_flags = PF_R | PF_W;
	phdr[3].p_offset = dynamic_off;
	phdr[3].p_vaddr = dynamic_va;
	phdr[3].p_paddr = dynamic_va;
	phdr[3].p_filesz = dynamic_count * sizeof(Elf64_Dyn);
	phdr[3].p_memsz = phdr[3].p_filesz;
	phdr[3].p_align = 8;
	phdr[4].p_type = PT_GNU_STACK;
	phdr[4].p_flags = PF_R | PF_W;
	phdr[4].p_align = 16;

	copy_into(file, interp_off, interpreter, sizeof(interpreter));
	copy_into(file, text_off, a->text.s, a->text.n);
	copy_into(file, rodata_off, a->rodata.s, a->rodata.n);
	copy_into(file, dynstr_off, dynstr.s, dynstr.n);
	copy_into(file, dynsym_off, dynsym, symbol_count * sizeof(Elf64_Sym));
	copy_into(file, hash_off, hash, (2 + bucket_count + symbol_count) * sizeof(uint32_t));
	copy_into(file, rela_dyn_off, rela_dyn, object_count * sizeof(Elf64_Rela));
	copy_into(file, rela_plt_off, rela_plt, function_count * sizeof(Elf64_Rela));
	copy_into(file, got_off, got, (3 + a->nimports) * sizeof(uint64_t));
	copy_into(file, dynamic_off, dynamic, dynamic_count * sizeof(Elf64_Dyn));
	write_file(path, (char *)file, file_size);
	if(chmod(path, 0755))
		fatal("cannot make %s executable: %s", path, strerror(errno));
}

/* ---------- Internal i386 ELF32 relocatable backend ---------- */

typedef struct
{
	char *name;
	CType *type;
	bool function;
	bool defined;
	bool local;
	uint16_t section;
	uint32_t value;
	uint32_t size;
	uint32_t index;
} I386ObjSymbol;

typedef struct
{
	uint32_t offset;
	uint32_t type;
	char *symbol;
	uint16_t section_symbol;
	uint16_t target_section;
} I386Relocation;

typedef struct
{
	char *name;
	uint32_t offset;
	bool defined;
} I386Label;

typedef struct
{
	char *name;
	uint32_t offset;
} I386JumpFixup;

typedef struct
{
	char *name;
	CType *type;
	long offset;
} I386Local;

typedef struct
{
	char *value;
	uint32_t offset;
} I386String;

typedef struct
{
	Program *program;
	Buf text;
	Buf rodata;
	Buf data;
	uint32_t bss_size;
	I386ObjSymbol *symbols;
	size_t nsymbols;
	size_t capsymbols;
	I386Relocation *relocations;
	size_t nrelocations;
	size_t caprelocations;
	I386Label *labels;
	size_t nlabels;
	size_t caplabels;
	I386JumpFixup *jump_fixups;
	size_t njump_fixups;
	size_t capjump_fixups;
	I386Local *locals;
	size_t nlocals;
	size_t caplocals;
	I386String *strings;
	size_t nstrings;
	size_t capstrings;
	Decl *current;
	long frame_size;
	long label_number;
	char return_label[128];
	char **break_labels;
	size_t nbreak_labels;
	size_t capbreak_labels;
	char **continue_labels;
	size_t ncontinue_labels;
	size_t capcontinue_labels;
} I386Gen;

enum
{
	I386_SEC_NULL,
	I386_SEC_TEXT,
	I386_SEC_RODATA,
	I386_SEC_DATA,
	I386_SEC_BSS,
	I386_SEC_REL_TEXT,
	I386_SEC_REL_DATA,
	I386_SEC_SYMTAB,
	I386_SEC_STRTAB,
	I386_SEC_SHSTRTAB,
	I386_SEC_COUNT
};

static long i386_type_align(CType *type);

static long i386_type_size(CType *type)
{
	switch(type->kind)
	{
	case TY_VOID:
		return 0;
	case TY_CHAR:
	case TY_U8:
		return 1;
	case TY_SHORT:
	case TY_U16:
		return 2;
	case TY_INT:
	case TY_U32:
	case TY_PTR:
	case TY_OPAQUE:
		return 4;
	case TY_U64:
	case TY_DOUBLE:
		return 8;
	case TY_STRUCT:
	{
		long offset = 0;
		long maximum = 1;
		size_t i;

		for(i = 0; i < type->nmembers; i++)
		{
			long alignment = type->packed ? 1 : i386_type_align(type->members[i].type);

			offset = align_up(offset, alignment);
			offset += i386_type_size(type->members[i].type);
			if(alignment > maximum)
				maximum = alignment;
		}
		return type->packed ? offset : align_up(offset, maximum);
	}
	case TY_ARRAY:
		if(is_vla(type))
			return 4;
		return i386_type_size(type->base) * type->count;
	case TY_XEVENT:
		return 96;
	case TY_VALIST:
		return 4;
	}
	fatal("internal: unknown i386 type");
	return 0;
}

static long i386_type_align(CType *type)
{
	long size;

	if(type->kind == TY_ARRAY)
		return i386_type_align(type->base);
	if(type->kind == TY_STRUCT)
	{
		long maximum = 1;

		if(type->packed)
			return 1;
		size_t i;

		for(i = 0; i < type->nmembers; i++)
		{
			long alignment = i386_type_align(type->members[i].type);

			if(alignment > maximum)
				maximum = alignment;
		}
		return maximum > 4 ? 4 : maximum;
	}
	size = i386_type_size(type);
	if(size <= 1)
		return 1;
	if(size == 2)
		return 2;
	return 4;
}

static void i386_put8(Buf *buffer, uint8_t value)
{
	bputn(buffer, (char *)&value, 1);
}

static void i386_put16(Buf *buffer, uint16_t value)
{
	bputn(buffer, (char *)&value, 2);
}

static void i386_put32(Buf *buffer, uint32_t value)
{
	bputn(buffer, (char *)&value, 4);
}

static void i386_patch32(Buf *buffer, uint32_t offset, uint32_t value)
{
	if((uint64_t)offset + 4 > buffer->n)
		fatal("internal: i386 patch is outside section");
	memcpy(buffer->s + offset, &value, 4);
}

static uint32_t i386_align32(uint32_t value, uint32_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

static I386ObjSymbol *i386_find_object_symbol(I386Gen *gen,
					      const char *name)
{
	size_t i;

	for(i = 0; i < gen->nsymbols; i++)
		if(!strcmp(gen->symbols[i].name, name))
			return &gen->symbols[i];
	return NULL;
}

static I386ObjSymbol *i386_add_object_symbol(I386Gen *gen,
					     const char *name,
					     CType *type,
					     bool function)
{
	I386ObjSymbol *symbol = i386_find_object_symbol(gen, name);

	if(symbol)
	{
		if(function != symbol->function)
			fatal("symbol %s declared as both object and function", name);
		return symbol;
	}
	ARR_GROW(gen->symbols, gen->nsymbols, gen->capsymbols, I386ObjSymbol);
	symbol = &gen->symbols[gen->nsymbols++];
	memset(symbol, 0, sizeof(*symbol));
	symbol->name = xstrdup(name);
	symbol->type = type;
	symbol->function = function;
	return symbol;
}

static void i386_add_relocation(I386Gen *gen, uint16_t target_section, uint32_t offset, uint32_t type, const char *symbol, uint16_t section_symbol)
{
	I386Relocation *relocation;

	ARR_GROW(gen->relocations, gen->nrelocations, gen->caprelocations, I386Relocation);
	relocation = &gen->relocations[gen->nrelocations++];
	relocation->offset = offset;
	relocation->type = type;
	relocation->symbol = symbol ? xstrdup(symbol) : NULL;
	relocation->section_symbol = section_symbol;
	relocation->target_section = target_section;
}

static I386Label *i386_find_label(I386Gen *gen, const char *name)
{
	size_t i;

	for(i = 0; i < gen->nlabels; i++)
		if(!strcmp(gen->labels[i].name, name))
			return &gen->labels[i];
	return NULL;
}

static void i386_define_label(I386Gen *gen, const char *name)
{
	I386Label *label = i386_find_label(gen, name);

	if(!label)
	{
		ARR_GROW(gen->labels, gen->nlabels, gen->caplabels, I386Label);
		label = &gen->labels[gen->nlabels++];
		memset(label, 0, sizeof(*label));
		label->name = xstrdup(name);
	}
	if(label->defined)
		fatal("internal: duplicate i386 label %s", name);
	label->defined = true;
	label->offset = (uint32_t)gen->text.n;
}

static char *i386_new_label(I386Gen *gen, const char *prefix)
{
	char buffer[128];

	snprintf(buffer, sizeof(buffer), "%s%ld", prefix, ++gen->label_number);
	return xstrdup(buffer);
}

static void i386_add_jump_fixup(I386Gen *gen, const char *name, uint32_t offset)
{
	I386JumpFixup *fixup;

	ARR_GROW(gen->jump_fixups, gen->njump_fixups, gen->capjump_fixups, I386JumpFixup);
	fixup = &gen->jump_fixups[gen->njump_fixups++];
	fixup->name = xstrdup(name);
	fixup->offset = offset;
}

static void i386_emit_jump(I386Gen *gen, uint8_t condition, const char *label)
{
	uint32_t offset;

	if(condition == 0xff)
	{
		i386_put8(&gen->text, 0xe9);
	} else {
		i386_put8(&gen->text, 0x0f);
		i386_put8(&gen->text, condition);
	}
	offset = (uint32_t)gen->text.n;
	i386_put32(&gen->text, 0);
	i386_add_jump_fixup(gen, label, offset);
}

static void i386_patch_jumps(I386Gen *gen)
{
	size_t i;

	for(i = 0; i < gen->njump_fixups; i++)
	{
		I386JumpFixup *fixup = &gen->jump_fixups[i];
		I386Label *label = i386_find_label(gen, fixup->name);
		int64_t displacement;

		if(!label || !label->defined)
			fatal("internal: undefined i386 label %s", fixup->name);
		displacement = (int64_t)label->offset -
			       ((int64_t)fixup->offset + 4);
		i386_patch32(&gen->text, fixup->offset, (uint32_t)(int32_t)displacement);
	}
}

static I386Local *i386_find_local(I386Gen *gen, const char *name)
{
	size_t i;

	for(i = 0; i < gen->nlocals; i++)
		if(!strcmp(gen->locals[i].name, name))
			return &gen->locals[i];
	return NULL;
}

static Decl *i386_find_function(Program *program, const char *name)
{
	size_t i;
	Decl *prototype = NULL;

	for(i = 0; i < program->n; i++)
	{
		Decl *declaration = program->a[i];

		if(strcmp(declaration->name, name))
			continue;
		if(!(declaration->body || declaration->prototype))
			continue;
		if(declaration->body)
			return declaration;
		prototype = declaration;
	}
	return prototype;
}

static Decl *i386_find_global(Program *program, const char *name)
{
	size_t i;
	Decl *external = NULL;

	for(i = 0; i < program->n; i++)
	{
		Decl *declaration = program->a[i];

		if(strcmp(declaration->name, name))
			continue;
		if(declaration->body || declaration->prototype)
			continue;
		if(!declaration->is_extern)
			return declaration;
		external = declaration;
	}
	return external;
}

static StructMember *i386_find_struct_member(CType *type,
					     const char *name,
					     long *member_offset)
{
	long offset = 0;
	size_t i;

	if(!type || type->kind != TY_STRUCT)
		return NULL;
	for(i = 0; i < type->nmembers; i++)
	{
		long alignment = type->packed ? 1 : i386_type_align(type->members[i].type);

		offset = align_up(offset, alignment);
		if(!strcmp(type->members[i].name, name))
		{
			if(member_offset)
				*member_offset = offset;
			return &type->members[i];
		}
		offset += i386_type_size(type->members[i].type);
	}
	return NULL;
}

static CType *i386_expr_type(I386Gen *gen, Expr *expression)
{
	CType *type;
	Decl *declaration;
	I386Local *local;

	if(expression->type)
		return expression->type;
	switch(expression->kind)
	{
	case EX_ID:
		local = i386_find_local(gen, expression->str);
		if(local)
			return local->type;
		declaration = i386_find_global(gen->program, expression->str);
		if(declaration)
			return declaration->type;
		declaration = i386_find_function(gen->program, expression->str);
		if(declaration)
			return declaration->type;
		return &T_U32;
	case EX_STR:
		return ptr_to(&T_CHAR);
	case EX_NUM:
		return &T_U32;
	case EX_UNARY:
		if(!strcmp(expression->op, "&"))
			return ptr_to(i386_expr_type(gen, expression->left));
		if(!strcmp(expression->op, "*"))
		{
			type = i386_expr_type(gen, expression->left);
			return type->base ? type->base : &T_U32;
		}
		return i386_expr_type(gen, expression->left);
	case EX_INDEX:
		type = i386_expr_type(gen, expression->left);
		return type->base ? type->base : &T_U32;
	case EX_MEMBER:
	case EX_PTRMEMBER:
	{
		StructMember *member;

		type = i386_expr_type(gen, expression->left);
		if(expression->kind == EX_PTRMEMBER)
			type = type && type->kind == TY_PTR ? type->base : NULL;
		member = i386_find_struct_member(type, expression->str, NULL);
		if(!member)
			fatal("unknown struct member %s", expression->str);
		return member->type;
	}
	case EX_SIZEOF:
		return &T_U32;
	case EX_BINARY:
		if(!strcmp(expression->op, "==") ||
		   !strcmp(expression->op, "!=") ||
		   !strcmp(expression->op, "<") ||
		   !strcmp(expression->op, "<=") ||
		   !strcmp(expression->op, ">") ||
		   !strcmp(expression->op, ">=") ||
		   !strcmp(expression->op, "&&") ||
		   !strcmp(expression->op, "||"))
			return &T_INT;
		return i386_expr_type(gen, expression->left);
	case EX_CALL:
		if(expression->left->kind == EX_ID)
		{
			declaration = i386_find_function(gen->program,
							 expression->left->str);
			if(declaration)
				return declaration->type;
		}
		return &T_U32;
	case EX_INITLIST:
		return &T_VOID;
	}
	return &T_U32;
}

static uint32_t i386_intern_string(I386Gen *gen, const char *value)
{
	size_t i;
	uint32_t offset;

	for(i = 0; i < gen->nstrings; i++)
		if(!strcmp(gen->strings[i].value, value))
			return gen->strings[i].offset;
	offset = (uint32_t)gen->rodata.n;
	bputn(&gen->rodata, value, strlen(value) + 1);
	ARR_GROW(gen->strings, gen->nstrings, gen->capstrings, I386String);
	gen->strings[gen->nstrings].value = xstrdup(value);
	gen->strings[gen->nstrings].offset = offset;
	gen->nstrings++;
	return offset;
}

static void i386_emit_mov_eax_imm(I386Gen *gen, uint32_t value)
{
	i386_put8(&gen->text, 0xb8);
	i386_put32(&gen->text, value);
}

static void i386_emit_mov_eax_symbol(I386Gen *gen, const char *name)
{
	uint32_t offset;

	i386_put8(&gen->text, 0xb8);
	offset = (uint32_t)gen->text.n;
	i386_put32(&gen->text, 0);
	i386_add_relocation(gen, I386_SEC_TEXT, offset, R_386_32, name, 0);
}

static void i386_emit_mov_eax_rodata(I386Gen *gen, uint32_t addend)
{
	uint32_t offset;

	i386_put8(&gen->text, 0xb8);
	offset = (uint32_t)gen->text.n;
	i386_put32(&gen->text, addend);
	i386_add_relocation(gen, I386_SEC_TEXT, offset, R_386_32, NULL, I386_SEC_RODATA);
}

static void i386_emit_lea_ebp(I386Gen *gen, long displacement)
{
	i386_put8(&gen->text, 0x8d);
	i386_put8(&gen->text, 0x85);
	i386_put32(&gen->text, (uint32_t)(int32_t)displacement);
}

static void i386_emit_load_ebp(I386Gen *gen, CType *type, long displacement)
{
	long size = i386_type_size(type);

	if(type->kind == TY_ARRAY || type->kind == TY_STRUCT ||
	   type->kind == TY_XEVENT || type->kind == TY_VALIST)
	{
		i386_emit_lea_ebp(gen, displacement);
		return;
	}
	if(size == 1)
	{
		i386_put8(&gen->text, 0x0f);
		i386_put8(&gen->text, 0xb6);
		i386_put8(&gen->text, 0x85);
		i386_put32(&gen->text, (uint32_t)(int32_t)displacement);
	} else if(size == 2)
	{
		i386_put8(&gen->text, 0x0f);
		i386_put8(&gen->text, 0xb7);
		i386_put8(&gen->text, 0x85);
		i386_put32(&gen->text, (uint32_t)(int32_t)displacement);
	} else if(size == 8)
	{
		i386_put8(&gen->text, 0x8b);
		i386_put8(&gen->text, 0x85);
		i386_put32(&gen->text, (uint32_t)(int32_t)displacement);
		i386_put8(&gen->text, 0x8b);
		i386_put8(&gen->text, 0x95);
		i386_put32(&gen->text,
			   (uint32_t)(int32_t)(displacement + 4));
	} else {
		i386_put8(&gen->text, 0x8b);
		i386_put8(&gen->text, 0x85);
		i386_put32(&gen->text, (uint32_t)(int32_t)displacement);
	}
}

static void i386_emit_load_ecx_address(I386Gen *gen, CType *type)
{
	long size = i386_type_size(type);

	if(type->kind == TY_ARRAY || type->kind == TY_STRUCT ||
	   type->kind == TY_XEVENT || type->kind == TY_VALIST)
		return;
	if(size == 1)
	{
		i386_put8(&gen->text, 0x0f);
		i386_put8(&gen->text, 0xb6);
		i386_put8(&gen->text, 0x01);
	} else if(size == 2)
	{
		i386_put8(&gen->text, 0x0f);
		i386_put8(&gen->text, 0xb7);
		i386_put8(&gen->text, 0x01);
	} else {
		i386_put8(&gen->text, 0x8b);
		i386_put8(&gen->text, 0x01);
	}
}

static void i386_emit_load_symbol(I386Gen *gen, const char *name, CType *type)
{
	long size = i386_type_size(type);
	uint32_t offset;

	if(type->kind == TY_ARRAY || type->kind == TY_STRUCT ||
	   type->kind == TY_XEVENT || type->kind == TY_VALIST)
	{
		i386_emit_mov_eax_symbol(gen, name);
		return;
	}
	if(size == 1)
	{
		i386_put8(&gen->text, 0x0f);
		i386_put8(&gen->text, 0xb6);
		i386_put8(&gen->text, 0x05);
		offset = (uint32_t)gen->text.n;
		i386_put32(&gen->text, 0);
	} else if(size == 2)
	{
		i386_put8(&gen->text, 0x0f);
		i386_put8(&gen->text, 0xb7);
		i386_put8(&gen->text, 0x05);
		offset = (uint32_t)gen->text.n;
		i386_put32(&gen->text, 0);
	} else if(size == 8)
	{
		i386_put8(&gen->text, 0xa1);
		offset = (uint32_t)gen->text.n;
		i386_put32(&gen->text, 0);
		i386_add_relocation(gen, I386_SEC_TEXT, offset, R_386_32, name, 0);
		i386_put8(&gen->text, 0x8b);
		i386_put8(&gen->text, 0x15);
		offset = (uint32_t)gen->text.n;
		i386_put32(&gen->text, 4);
		i386_add_relocation(gen, I386_SEC_TEXT, offset, R_386_32, name, 0);
		return;
	} else {
		i386_put8(&gen->text, 0xa1);
		offset = (uint32_t)gen->text.n;
		i386_put32(&gen->text, 0);
	}
	i386_add_relocation(gen, I386_SEC_TEXT, offset, R_386_32, name, 0);
}

static void i386_emit_store_ecx(I386Gen *gen, CType *type)
{
	long size = i386_type_size(type);

	if(size == 1)
	{
		i386_put8(&gen->text, 0x88);
		i386_put8(&gen->text, 0x01);
	} else if(size == 2)
	{
		i386_put8(&gen->text, 0x66);
		i386_put8(&gen->text, 0x89);
		i386_put8(&gen->text, 0x01);
	} else if(size == 8)
	{
		i386_put8(&gen->text, 0x89);
		i386_put8(&gen->text, 0x01);
		i386_put8(&gen->text, 0x89);
		i386_put8(&gen->text, 0x51);
		i386_put8(&gen->text, 0x04);
	} else {
		i386_put8(&gen->text, 0x89);
		i386_put8(&gen->text, 0x01);
	}
}

static void i386_gen_expression(I386Gen *gen, Expr *expression);

static void i386_emit_load_vla_pointer(I386Gen *gen, I386Local *local)
{
	i386_put8(&gen->text, 0x8b);
	i386_put8(&gen->text, 0x85);
	i386_put32(&gen->text, (uint32_t)(int32_t)local->offset);
}

static void i386_emit_vla_bound(I386Gen *gen, const char *name)
{
	I386Local *local = i386_find_local(gen, name);
	Decl *global;

	if(local)
	{
		i386_emit_load_ebp(gen, local->type, local->offset);
		return;
	}
	global = i386_find_global(gen->program, name);
	if(global)
	{
		i386_emit_load_symbol(gen, global->name, global->type);
		return;
	}
	i386_add_object_symbol(gen, name, &T_U32, false);
	i386_emit_load_symbol(gen, name, &T_U32);
}

static CType *i386_gen_address(I386Gen *gen, Expr *expression)
{
	I386Local *local;
	Decl *global;
	CType *type;
	long size;

	if(expression->kind == EX_ID)
	{
		local = i386_find_local(gen, expression->str);
		if(local)
		{
			if(is_vla(local->type))
				i386_emit_load_vla_pointer(gen, local);
			else
				i386_emit_lea_ebp(gen, local->offset);
			return local->type;
		}
		global = i386_find_global(gen->program, expression->str);
		if(global)
		{
			i386_emit_mov_eax_symbol(gen, global->name);
			return global->type;
		}
		global = i386_find_function(gen->program, expression->str);
		if(global)
		{
			i386_emit_mov_eax_symbol(gen, global->name);
			return global->type;
		}
		fatal("%s is not addressable", expression->str);
	}
	if(expression->kind == EX_UNARY &&
	   !strcmp(expression->op, "*"))
	{
		i386_gen_expression(gen, expression->left);
		type = i386_expr_type(gen, expression->left);
		return type->base ? type->base : &T_U32;
	}
	if(expression->kind == EX_INDEX)
	{
		i386_gen_expression(gen, expression->left);
		i386_put8(&gen->text, 0x50);
		i386_gen_expression(gen, expression->right);
		type = i386_expr_type(gen, expression->left);
		type = type->base ? type->base : &T_U32;
		size = i386_type_size(type);
		if(size != 1)
		{
			i386_put8(&gen->text, 0x69);
			i386_put8(&gen->text, 0xc0);
			i386_put32(&gen->text, (uint32_t)size);
		}
		i386_put8(&gen->text, 0x59);
		i386_put8(&gen->text, 0x01);
		i386_put8(&gen->text, 0xc8);
		return type;
	}
	if(expression->kind == EX_MEMBER ||
	   expression->kind == EX_PTRMEMBER)
	{
		StructMember *member;
		CType *owner;
		long member_offset;

		if(expression->kind == EX_MEMBER)
		{
			i386_gen_address(gen, expression->left);
			owner = i386_expr_type(gen, expression->left);
		} else {
			i386_gen_expression(gen, expression->left);
			owner = i386_expr_type(gen, expression->left);
			owner = owner && owner->kind == TY_PTR ? owner->base : NULL;
		}
		member = i386_find_struct_member(owner, expression->str, &member_offset);
		if(!member)
			fatal("unknown struct member %s", expression->str);
		if(member_offset)
		{
			i386_put8(&gen->text, 0x05);
			i386_put32(&gen->text, (uint32_t)member_offset);
		}
		return member->type;
	}
	fatal("expression is not an lvalue");
	return &T_U32;
}

static void i386_emit_setcc(I386Gen *gen, uint8_t condition)
{
	i386_put8(&gen->text, 0x0f);
	i386_put8(&gen->text, condition);
	i386_put8(&gen->text, 0xc0);
	i386_put8(&gen->text, 0x0f);
	i386_put8(&gen->text, 0xb6);
	i386_put8(&gen->text, 0xc0);
}

static bool i386_type_is_unsigned(CType *type)
{
	if(!type)
		return false;
	return type->kind == TY_U8 || type->kind == TY_U16 ||
	       type->kind == TY_U32 || type->kind == TY_U64 ||
	       type->kind == TY_PTR || type->kind == TY_ARRAY;
}

static bool i386_expression_is_unsigned(I386Gen *gen, Expr *expression)
{
	return i386_type_is_unsigned(i386_expr_type(gen, expression));
}

static void i386_emit_division(I386Gen *gen, bool is_unsigned)
{
	/*
	 * Entry: ECX = dividend, EAX = divisor.
	 * CDQ overwrites EDX, so the divisor must not live in EDX when CDQ
	 * executes.  Version 1.5 used IDIV EDX after CDQ and therefore
	 * divided by zero for every positive dividend.
	 */
	i386_put8(&gen->text, 0x89);
	i386_put8(&gen->text, 0xc2);
	i386_put8(&gen->text, 0x89);
	i386_put8(&gen->text, 0xc8);
	i386_put8(&gen->text, 0x89);
	i386_put8(&gen->text, 0xd1);
	if(is_unsigned)
	{
		i386_put8(&gen->text, 0x31);
		i386_put8(&gen->text, 0xd2);
		i386_put8(&gen->text, 0xf7);
		i386_put8(&gen->text, 0xf1);
	} else {
		i386_put8(&gen->text, 0x99);
		i386_put8(&gen->text, 0xf7);
		i386_put8(&gen->text, 0xf9);
	}
}

static bool i386_expression_is_u64(I386Gen *gen, Expr *expression)
{
	CType *type = i386_expr_type(gen, expression);

	return type && type->kind == TY_U64;
}

static void i386_emit_zero_edx(I386Gen *gen)
{
	i386_put8(&gen->text, 0x31);
	i386_put8(&gen->text, 0xd2);
}

static void i386_emit_mul64_imm(I386Gen *gen,
				unsigned long long multiplier)
{
	uint32_t low = (uint32_t)multiplier;
	uint32_t high = (uint32_t)(multiplier >> 32);

	/* Preserve the original halves and form the low 64 bits of the product. */
	i386_put8(&gen->text, 0x50);
	i386_put8(&gen->text, 0x52);
	i386_put8(&gen->text, 0xb9);
	i386_put32(&gen->text, low);
	i386_put8(&gen->text, 0xf7);
	i386_put8(&gen->text, 0xe1);
	i386_put8(&gen->text, 0x50);
	i386_put8(&gen->text, 0x52);
	/* ECX = high32(original_low * low). */
	i386_put8(&gen->text, 0x8b);
	i386_put8(&gen->text, 0x0c);
	i386_put8(&gen->text, 0x24);
	/* Add original_high * low. */
	i386_put8(&gen->text, 0x8b);
	i386_put8(&gen->text, 0x44);
	i386_put8(&gen->text, 0x24);
	i386_put8(&gen->text, 0x08);
	i386_put8(&gen->text, 0x69);
	i386_put8(&gen->text, 0xc0);
	i386_put32(&gen->text, low);
	i386_put8(&gen->text, 0x01);
	i386_put8(&gen->text, 0xc1);
	/* Add original_low * high. */
	i386_put8(&gen->text, 0x8b);
	i386_put8(&gen->text, 0x44);
	i386_put8(&gen->text, 0x24);
	i386_put8(&gen->text, 0x0c);
	i386_put8(&gen->text, 0x69);
	i386_put8(&gen->text, 0xc0);
	i386_put32(&gen->text, high);
	i386_put8(&gen->text, 0x01);
	i386_put8(&gen->text, 0xc1);
	/* Restore result low and install result high. */
	i386_put8(&gen->text, 0x8b);
	i386_put8(&gen->text, 0x44);
	i386_put8(&gen->text, 0x24);
	i386_put8(&gen->text, 0x04);
	i386_put8(&gen->text, 0x89);
	i386_put8(&gen->text, 0xca);
	i386_put8(&gen->text, 0x83);
	i386_put8(&gen->text, 0xc4);
	i386_put8(&gen->text, 0x10);
}

static void i386_emit_shift64_imm(I386Gen *gen, bool left, unsigned int count)
{
	count &= 63;
	if(!count)
		return;
	if(left)
	{
		if(count < 32)
		{
			i386_put8(&gen->text, 0x0f);
			i386_put8(&gen->text, 0xa4);
			i386_put8(&gen->text, 0xc2);
			i386_put8(&gen->text, (uint8_t)count);
			i386_put8(&gen->text, 0xc1);
			i386_put8(&gen->text, 0xe0);
			i386_put8(&gen->text, (uint8_t)count);
		} else if(count == 32)
		{
			i386_put8(&gen->text, 0x89);
			i386_put8(&gen->text, 0xc2);
			i386_put8(&gen->text, 0x31);
			i386_put8(&gen->text, 0xc0);
		} else {
			i386_put8(&gen->text, 0x89);
			i386_put8(&gen->text, 0xc2);
			i386_put8(&gen->text, 0xc1);
			i386_put8(&gen->text, 0xe2);
			i386_put8(&gen->text, (uint8_t)(count - 32));
			i386_put8(&gen->text, 0x31);
			i386_put8(&gen->text, 0xc0);
		}
	} else {
		if(count < 32)
		{
			i386_put8(&gen->text, 0x0f);
			i386_put8(&gen->text, 0xac);
			i386_put8(&gen->text, 0xd0);
			i386_put8(&gen->text, (uint8_t)count);
			i386_put8(&gen->text, 0xc1);
			i386_put8(&gen->text, 0xea);
			i386_put8(&gen->text, (uint8_t)count);
		} else if(count == 32)
		{
			i386_put8(&gen->text, 0x89);
			i386_put8(&gen->text, 0xd0);
			i386_emit_zero_edx(gen);
		} else {
			i386_put8(&gen->text, 0x89);
			i386_put8(&gen->text, 0xd0);
			i386_put8(&gen->text, 0xc1);
			i386_put8(&gen->text, 0xe8);
			i386_put8(&gen->text, (uint8_t)(count - 32));
			i386_emit_zero_edx(gen);
		}
	}
}

static void i386_gen_binary64(I386Gen *gen, Expr *expression)
{
	const char *operator= expression->op;
	CType *type;

	if(!strcmp(operator, "="))
	{
		type = i386_gen_address(gen, expression->left);
		i386_put8(&gen->text, 0x50);
		i386_gen_expression(gen, expression->right);
		if(!i386_expression_is_u64(gen, expression->right))
			i386_emit_zero_edx(gen);
		i386_put8(&gen->text, 0x59);
		i386_emit_store_ecx(gen, type);
		return;
	}
	if(!strcmp(operator, "^=") || !strcmp(operator, "|=") ||
	   !strcmp(operator, "&=") || !strcmp(operator, "*="))
	{
		type = i386_gen_address(gen, expression->left);
		i386_put8(&gen->text, 0x50);
		i386_put8(&gen->text, 0x89);
		i386_put8(&gen->text, 0xc1);
		i386_put8(&gen->text, 0x8b);
		i386_put8(&gen->text, 0x01);
		i386_put8(&gen->text, 0x8b);
		i386_put8(&gen->text, 0x51);
		i386_put8(&gen->text, 0x04);
		if(!strcmp(operator, "*="))
		{
			if(expression->right->kind != EX_NUM)
				fatal("64-bit compound multiplication requires a constant");
			i386_emit_mul64_imm(gen, expression->right->num);
		} else {
			i386_put8(&gen->text, 0x53);
			i386_put8(&gen->text, 0x52);
			i386_put8(&gen->text, 0x50);
			i386_gen_expression(gen, expression->right);
			if(!i386_expression_is_u64(gen, expression->right))
				i386_emit_zero_edx(gen);
			i386_put8(&gen->text, 0x89);
			i386_put8(&gen->text, 0xd3);
			i386_put8(&gen->text, 0x89);
			i386_put8(&gen->text, 0xc1);
			i386_put8(&gen->text, 0x58);
			i386_put8(&gen->text, 0x5a);
			if(!strcmp(operator, "^="))
			{
				i386_put8(&gen->text, 0x31);
				i386_put8(&gen->text, 0xc8);
				i386_put8(&gen->text, 0x31);
				i386_put8(&gen->text, 0xda);
			} else if(!strcmp(operator, "|="))
			{
				i386_put8(&gen->text, 0x09);
				i386_put8(&gen->text, 0xc8);
				i386_put8(&gen->text, 0x09);
				i386_put8(&gen->text, 0xda);
			} else {
				i386_put8(&gen->text, 0x21);
				i386_put8(&gen->text, 0xc8);
				i386_put8(&gen->text, 0x21);
				i386_put8(&gen->text, 0xda);
			}
			i386_put8(&gen->text, 0x5b);
		}
		i386_put8(&gen->text, 0x59);
		i386_emit_store_ecx(gen, type);
		return;
	}
	if(!strcmp(operator, "<<") || !strcmp(operator, ">>"))
	{
		if(expression->right->kind != EX_NUM)
			fatal("64-bit shifts require a constant count");
		i386_gen_expression(gen, expression->left);
		if(!i386_expression_is_u64(gen, expression->left))
			i386_emit_zero_edx(gen);
		i386_emit_shift64_imm(gen, !strcmp(operator, "<<"), (unsigned int)expression->right->num);
		return;
	}
	if(!strcmp(operator, "*") && expression->right->kind == EX_NUM)
	{
		i386_gen_expression(gen, expression->left);
		if(!i386_expression_is_u64(gen, expression->left))
			i386_emit_zero_edx(gen);
		i386_emit_mul64_imm(gen, expression->right->num);
		return;
	}
	if(!strcmp(operator, "|") || !strcmp(operator, "^") ||
	   !strcmp(operator, "&") || !strcmp(operator, "+") ||
	   !strcmp(operator, "-"))
	{
		i386_put8(&gen->text, 0x53);
		i386_gen_expression(gen, expression->left);
		if(!i386_expression_is_u64(gen, expression->left))
			i386_emit_zero_edx(gen);
		i386_put8(&gen->text, 0x52);
		i386_put8(&gen->text, 0x50);
		i386_gen_expression(gen, expression->right);
		if(!i386_expression_is_u64(gen, expression->right))
			i386_emit_zero_edx(gen);
		i386_put8(&gen->text, 0x89);
		i386_put8(&gen->text, 0xd3);
		i386_put8(&gen->text, 0x89);
		i386_put8(&gen->text, 0xc1);
		i386_put8(&gen->text, 0x58);
		i386_put8(&gen->text, 0x5a);
		if(!strcmp(operator, "|"))
		{
			i386_put8(&gen->text, 0x09);
			i386_put8(&gen->text, 0xc8);
			i386_put8(&gen->text, 0x09);
			i386_put8(&gen->text, 0xda);
		} else if(!strcmp(operator, "^"))
		{
			i386_put8(&gen->text, 0x31);
			i386_put8(&gen->text, 0xc8);
			i386_put8(&gen->text, 0x31);
			i386_put8(&gen->text, 0xda);
		} else if(!strcmp(operator, "&"))
		{
			i386_put8(&gen->text, 0x21);
			i386_put8(&gen->text, 0xc8);
			i386_put8(&gen->text, 0x21);
			i386_put8(&gen->text, 0xda);
		} else if(!strcmp(operator, "+"))
		{
			i386_put8(&gen->text, 0x01);
			i386_put8(&gen->text, 0xc8);
			i386_put8(&gen->text, 0x11);
			i386_put8(&gen->text, 0xda);
		} else {
			i386_put8(&gen->text, 0x29);
			i386_put8(&gen->text, 0xc8);
			i386_put8(&gen->text, 0x19);
			i386_put8(&gen->text, 0xda);
		}
		i386_put8(&gen->text, 0x5b);
		return;
	}
	fatal("unsupported 64-bit i386 operator %s", operator);
}

static void i386_gen_binary(I386Gen *gen, Expr *expression)
{
	const char *operator= expression->op;
	CType *type;

	if(i386_expression_is_u64(gen, expression->left) &&
	   strcmp(operator, "==") && strcmp(operator, "!=") &&
	   strcmp(operator, "<") && strcmp(operator, "<=") &&
	   strcmp(operator, ">") && strcmp(operator, ">="))
	{
		i386_gen_binary64(gen, expression);
		return;
	}

	if(!strcmp(operator, "="))
	{
		type = i386_gen_address(gen, expression->left);
		i386_put8(&gen->text, 0x50);
		i386_gen_expression(gen, expression->right);
		if(type->kind == TY_STRUCT)
		{
			long size = i386_type_size(type);

			i386_put8(&gen->text, 0x5a);
			i386_put8(&gen->text, 0x52);
			i386_put8(&gen->text, 0x56);
			i386_put8(&gen->text, 0x57);
			i386_put8(&gen->text, 0x89);
			i386_put8(&gen->text, 0xc6);
			i386_put8(&gen->text, 0x89);
			i386_put8(&gen->text, 0xd7);
			i386_put8(&gen->text, 0xb9);
			i386_put32(&gen->text, (uint32_t)size);
			i386_put8(&gen->text, 0xfc);
			i386_put8(&gen->text, 0xf3);
			i386_put8(&gen->text, 0xa4);
			i386_put8(&gen->text, 0x5f);
			i386_put8(&gen->text, 0x5e);
			i386_put8(&gen->text, 0x58);
		} else {
			i386_put8(&gen->text, 0x59);
			i386_emit_store_ecx(gen, type);
		}
		return;
	}
	if(!strcmp(operator, "+=") || !strcmp(operator, "-=") ||
	   !strcmp(operator, "*=") || !strcmp(operator, "/=") ||
	   !strcmp(operator, "%=") || !strcmp(operator, "&=") ||
	   !strcmp(operator, "|=") || !strcmp(operator, "^="))
	{
		type = i386_gen_address(gen, expression->left);
		i386_put8(&gen->text, 0x50);
		i386_put8(&gen->text, 0x89);
		i386_put8(&gen->text, 0xc1);
		i386_emit_load_ecx_address(gen, type);
		i386_put8(&gen->text, 0x50);
		i386_gen_expression(gen, expression->right);
		i386_put8(&gen->text, 0x59);
		if(!strcmp(operator, "+="))
		{
			i386_put8(&gen->text, 0x01);
			i386_put8(&gen->text, 0xc8);
		} else if(!strcmp(operator, "-="))
		{
			i386_put8(&gen->text, 0x29);
			i386_put8(&gen->text, 0xc1);
			i386_put8(&gen->text, 0x89);
			i386_put8(&gen->text, 0xc8);
		} else if(!strcmp(operator, "*="))
		{
			i386_put8(&gen->text, 0x0f);
			i386_put8(&gen->text, 0xaf);
			i386_put8(&gen->text, 0xc1);
		} else if(!strcmp(operator, "&="))
		{
			i386_put8(&gen->text, 0x21);
			i386_put8(&gen->text, 0xc8);
		} else if(!strcmp(operator, "|="))
		{
			i386_put8(&gen->text, 0x09);
			i386_put8(&gen->text, 0xc8);
		} else if(!strcmp(operator, "^="))
		{
			i386_put8(&gen->text, 0x31);
			i386_put8(&gen->text, 0xc8);
		} else {
			i386_emit_division(gen,
					   i386_type_is_unsigned(type) ||
						   i386_expression_is_unsigned(gen,
									       expression->right));
			if(!strcmp(operator, "%="))
			{
				i386_put8(&gen->text, 0x89);
				i386_put8(&gen->text, 0xd0);
			}
		}
		i386_put8(&gen->text, 0x59);
		i386_emit_store_ecx(gen, type);
		return;
	}
	if(!strcmp(operator, "&&") || !strcmp(operator, "||"))
	{
		char *short_label = i386_new_label(gen, ".Llogic");
		char *done_label = i386_new_label(gen, ".Llogic_done");

		i386_gen_expression(gen, expression->left);
		i386_put8(&gen->text, 0x85);
		i386_put8(&gen->text, 0xc0);
		i386_emit_jump(gen, !strcmp(operator, "&&") ? 0x84 : 0x85, short_label);
		i386_gen_expression(gen, expression->right);
		i386_put8(&gen->text, 0x85);
		i386_put8(&gen->text, 0xc0);
		i386_emit_setcc(gen, 0x95);
		i386_emit_jump(gen, 0xff, done_label);
		i386_define_label(gen, short_label);
		i386_emit_mov_eax_imm(gen, !strcmp(operator, "&&") ? 0 : 1);
		i386_define_label(gen, done_label);
		return;
	}
	i386_gen_expression(gen, expression->left);
	i386_put8(&gen->text, 0x50);
	i386_gen_expression(gen, expression->right);
	i386_put8(&gen->text, 0x59);
	if(!strcmp(operator, "+"))
	{
		i386_put8(&gen->text, 0x01);
		i386_put8(&gen->text, 0xc8);
	} else if(!strcmp(operator, "-"))
	{
		i386_put8(&gen->text, 0x29);
		i386_put8(&gen->text, 0xc1);
		i386_put8(&gen->text, 0x89);
		i386_put8(&gen->text, 0xc8);
	} else if(!strcmp(operator, "*"))
	{
		i386_put8(&gen->text, 0x0f);
		i386_put8(&gen->text, 0xaf);
		i386_put8(&gen->text, 0xc1);
	} else if(!strcmp(operator, "/") || !strcmp(operator, "%"))
	{
		i386_emit_division(gen,
				   i386_expression_is_unsigned(gen, expression->left) ||
					   i386_expression_is_unsigned(gen, expression->right));
		if(!strcmp(operator, "%"))
		{
			i386_put8(&gen->text, 0x89);
			i386_put8(&gen->text, 0xd0);
		}
	} else if(!strcmp(operator, "&"))
	{
		i386_put8(&gen->text, 0x21);
		i386_put8(&gen->text, 0xc8);
	} else if(!strcmp(operator, "|"))
	{
		i386_put8(&gen->text, 0x09);
		i386_put8(&gen->text, 0xc8);
	} else if(!strcmp(operator, "^"))
	{
		i386_put8(&gen->text, 0x31);
		i386_put8(&gen->text, 0xc8);
	} else if(!strcmp(operator, "<<") || !strcmp(operator, ">>"))
	{
		i386_put8(&gen->text, 0x89);
		i386_put8(&gen->text, 0xc2);
		i386_put8(&gen->text, 0x89);
		i386_put8(&gen->text, 0xc8);
		i386_put8(&gen->text, 0x89);
		i386_put8(&gen->text, 0xd1);
		i386_put8(&gen->text, 0xd3);
		if(!strcmp(operator, "<<"))
			i386_put8(&gen->text, 0xe0);
		else
			i386_put8(&gen->text,
				  i386_expression_is_unsigned(gen, expression->left) ? 0xe8 : 0xf8);
	} else if(!strcmp(operator, "==") || !strcmp(operator, "!=") ||
		  !strcmp(operator, "<") || !strcmp(operator, "<=") ||
		  !strcmp(operator, ">") || !strcmp(operator, ">="))
	{
		uint8_t condition;

		i386_put8(&gen->text, 0x39);
		i386_put8(&gen->text, 0xc1);
		if(!strcmp(operator, "=="))
			condition = 0x94;
		else if(!strcmp(operator, "!="))
			condition = 0x95;
		else if(i386_expression_is_unsigned(gen, expression->left) ||
			i386_expression_is_unsigned(gen, expression->right))
			condition = !strcmp(operator, "<") ? 0x92 : !strcmp(operator, "<=") ? 0x96
							    : !strcmp(operator, ">")	    ? 0x97
											    : 0x93;
		else
			condition = !strcmp(operator, "<") ? 0x9c : !strcmp(operator, "<=") ? 0x9e
							    : !strcmp(operator, ">")	    ? 0x9f
											    : 0x9d;
		i386_emit_setcc(gen, condition);
	} else {
		fatal("unsupported i386 operator %s", operator);
	}
}

static void i386_gen_call(I386Gen *gen, Expr *expression)
{
	size_t i;
	const char *name;
	uint32_t offset;

	if(expression->left->kind != EX_ID)
		fatal("only direct calls are supported by i386 backend");
	name = expression->left->str;
	if(!strcmp(name, "va_start") || !strcmp(name, "va_end"))
		fatal("variadic builtins are not supported by i386 backend yet");
	for(i = expression->nargs; i > 0; i--)
	{
		i386_gen_expression(gen, expression->args[i - 1]);
		i386_put8(&gen->text, 0x50);
	}
	i386_put8(&gen->text, 0xe8);
	offset = (uint32_t)gen->text.n;
	i386_put32(&gen->text, 0xfffffffcU);
	i386_add_relocation(gen, I386_SEC_TEXT, offset, R_386_PC32, name, 0);
	if(expression->nargs)
	{
		i386_put8(&gen->text, 0x81);
		i386_put8(&gen->text, 0xc4);
		i386_put32(&gen->text,
			   (uint32_t)(expression->nargs * 4));
	}
}

static void i386_gen_incdec(I386Gen *gen, Expr *expression)
{
	CType *type = i386_gen_address(gen, expression->left);
	long step = 1;
	bool postfix = !strcmp(expression->op, "post++") ||
		       !strcmp(expression->op, "post--");
	bool decrement = !strcmp(expression->op, "--") ||
			 !strcmp(expression->op, "post--");

	if(type->kind == TY_PTR && type->base)
	{
		step = i386_type_size(type->base);
		if(step <= 0)
			step = 1;
	}

	i386_put8(&gen->text, 0x50);
	i386_put8(&gen->text, 0x89);
	i386_put8(&gen->text, 0xc1);
	i386_emit_load_ecx_address(gen, type);
	if(postfix)
		i386_put8(&gen->text, 0x50);
	i386_put8(&gen->text, decrement ? 0x2d : 0x05);
	i386_put32(&gen->text, (uint32_t)step);
	if(postfix)
	{
		i386_put8(&gen->text, 0x5a);
		i386_put8(&gen->text, 0x59);
		i386_emit_store_ecx(gen, type);
		i386_put8(&gen->text, 0x89);
		i386_put8(&gen->text, 0xd0);
	} else {
		i386_put8(&gen->text, 0x59);
		i386_emit_store_ecx(gen, type);
	}
}

static void i386_gen_expression(I386Gen *gen, Expr *expression)
{
	I386Local *local;
	Decl *declaration;
	CType *type;
	uint32_t string_offset;

	switch(expression->kind)
	{
	case EX_NUM:
		i386_emit_mov_eax_imm(gen, (uint32_t)expression->num);
		if(i386_expression_is_u64(gen, expression))
		{
			i386_put8(&gen->text, 0xba);
			i386_put32(&gen->text, (uint32_t)(expression->num >> 32));
		}
		return;
	case EX_STR:
		string_offset = i386_intern_string(gen, expression->str);
		i386_emit_mov_eax_rodata(gen, string_offset);
		return;
	case EX_ID:
		if(!strcmp(expression->str, "NULL"))
		{
			i386_emit_mov_eax_imm(gen, 0);
			return;
		}
		local = i386_find_local(gen, expression->str);
		if(local)
		{
			if(is_vla(local->type))
				i386_emit_load_vla_pointer(gen, local);
			else
				i386_emit_load_ebp(gen, local->type, local->offset);
			return;
		}
		declaration = i386_find_global(gen->program, expression->str);
		if(declaration)
		{
			i386_emit_load_symbol(gen, declaration->name, declaration->type);
			return;
		}
		declaration = i386_find_function(gen->program, expression->str);
		if(declaration)
		{
			i386_emit_mov_eax_symbol(gen, declaration->name);
			return;
		}
		fatal("unknown identifier %s in i386 backend", expression->str);
		return;
	case EX_SIZEOF:
		i386_emit_mov_eax_imm(gen,
				      (uint32_t)i386_type_size(expression->sizeof_type ? expression->sizeof_type : i386_expr_type(gen, expression->left)));
		return;
	case EX_UNARY:
		if(!strcmp(expression->op, "++") ||
		   !strcmp(expression->op, "--") ||
		   !strcmp(expression->op, "post++") ||
		   !strcmp(expression->op, "post--"))
		{
			i386_gen_incdec(gen, expression);
		} else if(!strcmp(expression->op, "cast"))
		{
			bool source_wide = i386_expression_is_u64(gen, expression->left);

			i386_gen_expression(gen, expression->left);
			if(i386_type_size(expression->type) == 1)
			{
				i386_put8(&gen->text, 0x25);
				i386_put32(&gen->text, 0xff);
			} else if(i386_type_size(expression->type) == 2)
			{
				i386_put8(&gen->text, 0x25);
				i386_put32(&gen->text, 0xffff);
			} else if(i386_type_size(expression->type) == 8 && !source_wide)
			{
				i386_emit_zero_edx(gen);
			}
		} else if(!strcmp(expression->op, "&"))
		{
			i386_gen_address(gen, expression->left);
		} else if(!strcmp(expression->op, "*"))
		{
			i386_gen_expression(gen, expression->left);
			i386_put8(&gen->text, 0x89);
			i386_put8(&gen->text, 0xc1);
			type = i386_expr_type(gen, expression);
			i386_emit_load_ecx_address(gen, type);
		} else {
			i386_gen_expression(gen, expression->left);
			if(!strcmp(expression->op, "!"))
			{
				i386_put8(&gen->text, 0x85);
				i386_put8(&gen->text, 0xc0);
				i386_emit_setcc(gen, 0x94);
			} else if(!strcmp(expression->op, "~"))
			{
				i386_put8(&gen->text, 0xf7);
				i386_put8(&gen->text, 0xd0);
			} else if(!strcmp(expression->op, "-"))
			{
				i386_put8(&gen->text, 0xf7);
				i386_put8(&gen->text, 0xd8);
			}
		}
		return;
	case EX_INDEX:
	case EX_MEMBER:
	case EX_PTRMEMBER:
		type = i386_gen_address(gen, expression);
		i386_put8(&gen->text, 0x89);
		i386_put8(&gen->text, 0xc1);
		i386_emit_load_ecx_address(gen, type);
		return;
	case EX_BINARY:
		i386_gen_binary(gen, expression);
		return;
	case EX_CALL:
		i386_gen_call(gen, expression);
		return;
	case EX_INITLIST:
		fatal("initializer list used as an expression");
		return;
	}
	fatal("unsupported i386 expression");
}

static long i386_collect_locals(I386Gen *gen, Stmt *statement, long used)
{
	size_t i;
	Decl *declaration;
	long size;
	long alignment;

	if(!statement)
		return used;
	if(statement->kind == ST_DECL)
	{
		declaration = statement->decl;
		if(i386_find_local(gen, declaration->name))
			fatal("duplicate local %s", declaration->name);
		size = i386_type_size(declaration->type);
		if(size <= 0)
			size = 1;
		alignment = i386_type_align(declaration->type);
		used = align_up(used, alignment);
		used += size;
		ARR_GROW(gen->locals, gen->nlocals, gen->caplocals, I386Local);
		gen->locals[gen->nlocals].name = declaration->name;
		gen->locals[gen->nlocals].type = declaration->type;
		gen->locals[gen->nlocals].offset = -used;
		gen->nlocals++;
	} else if(statement->kind == ST_BLOCK)
	{
		for(i = 0; i < statement->nchildren; i++)
			used = i386_collect_locals(gen,
						   statement->children[i],
						   used);
	} else if(statement->kind == ST_IF)
	{
		used = i386_collect_locals(gen, statement->yes, used);
		used = i386_collect_locals(gen, statement->no, used);
	} else if(statement->kind == ST_WHILE)
	{
		used = i386_collect_locals(gen, statement->body, used);
	} else if(statement->kind == ST_FOR)
	{
		used = i386_collect_locals(gen, statement->init, used);
		used = i386_collect_locals(gen, statement->body, used);
	} else if(statement->kind == ST_SWITCH)
	{
		used = i386_collect_locals(gen, statement->body, used);
	}
	return used;
}

static void i386_gen_statement(I386Gen *gen, Stmt *statement)
{
	size_t i;
	I386Local *local;
	char *else_label;
	char *done_label;
	char *loop_label;
	char *condition_label;
	char *next_label;

	if(!statement)
		return;
	switch(statement->kind)
	{
	case ST_BLOCK:
		for(i = 0; i < statement->nchildren; i++)
			i386_gen_statement(gen, statement->children[i]);
		return;
	case ST_DECL:
		local = i386_find_local(gen, statement->decl->name);
		if(is_vla(local->type))
		{
			long element_size = i386_type_size(local->type->base);

			i386_emit_vla_bound(gen, local->type->name);
			if(element_size != 1)
			{
				i386_put8(&gen->text, 0x69);
				i386_put8(&gen->text, 0xc0);
				i386_put32(&gen->text, (uint32_t)element_size);
			}
			/* Round the allocation up to 16 bytes. */
			i386_put8(&gen->text, 0x83);
			i386_put8(&gen->text, 0xc0);
			i386_put8(&gen->text, 0x0f);
			i386_put8(&gen->text, 0x83);
			i386_put8(&gen->text, 0xe0);
			i386_put8(&gen->text, 0xf0);
			i386_put8(&gen->text, 0x29);
			i386_put8(&gen->text, 0xc4);
			/* Save the runtime array base in its fixed frame slot. */
			i386_put8(&gen->text, 0x89);
			i386_put8(&gen->text, 0xa5);
			i386_put32(&gen->text,
				   (uint32_t)(int32_t)local->offset);
		}
		if(statement->decl->init)
		{
			if(is_vla(local->type))
				fatal("variable-length array %s cannot have an initializer",
				      statement->decl->name);
			i386_emit_lea_ebp(gen, local->offset);
			i386_put8(&gen->text, 0x50);
			i386_gen_expression(gen, statement->decl->init);
			i386_put8(&gen->text, 0x59);
			i386_emit_store_ecx(gen, local->type);
		}
		return;
	case ST_EXPR:
		i386_gen_expression(gen, statement->expr);
		return;
	case ST_EMPTY:
		return;
	case ST_RETURN:
		if(statement->expr)
			i386_gen_expression(gen, statement->expr);
		else
			i386_put8(&gen->text, 0x31),
				i386_put8(&gen->text, 0xc0);
		i386_emit_jump(gen, 0xff, gen->return_label);
		return;
	case ST_IF:
		else_label = i386_new_label(gen, ".Lelse");
		done_label = i386_new_label(gen, ".Lifend");
		i386_gen_expression(gen, statement->cond);
		i386_put8(&gen->text, 0x85);
		i386_put8(&gen->text, 0xc0);
		i386_emit_jump(gen, 0x84, else_label);
		i386_gen_statement(gen, statement->yes);
		i386_emit_jump(gen, 0xff, done_label);
		i386_define_label(gen, else_label);
		i386_gen_statement(gen, statement->no);
		i386_define_label(gen, done_label);
		return;
	case ST_WHILE:
		loop_label = i386_new_label(gen, ".Lwhile");
		done_label = i386_new_label(gen, ".Lwend");
		ARR_GROW(gen->break_labels, gen->nbreak_labels, gen->capbreak_labels, char *);
		gen->break_labels[gen->nbreak_labels++] = done_label;
		ARR_GROW(gen->continue_labels, gen->ncontinue_labels, gen->capcontinue_labels, char *);
		gen->continue_labels[gen->ncontinue_labels++] = loop_label;
		i386_define_label(gen, loop_label);
		i386_gen_expression(gen, statement->cond);
		i386_put8(&gen->text, 0x85);
		i386_put8(&gen->text, 0xc0);
		i386_emit_jump(gen, 0x84, done_label);
		i386_gen_statement(gen, statement->body);
		i386_emit_jump(gen, 0xff, loop_label);
		i386_define_label(gen, done_label);
		gen->nbreak_labels--;
		gen->ncontinue_labels--;
		return;
	case ST_FOR:
		condition_label = i386_new_label(gen, ".Lforcond");
		next_label = i386_new_label(gen, ".Lfornext");
		done_label = i386_new_label(gen, ".Lforend");
		i386_gen_statement(gen, statement->init);
		ARR_GROW(gen->break_labels, gen->nbreak_labels, gen->capbreak_labels, char *);
		gen->break_labels[gen->nbreak_labels++] = done_label;
		ARR_GROW(gen->continue_labels, gen->ncontinue_labels, gen->capcontinue_labels, char *);
		gen->continue_labels[gen->ncontinue_labels++] = next_label;
		i386_define_label(gen, condition_label);
		if(statement->cond)
		{
			i386_gen_expression(gen, statement->cond);
			i386_put8(&gen->text, 0x85);
			i386_put8(&gen->text, 0xc0);
			i386_emit_jump(gen, 0x84, done_label);
		}
		i386_gen_statement(gen, statement->body);
		i386_define_label(gen, next_label);
		if(statement->post)
			i386_gen_expression(gen, statement->post);
		i386_emit_jump(gen, 0xff, condition_label);
		i386_define_label(gen, done_label);
		gen->nbreak_labels--;
		gen->ncontinue_labels--;
		return;
	case ST_SWITCH:
	{
		Stmt *body = statement->body;
		char *default_label;
		size_t j;

		done_label = i386_new_label(gen, ".Lswitchend");
		default_label = done_label;
		if(body && body->kind == ST_BLOCK)
		{
			for(j = 0; j < body->nchildren; j++)
			{
				Stmt *child = body->children[j];

				if(child->kind == ST_CASE)
				{
					long value;

					if(!eval_const_expr(child->expr, &value))
						fatal("case value is not an integer constant");
					child->label = i386_new_label(gen, ".Lcase");
				} else if(child->kind == ST_DEFAULT)
				{
					if(default_label != done_label)
						fatal("multiple default labels in switch");
					child->label = i386_new_label(gen, ".Ldefault");
					default_label = child->label;
				}
			}
		} else {
			fatal("switch body must be a block");
		}
		i386_gen_expression(gen, statement->cond);
		for(j = 0; j < body->nchildren; j++)
		{
			Stmt *child = body->children[j];

			if(child->kind == ST_CASE)
			{
				long value;

				eval_const_expr(child->expr, &value);
				i386_put8(&gen->text, 0x3d);
				i386_put32(&gen->text, (uint32_t)value);
				i386_emit_jump(gen, 0x84, child->label);
			}
		}
		i386_emit_jump(gen, 0xff, default_label);
		ARR_GROW(gen->break_labels, gen->nbreak_labels, gen->capbreak_labels, char *);
		gen->break_labels[gen->nbreak_labels++] = done_label;
		i386_gen_statement(gen, body);
		gen->nbreak_labels--;
		i386_define_label(gen, done_label);
		return;
	}
	case ST_CASE:
		if(!statement->label)
			fatal("case label outside switch");
		i386_define_label(gen, statement->label);
		return;
	case ST_DEFAULT:
		if(!statement->label)
			fatal("default label outside switch");
		i386_define_label(gen, statement->label);
		return;
	case ST_BREAK:
		if(!gen->nbreak_labels)
			fatal("break outside loop or switch");
		i386_emit_jump(gen, 0xff, gen->break_labels[gen->nbreak_labels - 1]);
		return;
	case ST_CONTINUE:
		if(!gen->ncontinue_labels)
			fatal("continue outside loop");
		i386_emit_jump(gen, 0xff, gen->continue_labels[gen->ncontinue_labels - 1]);
		return;
	case ST_ASM:
	{
		if(!strcmp(statement->asm_text, "rdtsc"))
		{
			size_t output_index;

			i386_put8(&gen->text, 0x0f);
			i386_put8(&gen->text, 0x31);
			for(output_index = 0; output_index < statement->nasm_outputs;
			    output_index++)
			{
				const char *constraint =
					statement->asm_constraints[output_index];
				Expr *output = statement->asm_outputs[output_index];
				CType *output_type;

				if(!strcmp(constraint, "=a"))
				{
					/* Preserve EDX:EAX while calculating the lvalue. */
					i386_put8(&gen->text, 0x52);
					i386_put8(&gen->text, 0x50);
					output_type = i386_gen_address(gen, output);
					i386_put8(&gen->text, 0x89);
					i386_put8(&gen->text, 0xc1);
					i386_put8(&gen->text, 0x58);
					i386_emit_store_ecx(gen, output_type);
					i386_put8(&gen->text, 0x5a);
				} else if(!strcmp(constraint, "=d"))
				{
					/* Store the high half while restoring low EAX. */
					i386_put8(&gen->text, 0x50);
					i386_put8(&gen->text, 0x52);
					output_type = i386_gen_address(gen, output);
					i386_put8(&gen->text, 0x89);
					i386_put8(&gen->text, 0xc1);
					i386_put8(&gen->text, 0x58);
					i386_emit_store_ecx(gen, output_type);
					i386_put8(&gen->text, 0x58);
				} else {
					fatal("unsupported rdtsc output constraint %s",
					      constraint);
				}
			}
			return;
		}
		char *copy = xstrdup(statement->asm_text);
		char *cursor = copy;

		while(cursor && *cursor)
		{
			char *next = strchr(cursor, ';');
			char *end;

			if(next)
				*next++ = 0;
			while(isspace((unsigned char)*cursor))
				cursor++;
			end = cursor + strlen(cursor);
			while(end > cursor && isspace((unsigned char)end[-1]))
				*--end = 0;
			if(!*cursor)
			{
				cursor = next;
				continue;
			}
			if(!strcmp(cursor, "hlt"))
				i386_put8(&gen->text, 0xf4);
			else if(!strcmp(cursor, "cli"))
				i386_put8(&gen->text, 0xfa);
			else if(!strcmp(cursor, "sti"))
				i386_put8(&gen->text, 0xfb);
			else if(!strcmp(cursor, "nop"))
				i386_put8(&gen->text, 0x90);
			else if(!strcmp(cursor, "cld"))
				i386_put8(&gen->text, 0xfc);
			else if(!strcmp(cursor, "std"))
				i386_put8(&gen->text, 0xfd);
			else if(!strcmp(cursor, "int3"))
				i386_put8(&gen->text, 0xcc);
			else if(!strcmp(cursor, "pause"))
			{
				i386_put8(&gen->text, 0xf3);
				i386_put8(&gen->text, 0x90);
			} else if(!strcmp(cursor, "ud2"))
			{
				i386_put8(&gen->text, 0x0f);
				i386_put8(&gen->text, 0x0b);
			} else {
				char *bad = xstrdup(cursor);

				free(copy);
				fatal("unsupported inline asm instruction: %s", bad);
			}
			cursor = next;
		}
		free(copy);
		return;
	}
	}
}

static void i386_gen_function(I386Gen *gen, Decl *declaration)
{
	I386ObjSymbol *symbol;
	long used = 0;
	size_t i;
	uint32_t start;

	gen->current = declaration;
	gen->nlocals = 0;
	for(i = 0; i < declaration->nparams; i++)
	{
		ARR_GROW(gen->locals, gen->nlocals, gen->caplocals, I386Local);
		gen->locals[gen->nlocals].name = declaration->params[i].name;
		gen->locals[gen->nlocals].type = declaration->params[i].type;
		gen->locals[gen->nlocals].offset = 8 + (long)i * 4;
		gen->nlocals++;
	}
	used = i386_collect_locals(gen, declaration->body, used);
	gen->frame_size = align_up(used, 4);
	snprintf(gen->return_label, sizeof(gen->return_label), ".Lreturn_%s_%ld", declaration->name, ++gen->label_number);
	start = (uint32_t)gen->text.n;
	symbol = i386_add_object_symbol(gen, declaration->name, declaration->type, true);
	symbol->local = declaration->is_static;
	symbol->defined = true;
	symbol->section = I386_SEC_TEXT;
	symbol->value = start;
	i386_put8(&gen->text, 0x55);
	i386_put8(&gen->text, 0x89);
	i386_put8(&gen->text, 0xe5);
	if(gen->frame_size)
	{
		i386_put8(&gen->text, 0x81);
		i386_put8(&gen->text, 0xec);
		i386_put32(&gen->text, (uint32_t)gen->frame_size);
	}
	i386_gen_statement(gen, declaration->body);
	i386_put8(&gen->text, 0x31);
	i386_put8(&gen->text, 0xc0);
	i386_define_label(gen, gen->return_label);
	i386_put8(&gen->text, 0xc9);
	i386_put8(&gen->text, 0xc3);
	symbol->size = (uint32_t)gen->text.n - start;
}

static uint32_t i386_intern_string(I386Gen *gen, const char *value);

static void i386_put_zeros(Buf *buffer, size_t count)
{
	while(count--)
		i386_put8(buffer, 0);
}

static void i386_write_global_initializer(I386Gen *gen, CType *type, Expr *initializer, const char *name)
{
	size_t start = gen->data.n;
	long size = i386_type_size(type);

	if(type->kind == TY_ARRAY)
	{
		size_t i;

		if(initializer->kind == EX_STR &&
		   (type->base->kind == TY_CHAR || type->base->kind == TY_U8))
		{
			size_t bytes = strlen(initializer->str) + 1;

			if(bytes > (size_t)type->count)
				fatal("initializer string is too long for %s", name);
			bputn(&gen->data, initializer->str, bytes);
			i386_put_zeros(&gen->data, (size_t)type->count - bytes);
			return;
		}
		if(initializer->kind != EX_INITLIST)
			fatal("array initializer for %s must use braces", name);
		if(initializer->nargs > (size_t)type->count)
			fatal("too many initializers for %s", name);
		for(i = 0; i < initializer->nargs; i++)
			i386_write_global_initializer(gen, type->base, initializer->args[i], name);
		i386_put_zeros(&gen->data,
			       (size_t)size - (gen->data.n - start));
		return;
	}
	if(type->kind == TY_STRUCT)
	{
		size_t i;

		if(initializer->kind != EX_INITLIST)
			fatal("struct initializer for %s must use braces", name);
		if(initializer->nargs > type->nmembers)
			fatal("too many initializers for %s", name);
		for(i = 0; i < initializer->nargs; i++)
		{
			long member_offset = 0;

			if(!i386_find_struct_member(type, type->members[i].name, &member_offset))
				fatal("internal: bad member offset in %s", name);
			if(gen->data.n < start + (size_t)member_offset)
				i386_put_zeros(&gen->data,
					       start + (size_t)member_offset - gen->data.n);
			i386_write_global_initializer(gen, type->members[i].type, initializer->args[i], name);
		}
		if(gen->data.n < start + (size_t)size)
			i386_put_zeros(&gen->data, start + (size_t)size - gen->data.n);
		return;
	}
	if(initializer->kind == EX_INITLIST)
	{
		if(initializer->nargs != 1)
			fatal("scalar initializer for %s has %zu elements", name, initializer->nargs);
		initializer = initializer->args[0];
	}
	if(initializer->kind == EX_STR && type->kind == TY_PTR)
	{
		uint32_t string_offset = i386_intern_string(gen, initializer->str);
		uint32_t relocation_offset = (uint32_t)gen->data.n;

		i386_put32(&gen->data, string_offset);
		i386_add_relocation(gen, I386_SEC_DATA, relocation_offset, R_386_32, NULL, I386_SEC_RODATA);
		return;
	}
	{
		long constant_value;
		uint64_t value;

		if(!eval_const_expr(initializer, &constant_value))
			fatal("unsupported i386 global initializer for %s", name);
		value = (uint64_t)(unsigned long)constant_value;
		if(size == 1)
			i386_put8(&gen->data, (uint8_t)value);
		else if(size == 2)
			i386_put16(&gen->data, (uint16_t)value);
		else if(size == 4)
			i386_put32(&gen->data, (uint32_t)value);
		else if(size == 8)
			bputn(&gen->data, (char *)&value, 8);
		else
			fatal("unsupported i386 scalar size for %s", name);
	}
}

static void i386_prepare_symbols_and_globals(I386Gen *gen)
{
	size_t i;

	for(i = 0; i < gen->program->n; i++)
	{
		Decl *declaration = gen->program->a[i];
		I386ObjSymbol *symbol;

		if(declaration->body || declaration->prototype)
		{
			symbol = i386_add_object_symbol(gen, declaration->name, declaration->type, true);
			if(declaration->is_static)
				symbol->local = true;
			if(declaration->body)
				symbol->defined = true;
			continue;
		}
		if(is_vla(declaration->type))
			fatal("variable-length array %s is only supported at block scope",
			      declaration->name);
		symbol = i386_add_object_symbol(gen, declaration->name, declaration->type, false);
		if(declaration->is_static)
			symbol->local = true;
		if(declaration->is_extern)
			continue;
		if(declaration->init)
		{
			long size = i386_type_size(declaration->type);

			while(gen->data.n % (size_t)i386_type_align(
						    declaration->type))
				i386_put8(&gen->data, 0);
			symbol->defined = true;
			symbol->section = I386_SEC_DATA;
			symbol->value = (uint32_t)gen->data.n;
			symbol->size = (uint32_t)size;
			i386_write_global_initializer(gen, declaration->type, declaration->init, declaration->name);
		} else {
			long alignment = i386_type_align(declaration->type);
			long size = i386_type_size(declaration->type);

			gen->bss_size = i386_align32(gen->bss_size,
						     (uint32_t)alignment);
			symbol->defined = true;
			symbol->section = I386_SEC_BSS;
			symbol->value = gen->bss_size;
			symbol->size = (uint32_t)(size > 0 ? size : 1);
			gen->bss_size += symbol->size;
		}
	}
}

static uint32_t i386_string_offset(Buf *strings, const char *string)
{
	uint32_t offset = (uint32_t)strings->n;

	bputn(strings, string, strlen(string) + 1);
	return offset;
}

static uint32_t i386_symbol_index(I386Gen *gen, const char *name)
{
	size_t i;

	for(i = 0; i < gen->nsymbols; i++)
		if(!strcmp(gen->symbols[i].name, name))
			return gen->symbols[i].index;
	fatal("internal: relocation references unknown symbol %s", name);
	return 0;
}

static void write_i386_relocatable(const char *path, Program *program)
{
	I386Gen gen = {0};
	Buf strtab = {0};
	Buf shstrtab = {0};
	Elf32_Sym *symtab;
	Elf32_Rel *text_relocations;
	Elf32_Rel *data_relocations;
	Elf32_Shdr sections[I386_SEC_COUNT];
	Elf32_Ehdr header;
	uint32_t section_names[I386_SEC_COUNT];
	uint32_t cursor;
	uint32_t text_offset;
	uint32_t rodata_offset;
	uint32_t data_offset;
	uint32_t rel_text_offset;
	uint32_t rel_data_offset;
	uint32_t symtab_offset;
	uint32_t strtab_offset;
	uint32_t shstrtab_offset;
	uint32_t section_headers_offset;
	uint32_t first_global;
	uint32_t local_symbol_count = 0;
	uint32_t symbol_count;
	uint32_t text_relocation_count = 0;
	uint32_t data_relocation_count = 0;
	uint8_t *file;
	size_t i;

	gen.program = program;
	i386_prepare_symbols_and_globals(&gen);
	for(i = 0; i < program->n; i++)
		if(program->a[i]->body)
			i386_gen_function(&gen, program->a[i]);
	i386_patch_jumps(&gen);
	put_u8(&strtab, 0);
	put_u8(&shstrtab, 0);
	memset(section_names, 0, sizeof(section_names));
	section_names[I386_SEC_TEXT] = i386_string_offset(&shstrtab,
							  ".text");
	section_names[I386_SEC_RODATA] = i386_string_offset(&shstrtab,
							    ".rodata");
	section_names[I386_SEC_DATA] = i386_string_offset(&shstrtab,
							  ".data");
	section_names[I386_SEC_BSS] = i386_string_offset(&shstrtab,
							 ".bss");
	section_names[I386_SEC_REL_TEXT] = i386_string_offset(&shstrtab,
							      ".rel.text");
	section_names[I386_SEC_REL_DATA] = i386_string_offset(&shstrtab,
							      ".rel.data");
	section_names[I386_SEC_SYMTAB] = i386_string_offset(&shstrtab,
							    ".symtab");
	section_names[I386_SEC_STRTAB] = i386_string_offset(&shstrtab,
							    ".strtab");
	section_names[I386_SEC_SHSTRTAB] = i386_string_offset(&shstrtab,
							      ".shstrtab");
	for(i = 0; i < gen.nsymbols; i++)
		if(gen.symbols[i].local)
			local_symbol_count++;
	first_global = 5 + local_symbol_count;
	symbol_count = 5 + (uint32_t)gen.nsymbols;
	symtab = xcalloc(symbol_count, sizeof(*symtab));
	symtab[1].st_info = ELF32_ST_INFO(STB_LOCAL, STT_SECTION);
	symtab[1].st_shndx = I386_SEC_TEXT;
	symtab[2].st_info = ELF32_ST_INFO(STB_LOCAL, STT_SECTION);
	symtab[2].st_shndx = I386_SEC_RODATA;
	symtab[3].st_info = ELF32_ST_INFO(STB_LOCAL, STT_SECTION);
	symtab[3].st_shndx = I386_SEC_DATA;
	symtab[4].st_info = ELF32_ST_INFO(STB_LOCAL, STT_SECTION);
	symtab[4].st_shndx = I386_SEC_BSS;
	{
		uint32_t local_index = 5;
		uint32_t global_index = first_global;

		for(i = 0; i < gen.nsymbols; i++)
		{
			I386ObjSymbol *object = &gen.symbols[i];
			Elf32_Sym *symbol;

			object->index = object->local ? local_index++ : global_index++;
			symbol = &symtab[object->index];
			symbol->st_name = i386_string_offset(&strtab, object->name);
			symbol->st_value = object->value;
			symbol->st_size = object->size;
			symbol->st_info = ELF32_ST_INFO(
				object->local ? STB_LOCAL : STB_GLOBAL,
				object->function ? STT_FUNC : STT_OBJECT);
			symbol->st_other = STV_DEFAULT;
			symbol->st_shndx = object->defined ? object->section : SHN_UNDEF;
		}
	}
	for(i = 0; i < gen.nrelocations; i++)
	{
		if(gen.relocations[i].target_section == I386_SEC_TEXT)
			text_relocation_count++;
		else if(gen.relocations[i].target_section == I386_SEC_DATA)
			data_relocation_count++;
		else
			fatal("internal: unsupported i386 relocation target section");
	}
	text_relocations = xcalloc(text_relocation_count ? text_relocation_count : 1, sizeof(*text_relocations));
	data_relocations = xcalloc(data_relocation_count ? data_relocation_count : 1, sizeof(*data_relocations));
	text_relocation_count = 0;
	data_relocation_count = 0;
	for(i = 0; i < gen.nrelocations; i++)
	{
		I386Relocation *source = &gen.relocations[i];
		Elf32_Rel *destination;
		uint32_t symbol_index;

		if(source->section_symbol)
		{
			switch(source->section_symbol)
			{
			case I386_SEC_TEXT:
				symbol_index = 1;
				break;
			case I386_SEC_RODATA:
				symbol_index = 2;
				break;
			case I386_SEC_DATA:
				symbol_index = 3;
				break;
			case I386_SEC_BSS:
				symbol_index = 4;
				break;
			default:
				fatal("internal: bad i386 section relocation");
			}
		} else {
			symbol_index = i386_symbol_index(&gen, source->symbol);
		}
		if(source->target_section == I386_SEC_TEXT)
			destination = &text_relocations[text_relocation_count++];
		else
			destination = &data_relocations[data_relocation_count++];
		destination->r_offset = source->offset;
		destination->r_info = ELF32_R_INFO(symbol_index,
						   source->type);
	}
	cursor = sizeof(Elf32_Ehdr);
	text_offset = i386_align32(cursor, 16);
	cursor = text_offset + (uint32_t)gen.text.n;
	rodata_offset = i386_align32(cursor, 4);
	cursor = rodata_offset + (uint32_t)gen.rodata.n;
	data_offset = i386_align32(cursor, 4);
	cursor = data_offset + (uint32_t)gen.data.n;
	rel_text_offset = i386_align32(cursor, 4);
	cursor = rel_text_offset +
		 text_relocation_count * sizeof(Elf32_Rel);
	rel_data_offset = i386_align32(cursor, 4);
	cursor = rel_data_offset +
		 data_relocation_count * sizeof(Elf32_Rel);
	symtab_offset = i386_align32(cursor, 4);
	cursor = symtab_offset + symbol_count * sizeof(Elf32_Sym);
	strtab_offset = cursor;
	cursor += (uint32_t)strtab.n;
	shstrtab_offset = cursor;
	cursor += (uint32_t)shstrtab.n;
	section_headers_offset = i386_align32(cursor, 4);
	cursor = section_headers_offset +
		 I386_SEC_COUNT * sizeof(Elf32_Shdr);
	file = xcalloc(cursor, 1);
	memset(&header, 0, sizeof(header));
	memcpy(header.e_ident, ELFMAG, SELFMAG);
	header.e_ident[EI_CLASS] = ELFCLASS32;
	header.e_ident[EI_DATA] = ELFDATA2LSB;
	header.e_ident[EI_VERSION] = EV_CURRENT;
	header.e_ident[EI_OSABI] = ELFOSABI_SYSV;
	header.e_type = ET_REL;
	header.e_machine = EM_386;
	header.e_version = EV_CURRENT;
	header.e_ehsize = sizeof(Elf32_Ehdr);
	header.e_shoff = section_headers_offset;
	header.e_shentsize = sizeof(Elf32_Shdr);
	header.e_shnum = I386_SEC_COUNT;
	header.e_shstrndx = I386_SEC_SHSTRTAB;
	memset(sections, 0, sizeof(sections));
	sections[I386_SEC_TEXT].sh_name = section_names[I386_SEC_TEXT];
	sections[I386_SEC_TEXT].sh_type = SHT_PROGBITS;
	sections[I386_SEC_TEXT].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
	sections[I386_SEC_TEXT].sh_offset = text_offset;
	sections[I386_SEC_TEXT].sh_size = gen.text.n;
	sections[I386_SEC_TEXT].sh_addralign = 16;
	sections[I386_SEC_RODATA].sh_name = section_names[I386_SEC_RODATA];
	sections[I386_SEC_RODATA].sh_type = SHT_PROGBITS;
	sections[I386_SEC_RODATA].sh_flags = SHF_ALLOC;
	sections[I386_SEC_RODATA].sh_offset = rodata_offset;
	sections[I386_SEC_RODATA].sh_size = gen.rodata.n;
	sections[I386_SEC_RODATA].sh_addralign = 1;
	sections[I386_SEC_DATA].sh_name = section_names[I386_SEC_DATA];
	sections[I386_SEC_DATA].sh_type = SHT_PROGBITS;
	sections[I386_SEC_DATA].sh_flags = SHF_ALLOC | SHF_WRITE;
	sections[I386_SEC_DATA].sh_offset = data_offset;
	sections[I386_SEC_DATA].sh_size = gen.data.n;
	sections[I386_SEC_DATA].sh_addralign = 4;
	sections[I386_SEC_BSS].sh_name = section_names[I386_SEC_BSS];
	sections[I386_SEC_BSS].sh_type = SHT_NOBITS;
	sections[I386_SEC_BSS].sh_flags = SHF_ALLOC | SHF_WRITE;
	sections[I386_SEC_BSS].sh_offset = data_offset + gen.data.n;
	sections[I386_SEC_BSS].sh_size = gen.bss_size;
	sections[I386_SEC_BSS].sh_addralign = 4;
	sections[I386_SEC_REL_TEXT].sh_name =
		section_names[I386_SEC_REL_TEXT];
	sections[I386_SEC_REL_TEXT].sh_type = SHT_REL;
	sections[I386_SEC_REL_TEXT].sh_offset = rel_text_offset;
	sections[I386_SEC_REL_TEXT].sh_size =
		text_relocation_count * sizeof(Elf32_Rel);
	sections[I386_SEC_REL_TEXT].sh_link = I386_SEC_SYMTAB;
	sections[I386_SEC_REL_TEXT].sh_info = I386_SEC_TEXT;
	sections[I386_SEC_REL_TEXT].sh_addralign = 4;
	sections[I386_SEC_REL_TEXT].sh_entsize = sizeof(Elf32_Rel);
	sections[I386_SEC_REL_DATA].sh_name =
		section_names[I386_SEC_REL_DATA];
	sections[I386_SEC_REL_DATA].sh_type = SHT_REL;
	sections[I386_SEC_REL_DATA].sh_offset = rel_data_offset;
	sections[I386_SEC_REL_DATA].sh_size =
		data_relocation_count * sizeof(Elf32_Rel);
	sections[I386_SEC_REL_DATA].sh_link = I386_SEC_SYMTAB;
	sections[I386_SEC_REL_DATA].sh_info = I386_SEC_DATA;
	sections[I386_SEC_REL_DATA].sh_addralign = 4;
	sections[I386_SEC_REL_DATA].sh_entsize = sizeof(Elf32_Rel);
	sections[I386_SEC_SYMTAB].sh_name = section_names[I386_SEC_SYMTAB];
	sections[I386_SEC_SYMTAB].sh_type = SHT_SYMTAB;
	sections[I386_SEC_SYMTAB].sh_offset = symtab_offset;
	sections[I386_SEC_SYMTAB].sh_size = symbol_count * sizeof(Elf32_Sym);
	sections[I386_SEC_SYMTAB].sh_link = I386_SEC_STRTAB;
	sections[I386_SEC_SYMTAB].sh_info = first_global;
	sections[I386_SEC_SYMTAB].sh_addralign = 4;
	sections[I386_SEC_SYMTAB].sh_entsize = sizeof(Elf32_Sym);
	sections[I386_SEC_STRTAB].sh_name = section_names[I386_SEC_STRTAB];
	sections[I386_SEC_STRTAB].sh_type = SHT_STRTAB;
	sections[I386_SEC_STRTAB].sh_offset = strtab_offset;
	sections[I386_SEC_STRTAB].sh_size = strtab.n;
	sections[I386_SEC_STRTAB].sh_addralign = 1;
	sections[I386_SEC_SHSTRTAB].sh_name =
		section_names[I386_SEC_SHSTRTAB];
	sections[I386_SEC_SHSTRTAB].sh_type = SHT_STRTAB;
	sections[I386_SEC_SHSTRTAB].sh_offset = shstrtab_offset;
	sections[I386_SEC_SHSTRTAB].sh_size = shstrtab.n;
	sections[I386_SEC_SHSTRTAB].sh_addralign = 1;
	memcpy(file, &header, sizeof(header));
	if(gen.text.n)
		memcpy(file + text_offset, gen.text.s, gen.text.n);
	if(gen.rodata.n)
		memcpy(file + rodata_offset, gen.rodata.s, gen.rodata.n);
	if(gen.data.n)
		memcpy(file + data_offset, gen.data.s, gen.data.n);
	if(text_relocation_count)
		memcpy(file + rel_text_offset, text_relocations, text_relocation_count * sizeof(Elf32_Rel));
	if(data_relocation_count)
		memcpy(file + rel_data_offset, data_relocations, data_relocation_count * sizeof(Elf32_Rel));
	memcpy(file + symtab_offset, symtab, symbol_count * sizeof(Elf32_Sym));
	memcpy(file + strtab_offset, strtab.s, strtab.n);
	memcpy(file + shstrtab_offset, shstrtab.s, shstrtab.n);
	memcpy(file + section_headers_offset, sections, sizeof(sections));
	write_file(path, (char *)file, cursor);
}

/* ---------- Driver ---------- */

static void usage(void)
{
	printf("%s\nUsage: aneoc [options] file.AC ...\n\n"
	       "  -o FILE          set output file\n"
	       "  -S               emit generated x86-64 assembly text\n"
	       "  -m32 -c          emit an ELF32 i386 relocatable object\n"
	       "  -ffreestanding   accepted for freestanding kernel builds\n"
	       "  -lNAME           add a DT_NEEDED library to x86-64 output\n"
	       "  --version        show version\n\n"
	       "AneoC type keywords: VD=void, C=char, S=short, "
	       "L=long, U=unsigned, DB=double, TD=typedef, ST=struct, SC=static.\n"
	       "AneoC directly writes x86-64 executables and ELF32 i386 object "
	       "files. It does not invoke GCC, Clang, cc, TinyCC, Python, as, "
	       "ld, or another linker.\n",
	       VERSION);
}

int main(int argc, char **argv)
{
	Program prog = {0};
	char **src = NULL;
	char **libs = NULL;
	size_t nsrc = 0, csrc = 0, nlibs = 0, clibs = 0, i;
	char *out = xstrdup("a.out");
	bool assembly_only = false;
	bool compile_only = false;
	bool target_i386 = false;
	bool freestanding = false;
	bool needs_x11 = false;

	for(i = 1; i < (size_t)argc; i++)
	{
		if(!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
		{
			usage();
			return 0;
		}
		if(!strcmp(argv[i], "--version"))
		{
			puts(VERSION);
			return 0;
		}
		if(!strcmp(argv[i], "-o"))
		{
			if(++i >= (size_t)argc)
				fatal("-o needs a file");
			out = argv[i];
		} else if(!strcmp(argv[i], "-S"))
		{
			assembly_only = true;
		} else if(!strcmp(argv[i], "-c"))
		{
			compile_only = true;
		} else if(!strcmp(argv[i], "-m32"))
		{
			target_i386 = true;
		} else if(!strcmp(argv[i], "-ffreestanding"))
		{
			freestanding = true;
		} else if(!strcmp(argv[i], "-fno-builtin") ||
			  !strcmp(argv[i], "-fno-stack-protector") ||
			  !strcmp(argv[i], "-fno-pie") ||
			  !strcmp(argv[i], "-fno-pic") ||
			  !strcmp(argv[i], "-nostdlib") ||
			  !strcmp(argv[i], "-nostdinc") ||
			  !strcmp(argv[i], "-Wall") ||
			  !strcmp(argv[i], "-Wextra") ||
			  !strcmp(argv[i], "-Wpedantic") ||
			  !strncmp(argv[i], "-O", 2) ||
			  !strncmp(argv[i], "-std=", 5) ||
			  !strncmp(argv[i], "-I", 2) ||
			  !strncmp(argv[i], "-D", 2))
		{
			/* Accepted compatibility flags; AneoC is always freestanding in -m32 -c mode. */
		} else if(!strncmp(argv[i], "-l", 2))
		{
			ARR_GROW(libs, nlibs, clibs, char *);
			libs[nlibs++] = argv[i];
		} else if(argv[i][0] == '-')
		{
			fatal("unsupported option %s", argv[i]);
		} else {
			ARR_GROW(src, nsrc, csrc, char *);
			src[nsrc++] = argv[i];
		}
	}
	if(!nsrc)
		fatal("no input files");
	if(!assembly_only && strlen(out) >= 2 &&
	   !strcmp(out + strlen(out) - 2, ".o"))
	{
		compile_only = true;
		target_i386 = true;
	}
	if(compile_only)
		target_i386 = true;
	for(i = 0; i < nsrc; i++)
	{
		char *raw = read_file(src[i]);
		char *text = preprocess_source(raw);
		Tokens tokens = lex_source(text, src[i]);

		if(strstr(raw, "X11/Xlib.h") ||
		   strstr(raw, "XOpenDisplay") ||
		   strstr(raw, "XCreateSimpleWindow") ||
		   strstr(raw, "XDrawString"))
			needs_x11 = true;
		parse_program(&tokens, &prog);
	}
	(void)freestanding;
	if(compile_only)
	{
		if(assembly_only)
			fatal("-S and -c cannot be combined yet");
		write_i386_relocatable(out, &prog);
		return 0;
	}
	if(target_i386)
		fatal("-m32 currently requires -c");
	{
		char *assembly = generate(&prog);

		if(assembly_only)
		{
			write_file(out, assembly, strlen(assembly));
			return 0;
		}
		{
			AsmImage image;

			internal_assemble(assembly, &image);
			write_independent_elf(out, &image, needs_x11, libs, nlibs);
		}
	}
	return 0;
}
