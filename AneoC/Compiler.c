/*
The official compiler for the AneoC Programming Language.
AneoC is made for AneoEngine, as its main programming language.
Copyright (C) 2025-2026 Rocco Himel.
AneoC is distributed under the AneoEngine License V*.*.
*/
//Macros, ANSI includes.
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
#define VERSION "AneoC 1.0"
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
	va_list argument_list;
	fprintf(stderr, "\033[37;41mERROR:\033[0m Compilation failed: ");
	va_start(argument_list, fmt);
	vfprintf(stderr, fmt, argument_list);
	va_end(argument_list);
	fputc('\n', stderr);
	exit(0);
}
//Compiler memory functions.

static void *xmalloc(size_t count)
{
	void *position = malloc(count ? count : 1);
	if(!position)
		fatal("out of memory");
	return position;
}

static void *xcalloc(size_t argument_count, size_t element_size_1)
{
	void *position = calloc(argument_count ? argument_count : 1, element_size_1 ? element_size_1 : 1);
	if(!position)
		fatal("out of memory");
	return position;
}

static void *xrealloc(void *pointer, size_t count)
{
	pointer = realloc(pointer, count ? count : 1);
	if(!pointer)
		fatal("out of memory");
	return pointer;
}

static char *xstrdup(const char *string)
{
	size_t length = strlen(string) + 1;
	char *position = xmalloc(length);
	memcpy(position, string, length);
	return position;
}

static char *xstrndup2(const char *string, size_t length)
{
	char *position = xmalloc(length + 1);
	memcpy(position, string, length);
	position[length] = 0;
	return position;
}

static void bneed(Buf *buffer, size_t extra)
{
	size_t need = buffer->n + extra + 1;
	if(need <= buffer->cap)
		return;
	if(!buffer->cap)
		buffer->cap = 4096;
	while(buffer->cap < need)
		buffer->cap *= 2;
	buffer->s = xrealloc(buffer->s, buffer->cap);
}

static void bputn(Buf *buffer, const char *string, size_t count)
{
	bneed(buffer, count);
	memcpy(buffer->s + buffer->n, string, count);
	buffer->n += count;
	buffer->s[buffer->n] = 0;
}

static void bputs(Buf *buffer, const char *string)
{
	bputn(buffer, string, strlen(string));
}

static void bprintf(Buf *buffer, const char *fmt, ...)
{
	va_list argument_list, copied_argument_list;
	int count;
	va_start(argument_list, fmt);
	va_copy(copied_argument_list, argument_list);
	count = vsnprintf(NULL, 0, fmt, copied_argument_list);
	va_end(copied_argument_list);
	if(count < 0)
		fatal("formatting failed");
	bneed(buffer, (size_t)count);
	vsnprintf(buffer->s + buffer->n, buffer->cap - buffer->n, fmt, argument_list);
	va_end(argument_list);
	buffer->n += (size_t)count;
}

static char *read_file(const char *path)
{
	FILE *file = fopen(path, "rb");
	Buf buffer = {0};
	char tmp[8192];
	size_t count;
	if(!file)
		fatal("cannot open %s: %s", path, strerror(errno));
	while((count = fread(tmp, 1, sizeof(tmp), file)) != 0)
		bputn(&buffer, tmp, count);
	if(ferror(file))
		fatal("cannot read %s", path);
	fclose(file);
	if(!buffer.s)
		buffer.s = xstrdup("");
	return buffer.s;
}

static void write_file(const char *path, const char *string, size_t count)
{
	FILE *file = fopen(path, "wb");
	if(!file)
		fatal("cannot create %s: %s", path, strerror(errno));
	if(count && fwrite(string, 1, count, file) != count)
		fatal("cannot write %s", path);
	if(fclose(file))
		fatal("cannot close %s", path);
}
typedef struct
{
	char *name;
	char *value;
} Macro;

static Macro *find_macro(Macro *macros, size_t nmacros, const char *name, size_t length)
{
	size_t index;
	for(index = 0; index < nmacros; index++)
		if(strlen(macros[index].name) == length &&
		   !memcmp(macros[index].name, name, length))
			return &macros[index];
	return NULL;
}

