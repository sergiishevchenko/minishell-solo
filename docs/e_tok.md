# `e_tok` — Token kinds (`t_tok`)

`e_tok` is the enum of **lexer token kinds**. Every node in the token list (`t_token`) carries one of these values in `type`.

Declared in `include/minishell.h` as typedef `t_tok`.

Related: [lexer.md](lexer.md) (how tokens are produced), [parser.md](parser.md) (how `T_*` become `N_*`), [e_ntype.md](e_ntype.md) (AST node kinds).

```c
typedef enum e_tok
{
	T_WORD,
	T_PIPE,
	T_IN,
	T_OUT,
	T_APPEND,
	T_HEREDOC,
	T_AND,
	T_OR,
	T_LPAREN,
	T_RPAREN
}	t_tok;
```

---

## What a token kind is (and is not)

| Concept | Role |
|---------|------|
| `t_tok` / `T_*` | **Lexeme class** in a flat linked list |
| `t_ntype` / `N_*` | **AST node class** in a tree |

The lexer never creates `N_*`. The parser never leaves `T_PIPE` / `T_AND` / … as tree nodes — it **consumes** those tokens while building `N_PIPE` / `N_AND` / ….

Redirection kinds are special: after parse they live again as `t_redir->type` (`T_IN`, `T_OUT`, `T_APPEND`, `T_HEREDOC`), not as `N_*`.

```text
line  ──lex──▶  [T_WORD]→[T_PIPE]→[T_WORD]  ──parse──▶  N_PIPE
                                                         /    \
                                                      N_CMD  N_CMD
```

---

## Quick reference

| Kind | Lexeme | `value` | Stage | Mandatory / bonus |
|------|--------|---------|-------|-------------------|
| `T_WORD` | non-operator chunk | heap string | argv / redir target / heredoc delim | both |
| `T_PIPE` | `\|` | `NULL` | becomes `N_PIPE` | both |
| `T_IN` | `<` | `NULL` | → `t_redir` | both |
| `T_OUT` | `>` | `NULL` | → `t_redir` | both |
| `T_APPEND` | `>>` | `NULL` | → `t_redir` | both |
| `T_HEREDOC` | `<<` | `NULL` | → `t_redir` + heredoc stage | both |
| `T_AND` | `&&` | `NULL` | becomes `N_AND` | bonus |
| `T_OR` | `\|\|` | `NULL` | becomes `N_OR` | bonus |
| `T_LPAREN` | `(` | `NULL` | starts `N_SUBSHELL` | bonus |
| `T_RPAREN` | `)` | `NULL` | closes `N_SUBSHELL` | bonus |

`token_str()` maps each kind to the string used in syntax-error messages (`src/lexer/lexer_utils.c`). `NULL` token → `"newline"`.

---

## Where kinds are recognized

`op_len(char *s, t_tok *type)` returns the operator length and writes `*type`. Returns `0` if the character is not an operator start → caller takes a `T_WORD`.

**Longer forms first** so `<<` is not two `<`, and `||` is not two `|`.

### Mandatory (`src/mandatory/lexer_ops.c`)

| Order | Match | Type | Length |
|-------|-------|------|--------|
| 1 | `<<` | `T_HEREDOC` | 2 |
| 2 | `>>` | `T_APPEND` | 2 |
| 3 | `<` | `T_IN` | 1 |
| 4 | `>` | `T_OUT` | 1 |
| 5 | `\|` | `T_PIPE` | 1 |

### Bonus (`src/bonus/lexer_ops_bonus.c`)

Logic / grouping checked first, then the same redirections / pipe:

| Order | Match | Type | Length |
|-------|-------|------|--------|
| 1 | `&&` | `T_AND` | 2 |
| 2 | `\|\|` | `T_OR` | 2 |
| 3 | `(` | `T_LPAREN` | 1 |
| 4 | `)` | `T_RPAREN` | 1 |
| 5–9 | same as mandatory | … | … |

A single `&` is **not** an operator here (no background jobs). It becomes part of a word or fails later as unexpected syntax depending on context.

---

## `T_WORD`

### Meaning