static char *preprocess_source(const char *text)
{
	Macro *macros = NULL;
	size_t nmacros = 0, capmacros = 0;
	Buf body = {0};
	Buf out = {0};
	const char *position = text;
	size_t index, count;
	enum
	{
		PP_NORMAL,
		PP_STRING,
		PP_CHAR,
		PP_LINE_COMMENT,
		PP_BLOCK_COMMENT
	} state = PP_NORMAL;
	bool escaped = false;
	while(*position) {
		const char *line = position;
		const char *end = strchr(position, '\n');
		const char *cursor;
		if(!end)
			end = position + strlen(position);
		cursor = line;
		while(cursor < end && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r'))
			cursor++;
		if(cursor < end && *cursor == '#') {
			cursor++;
			while(cursor < end && isspace((unsigned char)*cursor))
				cursor++;
			if(end - cursor >= 6 && !memcmp(cursor, "define", 6) &&
			   (cursor + 6 == end || isspace((unsigned char)cursor[6])))
			{
				const char *name;
				const char *value;
				const char *value_end;
				Macro *old;
				cursor += 6;
				while(cursor < end && isspace((unsigned char)*cursor))
					cursor++;
				name = cursor;
				if(cursor < end && (isalpha((unsigned char)*cursor) || *cursor == '_')) {
					cursor++;
					while(cursor < end && (isalnum((unsigned char)*cursor) || *cursor == '_'))
						cursor++;
					if(cursor < end && *cursor != '(') {
						value = cursor;
						while(value < end && isspace((unsigned char)*value))
							value++;
						value_end = end;
						while(value_end > value &&
						      isspace((unsigned char)value_end[-1]))
							value_end--;
						old = find_macro(macros, nmacros, name, (size_t)(cursor - name));
						if(!old) {
							ARR_GROW(macros, nmacros, capmacros, Macro);
							old = &macros[nmacros++];
							old->name = xstrndup2(name, (size_t)(cursor - name));
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
		position = *end ? end + 1 : end;
	}
	if(!body.s)
		body.s = xstrdup("");
	count = body.n;
	for(index = 0; index < count;) {
		char character = body.s[index];
		char next = index + 1 < count ? body.s[index + 1] : 0;
		if(state == PP_NORMAL) {
			if(character == '/' && next == '/') {
				bputn(&out, "//", 2);
				index += 2;
				state = PP_LINE_COMMENT;
				continue;
			}
			if(character == '/' && next == '*') {
				bputn(&out, "/*", 2);
				index += 2;
				state = PP_BLOCK_COMMENT;
				continue;
			}
			if(character == '"' || character == '\'') {
				bputn(&out, &character, 1);
				index++;
				state = character == '"' ? PP_STRING : PP_CHAR;
				escaped = false;
				continue;
			}
			if(isalpha((unsigned char)character) || character == '_') {
				size_t start = index;
				Macro *macro;
				index++;
				while(index < count && (isalnum((unsigned char)body.s[index]) ||
						body.s[index] == '_'))
					index++;
				macro = find_macro(macros, nmacros, body.s + start, index - start);
				if(macro) {
					bputn(&out, "(", 1);
					bputs(&out, macro->value);
					bputn(&out, ")", 1);
				} else {
					bputn(&out, body.s + start, index - start);
				}
				continue;
			}
			bputn(&out, &character, 1);
			index++;
			continue;
		}
		bputn(&out, &character, 1);
		index++;
		if(state == PP_LINE_COMMENT) {
			if(character == '\n')
				state = PP_NORMAL;
		} else if(state == PP_BLOCK_COMMENT)
		{
			if(character == '*' && next == '/') {
				bputn(&out, "/", 1);
				index++;
				state = PP_NORMAL;
			}
		} else if(escaped)
		{
			escaped = false;
		} else if(character == '\\')
		{
			escaped = true;
		} else if((state == PP_STRING && character == '"') ||
			  (state == PP_CHAR && character == '\''))
		{
			state = PP_NORMAL;
		}
	}
	if(!out.s)
		out.s = xstrdup("");
	return out.s;
}
//eclaration TD
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
//SC defs for AneoC types.
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

static CType *new_type(TypeKind third_index, CType *base, long count, const char *name)
{
	CType *type = xcalloc(1, sizeof(*type));
	type->kind = third_index;
	type->base = base;
	type->count = count;
	type->name = name;
	return type;
}

static CType *ptr_to(CType *base)
{
	return new_type(TY_PTR, base, 0, NULL);
}

static CType *array_of(CType *base, long count)
{
	return new_type(TY_ARRAY, base, count, NULL);
}

static CType *vla_of(CType *base, const char *bound)
{
	return new_type(TY_ARRAY, base, -1, xstrdup(bound));
}

static bool is_vla(CType *type)
{
	return type && type->kind == TY_ARRAY && type->count < 0;
}

static long type_size(CType *type)
{
	switch(type->kind) {
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
		return type->size;
	case TY_ARRAY:
		if(is_vla(type))
			return 8;
		return type_size(type->base) * type->count;
	case TY_XEVENT:
		return 192;
	case TY_VALIST:
		return 24;
	}
	fatal("internal: unknown type");
	return 0;
}

static long type_align(CType *type)
{
	if(type->kind == TY_CHAR || type->kind == TY_U8)
		return 1;
	if(type->kind == TY_SHORT || type->kind == TY_U16)
		return 2;
	if(type->kind == TY_INT || type->kind == TY_U32)
		return 4;
	if(type->kind == TY_ARRAY)
		return type_align(type->base);
	if(type->kind == TY_STRUCT)
		return type->align ? type->align : 1;
	return 8;
}
//Lexer
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

static void tok_push(Tokens *token_stream, TokKind third_index, const char *string, size_t count, int line, int col)
{
	ARR_GROW(token_stream->a, token_stream->n, token_stream->cap, Token);
	token_stream->a[token_stream->n].kind = third_index;
	token_stream->a[token_stream->n].v = xstrndup2(string, count);
	token_stream->a[token_stream->n].line = line;
	token_stream->a[token_stream->n].col = col;
	token_stream->n++;
}

static bool starts(const char *string, size_t index, const char *x_value)
{
	return !strncmp(string + index, x_value, strlen(x_value));
}

static Tokens lex_source(const char *text, const char *file)
{
	Tokens ts = {0};
	size_t index = 0, count = strlen(text);
	int line = 1, col = 1;
	bool bol = true;
	while(index < count) {
		char character = text[index];
		if(bol) {
			size_t inner_index = index;
			int ccol = col;
			while(inner_index < count && (text[inner_index] == ' ' || text[inner_index] == '\t' || text[inner_index] == '\r')) {
				inner_index++;
				ccol++;
			}
			if(inner_index < count && text[inner_index] == '#') {
				bool cont;
				do {
					cont = false;
					while(index < count && text[index] != '\n') {
						if(text[index] == '\\')
							cont = true;
						else if(text[index] != '\r' && !isspace((unsigned char)text[index]))
							cont = false;
						index++;
						col++;
					}
					if(index < count && text[index] == '\n') {
						index++;
						line++;
						col = 1;
						bol = true;
					}
				} while(cont && index < count);
				continue;
			}
		}
		if(isspace((unsigned char)character)) {
			if(character == '\n') {
				line++;
				col = 1;
				bol = true;
			} else
				col++;
			index++;
			continue;
		}
		bol = false;
		if(index + 1 < count && text[index] == '/' && text[index + 1] == '/') {
			index += 2;
			col += 2;
			while(index < count && text[index] != '\n') {
				index++;
				col++;
			}
			continue;
		}
		if(index + 1 < count && text[index] == '/' && text[index + 1] == '*') {
			index += 2;
			col += 2;
			while(index + 1 < count && !(text[index] == '*' && text[index + 1] == '/')) {
				if(text[index] == '\n') {
					line++;
					col = 1;
					bol = true;
					index++;
				} else {
					index++;
					col++;
				}
			}
			if(index + 1 >= count)
				fatal("%s:%d:%d: unterminated comment", file, line, col);
			index += 2;
			col += 2;
			continue;
		}
		{
			int source_line = line, scan_code = col;
			size_t start = index;
			if(isalpha((unsigned char)character) || character == '_') {
				index++;
				col++;
				while(index < count && (isalnum((unsigned char)text[index]) || text[index] == '_')) {
					index++;
					col++;
				}
				tok_push(&ts, TK_ID, text + start, index - start, source_line, scan_code);
				continue;
			}
			if(isdigit((unsigned char)character)) {
				bool floating = false;
				index++;
				col++;
				if(character == '0' && index < count && (text[index] == 'x' || text[index] == 'X')) {
					index++;
					col++;
					while(index < count && isxdigit((unsigned char)text[index])) {
						index++;
						col++;
					}
				} else {
					while(index < count && isdigit((unsigned char)text[index])) {
						index++;
						col++;
					}
					if(index < count && text[index] == '.') {
						floating = true;
						index++;
						col++;
						while(index < count && isdigit((unsigned char)text[index])) {
							index++;
							col++;
						}
					}
					if(index < count && (text[index] == 'e' || text[index] == 'E')) {
						floating = true;
						index++;
						col++;
						if(index < count && (text[index] == '+' || text[index] == '-')) {
							index++;
							col++;
						}
						while(index < count && isdigit((unsigned char)text[index])) {
							index++;
							col++;
						}
					}
				}
				if(floating) {
					if(index < count && (text[index] == 'f' || text[index] == 'F' || text[index] == 'l' || text[index] == 'L')) {
						index++;
						col++;
					}
				} else {
					while(index < count && strchr("uUlL", text[index])) {
						index++;
						col++;
					}
				}
				tok_push(&ts, TK_NUM, text + start, index - start, source_line, scan_code);
				continue;
			}
			if(character == '"' || character == '\'') {
				char cursor = character;
				bool esc = false;
				index++;
				col++;
				while(index < count) {
					char value = text[index++];
					col++;
					if(value == '\n') {
						line++;
						col = 1;
					}
					if(esc)
						esc = false;
					else if(value == '\\')
						esc = true;
					else if(value == cursor)
						break;
				}
				if(text[index - 1] != cursor)
					fatal("%s:%d:%d: unterminated literal", file, source_line, scan_code);
				tok_push(&ts, cursor == '"' ? TK_STR : TK_CHAR, text + start, index - start, source_line, scan_code);
				continue;
			}
			{
				static const char *ops[] = {
					"<<=", ">>=", "...", "==", "!=", "<=", ">=", "&&", "||", "++", "--", "->", "<<", ">>", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", NULL};
				int third_index;
				for(third_index = 0; ops[third_index]; third_index++) {
					size_t token_length = strlen(ops[third_index]);
					if(index + token_length <= count && starts(text, index, ops[third_index])) {
						tok_push(&ts, TK_OP, text + index, token_length, source_line, scan_code);
						index += token_length;
						col += (int)token_length;
						goto token_done;
					}
				}
			}
			if(strchr("{}[]();,.*&+-/%!~<>=|^?:", character)) {
				tok_push(&ts, TK_OP, text + index, 1, source_line, scan_code);
				index++;
				col++;
				continue;
			}
			fatal("%s:%d:%d: unexpected character '%c'", file, line, col, character);
		}
	token_done:;
	}
	tok_push(&ts, TK_EOF, "<eof>", 5, line, col);
	ts.file = file;
	return ts;
}
//AST tree
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

static Expr *new_expr(ExprKind third_index)
{
	Expr *expression = xcalloc(1, sizeof(*expression));
	expression->kind = third_index;
	return expression;
}

static Stmt *new_stmt(StmtKind third_index)
{
	Stmt *statement = xcalloc(1, sizeof(*statement));
	statement->kind = third_index;
	return statement;
}

static Decl *new_decl(void)
{
	return xcalloc(1, sizeof(Decl));
}
//Parser
typedef struct
{
	Tokens *ts;
	size_t p;
	Program *prog;
} Parser;

static Token *ptok(Parser *parser)
{
	return &parser->ts->a[parser->p];
}

static bool peq(Parser *parser, const char *string)
{
	return !strcmp(ptok(parser)->v, string);
}

static bool paccept(Parser *parser, const char *string)
{
	if(peq(parser, string)) {
		parser->p++;
		return true;
	}
	return false;
}

static void perr(Parser *parser, const char *fmt, ...)
{
	va_list argument_list;
	Token *token = ptok(parser);
	fprintf(stderr, "aneoc: %s:%d:%d: ", parser->ts->file, token->line, token->col);
	va_start(argument_list, fmt);
	vfprintf(stderr, fmt, argument_list);
	va_end(argument_list);
	fprintf(stderr, "; got '%s'\n", token->v);
	exit(1);
}

static void pexpect(Parser *parser, const char *string)
{
	if(!paccept(parser, string))
		perr(parser, "expected '%s'", string);
}

static char *pexpect_id(Parser *parser, bool optional)
{
	if(ptok(parser)->kind == TK_ID)
		return parser->ts->a[parser->p++].v;
	if(optional)
		return xstrdup("");
	perr(parser, "expected identifier");
	return NULL;
}

static CType *find_alias(Program *prog, const char *name)
{
	size_t index;
	for(index = 0; index < prog->naliases; index++)
		if(!strcmp(prog->aliases[index].name, name))
			return prog->aliases[index].type;
	return NULL;
}

static bool same_type(CType *array, CType *base_type)
{
	if(array == base_type)
		return true;
	if(!array || !base_type || array->kind != base_type->kind || array->count != base_type->count)
		return false;
	if(array->kind == TY_PTR || array->kind == TY_ARRAY)
		return same_type(array->base, base_type->base);
	return true;
}

static void add_alias(Parser *parser, char *name, CType *type)
{
	Program *prog = parser->prog;
	CType *old = find_alias(prog, name);
	if(old) {
		if(same_type(old, type))
			return;
		perr(parser, "conflicting typedef '%s'", name);
	}
	ARR_GROW(prog->aliases, prog->naliases, prog->capaliases, TypeAlias);
	prog->aliases[prog->naliases].name = name;
	prog->aliases[prog->naliases].type = type;
	prog->naliases++;
}

static CType *find_struct_tag(Program *prog, const char *name)
{
	size_t index;
	for(index = 0; index < prog->ntags; index++)
		if(!strcmp(prog->tags[index].name, name))
			return prog->tags[index].type;
	return NULL;
}

static void add_struct_tag(Parser *parser, char *name, CType *type)
{
	CType *old;
	if(!name || !*name)
		return;
	old = find_struct_tag(parser->prog, name);
	if(old && old != type)
		perr(parser, "conflicting struct tag '%s'", name);
	if(old)
		return;
	ARR_GROW(parser->prog->tags, parser->prog->ntags, parser->prog->captags, StructTag);
	parser->prog->tags[parser->prog->ntags].name = name;
	parser->prog->tags[parser->prog->ntags].type = type;
	parser->prog->ntags++;
}

static StructMember *find_struct_member(CType *type, const char *name)
{
	static StructMember xevent_type_member = {"type", &T_INT, 0};
	size_t index;
	if(!type)
		return NULL;
	if(type->kind == TY_XEVENT)
		return !strcmp(name, "type") ? &xevent_type_member : NULL;
	if(type->kind != TY_STRUCT)
		return NULL;
	for(index = 0; index < type->nmembers; index++)
		if(!strcmp(type->members[index].name, name))
			return &type->members[index];
	return NULL;
}
static Declarator parse_declarator(Parser *p, CType *base, bool unnamed);
static Expr *parse_expr(Parser *p, int minprec);
static bool eval_const_expr(Expr *e, long *value);

static bool type_start(Parser *parser)
{
	const char *character = ptok(parser)->v;
	if(find_alias(parser->prog, character))
		return true;
	return !strcmp(character, "VD") || !strcmp(character, "C") || !strcmp(character, "CC") ||
	       !strcmp(character, "INT") || !strcmp(character, "ULL") || !strcmp(character, "DB") || !strcmp(character, "L") ||
	       !strcmp(character, "S") || !strcmp(character, "U") || !strcmp(character, "void") ||
	       !strcmp(character, "char") || !strcmp(character, "short") || !strcmp(character, "int") ||
	       !strcmp(character, "double") ||
	       !strcmp(character, "FILE") || !strcmp(character, "Display") || !strcmp(character, "Window") ||
	       !strcmp(character, "GC") || !strcmp(character, "Font") || !strcmp(character, "XEvent") ||
	       !strcmp(character, "va_list") || !strcmp(character, "size_t") ||
	       !strcmp(character, "uint8_t") || !strcmp(character, "uint16_t") ||
	       !strcmp(character, "uint32_t") || !strcmp(character, "uint64_t") ||
	       !strcmp(character, "int8_t") || !strcmp(character, "int16_t") ||
	       !strcmp(character, "int32_t") || !strcmp(character, "int64_t") ||
	       !strcmp(character, "const") || !strcmp(character, "unsigned") ||
	       !strcmp(character, "long") || !strcmp(character, "signed") ||
	       !strcmp(character, "struct") || !strcmp(character, "ST");
}

static bool parse_gnu_attributes(Parser *parser)
{
	bool packed = false;
	while(peq(parser, "__attribute__") || peq(parser, "__attribute")) {
		int depth = 0;
		parser->p++;
		pexpect(parser, "(");
		depth = 1;
		while(depth > 0) {
			if(ptok(parser)->kind == TK_EOF)
				perr(parser, "unterminated __attribute__");
			if(peq(parser, "packed") || peq(parser, "__packed__"))
				packed = true;
			if(peq(parser, "("))
				depth++;
			else if(peq(parser, ")"))
				depth--;
			parser->p++;
		}
	}
	return packed;
}

static void finish_struct_layout(CType *type, bool packed)
{
	long offset = 0;
	long maximum = 1;
	size_t index;
	type->packed = packed;
	for(index = 0; index < type->nmembers; index++) {
		long alignment = packed ? 1 : type_align(type->members[index].type);
		long size = type_size(type->members[index].type);
		if(alignment < 1)
			alignment = 1;
		offset = (offset + alignment - 1) / alignment * alignment;
		type->members[index].offset = offset;
		offset += size;
		if(alignment > maximum)
			maximum = alignment;
	}
	type->align = packed ? 1 : maximum;
	type->size = packed ? offset : (offset + maximum - 1) / maximum * maximum;
}

static CType *parse_type(Parser *parser)
{
	const char *character;
	CType *alias;
	bool is_unsigned = false;
	paccept(parser, "const");
	if(paccept(parser, "struct") || paccept(parser, "ST")) {
		char *tag = NULL;
		CType *type;
		if(ptok(parser)->kind == TK_ID && !peq(parser, "{"))
			tag = pexpect_id(parser, false);
		if(!paccept(parser, "{")) {
			type = find_struct_tag(parser->prog, tag ? tag : "");
			if(!type)
				perr(parser, "unknown struct '%s'", tag ? tag : "");
			return type;
		}
		type = new_type(TY_STRUCT, NULL, 0, tag ? tag : "<anonymous>");
		add_struct_tag(parser, tag, type);
		while(!paccept(parser, "}")) {
			CType *member_base = parse_type(parser);
			Declarator member = parse_declarator(parser, member_base, false);
			if(member.function)
				perr(parser, "struct member cannot be a function");
			pexpect(parser, ";");
			ARR_GROW(type->members, type->nmembers, type->capmembers, StructMember);
			type->members[type->nmembers].name = member.name;
			type->members[type->nmembers].type = member.type;
			type->members[type->nmembers].offset = 0;
			type->nmembers++;
		}
		finish_struct_layout(type, parse_gnu_attributes(parser));
		return type;
	}
	if(paccept(parser, "unsigned") || paccept(parser, "U"))
		is_unsigned = true;
	else
		paccept(parser, "signed");
	if(is_unsigned) {
		if(paccept(parser, "char") || paccept(parser, "C"))
			return &T_U8;
		if(paccept(parser, "short") || paccept(parser, "S")) {
			paccept(parser, "int");
			paccept(parser, "INT");
			return &T_U16;
		}
		if(paccept(parser, "long") || paccept(parser, "L")) {
			paccept(parser, "long");
			paccept(parser, "L");
			return &T_U64;
		}
		paccept(parser, "int");
		paccept(parser, "INT");
		return &T_U32;
	}
	if(paccept(parser, "long") || paccept(parser, "L")) {
		paccept(parser, "long");
		paccept(parser, "L");
		return &T_U64;
	}
	if(paccept(parser, "short") || paccept(parser, "S")) {
		paccept(parser, "int");
		paccept(parser, "INT");
		return &T_SHORT;
	}
	character = ptok(parser)->v;
	alias = find_alias(parser->prog, character);
	if(alias) {
		parser->p++;
		return alias;
	}
	if(!strcmp(character, "VD") || !strcmp(character, "void")) {
		parser->p++;
		return &T_VOID;
	}
	if(!strcmp(character, "C") || !strcmp(character, "CC") || !strcmp(character, "char") ||
	   !strcmp(character, "int8_t"))
	{
		parser->p++;
		return &T_CHAR;
	}
	if(!strcmp(character, "INT") || !strcmp(character, "int") || !strcmp(character, "int32_t")) {
		parser->p++;
		return &T_INT;
	}
	if(!strcmp(character, "DB") || !strcmp(character, "double")) {
		parser->p++;
		return &T_DOUBLE;
	}
	if(!strcmp(character, "uint8_t")) {
		parser->p++;
		return &T_U8;
	}
	if(!strcmp(character, "uint16_t")) {
		parser->p++;
		return &T_U16;
	}
	if(!strcmp(character, "uint32_t")) {
		parser->p++;
		return &T_U32;
	}
	if(!strcmp(character, "ULL") || !strcmp(character, "uint64_t") ||
	   !strcmp(character, "int64_t") || !strcmp(character, "Window") ||
	   !strcmp(character, "Font") || !strcmp(character, "size_t"))
	{
		parser->p++;
		return &T_U64;
	}
	if(!strcmp(character, "int16_t")) {
		parser->p++;
		return &T_SHORT;
	}
	if(!strcmp(character, "FILE")) {
		parser->p++;
		return &T_FILE;
	}
	if(!strcmp(character, "Display")) {
		parser->p++;
		return &T_DISPLAY;
	}
	if(!strcmp(character, "GC")) {
		parser->p++;
		return ptr_to(&T_VOID);
	}
	if(!strcmp(character, "XEvent")) {
		parser->p++;
		return &T_XEVENT;
	}
	if(!strcmp(character, "va_list")) {
		parser->p++;
		return &T_VALIST;
	}
	perr(parser, "expected type");
	return NULL;
}

static Declarator parse_declarator(Parser *parser, CType *base, bool unnamed)
{
	Declarator d = {0};
	CType *type_1 = base;
	while(paccept(parser, "*"))
		type_1 = ptr_to(type_1);
	d.name = pexpect_id(parser, unnamed);
	d.type = type_1;
	if(paccept(parser, "(")) {
		d.function = true;
		if(paccept(parser, ")"))
			return d;
		if((peq(parser, "VD") || peq(parser, "void")) &&
		   !strcmp(parser->ts->a[parser->p + 1].v, ")"))
		{
			parser->p += 2;
			return d;
		}
		for(;;) {
			CType *parsed_type;
			char *parameter_name;
			if(paccept(parser, "...")) {
				d.variadic = true;
				pexpect(parser, ")");
				break;
			}
			parsed_type = parse_type(parser);
			while(paccept(parser, "*"))
				parsed_type = ptr_to(parsed_type);
			parameter_name = pexpect_id(parser, true);
			if(!*parameter_name) {
				char temporary_buffer[32];
				snprintf(temporary_buffer, sizeof(temporary_buffer), "__arg%zu", d.nparams);
				parameter_name = xstrdup(temporary_buffer);
			}
			if(paccept(parser, "[")) {
				if(ptok(parser)->kind == TK_NUM)
					parser->p++;
				pexpect(parser, "]");
				parsed_type = ptr_to(parsed_type);
			}
			ARR_GROW(d.params, d.nparams, d.capparams, Param);
			d.params[d.nparams].name = parameter_name;
			d.params[d.nparams].type = parsed_type;
			d.nparams++;
			if(paccept(parser, ")"))
				break;
			pexpect(parser, ",");
		}
		return d;
	}
	while(paccept(parser, "[")) {
		Expr *bound;
		long count;
		if(paccept(parser, "]")) {
			type_1 = array_of(type_1, 0);
			d.type = type_1;
			continue;
		}
		bound = parse_expr(parser, 1);
		pexpect(parser, "]");
		if(eval_const_expr(bound, &count)) {
			if(count < 0)
				perr(parser, "array length cannot be negative");
			type_1 = array_of(type_1, count);
		} else if(bound->kind == EX_ID)
		{
			type_1 = vla_of(type_1, bound->str);
		} else {
			perr(parser, "array length must be a constant expression or identifier");
		}
		d.type = type_1;
	}
	return d;
}

static char decode_escape(const char **pointer_pointer)
{
	const char *position = *pointer_pointer;
	char character = *position++;
	if(character != '\\') {
		*pointer_pointer = position;
		return character;
	}
	character = *position++;
	switch(character) {
	case 'a':
		character = '\a';
		break;
	case 'b':
		character = '\b';
		break;
	case 'f':
		character = '\f';
		break;
	case 'n':
		character = '\n';
		break;
	case 'r':
		character = '\r';
		break;
	case 't':
		character = '\t';
		break;
	case 'v':
		character = '\v';
		break;
	case '0':
		character = '\0';
		break;
	case '\\':
		character = '\\';
		break;
	case '\'':
		character = '\'';
		break;
	case '"':
		character = '"';
		break;
	default:
		break;
	}
	*pointer_pointer = position;
	return character;
}

static char *decode_string(const char *raw)
{
	size_t length = strlen(raw), index = 1;
	Buf buffer = {0};
	while(index + 1 < length) {
		const char *position = raw + index;
		char character = decode_escape(&position);
		bputn(&buffer, &character, 1);
		index = (size_t)(position - raw);
	}
	if(!buffer.s)
		buffer.s = xstrdup("");
	return buffer.s;
}

static Expr *parse_primary(Parser *parser)
{
	Token *token = ptok(parser);
	Expr *expression;
	if(paccept(parser, "(")) {
		expression = parse_expr(parser, 1);
		pexpect(parser, ")");
		return expression;
	}
	if(token->kind == TK_NUM) {
		bool floating = strchr(token->v, '.') != NULL;
		const char *number = token->v;
		if(!floating && !(number[0] == '0' && (number[1] == 'x' || number[1] == 'X')) &&
		   (strchr(number, 'e') || strchr(number, 'E')))
			floating = true;
		if(floating) {
			expression = new_expr(EX_NUM);
			expression->fnum = strtod(token->v, NULL);
			expression->type = &T_DOUBLE;
			parser->p++;
			return expression;
		}
		char *cursor_1 = xstrdup(token->v), *x_value = cursor_1;
		char *suffix = NULL;
		unsigned long long value;
		bool wide = false;
		bool uns = false;
		while(*x_value) {
			if(strchr("uUlL", *x_value)) {
				suffix = x_value;
				*x_value = 0;
				break;
			}
			x_value++;
		}
		if(suffix) {
			const char *result = token->v + (suffix - cursor_1);
			while(*result) {
				if(*result == 'u' || *result == 'U')
					uns = true;
				if((*result == 'l' || *result == 'L') &&
				   (result[1] == 'l' || result[1] == 'L'))
					wide = true;
				result++;
			}
		}
		value = strtoull(cursor_1, NULL, 0);
		free(cursor_1);
		parser->p++;
		expression = new_expr(EX_NUM);
		expression->num = value;
		expression->type = (wide || value > 0xffffffffULL) ? &T_U64 : (uns ? &T_U32 : &T_INT);
		return expression;
	}
	if(token->kind == TK_STR) {
		Buf joined = {0};
		while(ptok(parser)->kind == TK_STR) {
			char *part = decode_string(ptok(parser)->v);
			bputs(&joined, part);
			free(part);
			parser->p++;
		}
		expression = new_expr(EX_STR);
		expression->str = joined.s ? joined.s : xstrdup("");
		expression->type = ptr_to(&T_CHAR);
		return expression;
	}
	if(token->kind == TK_CHAR) {
		const char *cursor_1 = token->v + 1;
		char character = decode_escape(&cursor_1);
		parser->p++;
		expression = new_expr(EX_NUM);
		expression->num = (unsigned char)character;
		expression->type = &T_INT;
		return expression;
	}
	if(token->kind == TK_ID) {
		parser->p++;
		expression = new_expr(EX_ID);
		expression->str = token->v;
		return expression;
	}
	perr(parser, "expected expression");
	return NULL;
}

static Expr *parse_postfix(Parser *parser)
{
	Expr *expression = parse_primary(parser);
	for(;;) {
		if(paccept(parser, "(")) {
			Expr *call_expression = new_expr(EX_CALL);
			call_expression->left = expression;
			if(!paccept(parser, ")"))
				for(;;) {
					Expr *expression_1 = parse_expr(parser, 1);
					ARR_GROW(call_expression->args, call_expression->nargs, call_expression->capargs, Expr *);
					call_expression->args[call_expression->nargs++] = expression_1;
					if(paccept(parser, ")"))
						break;
					pexpect(parser, ",");
				}
			expression = call_expression;
			continue;
		}
		if(paccept(parser, "[")) {
			Expr *expression_2 = new_expr(EX_INDEX);
			expression_2->left = expression;
			expression_2->right = parse_expr(parser, 1);
			pexpect(parser, "]");
			expression = expression_2;
			continue;
		}
		if(paccept(parser, ".")) {
			Expr *expression_2 = new_expr(EX_MEMBER);
			expression_2->left = expression;
			expression_2->str = pexpect_id(parser, false);
			expression = expression_2;
			continue;
		}
		if(paccept(parser, "->")) {
			Expr *expression_2 = new_expr(EX_PTRMEMBER);
			expression_2->left = expression;
			expression_2->str = pexpect_id(parser, false);
			expression = expression_2;
			continue;
		}
		if(paccept(parser, "++")) {
			Expr *expression_2 = new_expr(EX_UNARY);
			expression_2->op = "post++";
			expression_2->left = expression;
			expression = expression_2;
			continue;
		}
		if(paccept(parser, "--")) {
			Expr *expression_2 = new_expr(EX_UNARY);
			expression_2->op = "post--";
			expression_2->left = expression;
			expression = expression_2;
			continue;
		}
		break;
	}
	return expression;
}

static Expr *parse_unary(Parser *parser)
{
	if(peq(parser, "(")) {
		size_t save = parser->p;
		CType *cast_type;
		Expr *expression_1;
		parser->p++;
		if(type_start(parser)) {
			cast_type = parse_type(parser);
			while(paccept(parser, "*"))
				cast_type = ptr_to(cast_type);
			if(paccept(parser, ")")) {
				expression_1 = new_expr(EX_UNARY);
				expression_1->op = "cast";
				expression_1->type = cast_type;
				expression_1->left = parse_unary(parser);
				return expression_1;
			}
		}
		parser->p = save;
	}
	if(peq(parser, "++") || peq(parser, "--")) {
		Expr *expression_1 = new_expr(EX_UNARY);
		expression_1->op = ptok(parser)->v;
		parser->p++;
		expression_1->left = parse_unary(parser);
		return expression_1;
	}
	if(peq(parser, "!") || peq(parser, "~") || peq(parser, "-") || peq(parser, "+") || peq(parser, "&") || peq(parser, "*")) {
		Expr *expression_1 = new_expr(EX_UNARY);
		expression_1->op = ptok(parser)->v;
		parser->p++;
		expression_1->left = parse_unary(parser);
		return expression_1;
	}
	if(paccept(parser, "sizeof")) {
		Expr *expression_1 = new_expr(EX_SIZEOF);
		pexpect(parser, "(");
		if(type_start(parser)) {
			expression_1->sizeof_type = parse_type(parser);
			while(paccept(parser, "*"))
				expression_1->sizeof_type = ptr_to(expression_1->sizeof_type);
		} else {
			expression_1->left = parse_expr(parser, 1);
		}
		pexpect(parser, ")");
		return expression_1;
	}
	return parse_postfix(parser);
}

static int prec(const char *string)
{
	if(!strcmp(string, "=") || !strcmp(string, "+=") || !strcmp(string, "-=") ||
	   !strcmp(string, "*=") || !strcmp(string, "/=") || !strcmp(string, "%=") ||
	   !strcmp(string, "&=") || !strcmp(string, "|=") || !strcmp(string, "^=") ||
	   !strcmp(string, "<<=") || !strcmp(string, ">>="))
		return 1;
	if(!strcmp(string, "||"))
		return 2;
	if(!strcmp(string, "&&"))
		return 3;
	if(!strcmp(string, "|"))
		return 4;
	if(!strcmp(string, "^"))
		return 5;
	if(!strcmp(string, "&"))
		return 6;
	if(!strcmp(string, "==") || !strcmp(string, "!="))
		return 7;
	if(!strcmp(string, "<") || !strcmp(string, "<=") || !strcmp(string, ">") || !strcmp(string, ">="))
		return 8;
	if(!strcmp(string, "<<") || !strcmp(string, ">>"))
		return 9;
	if(!strcmp(string, "+") || !strcmp(string, "-"))
		return 10;
	if(!strcmp(string, "*") || !strcmp(string, "/") || !strcmp(string, "%"))
		return 11;
	return 0;
}

static Expr *parse_expr(Parser *parser, int minprec)
{
	Expr *lhs = parse_unary(parser);
	for(;;) {
		int precedence = prec(ptok(parser)->v);
		char *operator;
		Expr *rhs, *expression;
		bool right;
		if(precedence < minprec)
			break;
		operator = ptok(parser)->v;
		parser->p++;
		right = (!strcmp(operator, "=") || !strcmp(operator, "+=") || !strcmp(operator, "-=") ||
			 !strcmp(operator, "*=") || !strcmp(operator, "/=") || !strcmp(operator, "%=") ||
			 !strcmp(operator, "&=") || !strcmp(operator, "|=") || !strcmp(operator, "^=") ||
			 !strcmp(operator, "<<=") || !strcmp(operator, ">>="));
		rhs = parse_expr(parser, right ? precedence : precedence + 1);
		expression = new_expr(EX_BINARY);
		expression->op = operator;
		expression->left = lhs;
		expression->right = rhs;
		lhs = expression;
	}
	return lhs;
}

static Expr *parse_initializer(Parser *parser)
{
	Expr *expression;
	if(!paccept(parser, "{"))
		return parse_expr(parser, 1);
	expression = new_expr(EX_INITLIST);
	if(paccept(parser, "}"))
		return expression;
	for(;;) {
		Expr *item = parse_initializer(parser);
		ARR_GROW(expression->args, expression->nargs, expression->capargs, Expr *);
		expression->args[expression->nargs++] = item;
		if(paccept(parser, "}"))
			break;
		pexpect(parser, ",");
		if(paccept(parser, "}"))
			break;
	}
	return expression;
}

static bool eval_const_expr(Expr *expression, long *value)
{
	long array, boundary;
	if(!expression)
		return false;
	if(expression->kind == EX_NUM) {
		*value = (long)expression->num;
		return true;
	}
	if(expression->kind == EX_ID && !strcmp(expression->str, "NULL")) {
		*value = 0;
		return true;
	}
	if(expression->kind == EX_UNARY && eval_const_expr(expression->left, &array)) {
		if(!strcmp(expression->op, "+") || !strcmp(expression->op, "cast"))
			*value = array;
		else if(!strcmp(expression->op, "-"))
			*value = -array;
		else if(!strcmp(expression->op, "~"))
			*value = ~array;
		else if(!strcmp(expression->op, "!"))
			*value = !array;
		else
			return false;
		return true;
	}
	if(expression->kind != EX_BINARY || !eval_const_expr(expression->left, &array) ||
	   !eval_const_expr(expression->right, &boundary))
		return false;
	if(!strcmp(expression->op, "+"))
		*value = array + boundary;
	else if(!strcmp(expression->op, "-"))
		*value = array - boundary;
	else if(!strcmp(expression->op, "*"))
		*value = array * boundary;
	else if(!strcmp(expression->op, "/")) {
		if(!boundary)
			return false;
		*value = array / boundary;
	} else if(!strcmp(expression->op, "%"))
	{
		if(!boundary)
			return false;
		*value = array % boundary;
	} else if(!strcmp(expression->op, "<<"))
		*value = array << boundary;
	else if(!strcmp(expression->op, ">>"))
		*value = array >> boundary;
	else if(!strcmp(expression->op, "&"))
		*value = array & boundary;
	else if(!strcmp(expression->op, "|"))
		*value = array | boundary;
	else if(!strcmp(expression->op, "^"))
		*value = array ^ boundary;
	else
		return false;
	return true;
}

static void infer_array_bound(Parser *parser, CType *type, Expr *initializer)
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
		perr(parser, "array with omitted length requires an initializer");
}
static Stmt *parse_stmt(Parser *p);

static Stmt *parse_block(Parser *parser)
{
	Stmt *statement = new_stmt(ST_BLOCK);
	pexpect(parser, "{");
	while(!paccept(parser, "}")) {
		Stmt *statement_1;
		if(type_start(parser)) {
			CType *base_type = parse_type(parser);
			for(;;) {
				Declarator q = parse_declarator(parser, base_type, false);
				Decl *declaration = new_decl();
				if(q.function)
					perr(parser, "nested function unsupported");
				declaration->name = q.name;
				declaration->type = q.type;
				if(paccept(parser, "="))
					declaration->init = parse_initializer(parser);
				infer_array_bound(parser, declaration->type, declaration->init);
				statement_1 = new_stmt(ST_DECL);
				statement_1->decl = declaration;
				ARR_GROW(statement->children, statement->nchildren, statement->capchildren, Stmt *);
				statement->children[statement->nchildren++] = statement_1;
				if(!paccept(parser, ","))
					break;
			}
			pexpect(parser, ";");
			continue;
		}
		statement_1 = parse_stmt(parser);
		ARR_GROW(statement->children, statement->nchildren, statement->capchildren, Stmt *);
		statement->children[statement->nchildren++] = statement_1;
	}
	return statement;
}

static Stmt *parse_stmt(Parser *parser)
{
	Stmt *statement;
	if(peq(parser, "{"))
		return parse_block(parser);
	if(paccept(parser, "if")) {
		statement = new_stmt(ST_IF);
		pexpect(parser, "(");
		statement->cond = parse_expr(parser, 1);
		pexpect(parser, ")");
		statement->yes = parse_stmt(parser);
		if(paccept(parser, "else"))
			statement->no = parse_stmt(parser);
		return statement;
	}
	if(paccept(parser, "while")) {
		statement = new_stmt(ST_WHILE);
		pexpect(parser, "(");
		statement->cond = parse_expr(parser, 1);
		pexpect(parser, ")");
		statement->body = parse_stmt(parser);
		return statement;
	}
	if(paccept(parser, "for")) {
		statement = new_stmt(ST_FOR);
		pexpect(parser, "(");
		if(!paccept(parser, ";")) {
			if(type_start(parser)) {
				CType *base_type = parse_type(parser);
				Declarator q = parse_declarator(parser, base_type, false);
				Decl *declaration = new_decl();
				if(q.function)
					perr(parser, "function declaration in for initializer unsupported");
				declaration->name = q.name;
				declaration->type = q.type;
				if(paccept(parser, "="))
					declaration->init = parse_initializer(parser);
				infer_array_bound(parser, declaration->type, declaration->init);
				pexpect(parser, ";");
				statement->init = new_stmt(ST_DECL);
				statement->init->decl = declaration;
			} else {
				statement->init = new_stmt(ST_EXPR);
				statement->init->expr = parse_expr(parser, 1);
				pexpect(parser, ";");
			}
		}
		if(!paccept(parser, ";")) {
			statement->cond = parse_expr(parser, 1);
			pexpect(parser, ";");
		}
		if(!paccept(parser, ")")) {
			statement->post = parse_expr(parser, 1);
			pexpect(parser, ")");
		}
		statement->body = parse_stmt(parser);
		return statement;
	}
	if(paccept(parser, "switch")) {
		statement = new_stmt(ST_SWITCH);
		pexpect(parser, "(");
		statement->cond = parse_expr(parser, 1);
		pexpect(parser, ")");
		statement->body = parse_stmt(parser);
		return statement;
	}
	if(paccept(parser, "case")) {
		statement = new_stmt(ST_CASE);
		statement->expr = parse_expr(parser, 1);
		pexpect(parser, ":");
		return statement;
	}
	if(paccept(parser, "default")) {
		statement = new_stmt(ST_DEFAULT);
		pexpect(parser, ":");
		return statement;
	}
	if(paccept(parser, "return")) {
		statement = new_stmt(ST_RETURN);
		if(!paccept(parser, ";")) {
			statement->expr = parse_expr(parser, 1);
			pexpect(parser, ";");
		}
		return statement;
	}
	if(paccept(parser, "break")) {
		pexpect(parser, ";");
		return new_stmt(ST_BREAK);
	}
	if(paccept(parser, "continue")) {
		pexpect(parser, ";");
		return new_stmt(ST_CONTINUE);
	}
	if(paccept(parser, "asm") || paccept(parser, "__asm__")) {
		Buf text = {0};
		statement = new_stmt(ST_ASM);
		paccept(parser, "volatile");
		paccept(parser, "__volatile__");
		pexpect(parser, "(");
		if(ptok(parser)->kind != TK_STR)
			perr(parser, "inline asm requires a string literal");
		while(ptok(parser)->kind == TK_STR) {
			char *part = decode_string(ptok(parser)->v);
			bputs(&text, part);
			free(part);
			parser->p++;
		}
		if(paccept(parser, ":")) {
			if(!peq(parser, ")") && !peq(parser, ":")) {
				for(;;) {
					char *constraint;
					Expr *output;
					if(ptok(parser)->kind != TK_STR)
						perr(parser, "asm output constraint must be a string");
					constraint = decode_string(ptok(parser)->v);
					parser->p++;
					pexpect(parser, "(");
					output = parse_expr(parser, 1);
					pexpect(parser, ")");
					ARR_GROW(statement->asm_constraints, statement->nasm_outputs, statement->capasm_constraints, char *);
					statement->asm_constraints[statement->nasm_outputs] = constraint;
					ARR_GROW(statement->asm_outputs, statement->nasm_outputs, statement->capasm_outputs, Expr *);
					statement->asm_outputs[statement->nasm_outputs] = output;
					statement->nasm_outputs++;
					if(!paccept(parser, ","))
						break;
				}
			}
			//Inputs and clobbers are parsed only when empty for now.
			if(paccept(parser, ":")) {
				if(!peq(parser, ")") && !peq(parser, ":"))
					perr(parser, "asm inputs are not supported yet");
				paccept(parser, ":");
			}
		}
		pexpect(parser, ")");
		pexpect(parser, ";");
		statement->asm_text = text.s ? text.s : xstrdup("");
		return statement;
	}
	if(paccept(parser, ";"))
		return new_stmt(ST_EMPTY);
	statement = new_stmt(ST_EXPR);
	statement->expr = parse_expr(parser, 1);
	pexpect(parser, ";");
	return statement;
}

static void parse_program(Tokens *token_stream, Program *prog)
{
	Parser p = {token_stream, 0, prog};
	while(ptok(&p)->kind != TK_EOF) {
		bool is_typedef = false;
		bool is_extern = false;
		bool is_static = false;
		bool is_inline = false;
		CType *base_type;
		Declarator q;
		Decl *declaration;
		for(;;) {
			if(paccept(&p, "typedef") || paccept(&p, "TD")) {
				if(is_typedef)
					perr(&p, "duplicate typedef specifier");
				is_typedef = true;
				continue;
			}
			if(paccept(&p, "extern")) {
				if(is_extern)
					perr(&p, "duplicate extern specifier");
				is_extern = true;
				continue;
			}
			if(paccept(&p, "static") || paccept(&p, "SC")) {
				if(is_static)
					perr(&p, "duplicate static specifier");
				is_static = true;
				continue;
			}
			if(paccept(&p, "inline") || paccept(&p, "IL")) {
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
		base_type = parse_type(&p);
		if(paccept(&p, ";")) {
			if(is_typedef || is_extern || is_static || is_inline)
				perr(&p, "declaration specifier requires a declarator");
			continue;
		}
		q = parse_declarator(&p, base_type, false);
		if(is_typedef) {
			if(q.function)
				perr(&p, "function typedefs are not supported yet");
			pexpect(&p, ";");
			add_alias(&p, q.name, q.type);
			continue;
		}
		if(is_inline && !q.function)
			perr(&p, "inline can only be used on a function");
		declaration = new_decl();
		declaration->name = q.name;
		declaration->type = q.type;
		declaration->params = q.params;
		declaration->nparams = q.nparams;
		declaration->capparams = q.capparams;
		declaration->variadic = q.variadic;
		declaration->is_extern = is_extern;
		declaration->is_static = is_static;
		declaration->is_inline = is_inline;
		if(q.function) {
			if(paccept(&p, ";"))
				declaration->prototype = true;
			else {
				if(is_extern)
					perr(&p, "extern function cannot have a body");
				declaration->body = parse_block(&p);
			}
		} else {
			if(paccept(&p, "=")) {
				if(is_extern)
					perr(&p, "extern object cannot have an initializer");
				declaration->init = parse_initializer(&p);
			}
			infer_array_bound(&p, declaration->type, declaration->init);
			pexpect(&p, ";");
		}
		ARR_GROW(prog->a, prog->n, prog->cap, Decl *);
		prog->a[prog->n++] = declaration;
	}
}
//Code generation
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

static long align_up(long value, long array)
{
	return (value + array - 1) / array * array;
}

static void emit(Gen *generator, const char *fmt, ...)
{
	va_list argument_list, copied_argument_list;
	int count;
	va_start(argument_list, fmt);
	va_copy(copied_argument_list, argument_list);
	count = vsnprintf(NULL, 0, fmt, copied_argument_list);
	va_end(copied_argument_list);
	bneed(&generator->out, (size_t)count + 1);
	vsnprintf(generator->out.s + generator->out.n, generator->out.cap - generator->out.n, fmt, argument_list);
	va_end(argument_list);
	generator->out.n += (size_t)count;
	bputs(&generator->out, "\n");
}

static char *new_label(Gen *generator, const char *prefix)
{
	char label_buffer_size[128];
	snprintf(label_buffer_size, sizeof(label_buffer_size), "%s%ld", prefix, ++generator->label);
	return xstrdup(label_buffer_size);
}

static Symbol *find_local(Gen *generator, const char *count)
{
	size_t index;
	for(index = 0; index < generator->nlocals; index++)
		if(!strcmp(generator->locals[index].name, count))
			return &generator->locals[index];
	return NULL;
}

static Symbol *find_global(Gen *generator, const char *count)
{
	size_t index;
	Symbol *external = NULL;
	for(index = 0; index < generator->nglobals; index++) {
		if(strcmp(generator->globals[index].name, count))
			continue;
		if(generator->globals[index].kind == SY_GLOBAL)
			return &generator->globals[index];
		external = &generator->globals[index];
	}
	return external;
}

static Decl *find_func(Gen *generator, const char *count)
{
	size_t index;
	for(index = 0; index < generator->nfuncs; index++)
		if(!strcmp(generator->funcs[index]->name, count))
			return generator->funcs[index];
	return NULL;
}

static Symbol lookup(Gen *generator, const char *count)
{
	Symbol *s = find_local(generator, count);
	Decl *function_declaration;
	if(s)
		return *s;
	s = find_global(generator, count);
	if(s)
		return *s;
	if(!strcmp(count, "NULL"))
		return (Symbol){(char *)count, &T_U64, SY_CONST, 0, NULL};
	if(!strcmp(count, "ExposureMask"))
		return (Symbol){(char *)count, &T_U64, SY_CONST, 1L << 15, NULL};
	if(!strcmp(count, "KeyPressMask"))
		return (Symbol){(char *)count, &T_U64, SY_CONST, 1L << 0, NULL};
	if(!strcmp(count, "KeyPress"))
		return (Symbol){(char *)count, &T_U64, SY_CONST, 2, NULL};
	if(!strcmp(count, "stderr"))
		return (Symbol){(char *)count, ptr_to(&T_VOID), SY_EXTERN_GLOBAL, 0, NULL};
	function_declaration = find_func(generator, count);
	if(function_declaration)
		return (Symbol){(char *)count, function_declaration->type, SY_FUNCTION, 0, function_declaration};
	return (Symbol){(char *)count, &T_U64, SY_EXTERN_FUNCTION, 0, NULL};
}

static CType *expr_type(Gen *generator, Expr *expression)
{
	CType *type_1;
	if(expression->type)
		return expression->type;
	switch(expression->kind) {
	case EX_ID:
	{
		Symbol s = lookup(generator, expression->str);
		return s.type;
	}
	case EX_STR:
		return ptr_to(&T_CHAR);
	case EX_NUM:
		return &T_U64;
	case EX_UNARY:
		if(!strcmp(expression->op, "&"))
			return ptr_to(expr_type(generator, expression->left));
		if(!strcmp(expression->op, "*")) {
			type_1 = expr_type(generator, expression->left);
			return type_1->base ? type_1->base : &T_U64;
		}
		return expr_type(generator, expression->left);
	case EX_INDEX:
		type_1 = expr_type(generator, expression->left);
		return type_1->base ? type_1->base : &T_U64;
	case EX_MEMBER:
	case EX_PTRMEMBER:
	{
		StructMember *member;
		type_1 = expr_type(generator, expression->left);
		if(expression->kind == EX_PTRMEMBER)
			type_1 = type_1 && type_1->kind == TY_PTR ? type_1->base : NULL;
		member = find_struct_member(type_1, expression->str);
		if(!member)
			fatal("unknown struct member %s", expression->str);
		return member->type;
	}
	case EX_SIZEOF:
		return &T_U64;
	case EX_BINARY:
		if(!strcmp(expression->op, "==") || !strcmp(expression->op, "!=") || !strcmp(expression->op, "<") || !strcmp(expression->op, "<=") || !strcmp(expression->op, ">") || !strcmp(expression->op, ">=") || !strcmp(expression->op, "&&") || !strcmp(expression->op, "||"))
			return &T_INT;
		if(!strcmp(expression->op, "=") || !strcmp(expression->op, "+=") || !strcmp(expression->op, "-=") ||
		   !strcmp(expression->op, "*=") || !strcmp(expression->op, "/="))
			return expr_type(generator, expression->left);
		if(expr_type(generator, expression->left)->kind == TY_DOUBLE ||
		   expr_type(generator, expression->right)->kind == TY_DOUBLE)
			return &T_DOUBLE;
		return expr_type(generator, expression->left);
	case EX_CALL:
		if(expression->left->kind == EX_ID) {
			Decl *function_declaration = find_func(generator, expression->left->str);
			if(function_declaration)
				return function_declaration->type;
		}
		return &T_U64;
	case EX_INITLIST:
		return &T_VOID;
	}
	return &T_U64;
}

static char *intern_string(Gen *generator, const char *value_1)
{
	size_t index, inner_index, length;
	for(index = 0; index < generator->nstrings; index++)
		if(!strcmp(generator->strings[index].value, value_1))
			return generator->strings[index].label;
	ARR_GROW(generator->strings, generator->nstrings, generator->capstrings, StringLit);
	generator->strings[generator->nstrings].value = xstrdup(value_1);
	snprintf(generator->strings[generator->nstrings].label, sizeof(generator->strings[generator->nstrings].label), ".LC%zu", generator->nstrings);
	bprintf(&generator->ro, "%s:\n    .byte ", generator->strings[generator->nstrings].label);
	length = strlen(value_1);
	for(inner_index = 0; inner_index < length; inner_index++)
		bprintf(&generator->ro, "%u, ", (unsigned char)value_1[inner_index]);
	bputs(&generator->ro, "0\n");
	return generator->strings[generator->nstrings++].label;
}
static void gen_expr(Gen *, Expr *);
static CType *gen_addr(Gen *, Expr *);

static void pushreg(Gen *generator, const char *register_name)
{
	emit(generator, "    push %s", register_name);
	generator->tempdepth += 8;
}

static void popreg(Gen *generator, const char *register_name)
{
	emit(generator, "    pop %s", register_name);
	generator->tempdepth -= 8;
}

static void load_rax(Gen *generator, CType *type)
{
	if(type->kind == TY_ARRAY || type->kind == TY_STRUCT ||
	   type->kind == TY_XEVENT || type->kind == TY_VALIST)
		return;
	if(type->kind == TY_CHAR || type->kind == TY_U8)
		emit(generator, "    movzx eax, byte ptr [rax]");
	else if(type->kind == TY_SHORT || type->kind == TY_U16)
		emit(generator, "    movzx eax, word ptr [rax]");
	else if(type->kind == TY_INT)
		emit(generator, "    movsxd rax, dword ptr [rax]");
	else if(type->kind == TY_U32)
		emit(generator, "    mov eax, dword ptr [rax]");
	else
		emit(generator, "    mov rax, qword ptr [rax]");
}

static void store_rcx(Gen *generator, CType *type)
{
	if(type->kind == TY_CHAR || type->kind == TY_U8)
		emit(generator, "    mov byte ptr [rcx], al");
	else if(type->kind == TY_SHORT || type->kind == TY_U16)
		emit(generator, "    mov word ptr [rcx], ax");
	else if(type->kind == TY_INT || type->kind == TY_U32)
		emit(generator, "    mov dword ptr [rcx], eax");
	else
		emit(generator, "    mov qword ptr [rcx], rax");
}

static bool is_double_type(CType *type)
{
	return type && type->kind == TY_DOUBLE;
}

static void integer_bits_to_double(Gen *generator)
{
	emit(generator, "    cvtsi2sd xmm0, rax");
	emit(generator, "    movq rax, xmm0");
}

static void gen_expr_as_double(Gen *generator, Expr *expression)
{
	CType *source = expr_type(generator, expression);
	gen_expr(generator, expression);
	if(!is_double_type(source))
		integer_bits_to_double(generator);
}

static void load_extern_global(Gen *generator, const char *name, CType *type)
{
	if(type->kind == TY_CHAR || type->kind == TY_U8)
		emit(generator, "    movzx eax, byte ptr [rip+%s]", name);
	else if(type->kind == TY_SHORT || type->kind == TY_U16)
		emit(generator, "    movzx eax, word ptr [rip+%s]", name);
	else if(type->kind == TY_INT)
		emit(generator, "    movsxd rax, dword ptr [rip+%s]", name);
	else if(type->kind == TY_U32)
		emit(generator, "    mov eax, dword ptr [rip+%s]", name);
	else
		emit(generator, "    mov rax, qword ptr [rip+%s]", name);
}

static CType *gen_addr(Gen *generator, Expr *expression)
{
	CType *type_1;
	if(expression->kind == EX_ID) {
		Symbol s = lookup(generator, expression->str);
		if(s.kind == SY_LOCAL) {
			emit(generator, "    lea rax, [rbp-%ld]", s.off);
			return s.type;
		}
		if(s.kind == SY_GLOBAL) {
			emit(generator, "    lea rax, [rip+%s]", s.name);
			return s.type;
		}
		fatal("%s is not assignable", expression->str);
	}
	if(expression->kind == EX_UNARY && !strcmp(expression->op, "*")) {
		gen_expr(generator, expression->left);
		type_1 = expr_type(generator, expression->left);
		return type_1->base ? type_1->base : &T_U64;
	}
	if(expression->kind == EX_INDEX) {
		gen_expr(generator, expression->left);
		pushreg(generator, "rax");
		gen_expr(generator, expression->right);
		type_1 = expr_type(generator, expression->left);
		type_1 = type_1->base ? type_1->base : &T_U64;
		if(type_size(type_1) != 1)
			emit(generator, "    imul rax, %ld", type_size(type_1));
		popreg(generator, "rcx");
		emit(generator, "    add rax, rcx");
		return type_1;
	}
	if(expression->kind == EX_MEMBER || expression->kind == EX_PTRMEMBER) {
		StructMember *member;
		CType *owner;
		if(expression->kind == EX_MEMBER) {
			gen_addr(generator, expression->left);
			owner = expr_type(generator, expression->left);
		} else {
			gen_expr(generator, expression->left);
			owner = expr_type(generator, expression->left);
			owner = owner && owner->kind == TY_PTR ? owner->base : NULL;
		}
		member = find_struct_member(owner, expression->str);
		if(!member)
			fatal("unknown struct member %s", expression->str);
		if(member->offset)
			emit(generator, "    add rax, %ld", member->offset);
		return member->type;
	}
	fatal("expression is not an lvalue");
	return &T_U64;
}

static void gen_binary(Gen *generator, Expr *expression)
{
	const char *operator = expression->op;
	CType *type;
	bool floating = is_double_type(expr_type(generator, expression->left)) ||
			is_double_type(expr_type(generator, expression->right));
	if(!strcmp(operator, "=")) {
		type = gen_addr(generator, expression->left);
		pushreg(generator, "rax");
		if(is_double_type(type))
			gen_expr_as_double(generator, expression->right);
		else {
			gen_expr(generator, expression->right);
			if(is_double_type(expr_type(generator, expression->right))) {
				emit(generator, "    movq xmm0, rax");
				emit(generator, "    cvttsd2si rax, xmm0");
			}
		}
		popreg(generator, "rcx");
		store_rcx(generator, type);
		return;
	}
	if(!strcmp(operator, "+=") || !strcmp(operator, "-=") || !strcmp(operator, "*=") || !strcmp(operator, "/=")) {
		type = gen_addr(generator, expression->left);
		pushreg(generator, "rax");
		load_rax(generator, type);
		if(is_double_type(type)) {
			pushreg(generator, "rax");
			gen_expr_as_double(generator, expression->right);
			popreg(generator, "rcx");
			emit(generator, "    movq xmm0, rcx");
			emit(generator, "    movq xmm1, rax");
			if(!strcmp(operator, "+="))
				emit(generator, "    addsd xmm0, xmm1");
			else if(!strcmp(operator, "-="))
				emit(generator, "    subsd xmm0, xmm1");
			else if(!strcmp(operator, "*="))
				emit(generator, "    mulsd xmm0, xmm1");
			else
				emit(generator, "    divsd xmm0, xmm1");
			emit(generator, "    movq rax, xmm0");
		} else {
			pushreg(generator, "rax");
			gen_expr(generator, expression->right);
			popreg(generator, "rcx");
			if(!strcmp(operator, "+="))
				emit(generator, "    add rax, rcx");
			else if(!strcmp(operator, "-=")) {
				emit(generator, "    sub rcx, rax");
				emit(generator, "    mov rax, rcx");
			} else if(!strcmp(operator, "*="))
				emit(generator, "    imul rax, rcx");
			else {
				emit(generator, "    mov r10, rax");
				emit(generator, "    mov rax, rcx");
				emit(generator, "    cqo");
				emit(generator, "    idiv r10");
			}
		}
		popreg(generator, "rcx");
		store_rcx(generator, type);
		return;
	}
	if(!strcmp(operator, "&&") || !strcmp(operator, "||")) {
		char *array = new_label(generator, ".Llogic"), *value = new_label(generator, ".Llogicdone");
		gen_expr(generator, expression->left);
		emit(generator, "    test rax, rax");
		if(!strcmp(operator, "&&"))
			emit(generator, "    jz %s", array);
		else
			emit(generator, "    jnz %s", array);
		gen_expr(generator, expression->right);
		emit(generator, "    test rax, rax");
		emit(generator, "    setne al");
		emit(generator, "    movzx rax, al");
		emit(generator, "    jmp %s", value);
		emit(generator, "%s:", array);
		if(!strcmp(operator, "&&"))
			emit(generator, "    xor eax, eax");
		else
			emit(generator, "    mov eax, 1");
		emit(generator, "%s:", value);
		return;
	}
	if(floating && (!strcmp(operator, "+") || !strcmp(operator, "-") || !strcmp(operator, "*") || !strcmp(operator, "/"))) {
		gen_expr_as_double(generator, expression->left);
		pushreg(generator, "rax");
		gen_expr_as_double(generator, expression->right);
		popreg(generator, "rcx");
		emit(generator, "    movq xmm0, rcx");
		emit(generator, "    movq xmm1, rax");
		if(!strcmp(operator, "+"))
			emit(generator, "    addsd xmm0, xmm1");
		else if(!strcmp(operator, "-"))
			emit(generator, "    subsd xmm0, xmm1");
		else if(!strcmp(operator, "*"))
			emit(generator, "    mulsd xmm0, xmm1");
		else
			emit(generator, "    divsd xmm0, xmm1");
		emit(generator, "    movq rax, xmm0");
		return;
	}
	gen_expr(generator, expression->left);
	pushreg(generator, "rax");
	gen_expr(generator, expression->right);
	popreg(generator, "rcx");
	if(!strcmp(operator, "+"))
		emit(generator, "    add rax, rcx");
	else if(!strcmp(operator, "-")) {
		emit(generator, "    sub rcx, rax");
		emit(generator, "    mov rax, rcx");
	} else if(!strcmp(operator, "*"))
		emit(generator, "    imul rax, rcx");
	else if(!strcmp(operator, "/") || !strcmp(operator, "%")) {
		emit(generator, "    mov r10, rax");
		emit(generator, "    mov rax, rcx");
		emit(generator, "    cqo");
		emit(generator, "    idiv r10");
		if(!strcmp(operator, "%"))
			emit(generator, "    mov rax, rdx");
	} else if(!strcmp(operator, "&"))
		emit(generator, "    and rax, rcx");
	else if(!strcmp(operator, "|"))
		emit(generator, "    or rax, rcx");
	else if(!strcmp(operator, "^"))
		emit(generator, "    xor rax, rcx");
	else if(!strcmp(operator, "<<") || !strcmp(operator, ">>")) {
		emit(generator, "    mov rdx, rax");
		emit(generator, "    mov rax, rcx");
		emit(generator, "    mov rcx, rdx");
		emit(generator, !strcmp(operator, "<<") ? "    shl rax, cl" : "    sar rax, cl");
	} else if(!strcmp(operator, "==") || !strcmp(operator, "!=") || !strcmp(operator, "<") || !strcmp(operator, "<=") || !strcmp(operator, ">") || !strcmp(operator, ">="))
	{
		const char *condition_code = !strcmp(operator, "==") ? "e" : !strcmp(operator, "!=") ? "ne"
						   : !strcmp(operator, "<")	     ? "l"
						   : !strcmp(operator, "<=")	     ? "le"
						   : !strcmp(operator, ">")	     ? "g"
									     : "ge";
		emit(generator, "    cmp rcx, rax");
		emit(generator, "    set%s al", condition_code);
		emit(generator, "    movzx rax, al");
	} else
		fatal("unsupported operator %s", operator);
}

static const char *call_name(const char *argument_count)
{
	if(!strcmp(argument_count, "DefaultScreen"))
		return "XDefaultScreen";
	if(!strcmp(argument_count, "RootWindow"))
		return "XRootWindow";
	if(!strcmp(argument_count, "BlackPixel"))
		return "XBlackPixel";
	if(!strcmp(argument_count, "WhitePixel"))
		return "XWhitePixel";
	return argument_count;
}

static void gen_call(Gen *generator, Expr *expression)
{
	size_t index, argument_count = expression->nargs;
	long nstack, pad, cleanup;
	const char *name;
	static const char *regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
	bool has_double = false;
	if(expression->left->kind != EX_ID)
		fatal("only direct calls supported");
	name = expression->left->str;
	if(!strcmp(name, "va_start")) {
		Symbol ap;
		long named;
		if(!generator->current || !generator->current->variadic || argument_count != 2 || expression->args[0]->kind != EX_ID)
			fatal("bad va_start");
		ap = lookup(generator, expression->args[0]->str);
		named = (long)(generator->current->nparams < 6 ? generator->current->nparams : 6);
		emit(generator, "    lea rax, [rbp-%ld]", ap.off);
		emit(generator, "    mov dword ptr [rax], %ld", named * 8);
		emit(generator, "    mov dword ptr [rax+4], 48");
		emit(generator, "    lea rcx, [rbp+16]");
		emit(generator, "    mov qword ptr [rax+8], rcx");
		emit(generator, "    lea rcx, [rbp-%ld]", generator->vaoff);
		emit(generator, "    mov qword ptr [rax+16], rcx");
		emit(generator, "    xor eax, eax");
		return;
	}
	if(!strcmp(name, "va_end")) {
		emit(generator, "    xor eax, eax");
		return;
	}
	for(index = 0; index < argument_count; index++)
		if(is_double_type(expr_type(generator, expression->args[index])))
			has_double = true;
	name = call_name(name);
	if(has_double) {
		size_t integer_count = 0, double_count = 0;
		for(index = 0; index < argument_count; index++) {
			if(is_double_type(expr_type(generator, expression->args[index])))
				double_count++;
			else
				integer_count++;
		}
		if(integer_count > 6 || double_count > 8)
			fatal("native calls with floating arguments currently support 6 integer and 8 double register arguments");
		pad = (16 - ((generator->tempdepth + (long)argument_count * 8) % 16)) % 16;
		if(pad) {
			emit(generator, "    sub rsp, %ld", pad);
			generator->tempdepth += pad;
		}
		for(index = argument_count; index > 0; index--) {
			if(is_double_type(expr_type(generator, expression->args[index - 1])))
				gen_expr_as_double(generator, expression->args[index - 1]);
			else
				gen_expr(generator, expression->args[index - 1]);
			pushreg(generator, "rax");
		}
		integer_count = double_count = 0;
		for(index = 0; index < argument_count; index++) {
			if(is_double_type(expr_type(generator, expression->args[index]))) {
				popreg(generator, "rax");
				emit(generator, "    movq xmm%zu, rax", double_count++);
			} else {
				popreg(generator, regs[integer_count++]);
			}
		}
		emit(generator, "    mov eax, %zu", double_count);
		emit(generator, "    call %s@PLT", name);
		if(pad) {
			emit(generator, "    add rsp, %ld", pad);
			generator->tempdepth -= pad;
		}
		if(is_double_type(expr_type(generator, expression)))
			emit(generator, "    movq rax, xmm0");
		return;
	}
	nstack = (long)(argument_count > 6 ? argument_count - 6 : 0);
	pad = (16 - ((generator->tempdepth + nstack * 8) % 16)) % 16;
	if(pad) {
		emit(generator, "    sub rsp, %ld", pad);
		generator->tempdepth += pad;
	}
	for(index = argument_count; index > 0; index--) {
		gen_expr(generator, expression->args[index - 1]);
		pushreg(generator, "rax");
	}
	for(index = 0; index < argument_count && index < 6; index++)
		popreg(generator, regs[index]);
	emit(generator, "    xor eax, eax");
	emit(generator, "    call %s@PLT", name);
	cleanup = nstack * 8 + pad;
	if(cleanup) {
		emit(generator, "    add rsp, %ld", cleanup);
		generator->tempdepth -= cleanup;
	}
	if(is_double_type(expr_type(generator, expression)))
		emit(generator, "    movq rax, xmm0");
}

static void gen_incdec(Gen *generator, Expr *expression)
{
	CType *type = gen_addr(generator, expression->left);
	long step = 1;
	bool postfix = !strcmp(expression->op, "post++") ||
		       !strcmp(expression->op, "post--");
	bool decrement = !strcmp(expression->op, "--") ||
			 !strcmp(expression->op, "post--");
	if(type->kind == TY_PTR && type->base) {
		step = type_size(type->base);
		if(step <= 0)
			step = 1;
	}
	pushreg(generator, "rax");
	load_rax(generator, type);
	if(postfix)
		pushreg(generator, "rax");
	if(decrement)
		emit(generator, "    sub rax, %ld", step);
	else
		emit(generator, "    add rax, %ld", step);
	if(postfix) {
		popreg(generator, "rdx");
		popreg(generator, "rcx");
		store_rcx(generator, type);
		emit(generator, "    mov rax, rdx");
	} else {
		popreg(generator, "rcx");
		store_rcx(generator, type);
	}
}

static void gen_expr(Gen *generator, Expr *expression)
{
	CType *type_1;
	char *lab;
	Symbol s;
	switch(expression->kind) {
	case EX_NUM:
		if(is_double_type(expression->type)) {
			union
			{
				double d;
				uint64_t u;
			} bits;
			bits.d = expression->fnum;
			emit(generator, "    mov rax, %llu", (unsigned long long)bits.u);
		} else
			emit(generator, "    mov rax, %llu", expression->num);
		return;
	case EX_STR:
		lab = intern_string(generator, expression->str);
		emit(generator, "    lea rax, [rip+%s]", lab);
		return;
	case EX_ID:
		s = lookup(generator, expression->str);
		if(s.kind == SY_CONST) {
			emit(generator, "    mov rax, %ld", s.off);
			return;
		}
		if(s.kind == SY_EXTERN_GLOBAL) {
			load_extern_global(generator, s.name, s.type);
			return;
		}
		if(s.kind == SY_FUNCTION || s.kind == SY_EXTERN_FUNCTION) {
			emit(generator, "    lea rax, [rip+%s]", s.name);
			return;
		}
		type_1 = gen_addr(generator, expression);
		load_rax(generator, type_1);
		return;
	case EX_SIZEOF:
		emit(generator, "    mov rax, %ld", type_size(expression->sizeof_type ? expression->sizeof_type : expr_type(generator, expression->left)));
		return;
	case EX_UNARY:
		if(!strcmp(expression->op, "++") || !strcmp(expression->op, "--") ||
		   !strcmp(expression->op, "post++") || !strcmp(expression->op, "post--"))
		{
			gen_incdec(generator, expression);
		} else if(!strcmp(expression->op, "cast"))
		{
			CType *source = expr_type(generator, expression->left);
			gen_expr(generator, expression->left);
			if(expression->type->kind == TY_DOUBLE) {
				if(!is_double_type(source))
					integer_bits_to_double(generator);
			} else {
				if(is_double_type(source)) {
					emit(generator, "    movq xmm0, rax");
					emit(generator, "    cvttsd2si rax, xmm0");
				}
				if(expression->type->kind == TY_CHAR || expression->type->kind == TY_U8)
					emit(generator, "    movzx eax, al");
				else if(expression->type->kind == TY_SHORT || expression->type->kind == TY_U16)
					emit(generator, "    movzx eax, ax");
				else if(expression->type->kind == TY_INT)
					emit(generator, "    movsxd rax, eax");
				else if(expression->type->kind == TY_U32)
					emit(generator, "    mov eax, eax");
			}
		} else if(!strcmp(expression->op, "&"))
			gen_addr(generator, expression->left);
		else if(!strcmp(expression->op, "*")) {
			gen_expr(generator, expression->left);
			load_rax(generator, expr_type(generator, expression));
		} else {
			gen_expr(generator, expression->left);
			if(!strcmp(expression->op, "!")) {
				emit(generator, "    test rax, rax");
				emit(generator, "    sete al");
				emit(generator, "    movzx rax, al");
			} else if(!strcmp(expression->op, "~"))
				emit(generator, "    not rax");
			else if(!strcmp(expression->op, "-"))
				emit(generator, "    neg rax");
		}
		return;
	case EX_INDEX:
	case EX_MEMBER:
	case EX_PTRMEMBER:
		type_1 = gen_addr(generator, expression);
		load_rax(generator, type_1);
		return;
	case EX_BINARY:
		gen_binary(generator, expression);
		return;
	case EX_CALL:
		gen_call(generator, expression);
		return;
	case EX_INITLIST:
		fatal("initializer list used as an expression");
		return;
	}
	fatal("unsupported expression");
}

static long collect_locals(Gen *generator, Stmt *statement, long off)
{
	size_t index;
	Decl *declaration;
	if(!statement)
		return off;
	if(statement->kind == ST_DECL) {
		declaration = statement->decl;
		if(find_local(generator, declaration->name))
			fatal("duplicate local %s", declaration->name);
		off = align_up(off, type_align(declaration->type));
		off += type_size(declaration->type) > 0 ? type_size(declaration->type) : 1;
		ARR_GROW(generator->locals, generator->nlocals, generator->caplocals, Symbol);
		generator->locals[generator->nlocals++] = (Symbol){declaration->name, declaration->type, SY_LOCAL, off, NULL};
	} else if(statement->kind == ST_BLOCK)
		for(index = 0; index < statement->nchildren; index++)
			off = collect_locals(generator, statement->children[index], off);
	else if(statement->kind == ST_IF) {
		off = collect_locals(generator, statement->yes, off);
		off = collect_locals(generator, statement->no, off);
	} else if(statement->kind == ST_WHILE)
		off = collect_locals(generator, statement->body, off);
	else if(statement->kind == ST_FOR) {
		off = collect_locals(generator, statement->init, off);
		off = collect_locals(generator, statement->body, off);
	}
	return off;
}

static void gen_stmt(Gen *generator, Stmt *statement)
{
	size_t index;
	Symbol *x;
	char *array, *value;
	if(!statement)
		return;
	switch(statement->kind) {
	case ST_BLOCK:
		for(index = 0; index < statement->nchildren; index++)
			gen_stmt(generator, statement->children[index]);
		return;
	case ST_DECL:
		if(statement->decl->init) {
			x = find_local(generator, statement->decl->name);
			if(is_double_type(x->type))
				gen_expr_as_double(generator, statement->decl->init);
			else
				gen_expr(generator, statement->decl->init);
			emit(generator, "    lea rcx, [rbp-%ld]", x->off);
			store_rcx(generator, x->type);
		}
		return;
	case ST_EXPR:
		gen_expr(generator, statement->expr);
		return;
	case ST_EMPTY:
		return;
	case ST_RETURN:
		if(statement->expr) {
			if(generator->current && is_double_type(generator->current->type)) {
				gen_expr_as_double(generator, statement->expr);
				emit(generator, "    movq xmm0, rax");
			} else
				gen_expr(generator, statement->expr);
		} else
			emit(generator, "    xor eax, eax");
		emit(generator, "    jmp %s", generator->retlabel);
		return;
	case ST_IF:
		array = new_label(generator, ".Lelse");
		value = new_label(generator, ".Lifend");
		gen_expr(generator, statement->cond);
		emit(generator, "    test rax, rax");
		emit(generator, "    jz %s", array);
		gen_stmt(generator, statement->yes);
		emit(generator, "    jmp %s", value);
		emit(generator, "%s:", array);
		gen_stmt(generator, statement->no);
		emit(generator, "%s:", value);
		return;
	case ST_WHILE:
		array = new_label(generator, ".Lwhile");
		value = new_label(generator, ".Lwend");
		ARR_GROW(generator->breaks, generator->nbreaks, generator->capbreaks, char *);
		generator->breaks[generator->nbreaks++] = value;
		ARR_GROW(generator->continues, generator->ncontinues, generator->capcontinues, char *);
		generator->continues[generator->ncontinues++] = array;
		emit(generator, "%s:", array);
		gen_expr(generator, statement->cond);
		emit(generator, "    test rax, rax");
		emit(generator, "    jz %s", value);
		gen_stmt(generator, statement->body);
		emit(generator, "    jmp %s", array);
		emit(generator, "%s:", value);
		generator->nbreaks--;
		generator->ncontinues--;
		return;
	case ST_FOR:
	{
		char *character = new_label(generator, ".Lforcond");
		char *count = new_label(generator, ".Lfornext");
		value = new_label(generator, ".Lforend");
		gen_stmt(generator, statement->init);
		ARR_GROW(generator->breaks, generator->nbreaks, generator->capbreaks, char *);
		generator->breaks[generator->nbreaks++] = value;
		ARR_GROW(generator->continues, generator->ncontinues, generator->capcontinues, char *);
		generator->continues[generator->ncontinues++] = count;
		emit(generator, "%s:", character);
		if(statement->cond) {
			gen_expr(generator, statement->cond);
			emit(generator, "    test rax, rax");
			emit(generator, "    jz %s", value);
		}
		gen_stmt(generator, statement->body);
		emit(generator, "%s:", count);
		if(statement->post)
			gen_expr(generator, statement->post);
		emit(generator, "    jmp %s", character);
		emit(generator, "%s:", value);
		generator->nbreaks--;
		generator->ncontinues--;
		return;
	}
	case ST_SWITCH:
	case ST_CASE:
	case ST_DEFAULT:
		fatal("switch is supported by the i386 object backend only");
		return;
	case ST_BREAK:
		if(!generator->nbreaks)
			fatal("break outside loop");
		emit(generator, "    jmp %s", generator->breaks[generator->nbreaks - 1]);
		return;
	case ST_CONTINUE:
		if(!generator->ncontinues)
			fatal("continue outside loop");
		emit(generator, "    jmp %s", generator->continues[generator->ncontinues - 1]);
		return;
	case ST_ASM:
		if(!strcmp(statement->asm_text, "hlt") || !strcmp(statement->asm_text, "cli") ||
		   !strcmp(statement->asm_text, "sti") || !strcmp(statement->asm_text, "nop") ||
		   !strcmp(statement->asm_text, "cld") || !strcmp(statement->asm_text, "std") ||
		   !strcmp(statement->asm_text, "int3") || !strcmp(statement->asm_text, "pause") ||
		   !strcmp(statement->asm_text, "ud2"))
			emit(generator, "    %s", statement->asm_text);
		else
			fatal("unsupported inline asm instruction: %s", statement->asm_text);
		return;
	}
}

static void gen_function(Gen *generator, Decl *declaration)
{
	size_t index;
	long off = 0;
	static const char *regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
	Symbol *s;
	generator->current = declaration;
	generator->nlocals = 0;
	generator->tempdepth = 0;
	generator->vaoff = 0;
	for(index = 0; index < declaration->nparams; index++) {
		off = align_up(off, 8) + 8;
		ARR_GROW(generator->locals, generator->nlocals, generator->caplocals, Symbol);
		generator->locals[generator->nlocals++] = (Symbol){declaration->params[index].name, declaration->params[index].type, SY_LOCAL, off, NULL};
	}
	off = collect_locals(generator, declaration->body, off);
	if(declaration->variadic) {
		off = align_up(off, 16) + 176;
		generator->vaoff = off;
	}
	generator->frame = align_up(off, 16);
	snprintf(generator->retlabel, sizeof(generator->retlabel), ".Lreturn_%s_%ld", declaration->name, ++generator->label);
	emit(generator, ".text");
	if(!declaration->is_static)
		emit(generator, ".globl %s", declaration->name);
	emit(generator, ".type %s, @function", declaration->name);
	emit(generator, "%s:", declaration->name);
	emit(generator, "    push rbp");
	emit(generator, "    mov rbp, rsp");
	if(generator->frame)
		emit(generator, "    sub rsp, %ld", generator->frame);
	{
		size_t integer_parameter = 0;
		size_t double_parameter = 0;
		for(index = 0; index < declaration->nparams; index++) {
			s = find_local(generator, declaration->params[index].name);
			if(is_double_type(declaration->params[index].type)) {
				if(double_parameter >= 8)
					fatal("native functions currently support up to 8 double register parameters");
				emit(generator, "    movq rax, xmm%zu", double_parameter++);
				emit(generator, "    mov qword ptr [rbp-%ld], rax", s->off);
			} else if(integer_parameter < 6)
			{
				emit(generator, "    mov qword ptr [rbp-%ld], %s", s->off, regs[integer_parameter++]);
			} else {
				emit(generator, "    mov rax, qword ptr [rbp+%zu]", 16 + (integer_parameter - 6) * 8);
				emit(generator, "    mov qword ptr [rbp-%ld], rax", s->off);
				integer_parameter++;
			}
		}
	}
	if(declaration->variadic)
		for(index = 0; index < 6; index++)
			emit(generator, "    mov qword ptr [rbp-%ld], %s", generator->vaoff - (long)index * 8, regs[index]);
	gen_stmt(generator, declaration->body);
	emit(generator, "    xor eax, eax");
	emit(generator, "%s:", generator->retlabel);
	emit(generator, "    leave");
	emit(generator, "    ret");
	emit(generator, ".size %s, .-%s", declaration->name, declaration->name);
}

static char *generate(Program *pointer)
{
	Gen g = {0};
	size_t index;
	Decl *declaration;
	g.prog = pointer;
	for(index = 0; index < pointer->n; index++) {
		declaration = pointer->a[index];
		if(declaration->body || declaration->prototype) {
			ARR_GROW(g.funcs, g.nfuncs, g.capfuncs, Decl *);
			g.funcs[g.nfuncs++] = declaration;
		} else {
			ARR_GROW(g.globals, g.nglobals, g.capglobals, Symbol);
			g.globals[g.nglobals++] = (Symbol){
				declaration->name, declaration->type, declaration->is_extern ? SY_EXTERN_GLOBAL : SY_GLOBAL, 0, NULL};
		}
	}
	emit(&g, ".intel_syntax noprefix");
	emit(&g, ".text");
	for(index = 0; index < pointer->n; index++)
		if(pointer->a[index]->body)
			gen_function(&g, pointer->a[index]);
	if(g.nglobals) {
		bool emitted_bss = false;
		for(index = 0; index < g.nglobals; index++) {
			Symbol *sym = &g.globals[index];
			if(sym->kind != SY_GLOBAL)
				continue;
			if(!emitted_bss) {
				emit(&g, ".bss");
				emitted_bss = true;
			}
			emit(&g, ".globl %s", sym->name);
			emit(&g, ".align %ld", type_align(sym->type));
			emit(&g, "%s:", sym->name);
			emit(&g, "    .zero %ld", type_size(sym->type) > 0 ? type_size(sym->type) : 1);
		}
	}
	if(g.ro.n) {
		emit(&g, ".section .rodata");
		bputn(&g.out, g.ro.s, g.ro.n);
	}
	emit(&g, ".section .note.GNU-stack,\"\",@progbits");
	return g.out.s;
}
//assembler
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

static void put_u8(Buf *buffer, uint8_t value)
{
	bputn(buffer, (char *)&value, 1);
}

static void put_u32(Buf *buffer, uint32_t value)
{
	uint8_t bytes[4];
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
	bputn(buffer, (char *)bytes, sizeof(bytes));
}

static void put_u64(Buf *buffer, uint64_t value)
{
	uint8_t bytes[8];
	int index;
	for(index = 0; index < 8; index++)
		bytes[index] = (uint8_t)(value >> (index * 8));
	bputn(buffer, (char *)bytes, sizeof(bytes));
}

static void patch_u32(Buf *buffer, uint64_t offset, uint32_t value)
{
	if(offset + 4 > buffer->n)
		fatal("internal: patch outside code buffer");
	buffer->s[offset + 0] = (char)value;
	buffer->s[offset + 1] = (char)(value >> 8);
	buffer->s[offset + 2] = (char)(value >> 16);
	buffer->s[offset + 3] = (char)(value >> 24);
}

static Buf *current_buffer(AsmImage *array)
{
	if(array->section == ASEC_TEXT)
		return &array->text;
	if(array->section == ASEC_RODATA)
		return &array->rodata;
	fatal("internal: attempted to emit bytes into BSS");
	return NULL;
}

static uint64_t current_offset(AsmImage *array)
{
	if(array->section == ASEC_TEXT)
		return array->text.n;
	if(array->section == ASEC_RODATA)
		return array->rodata.n;
	return array->bss_size;
}

static void add_label(AsmImage *array, const char *name)
{
	size_t index;
	for(index = 0; index < array->nlabels; index++)
		if(!strcmp(array->labels[index].name, name))
			fatal("duplicate assembly label %s", name);
	ARR_GROW(array->labels, array->nlabels, array->caplabels, ALabel);
	array->labels[array->nlabels].name = xstrdup(name);
	array->labels[array->nlabels].section = array->section;
	array->labels[array->nlabels].offset = current_offset(array);
	array->nlabels++;
}

static ALabel *find_label(AsmImage *array, const char *name)
{
	size_t index;
	for(index = 0; index < array->nlabels; index++)
		if(!strcmp(array->labels[index].name, name))
			return &array->labels[index];
	return NULL;
}

static Import *find_import(AsmImage *array, const char *name)
{
	size_t index;
	for(index = 0; index < array->nimports; index++)
		if(!strcmp(array->imports[index].name, name))
			return &array->imports[index];
	return NULL;
}

static Import *add_import(AsmImage *array, const char *name, bool object)
{
	Import *import = find_import(array, name);
	if(import) {
		if(object)
			import->object = true;
		return import;
	}
	ARR_GROW(array->imports, array->nimports, array->capimports, Import);
	import = &array->imports[array->nimports++];
	memset(import, 0, sizeof(*import));
	import->name = xstrdup(name);
	import->object = object;
	return import;
}

static void add_fixup(AsmImage *array, FixKind kind, uint64_t offset, const char *name, long aux)
{
	ARR_GROW(array->fixups, array->nfixups, array->capfixups, Fixup);
	array->fixups[array->nfixups].kind = kind;
	array->fixups[array->nfixups].offset = offset;
	array->fixups[array->nfixups].name = name ? xstrdup(name) : NULL;
	array->fixups[array->nfixups].aux = aux;
	array->nfixups++;
}

static int register_number(const char *name)
{
	static const char *names[] = {
		"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
	int index;
	for(index = 0; index < 16; index++)
		if(!strcmp(name, names[index]))
			return index;
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

static void emit_rex(Buf *buffer, bool wide_operand, int reg, int index, int base)
{
	uint8_t rex = 0x40;
	if(wide_operand)
		rex |= 8;
	if(reg & 8)
		rex |= 4;
	if(index & 8)
		rex |= 2;
	if(base & 8)
		rex |= 1;
	if(rex != 0x40)
		put_u8(buffer, rex);
}

static void emit_modrm(Buf *buffer, int mod, int reg, int rm_operand)
{
	put_u8(buffer, (uint8_t)((mod << 6) | ((reg & 7) << 3) | (rm_operand & 7)));
}

static void emit_reg_reg(Buf *buffer, uint8_t opcode, int destination, int source)
{
	emit_rex(buffer, true, source, 0, destination);
	put_u8(buffer, opcode);
	emit_modrm(buffer, 3, source, destination);
}

static void emit_memory_operand(Buf *buffer, int reg, int base, long displacement)
{
	int mod;
	if(displacement == 0 && (base & 7) != 5)
		mod = 0;
	else if(displacement >= -128 && displacement <= 127)
		mod = 1;
	else
		mod = 2;
	emit_modrm(buffer, mod, reg, base);
	if((base & 7) == 4)
		put_u8(buffer, 0x24);
	if(mod == 1)
		put_u8(buffer, (uint8_t)displacement);
	else if(mod == 2 || (mod == 0 && (base & 7) == 5))
		put_u32(buffer, (uint32_t)displacement);
}

static void emit_mov_memory_register(Buf *buffer, int base, long displacement, int source, int size)
{
	if(size == 8) {
		emit_rex(buffer, true, source, 0, base);
		put_u8(buffer, 0x89);
	} else if(size == 4)
	{
		emit_rex(buffer, false, source, 0, base);
		put_u8(buffer, 0x89);
	} else {
		emit_rex(buffer, false, source, 0, base);
		put_u8(buffer, 0x88);
	}
	emit_memory_operand(buffer, source, base, displacement);
}

static void emit_mov_register_memory(Buf *buffer, int destination, int base, long displacement, int size)
{
	if(size == 8) {
		emit_rex(buffer, true, destination, 0, base);
		put_u8(buffer, 0x8b);
	} else {
		emit_rex(buffer, false, destination, 0, base);
		put_u8(buffer, 0x8b);
	}
	emit_memory_operand(buffer, destination, base, displacement);
}

static void emit_lea_memory(Buf *buffer, int destination, int base, long displacement)
{
	emit_rex(buffer, true, destination, 0, base);
	put_u8(buffer, 0x8d);
	emit_memory_operand(buffer, destination, base, displacement);
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

static void emit_relative_fixup(AsmImage *array, uint8_t opcode, const char *name, bool conditional, uint8_t condition)
{
	Buf *buffer = &array->text;
	if(conditional) {
		put_u8(buffer, 0x0f);
		put_u8(buffer, condition);
	} else {
		put_u8(buffer, opcode);
	}
	add_fixup(array, FIX_REL32_SYMBOL, buffer->n, name, 0);
	put_u32(buffer, 0);
}

static void assemble_instruction(AsmImage *array, char *line)
{
	Buf *buffer = current_buffer(array);
	char left[128], right[128], name[256], reg1[32], reg2[32];
	long long signed_value;
	unsigned long long unsigned_value;
	long displacement;
	int first_register, second_register;
	if(!strcmp(line, "push rbp")) {
		put_u8(buffer, 0x55);
		return;
	}
	if(!strcmp(line, "leave")) {
		put_u8(buffer, 0xc9);
		return;
	}
	if(!strcmp(line, "ret")) {
		put_u8(buffer, 0xc3);
		return;
	}
	if(!strcmp(line, "hlt")) {
		put_u8(buffer, 0xf4);
		return;
	}
	if(!strcmp(line, "cli")) {
		put_u8(buffer, 0xfa);
		return;
	}
	if(!strcmp(line, "sti")) {
		put_u8(buffer, 0xfb);
		return;
	}
	if(!strcmp(line, "nop")) {
		put_u8(buffer, 0x90);
		return;
	}
	if(!strcmp(line, "cld")) {
		put_u8(buffer, 0xfc);
		return;
	}
	if(!strcmp(line, "std")) {
		put_u8(buffer, 0xfd);
		return;
	}
	if(!strcmp(line, "int3")) {
		put_u8(buffer, 0xcc);
		return;
	}
	if(!strcmp(line, "pause")) {
		put_u8(buffer, 0xf3);
		put_u8(buffer, 0x90);
		return;
	}
	if(!strcmp(line, "ud2")) {
		put_u8(buffer, 0x0f);
		put_u8(buffer, 0x0b);
		return;
	}
	if(!strcmp(line, "cqo")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x99);
		return;
	}
	if(!strcmp(line, "xor eax, eax")) {
		put_u8(buffer, 0x31);
		put_u8(buffer, 0xc0);
		return;
	}
	if(!strcmp(line, "test rax, rax")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x85);
		put_u8(buffer, 0xc0);
		return;
	}
	if(!strcmp(line, "cmp rcx, rax")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x39);
		put_u8(buffer, 0xc1);
		return;
	}
	if(!strcmp(line, "movzx eax, byte ptr [rax]")) {
		put_u8(buffer, 0x0f);
		put_u8(buffer, 0xb6);
		put_u8(buffer, 0x00);
		return;
	}
	if(!strcmp(line, "movzx eax, word ptr [rax]")) {
		put_u8(buffer, 0x0f);
		put_u8(buffer, 0xb7);
		put_u8(buffer, 0x00);
		return;
	}
	if(!strcmp(line, "mov eax, dword ptr [rax]")) {
		put_u8(buffer, 0x8b);
		put_u8(buffer, 0x00);
		return;
	}
	if(!strcmp(line, "movsxd rax, dword ptr [rax]")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x63);
		put_u8(buffer, 0x00);
		return;
	}
	if(!strcmp(line, "mov rax, qword ptr [rax]")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x8b);
		put_u8(buffer, 0x00);
		return;
	}
	if(!strcmp(line, "mov byte ptr [rcx], al")) {
		put_u8(buffer, 0x88);
		put_u8(buffer, 0x01);
		return;
	}
	if(!strcmp(line, "mov word ptr [rcx], ax")) {
		put_u8(buffer, 0x66);
		put_u8(buffer, 0x89);
		put_u8(buffer, 0x01);
		return;
	}
	if(!strcmp(line, "mov dword ptr [rcx], eax")) {
		put_u8(buffer, 0x89);
		put_u8(buffer, 0x01);
		return;
	}
	if(!strcmp(line, "mov qword ptr [rcx], rax")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x89);
		put_u8(buffer, 0x01);
		return;
	}
	if(!strcmp(line, "movzx rax, al")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x0f);
		put_u8(buffer, 0xb6);
		put_u8(buffer, 0xc0);
		return;
	}
	if(!strcmp(line, "movzx eax, al")) {
		put_u8(buffer, 0x0f);
		put_u8(buffer, 0xb6);
		put_u8(buffer, 0xc0);
		return;
	}
	if(!strcmp(line, "movzx eax, ax")) {
		put_u8(buffer, 0x0f);
		put_u8(buffer, 0xb7);
		put_u8(buffer, 0xc0);
		return;
	}
	if(!strcmp(line, "movsxd rax, eax")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x63);
		put_u8(buffer, 0xc0);
		return;
	}
	if(!strcmp(line, "mov eax, eax")) {
		put_u8(buffer, 0x89);
		put_u8(buffer, 0xc0);
		return;
	}
	if(sscanf(line, "movq %31[^,], %31s", reg1, reg2) == 2) {
		int destination_xmm = xmm_number(reg1);
		int source_xmm = xmm_number(reg2);
		first_register = register_number(reg1);
		second_register = register_number(reg2);
		if(destination_xmm >= 0 && second_register >= 0) {
			put_u8(buffer, 0x66);
			emit_rex(buffer, true, destination_xmm, 0, second_register);
			put_u8(buffer, 0x0f);
			put_u8(buffer, 0x6e);
			emit_modrm(buffer, 3, destination_xmm, second_register);
			return;
		}
		if(first_register >= 0 && source_xmm >= 0) {
			put_u8(buffer, 0x66);
			emit_rex(buffer, true, source_xmm, 0, first_register);
			put_u8(buffer, 0x0f);
			put_u8(buffer, 0x7e);
			emit_modrm(buffer, 3, source_xmm, first_register);
			return;
		}
	}
	if(sscanf(line, "cvtsi2sd %31[^,], %31s", reg1, reg2) == 2) {
		int destination_xmm = xmm_number(reg1);
		second_register = register_number(reg2);
		if(destination_xmm >= 0 && second_register >= 0) {
			put_u8(buffer, 0xf2);
			emit_rex(buffer, true, destination_xmm, 0, second_register);
			put_u8(buffer, 0x0f);
			put_u8(buffer, 0x2a);
			emit_modrm(buffer, 3, destination_xmm, second_register);
			return;
		}
	}
	if(sscanf(line, "cvttsd2si %31[^,], %31s", reg1, reg2) == 2) {
		first_register = register_number(reg1);
		int source_xmm = xmm_number(reg2);
		if(first_register >= 0 && source_xmm >= 0) {
			put_u8(buffer, 0xf2);
			emit_rex(buffer, true, first_register, 0, source_xmm);
			put_u8(buffer, 0x0f);
			put_u8(buffer, 0x2c);
			emit_modrm(buffer, 3, first_register, source_xmm);
			return;
		}
	}
	{
		char mnemonic[16];
		if(sscanf(line, "%15s %31[^,], %31s", mnemonic, reg1, reg2) == 3) {
			int destination_xmm = xmm_number(reg1);
			int source_xmm = xmm_number(reg2);
			uint8_t opcode = 0;
			if(!strcmp(mnemonic, "addsd"))
				opcode = 0x58;
			else if(!strcmp(mnemonic, "subsd"))
				opcode = 0x5c;
			else if(!strcmp(mnemonic, "mulsd"))
				opcode = 0x59;
			else if(!strcmp(mnemonic, "divsd"))
				opcode = 0x5e;
			if(opcode && destination_xmm >= 0 && source_xmm >= 0) {
				put_u8(buffer, 0xf2);
				emit_rex(buffer, false, destination_xmm, 0, source_xmm);
				put_u8(buffer, 0x0f);
				put_u8(buffer, opcode);
				emit_modrm(buffer, 3, destination_xmm, source_xmm);
				return;
			}
		}
	}
	if(!strcmp(line, "not rax")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0xf7);
		put_u8(buffer, 0xd0);
		return;
	}
	if(!strcmp(line, "neg rax")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0xf7);
		put_u8(buffer, 0xd8);
		return;
	}
	if(!strcmp(line, "idiv r10")) {
		put_u8(buffer, 0x49);
		put_u8(buffer, 0xf7);
		put_u8(buffer, 0xfa);
		return;
	}
	if(!strcmp(line, "shl rax, cl")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0xd3);
		put_u8(buffer, 0xe0);
		return;
	}
	if(!strcmp(line, "sar rax, cl")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0xd3);
		put_u8(buffer, 0xf8);
		return;
	}
	if(!strncmp(line, "set", 3) && strstr(line, " al")) {
		uint8_t condition;
		char condition_code[8];
		if(sscanf(line, "set%7s al", condition_code) != 1)
			fatal("internal assembler: %s", line);
		if(!strcmp(condition_code, "e"))
			condition = 0x94;
		else if(!strcmp(condition_code, "ne"))
			condition = 0x95;
		else if(!strcmp(condition_code, "l"))
			condition = 0x9c;
		else if(!strcmp(condition_code, "le"))
			condition = 0x9e;
		else if(!strcmp(condition_code, "g"))
			condition = 0x9f;
		else if(!strcmp(condition_code, "ge"))
			condition = 0x9d;
		else
			fatal("unsupported condition code %s", condition_code);
		put_u8(buffer, 0x0f);
		put_u8(buffer, condition);
		put_u8(buffer, 0xc0);
		return;
	}
	if(sscanf(line, "push %31s", reg1) == 1 &&
	   register_number(reg1) >= 0)
	{
		first_register = register_number(reg1);
		if(first_register >= 8)
			put_u8(buffer, 0x41);
		put_u8(buffer, (uint8_t)(0x50 + (first_register & 7)));
		return;
	}
	if(sscanf(line, "pop %31s", reg1) == 1 &&
	   register_number(reg1) >= 0)
	{
		first_register = register_number(reg1);
		if(first_register >= 8)
			put_u8(buffer, 0x41);
		put_u8(buffer, (uint8_t)(0x58 + (first_register & 7)));
		return;
	}
	if(sscanf(line, "jmp %255s", name) == 1) {
		emit_relative_fixup(array, 0xe9, name, false, 0);
		return;
	}
	if(sscanf(line, "jz %255s", name) == 1) {
		emit_relative_fixup(array, 0, name, true, 0x84);
		return;
	}
	if(sscanf(line, "jnz %255s", name) == 1) {
		emit_relative_fixup(array, 0, name, true, 0x85);
		return;
	}
	if(sscanf(line, "call %255s", name) == 1) {
		char *suffix = strstr(name, "@PLT");
		if(suffix)
			*suffix = 0;
		put_u8(buffer, 0xe8);
		add_fixup(array, FIX_REL32_SYMBOL, buffer->n, name, 0);
		put_u32(buffer, 0);
		add_import(array, name, false);
		return;
	}
	if(sscanf(line, "mov %31[^,], %31s", reg1, reg2) == 2) {
		first_register = register_number(reg1);
		second_register = register_number(reg2);
		if(first_register >= 0 && second_register >= 0) {
			emit_reg_reg(buffer, 0x89, first_register, second_register);
			return;
		}
	}
	if(sscanf(line, "mov rax, %llu", &unsigned_value) == 1) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0xb8);
		put_u64(buffer, (uint64_t)unsigned_value);
		return;
	}
	if(sscanf(line, "mov eax, %lld", &signed_value) == 1) {
		put_u8(buffer, 0xb8);
		put_u32(buffer, (uint32_t)signed_value);
		return;
	}
	if(sscanf(line, "lea %31[^,], [rbp-%ld]", reg1, &displacement) == 2) {
		first_register = register_number(reg1);
		if(first_register < 0)
			fatal("bad register in %s", line);
		emit_lea_memory(buffer, first_register, 5, -displacement);
		return;
	}
	if(sscanf(line, "lea %31[^,], [rbp+%ld]", reg1, &displacement) == 2) {
		first_register = register_number(reg1);
		if(first_register < 0)
			fatal("bad register in %s", line);
		emit_lea_memory(buffer, first_register, 5, displacement);
		return;
	}
	if(sscanf(line, "lea rax, [rip+%255[^]]]", name) == 1) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x8d);
		put_u8(buffer, 0x05);
		add_fixup(array, FIX_RIP32_SYMBOL, buffer->n, name, 0);
		put_u32(buffer, 0);
		return;
	}
	if(sscanf(line, "movzx eax, byte ptr [rip+%255[^]]]", name) == 1) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x8b);
		put_u8(buffer, 0x05);
		add_fixup(array, FIX_RIP32_OBJECT, buffer->n, name, 0);
		put_u32(buffer, 0);
		put_u8(buffer, 0x0f);
		put_u8(buffer, 0xb6);
		put_u8(buffer, 0x00);
		add_import(array, name, true);
		return;
	}
	if(sscanf(line, "movzx eax, word ptr [rip+%255[^]]]", name) == 1) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x8b);
		put_u8(buffer, 0x05);
		add_fixup(array, FIX_RIP32_OBJECT, buffer->n, name, 0);
		put_u32(buffer, 0);
		put_u8(buffer, 0x0f);
		put_u8(buffer, 0xb7);
		put_u8(buffer, 0x00);
		add_import(array, name, true);
		return;
	}
	if(sscanf(line, "movsxd rax, dword ptr [rip+%255[^]]]", name) == 1) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x8b);
		put_u8(buffer, 0x05);
		add_fixup(array, FIX_RIP32_OBJECT, buffer->n, name, 0);
		put_u32(buffer, 0);
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x63);
		put_u8(buffer, 0x00);
		add_import(array, name, true);
		return;
	}
	if(sscanf(line, "mov eax, dword ptr [rip+%255[^]]]", name) == 1) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x8b);
		put_u8(buffer, 0x05);
		add_fixup(array, FIX_RIP32_OBJECT, buffer->n, name, 0);
		put_u32(buffer, 0);
		put_u8(buffer, 0x8b);
		put_u8(buffer, 0x00);
		add_import(array, name, true);
		return;
	}
	if(sscanf(line, "mov rax, qword ptr [rip+%255[^]]]", name) == 1) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x8b);
		put_u8(buffer, 0x05);
		add_fixup(array, FIX_RIP32_OBJECT, buffer->n, name, 0);
		put_u32(buffer, 0);
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x8b);
		put_u8(buffer, 0x00);
		add_import(array, name, true);
		return;
	}
	if(sscanf(line, "mov rax, qword ptr [rbp+%ld]", &displacement) == 1) {
		emit_mov_register_memory(buffer, 0, 5, displacement, 8);
		return;
	}
	if(sscanf(line, "mov qword ptr [rbp-%ld], %31s", &displacement, reg1) == 2) {
		first_register = register_number(reg1);
		if(first_register < 0)
			fatal("bad register in %s", line);
		emit_mov_memory_register(buffer, 5, -displacement, first_register, 8);
		return;
	}
	if(sscanf(line, "mov qword ptr [rax+%ld], %31s", &displacement, reg1) == 2) {
		first_register = register_number(reg1);
		if(first_register < 0)
			fatal("bad register in %s", line);
		emit_mov_memory_register(buffer, 0, displacement, first_register, 8);
		return;
	}
	if(sscanf(line, "mov qword ptr [rax], %31s", reg1) == 1) {
		first_register = register_number(reg1);
		if(first_register < 0)
			fatal("bad register in %s", line);
		emit_mov_memory_register(buffer, 0, 0, first_register, 8);
		return;
	}
	if(sscanf(line, "mov dword ptr [rax+%ld], %lld", &displacement, &signed_value) == 2) {
		put_u8(buffer, 0xc7);
		emit_memory_operand(buffer, 0, 0, displacement);
		put_u32(buffer, (uint32_t)signed_value);
		return;
	}
	if(sscanf(line, "mov dword ptr [rax], %lld", &signed_value) == 1) {
		put_u8(buffer, 0xc7);
		emit_memory_operand(buffer, 0, 0, 0);
		put_u32(buffer, (uint32_t)signed_value);
		return;
	}
	if(sscanf(line, "sub rsp, %lld", &signed_value) == 1 ||
	   sscanf(line, "add rsp, %lld", &signed_value) == 1)
	{
		bool add = !strncmp(line, "add", 3);
		put_u8(buffer, 0x48);
		if(signed_value >= -128 && signed_value <= 127) {
			put_u8(buffer, 0x83);
			put_u8(buffer, add ? 0xc4 : 0xec);
			put_u8(buffer, (uint8_t)signed_value);
		} else {
			put_u8(buffer, 0x81);
			put_u8(buffer, add ? 0xc4 : 0xec);
			put_u32(buffer, (uint32_t)signed_value);
		}
		return;
	}
	if(sscanf(line, "imul rax, %lld", &signed_value) == 1) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x69);
		put_u8(buffer, 0xc0);
		put_u32(buffer, (uint32_t)signed_value);
		return;
	}
	if(sscanf(line, "add rax, %lld", &signed_value) == 1 ||
	   sscanf(line, "sub rax, %lld", &signed_value) == 1)
	{
		bool add = !strncmp(line, "add", 3);
		put_u8(buffer, 0x48);
		if(signed_value >= -128 && signed_value <= 127) {
			put_u8(buffer, 0x83);
			put_u8(buffer, add ? 0xc0 : 0xe8);
			put_u8(buffer, (uint8_t)signed_value);
		} else {
			put_u8(buffer, add ? 0x05 : 0x2d);
			put_u32(buffer, (uint32_t)signed_value);
		}
		return;
	}
	if(!strcmp(line, "add rax, rcx")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x01);
		put_u8(buffer, 0xc8);
		return;
	}
	if(!strcmp(line, "sub rcx, rax")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x29);
		put_u8(buffer, 0xc1);
		return;
	}
	if(!strcmp(line, "imul rax, rcx")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x0f);
		put_u8(buffer, 0xaf);
		put_u8(buffer, 0xc1);
		return;
	}
	if(!strcmp(line, "and rax, rcx")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x21);
		put_u8(buffer, 0xc8);
		return;
	}
	if(!strcmp(line, "or rax, rcx")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x09);
		put_u8(buffer, 0xc8);
		return;
	}
	if(!strcmp(line, "xor rax, rcx")) {
		put_u8(buffer, 0x48);
		put_u8(buffer, 0x31);
		put_u8(buffer, 0xc8);
		return;
	}
	if(sscanf(line, "%127[^,], %127s", left, right) == 2)
		fatal("unsupported internal assembly instruction: %s, %s", left, right);
	fatal("unsupported internal assembly instruction: %s", line);
}

static void align_assembly_section(AsmImage *array, uint64_t alignment)
{
	if(array->section == ASEC_BSS) {
		array->bss_size = ualign(array->bss_size, alignment);
		return;
	}
	while(current_buffer(array)->n % alignment)
		put_u8(current_buffer(array), 0);
}

static void parse_byte_directive(Buf *buffer, const char *line)
{
	const char *position = line + 5;
	while(*position) {
		char *end;
		unsigned long value;
		while(*position == ' ' || *position == '\t' || *position == ',')
			position++;
		if(!*position)
			break;
		value = strtoul(position, &end, 0);
		if(end == position || value > 255)
			fatal("bad .byte directive");
		put_u8(buffer, (uint8_t)value);
		position = end;
	}
}

static void add_startup(AsmImage *array)
{
	Buf *buffer = &array->text;
	array->section = ASEC_TEXT;
	add_label(array, "_start");
	put_u8(buffer, 0x31);
	put_u8(buffer, 0xed);
	put_u8(buffer, 0x48);
	put_u8(buffer, 0x8b);
	put_u8(buffer, 0x3c);
	put_u8(buffer, 0x24);
	put_u8(buffer, 0x48);
	put_u8(buffer, 0x8d);
	put_u8(buffer, 0x74);
	put_u8(buffer, 0x24);
	put_u8(buffer, 0x08);
	put_u8(buffer, 0x48);
	put_u8(buffer, 0x83);
	put_u8(buffer, 0xe4);
	put_u8(buffer, 0xf0);
	put_u8(buffer, 0xe8);
	add_fixup(array, FIX_REL32_SYMBOL, buffer->n, "main", 0);
	put_u32(buffer, 0);
	put_u8(buffer, 0x89);
	put_u8(buffer, 0xc7);
	put_u8(buffer, 0xb8);
	put_u32(buffer, 60);
	put_u8(buffer, 0x0f);
	put_u8(buffer, 0x05);
}