Any contiguous non-space, non-operator stretch of the line. That includes:

- command names (`ls`, `/bin/echo`)
- arguments (`-la`, `hello`)
- redirection / heredoc targets (`file`, `EOF`) — still lexed as `T_WORD`, then attached by the parser
- quoted strings (`'a|b'`, `"$HOME"`) as **one** word, quotes kept in the string

### `value`

Always a heap copy (`ft_substr`). Operators never have a `value`.

Quotes are **not** stripped by the lexer. Expansion (`expand_word`) removes them and expands `$` later.

### How it is cut (`word_len`)

Stop before `\0`, whitespace, or an operator start (`op_len != 0`). Inside `'…'` / `"…"`:

- spaces and `|`, `>`, etc. stay inside the word
- the other quote type is literal
- missing closer → lex error (`unexpected EOF while looking for matching …`)

### Parser / ownership

| Use | What happens |
|-----|----------------|
| argv | `add_word` → string moves into `N_CMD->words` |
| redir / heredoc target | `add_redir` → string moves into `t_redir->target` |
| leftover | should not remain after a successful parse |

After the move, `token->value = NULL` so `free_tokens` does not free the string twice.

### Examples

| Input fragment | Tokens |
|----------------|--------|
| `ls -la` | `T_WORD "ls"` → `T_WORD "-la"` |
| `echo 'a\|b'` | `T_WORD "echo"` → `T_WORD "'a\|b'"` |
| `ls\|wc` | `T_WORD "ls"` → `T_PIPE` → `T_WORD "wc"` (no spaces needed) |

---

## `T_PIPE`

### Meaning

Lexeme `|` — separate two pipeline sides.

### Lifecycle

1. Lexer: one list node, `value = NULL`.
2. Parser (`parse_pipeline`): if current token is `T_PIPE`, allocate `N_PIPE`, set `left`, eat the token, recurse for `right`.
3. Executor: `exec_pipeline` on the `N_PIPE` node.

`T_PIPE` does **not** appear in the AST. A lone `|` or trailing `|` is a syntax error (unexpected `|` or `newline`).

### Not the same as `T_OR`

| Token | Lexeme | Length | AST |
|-------|--------|--------|-----|
| `T_PIPE` | `\|` | 1 | `N_PIPE` |
| `T_OR` | `\|\|` | 2 | `N_OR` |

Bonus `op_len` checks `||` before falling through to `|`.

---

## Redirection family: `T_IN`, `T_OUT`, `T_APPEND`, `T_HEREDOC`

These four share the same parse path (`is_redir`) and become `t_redir` nodes on an `N_CMD` (or on `N_SUBSHELL` after `)` in bonus).

```c
int	is_redir(t_tok type)
{
	return (type == T_IN || type == T_OUT
		|| type == T_APPEND || type == T_HEREDOC);
}
```

### Common parse rule

Operator **must** be followed by a `T_WORD` (filename or heredoc delimiter). Otherwise: syntax error.

```text
cur = T_OUT, next = T_WORD "out.txt"
        │                    │
        ▼                    ▼
   redir->type          redir->target
   redir->fd = -1
```

Both tokens are consumed; the word string moves into `target`.

### `T_IN` — `<`

| | |
|--|--|
| Lexeme | `<` |
| Meaning | redirect stdin from a file |
| At exec | `open(path, O_RDONLY)` then `dup2` onto `STDIN_FILENO` |
| Expand | target expanded; 0 or 2+ fields → `ambiguous redirect` |

Example: `cat < file`

### `T_OUT` — `>`

| | |
|--|--|
| Lexeme | `>` |
| Meaning | redirect stdout to a file (truncate / create) |
| At exec | `open(path, O_WRONLY \| O_CREAT \| O_TRUNC, 0644)` → `STDOUT_FILENO` |

Example: `echo hi > out`

### `T_APPEND` — `>>`

| | |
|--|--|
| Lexeme | `>>` |
| Meaning | redirect stdout appending to a file |
| At exec | `open(path, O_WRONLY \| O_CREAT \| O_APPEND, 0644)` → `STDOUT_FILENO` |

Example: `echo hi >> out`