static void internal_assemble(const char *assembly, AsmImage *array)
{
	char *copy = xstrdup(assembly);
	char *save = NULL;
	char *raw;
	memset(array, 0, sizeof(*array));
	add_startup(array);
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
		if(!strcmp(line, ".text")) {
			array->section = ASEC_TEXT;
			continue;
		}
		if(!strcmp(line, ".bss")) {
			array->section = ASEC_BSS;
			continue;
		}
		if(!strcmp(line, ".section .rodata")) {
			array->section = ASEC_RODATA;
			continue;
		}
		if(!strncmp(line, ".align ", 7)) {
			align_assembly_section(array, strtoull(line + 7, NULL, 0));
			continue;
		}
		if(!strncmp(line, ".zero ", 6)) {
			uint64_t count = strtoull(line + 6, NULL, 0);
			if(array->section == ASEC_BSS)
				array->bss_size += count;
			else
				while(count--)
					put_u8(current_buffer(array), 0);
			continue;
		}
		if(!strncmp(line, ".byte", 5)) {
			parse_byte_directive(current_buffer(array), line);
			continue;
		}
		length = strlen(line);
		if(length && line[length - 1] == ':') {
			line[length - 1] = 0;
			add_label(array, line);
			continue;
		}
		if(array->section != ASEC_TEXT)
			fatal("instruction outside text section: %s", line);
		assemble_instruction(array, line);
	}
	free(copy);
}

static void append_plt(AsmImage *array)
{
	Buf *buffer = &array->text;
	size_t index;
	uint32_t relocation_index = 0;
	while(buffer->n & 15)
		put_u8(buffer, 0x90);
	array->plt0_offset = buffer->n;
	put_u8(buffer, 0xff);
	put_u8(buffer, 0x35);
	add_fixup(array, FIX_RIP32_GOT, buffer->n, NULL, 1);
	put_u32(buffer, 0);
	put_u8(buffer, 0xff);
	put_u8(buffer, 0x25);
	add_fixup(array, FIX_RIP32_GOT, buffer->n, NULL, 2);
	put_u32(buffer, 0);
	put_u8(buffer, 0x0f);
	put_u8(buffer, 0x1f);
	put_u8(buffer, 0x40);
	put_u8(buffer, 0x00);
	for(index = 0; index < array->nimports; index++) {
		Import *import = &array->imports[index];
		if(import->object)
			continue;
		import->plt_offset = buffer->n;
		import->plt_reloc_index = relocation_index++;
		put_u8(buffer, 0xff);
		put_u8(buffer, 0x25);
		add_fixup(array, FIX_RIP32_GOT, buffer->n, NULL, import->got_index);
		put_u32(buffer, 0);
		put_u8(buffer, 0x68);
		put_u32(buffer, import->plt_reloc_index);
		put_u8(buffer, 0xe9);
		add_fixup(array, FIX_REL32_SYMBOL, buffer->n, "@plt0", 0);
		put_u32(buffer, 0);
	}
}