Must be recognized **before** single `>` so `>>` is not `T_OUT` + `T_OUT`.

### `T_HEREDOC` — `<<`

| | |
|--|--|
| Lexeme | `<<` |
| Meaning | read stdin from a temporary heredoc until a delimiter line |
| Parse | same as other redirs: `<<` + word → `t_redir` with `target` = delimiter |
| After parse | `read_heredocs` fills content into a temp file / fd; `redir->fd` set |
| At exec | `dup2(redir->fd, STDIN_FILENO)` — **no** `open` of `target` as a path |

Delimiter quoting controls expansion of heredoc body (see [heredoc.md](heredoc.md)).

Must be recognized **before** single `<`.

### Redirs on the same command

Order is preserved in the `t_redir` list. Applied left to right in `apply_redirs`.

```sh
> out echo hi < in
```

One `N_CMD`: words `echo`, `hi`; redirs `T_OUT→out`, `T_IN→in`.

---

## Bonus logical operators: `T_AND`, `T_OR`

Only produced when bonus `op_len` is linked (`make bonus`).

### `T_AND` — `&&`

| | |
|--|--|
| Lexeme | `&&` |
| Parser | `parse_line`: operand is a full **pipeline**; builds `N_AND` |
| Exec | run `right` only if `left` exited `0` |

### `T_OR` — `||`

| | |
|--|--|
| Lexeme | `\|\|` |
| Parser | same loop as `&&`, builds `N_OR` |
| Exec | run `right` only if `left` exited non-zero |

### Precedence vs pipe

Each side of `&&` / `||` is a pipeline. So `|` binds “tighter” in the sense that pipes sit **inside** the operands:

```sh
true && ls | wc
```

```text
Tokens: WORD true → AND → WORD ls → PIPE → WORD wc

AST:        N_AND
           /     \
       N_CMD     N_PIPE
        true     /    \
               ls      wc
```

Chaining is left-associative at the line level: `a && b || c` builds nested `N_AND` / `N_OR` with the previous structure on `left`.

---

## Bonus grouping: `T_LPAREN`, `T_RPAREN`

### `T_LPAREN` — `(`

Starts a subshell parse path in `parse_command`:

1. Create `N_SUBSHELL`
2. Eat `(`
3. `node->left = parse_line(p)` (full bonus line inside)
4. Expect `T_RPAREN`

### `T_RPAREN` — `)`

Closes the subshell. Optional redirections **after** `)` attach to the `N_SUBSHELL` node (`(ls) > out`).

Mismatched / leftover `)` after `parse_line` returns → syntax error in `parse()` (cursor not exhausted).

### Example

```sh
(false || echo ok) && ls
```

```text
[LPAREN] → [WORD false] → [OR] → [WORD echo] → [WORD ok] → [RPAREN]
  → [AND] → [WORD ls]
```

Tree shape: see [e_ntype.md](e_ntype.md) / [ast.md](ast.md).

---

## Where each kind is used after lex

| Kind | Parser | Later |
|------|--------|-------|
| `T_WORD` | `add_word` / redir target | expand → argv / path / delim |
| `T_PIPE` | eaten in `parse_pipeline` | — |
| `T_IN` / `T_OUT` / `T_APPEND` | `add_redir` → `t_redir->type` | `apply_redirs` / `open_redir` |
| `T_HEREDOC` | `add_redir` | `read_heredocs` then `dup2` |
| `T_AND` / `T_OR` | eaten in bonus `parse_line` | — |
| `T_LPAREN` / `T_RPAREN` | subshell framing | — |

---

## Error display (`token_str`)

| Token | Printed as |
|-------|------------|
| `NULL` | `newline` |
| `T_WORD` | `token->value` |
| `T_PIPE` | `\|` |
| `T_IN` | `<` |
| `T_OUT` | `>` |
| `T_APPEND` | `>>` |
| `T_HEREDOC` | `<<` |
| `T_AND` | `&&` |
| `T_OR` | `\|\|` |
| `T_LPAREN` | `(` |
| `T_RPAREN` | `)` |

Used by `syntax_error` → `syntax error near unexpected token \`…'`.