static void prune_local_imports(AsmImage *array)
{
	size_t read_index;
	size_t write_index = 0;
	for(read_index = 0; read_index < array->nimports; read_index++) {
		Import *import = &array->imports[read_index];
		if(find_label(array, import->name))
			continue;
		if(write_index != read_index)
			array->imports[write_index] = *import;
		write_index++;
	}
	array->nimports = write_index;
}

static uint32_t sysv_hash(const unsigned char *name)
{
	uint32_t hash = 0;
	while(*name) {
		uint32_t high;
		hash = (hash << 4) + *name++;
		high = hash & 0xf0000000;
		if(high)
			hash ^= high >> 24;
		hash &= ~high;
	}
	return hash;
}

static uint64_t symbol_address(AsmImage *array_1, ALabel *label, uint64_t text_va, uint64_t rodata_va, uint64_t bss_va)
{
	(void)array_1;
	if(label->section == ASEC_TEXT)
		return text_va + label->offset;
	if(label->section == ASEC_RODATA)
		return rodata_va + label->offset;
	return bss_va + label->offset;
}

static Import *require_import(AsmImage *array, const char *name, bool object)
{
	Import *import = add_import(array, name, object);
	if(object)
		import->object = true;
	return import;
}

static void patch_code(AsmImage *array, uint64_t text_va, uint64_t rodata_va, uint64_t bss_va, uint64_t got_va)
{
	size_t index;
	for(index = 0; index < array->nfixups; index++) {
		Fixup *fix = &array->fixups[index];
		uint64_t place = text_va + fix->offset;
		uint64_t target = 0;
		int64_t relative;
		if(fix->kind == FIX_RIP32_GOT) {
			target = got_va + (uint64_t)fix->aux * 8;
		} else if(fix->name && !strcmp(fix->name, "@plt0"))
		{
			target = text_va + array->plt0_offset;
		} else {
			ALabel *label = fix->name ? find_label(array, fix->name) : NULL;
			if(label)
				target = symbol_address(array, label, text_va, rodata_va, bss_va);
			else {
				Import *import;
				if(fix->kind == FIX_RIP32_OBJECT)
					import = require_import(array, fix->name, true);
				else
					import = require_import(array, fix->name, false);
				if(fix->kind == FIX_RIP32_OBJECT)
					target = got_va + (uint64_t)import->got_index * 8;
				else
					target = text_va + import->plt_offset;
			}
		}
		relative = (int64_t)target - (int64_t)(place + 4);
		if(relative < INT32_MIN || relative > INT32_MAX)
			fatal("internal: relative relocation overflow");
		patch_u32(&array->text, fix->offset, (uint32_t)(int32_t)relative);
	}
}

static void copy_into(uint8_t *file, uint64_t offset, const void *data, size_t size)
{
	memcpy(file + offset, data, size);
}

static void write_independent_elf(const char *path, AsmImage *array, bool needs_x11, char **libraries, size_t library_count)
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
	size_t index_1;
	uint32_t next_got = 3;
	uint32_t next_symbol = 1;
	uint32_t next_function_relocation = 0;
	uint32_t next_object_relocation = 0;
	prune_local_imports(array);
	put_u8(&dynstr, 0);
	needed_offsets = xcalloc(needed_count, sizeof(*needed_offsets));
	needed_offsets[0] = dynstr.n;
	bputs(&dynstr, "libc.so.6");
	put_u8(&dynstr, 0);
	if(needs_x11) {
		needed_offsets[1] = dynstr.n;
		bputs(&dynstr, "libX11.so.6");
		put_u8(&dynstr, 0);
	}
	for(index_1 = 0; index_1 < library_count; index_1++) {
		char library_name[256];
		uint32_t index = 1 + (needs_x11 ? 1 : 0) + (uint32_t)index_1;
		const char *argument = libraries[index_1];
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
	for(index_1 = 0; index_1 < array->nimports; index_1++) {
		Import *import = &array->imports[index_1];
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
	append_plt(array);
	symbol_count = (uint32_t)array->nimports + 1;
	dynsym = xcalloc(symbol_count, sizeof(*dynsym));
	for(index_1 = 0; index_1 < array->nimports; index_1++) {
		Import *import = &array->imports[index_1];
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
	for(index_1 = 1; index_1 < symbol_count; index_1++) {
		uint32_t bucket = sysv_hash((unsigned char *)array->imports[index_1 - 1].name) % bucket_count;
		if(!buckets[bucket])
			buckets[bucket] = (uint32_t)index_1;
		else {
			uint32_t link = buckets[bucket];
			while(chains[link])
				link = chains[link];
			chains[link] = (uint32_t)index_1;
		}
	}
	rela_dyn = xcalloc(object_count ? object_count : 1, sizeof(*rela_dyn));
	rela_plt = xcalloc(function_count ? function_count : 1, sizeof(*rela_plt));
	got = xcalloc(3 + array->nimports, sizeof(*got));
	cursor = ualign(sizeof(Elf64_Ehdr) + phnum * sizeof(Elf64_Phdr), 16);
	interp_off = cursor;
	cursor += sizeof(interpreter);
	text_off = ualign(cursor, 16);
	cursor = text_off + array->text.n;
	rodata_off = ualign(cursor, 16);
	cursor = rodata_off + array->rodata.n;
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
	cursor = got_off + (3 + array->nimports) * sizeof(uint64_t);
	dynamic_count = needed_count + 12 + 1;
	dynamic_off = ualign(cursor, 8);
	cursor = dynamic_off + dynamic_count * sizeof(Elf64_Dyn);
	file_size = ualign(cursor, 16);
	bss_off = file_size;
	memory_size = bss_off + array->bss_size;
	text_va = base + text_off;
	rodata_va = base + rodata_off;
	bss_va = base + bss_off;
	got_va = base + got_off;
	dynamic_va = base + dynamic_off;
	got[0] = dynamic_va;
	for(index_1 = 0; index_1 < array->nimports; index_1++) {
		Import *import = &array->imports[index_1];
		if(import->object) {
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
	patch_code(array, text_va, rodata_va, bss_va, got_va);
	dynamic = xcalloc(dynamic_count, sizeof(*dynamic));
	{
		uint32_t value = 0;
		for(index_1 = 0; index_1 < needed_count; index_1++) {
			dynamic[value].d_tag = DT_NEEDED;
			dynamic[value++].d_un.d_val = needed_offsets[index_1];
		}
		dynamic[value].d_tag = DT_HASH;
		dynamic[value++].d_un.d_ptr = base + hash_off;
		dynamic[value].d_tag = DT_STRTAB;
		dynamic[value++].d_un.d_ptr = base + dynstr_off;
		dynamic[value].d_tag = DT_SYMTAB;
		dynamic[value++].d_un.d_ptr = base + dynsym_off;
		dynamic[value].d_tag = DT_STRSZ;
		dynamic[value++].d_un.d_val = dynstr.n;
		dynamic[value].d_tag = DT_SYMENT;
		dynamic[value++].d_un.d_val = sizeof(Elf64_Sym);
		dynamic[value].d_tag = DT_PLTGOT;
		dynamic[value++].d_un.d_ptr = got_va;
		dynamic[value].d_tag = DT_PLTRELSZ;
		dynamic[value++].d_un.d_val = function_count * sizeof(Elf64_Rela);
		dynamic[value].d_tag = DT_PLTREL;
		dynamic[value++].d_un.d_val = DT_RELA;
		dynamic[value].d_tag = DT_JMPREL;
		dynamic[value++].d_un.d_ptr = base + rela_plt_off;
		dynamic[value].d_tag = DT_RELA;
		dynamic[value++].d_un.d_ptr = base + rela_dyn_off;
		dynamic[value].d_tag = DT_RELASZ;
		dynamic[value++].d_un.d_val = object_count * sizeof(Elf64_Rela);
		dynamic[value].d_tag = DT_RELAENT;
		dynamic[value++].d_un.d_val = sizeof(Elf64_Rela);
		dynamic[value].d_tag = DT_NULL;
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
	copy_into(file, text_off, array->text.s, array->text.n);
	copy_into(file, rodata_off, array->rodata.s, array->rodata.n);
	copy_into(file, dynstr_off, dynstr.s, dynstr.n);
	copy_into(file, dynsym_off, dynsym, symbol_count * sizeof(Elf64_Sym));
	copy_into(file, hash_off, hash, (2 + bucket_count + symbol_count) * sizeof(uint32_t));
	copy_into(file, rela_dyn_off, rela_dyn, object_count * sizeof(Elf64_Rela));
	copy_into(file, rela_plt_off, rela_plt, function_count * sizeof(Elf64_Rela));
	copy_into(file, got_off, got, (3 + array->nimports) * sizeof(uint64_t));
	copy_into(file, dynamic_off, dynamic, dynamic_count * sizeof(Elf64_Dyn));
	write_file(path, (char *)file, file_size);
	if(chmod(path, 0755))
		fatal("cannot make %s executable: %s", path, strerror(errno));
}
//i386 backend
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
	switch(type->kind) {
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
		size_t index;
		for(index = 0; index < type->nmembers; index++) {
			long alignment = type->packed ? 1 : i386_type_align(type->members[index].type);
			offset = align_up(offset, alignment);
			offset += i386_type_size(type->members[index].type);
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
	if(type->kind == TY_STRUCT) {
		long maximum = 1;
		if(type->packed)
			return 1;
		size_t index;
		for(index = 0; index < type->nmembers; index++) {
			long alignment = i386_type_align(type->members[index].type);
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
	size_t index;
	for(index = 0; index < gen->nsymbols; index++)
		if(!strcmp(gen->symbols[index].name, name))
			return &gen->symbols[index];
	return NULL;
}

static I386ObjSymbol *i386_add_object_symbol(I386Gen *gen,
					     const char *name,
					     CType *type,
					     bool function)
{
	I386ObjSymbol *symbol = i386_find_object_symbol(gen, name);
	if(symbol) {
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
	size_t index;
	for(index = 0; index < gen->nlabels; index++)
		if(!strcmp(gen->labels[index].name, name))
			return &gen->labels[index];
	return NULL;
}

static void i386_define_label(I386Gen *gen, const char *name)
{
	I386Label *label = i386_find_label(gen, name);
	if(!label) {
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
	if(condition == 0xff) {
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
	size_t index;
	for(index = 0; index < gen->njump_fixups; index++) {
		I386JumpFixup *fixup = &gen->jump_fixups[index];
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
	size_t index;
	for(index = 0; index < gen->nlocals; index++)
		if(!strcmp(gen->locals[index].name, name))
			return &gen->locals[index];
	return NULL;
}

static Decl *i386_find_function(Program *program, const char *name)
{
	size_t index;
	Decl *prototype = NULL;
	for(index = 0; index < program->n; index++) {
		Decl *declaration = program->a[index];
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
	size_t index;
	Decl *external = NULL;
	for(index = 0; index < program->n; index++) {
		Decl *declaration = program->a[index];
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
	size_t index;
	if(!type || type->kind != TY_STRUCT)
		return NULL;
	for(index = 0; index < type->nmembers; index++) {
		long alignment = type->packed ? 1 : i386_type_align(type->members[index].type);
		offset = align_up(offset, alignment);
		if(!strcmp(type->members[index].name, name)) {
			if(member_offset)
				*member_offset = offset;
			return &type->members[index];
		}
		offset += i386_type_size(type->members[index].type);
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
	switch(expression->kind) {
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
		if(!strcmp(expression->op, "*")) {
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
		if(expression->left->kind == EX_ID) {
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
	size_t index;
	uint32_t offset;
	for(index = 0; index < gen->nstrings; index++)
		if(!strcmp(gen->strings[index].value, value))
			return gen->strings[index].offset;
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
	if(size == 1) {
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
	if(size == 1) {
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
	if(size == 1) {
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
	if(size == 1) {
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
	if(local) {
		i386_emit_load_ebp(gen, local->type, local->offset);
		return;
	}
	global = i386_find_global(gen->program, name);
	if(global) {
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
	if(expression->kind == EX_ID) {
		local = i386_find_local(gen, expression->str);
		if(local) {
			if(is_vla(local->type))
				i386_emit_load_vla_pointer(gen, local);
			else
				i386_emit_lea_ebp(gen, local->offset);
			return local->type;
		}
		global = i386_find_global(gen->program, expression->str);
		if(global) {
			i386_emit_mov_eax_symbol(gen, global->name);
			return global->type;
		}
		global = i386_find_function(gen->program, expression->str);
		if(global) {
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
	if(expression->kind == EX_INDEX) {
		i386_gen_expression(gen, expression->left);
		i386_put8(&gen->text, 0x50);
		i386_gen_expression(gen, expression->right);
		type = i386_expr_type(gen, expression->left);
		type = type->base ? type->base : &T_U32;
		size = i386_type_size(type);
		if(size != 1) {
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
		if(expression->kind == EX_MEMBER) {
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
		if(member_offset) {
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
	Entry: ECX = dividend, EAX = divisor.
	ECDQ overwrites EDX, so the divisor must not live in EDX when CDQ
	executes.  Version 1.5 used IDIV EDX after CDQ and therefore
	divided by zero for every positive dividend.
	*/
	i386_put8(&gen->text, 0x89);
	i386_put8(&gen->text, 0xc2);
	i386_put8(&gen->text, 0x89);
	i386_put8(&gen->text, 0xc8);
	i386_put8(&gen->text, 0x89);
	i386_put8(&gen->text, 0xd1);
	if(is_unsigned) {
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
	//Preserve the original halves and form the low 64 bits of the product.
	i386_put8(&gen->text, 0x50);
	i386_put8(&gen->text, 0x52);
	i386_put8(&gen->text, 0xb9);
	i386_put32(&gen->text, low);
	i386_put8(&gen->text, 0xf7);
	i386_put8(&gen->text, 0xe1);
	i386_put8(&gen->text, 0x50);
	i386_put8(&gen->text, 0x52);
	//ECX = high32(original_low * low).
	i386_put8(&gen->text, 0x8b);
	i386_put8(&gen->text, 0x0c);
	i386_put8(&gen->text, 0x24);
	//Add original_high * low.
	i386_put8(&gen->text, 0x8b);
	i386_put8(&gen->text, 0x44);
	i386_put8(&gen->text, 0x24);
	i386_put8(&gen->text, 0x08);
	i386_put8(&gen->text, 0x69);
	i386_put8(&gen->text, 0xc0);
	i386_put32(&gen->text, low);
	i386_put8(&gen->text, 0x01);
	i386_put8(&gen->text, 0xc1);
	//Add original_low * high.
	i386_put8(&gen->text, 0x8b);
	i386_put8(&gen->text, 0x44);
	i386_put8(&gen->text, 0x24);
	i386_put8(&gen->text, 0x0c);
	i386_put8(&gen->text, 0x69);
	i386_put8(&gen->text, 0xc0);
	i386_put32(&gen->text, high);
	i386_put8(&gen->text, 0x01);
	i386_put8(&gen->text, 0xc1);
	//Restore result low and install result high.
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
	if(left) {
		if(count < 32) {
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
		if(count < 32) {
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
	if(!strcmp(operator, "=")) {
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
		if(!strcmp(operator, "*=")) {
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
			if(!strcmp(operator, "^=")) {
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
	if(!strcmp(operator, "<<") || !strcmp(operator, ">>")) {
		if(expression->right->kind != EX_NUM)
			fatal("64-bit shifts require a constant count");
		i386_gen_expression(gen, expression->left);
		if(!i386_expression_is_u64(gen, expression->left))
			i386_emit_zero_edx(gen);
		i386_emit_shift64_imm(gen, !strcmp(operator, "<<"), (unsigned int)expression->right->num);
		return;
	}
	if(!strcmp(operator, "*") && expression->right->kind == EX_NUM) {
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
		if(!strcmp(operator, "|")) {
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
	if(!strcmp(operator, "=")) {
		type = i386_gen_address(gen, expression->left);
		i386_put8(&gen->text, 0x50);
		i386_gen_expression(gen, expression->right);
		if(type->kind == TY_STRUCT) {
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
		if(!strcmp(operator, "+=")) {
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
			if(!strcmp(operator, "%=")) {
				i386_put8(&gen->text, 0x89);
				i386_put8(&gen->text, 0xd0);
			}
		}
		i386_put8(&gen->text, 0x59);
		i386_emit_store_ecx(gen, type);
		return;
	}
	if(!strcmp(operator, "&&") || !strcmp(operator, "||")) {
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
	if(!strcmp(operator, "+")) {
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
		if(!strcmp(operator, "%")) {
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
	size_t index;
	const char *name;
	uint32_t offset;
	if(expression->left->kind != EX_ID)
		fatal("only direct calls are supported by i386 backend");
	name = expression->left->str;
	if(!strcmp(name, "va_start") || !strcmp(name, "va_end"))
		fatal("variadic builtins are not supported by i386 backend yet");
	for(index = expression->nargs; index > 0; index--) {
		i386_gen_expression(gen, expression->args[index - 1]);
		i386_put8(&gen->text, 0x50);
	}
	i386_put8(&gen->text, 0xe8);
	offset = (uint32_t)gen->text.n;
	i386_put32(&gen->text, 0xfffffffcU);
	i386_add_relocation(gen, I386_SEC_TEXT, offset, R_386_PC32, name, 0);
	if(expression->nargs) {
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
	if(type->kind == TY_PTR && type->base) {
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
	if(postfix) {
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
	switch(expression->kind) {
	case EX_NUM:
		i386_emit_mov_eax_imm(gen, (uint32_t)expression->num);
		if(i386_expression_is_u64(gen, expression)) {
			i386_put8(&gen->text, 0xba);
			i386_put32(&gen->text, (uint32_t)(expression->num >> 32));
		}
		return;
	case EX_STR:
		string_offset = i386_intern_string(gen, expression->str);
		i386_emit_mov_eax_rodata(gen, string_offset);
		return;
	case EX_ID:
		if(!strcmp(expression->str, "NULL")) {
			i386_emit_mov_eax_imm(gen, 0);
			return;
		}
		local = i386_find_local(gen, expression->str);
		if(local) {
			if(is_vla(local->type))
				i386_emit_load_vla_pointer(gen, local);
			else
				i386_emit_load_ebp(gen, local->type, local->offset);
			return;
		}
		declaration = i386_find_global(gen->program, expression->str);
		if(declaration) {
			i386_emit_load_symbol(gen, declaration->name, declaration->type);
			return;
		}
		declaration = i386_find_function(gen->program, expression->str);
		if(declaration) {
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
			if(i386_type_size(expression->type) == 1) {
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
			if(!strcmp(expression->op, "!")) {
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
	size_t index;
	Decl *declaration;
	long size;
	long alignment;
	if(!statement)
		return used;
	if(statement->kind == ST_DECL) {
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
		for(index = 0; index < statement->nchildren; index++)
			used = i386_collect_locals(gen,
						   statement->children[index],
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
	size_t index;
	I386Local *local;
	char *else_label;
	char *done_label;
	char *loop_label;
	char *condition_label;
	char *next_label;
	if(!statement)
		return;
	switch(statement->kind) {
	case ST_BLOCK:
		for(index = 0; index < statement->nchildren; index++)
			i386_gen_statement(gen, statement->children[index]);
		return;
	case ST_DECL:
		local = i386_find_local(gen, statement->decl->name);
		if(is_vla(local->type)) {
			long element_size = i386_type_size(local->type->base);
			i386_emit_vla_bound(gen, local->type->name);
			if(element_size != 1) {
				i386_put8(&gen->text, 0x69);
				i386_put8(&gen->text, 0xc0);
				i386_put32(&gen->text, (uint32_t)element_size);
			}
			//Round the allocation up to 16 bytes.
			i386_put8(&gen->text, 0x83);
			i386_put8(&gen->text, 0xc0);
			i386_put8(&gen->text, 0x0f);
			i386_put8(&gen->text, 0x83);
			i386_put8(&gen->text, 0xe0);
			i386_put8(&gen->text, 0xf0);
			i386_put8(&gen->text, 0x29);
			i386_put8(&gen->text, 0xc4);
			//Save the runtime array base in its fixed frame slot.
			i386_put8(&gen->text, 0x89);
			i386_put8(&gen->text, 0xa5);
			i386_put32(&gen->text,
				   (uint32_t)(int32_t)local->offset);
		}
		if(statement->decl->init) {
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
		if(statement->cond) {
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
		size_t inner_index;
		done_label = i386_new_label(gen, ".Lswitchend");
		default_label = done_label;
		if(body && body->kind == ST_BLOCK) {
			for(inner_index = 0; inner_index < body->nchildren; inner_index++) {
				Stmt *child = body->children[inner_index];
				if(child->kind == ST_CASE) {
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
		for(inner_index = 0; inner_index < body->nchildren; inner_index++) {
			Stmt *child = body->children[inner_index];
			if(child->kind == ST_CASE) {
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
		if(!strcmp(statement->asm_text, "rdtsc")) {
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
				if(!strcmp(constraint, "=a")) {
					//Preserve EDX:EAX while calculating the lvalue.
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
					//Store the high half while restoring low EAX.
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
		while(cursor && *cursor) {
			char *next = strchr(cursor, ';');
			char *end;
			if(next)
				*next++ = 0;
			while(isspace((unsigned char)*cursor))
				cursor++;
			end = cursor + strlen(cursor);
			while(end > cursor && isspace((unsigned char)end[-1]))
				*--end = 0;
			if(!*cursor) {
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
			else if(!strcmp(cursor, "pause")) {
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
	size_t index;
	uint32_t start;
	gen->current = declaration;
	gen->nlocals = 0;
	for(index = 0; index < declaration->nparams; index++) {
		ARR_GROW(gen->locals, gen->nlocals, gen->caplocals, I386Local);
		gen->locals[gen->nlocals].name = declaration->params[index].name;
		gen->locals[gen->nlocals].type = declaration->params[index].type;
		gen->locals[gen->nlocals].offset = 8 + (long)index * 4;
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
	if(gen->frame_size) {
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
	if(type->kind == TY_ARRAY) {
		size_t index_1;
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
		for(index_1 = 0; index_1 < initializer->nargs; index_1++)
			i386_write_global_initializer(gen, type->base, initializer->args[index_1], name);
		i386_put_zeros(&gen->data,
			       (size_t)size - (gen->data.n - start));
		return;
	}
	if(type->kind == TY_STRUCT) {
		size_t index_1;
		if(initializer->kind != EX_INITLIST)
			fatal("struct initializer for %s must use braces", name);
		if(initializer->nargs > type->nmembers)
			fatal("too many initializers for %s", name);
		for(index_1 = 0; index_1 < initializer->nargs; index_1++) {
			long member_offset = 0;
			if(!i386_find_struct_member(type, type->members[index_1].name, &member_offset))
				fatal("internal: bad member offset in %s", name);
			if(gen->data.n < start + (size_t)member_offset)
				i386_put_zeros(&gen->data,
					       start + (size_t)member_offset - gen->data.n);
			i386_write_global_initializer(gen, type->members[index_1].type, initializer->args[index_1], name);
		}
		if(gen->data.n < start + (size_t)size)
			i386_put_zeros(&gen->data, start + (size_t)size - gen->data.n);
		return;
	}
	if(initializer->kind == EX_INITLIST) {
		if(initializer->nargs != 1)
			fatal("scalar initializer for %s has %zu elements", name, initializer->nargs);
		initializer = initializer->args[0];
	}
	if(initializer->kind == EX_STR && type->kind == TY_PTR) {
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
	size_t index;
	for(index = 0; index < gen->program->n; index++) {
		Decl *declaration = gen->program->a[index];
		I386ObjSymbol *symbol;
		if(declaration->body || declaration->prototype) {
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
		if(declaration->init) {
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
	size_t index_1;
	for(index_1 = 0; index_1 < gen->nsymbols; index_1++)
		if(!strcmp(gen->symbols[index_1].name, name))
			return gen->symbols[index_1].index;
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
	size_t index_1;
	gen.program = program;
	i386_prepare_symbols_and_globals(&gen);
	for(index_1 = 0; index_1 < program->n; index_1++)
		if(program->a[index_1]->body)
			i386_gen_function(&gen, program->a[index_1]);
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
	for(index_1 = 0; index_1 < gen.nsymbols; index_1++)
		if(gen.symbols[index_1].local)
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
		for(index_1 = 0; index_1 < gen.nsymbols; index_1++) {
			I386ObjSymbol *object = &gen.symbols[index_1];
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
	for(index_1 = 0; index_1 < gen.nrelocations; index_1++) {
		if(gen.relocations[index_1].target_section == I386_SEC_TEXT)
			text_relocation_count++;
		else if(gen.relocations[index_1].target_section == I386_SEC_DATA)
			data_relocation_count++;
		else
			fatal("internal: unsupported i386 relocation target section");
	}
	text_relocations = xcalloc(text_relocation_count ? text_relocation_count : 1, sizeof(*text_relocations));
	data_relocations = xcalloc(data_relocation_count ? data_relocation_count : 1, sizeof(*data_relocations));
	text_relocation_count = 0;
	data_relocation_count = 0;
	for(index_1 = 0; index_1 < gen.nrelocations; index_1++) {
		I386Relocation *source = &gen.relocations[index_1];
		Elf32_Rel *destination;
		uint32_t symbol_index;
		if(source->section_symbol) {
			switch(source->section_symbol) {
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
	/*
		PREPARE YOUR EYES FOR THIS GIGANTIC CHUNK!
	*/
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

static void usage(void)
{
	printf("%s\nUsage: AneoC [options] Cmds.AC ...\n\n"
	       "  -o FILE          set output file\n"
	       "  -S               emit generated x86-64 assembly text\n"
	       "  -m32 -c          emit an ELF32 i386 relocatable object\n"
	       "  -ffreestanding   accepted for freestanding kernel builds\n"
	       "  -lNAME           add a DT_NEEDED library to x86-64 output\n"
	       "  --version        show version\n\n",
	       VERSION);
}

int main(int argc, char **argv)
{
	Program prog = {0};
	char **src = NULL;
	char **libs = NULL;
	size_t nsrc = 0, csrc = 0, nlibs = 0, clibs = 0, index;
	char *out = xstrdup("a.out");
	bool assembly_only = false;
	bool compile_only = false;
	bool target_i386 = false;
	bool freestanding = false;
	bool needs_x11 = false;
	for(index = 1; index < (size_t)argc; index++) {
		if(!strcmp(argv[index], "--help") || !strcmp(argv[index], "-h")) {
			usage();
			return 0;
		}
		if(!strcmp(argv[index], "--version")) {
			puts(VERSION);
			return 0;
		}
		if(!strcmp(argv[index], "-o")) {
			if(++index >= (size_t)argc)
				fatal("-o needs a file");
			out = argv[index];
		} else if(!strcmp(argv[index], "-S"))
		{
			assembly_only = true;
		} else if(!strcmp(argv[index], "-c"))
		{
			compile_only = true;
		} else if(!strcmp(argv[index], "-m32"))
		{
			target_i386 = true;
		} else if(!strcmp(argv[index], "-ffreestanding"))
		{
			freestanding = true;
		} else if(!strcmp(argv[index], "-fno-builtin") ||
			  !strcmp(argv[index], "-fno-stack-protector") ||
			  !strcmp(argv[index], "-fno-pie") ||
			  !strcmp(argv[index], "-fno-pic") ||
			  !strcmp(argv[index], "-nostdlib") ||
			  !strcmp(argv[index], "-nostdinc") ||
			  !strcmp(argv[index], "-Wall") ||
			  !strcmp(argv[index], "-Wextra") ||
			  !strcmp(argv[index], "-Wpedantic") ||
			  !strncmp(argv[index], "-O", 2) ||
			  !strncmp(argv[index], "-std=", 5) ||
			  !strncmp(argv[index], "-I", 2) ||
			  !strncmp(argv[index], "-D", 2))
		{
			//Accepted compatibility flags; AneoC is always freestanding in -m32 -c mode.
		} else if(!strncmp(argv[index], "-l", 2))
		{
			ARR_GROW(libs, nlibs, clibs, char *);
			libs[nlibs++] = argv[index];
		} else if(argv[index][0] == '-')
		{
			fatal("unsupported option %s", argv[index]);
		} else {
			ARR_GROW(src, nsrc, csrc, char *);
			src[nsrc++] = argv[index];
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
	for(index = 0; index < nsrc; index++) {
		char *raw = read_file(src[index]);
		char *text = preprocess_source(raw);
		Tokens tokens = lex_source(text, src[index]);
		if(strstr(raw, "X11/Xlib.h") ||
		   strstr(raw, "XOpenDisplay") ||
		   strstr(raw, "XCreateSimpleWindow") ||
		   strstr(raw, "XDrawString"))
			needs_x11 = true;
		parse_program(&tokens, &prog);
	}
	(void)freestanding;
	if(compile_only) {
		if(assembly_only)
			fatal("-S and -c cannot be combined yet");
		write_i386_relocatable(out, &prog);
		return 0;
	}
	if(target_i386)
		fatal("-m32 currently requires -c");
	{
		char *assembly = generate(&prog);
		if(assembly_only) {
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
