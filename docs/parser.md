# Parser

The parser turns a flat **token list** (`t_token *`) from the [lexer](lexer.md) into an **AST** (`t_node *`).

It **consumes** lexer kinds (`T_*`) and **builds** node kinds (`N_*`). Execution walks that tree later. Deep AST notes: [ast.md](ast.md). Field reference: [structs.md](structs.md).

**Entry point:** `parse(t_token *tokens)` in `src/parser/parser.c`  
**Caller:** `process_line` — after a successful `lex`, before heredocs / exec.

```c
sh->ast = parse(tokens);
free_tokens(tokens);
if (!sh->ast)
{
	sh->status = 2;
	return ;
}
```

---

## Place in the pipeline

```text
lex → [T_WORD]→[T_PIPE]→[T_WORD]→…
         │
         ▼ parse()
      N_PIPE
     /      \
  N_CMD    N_CMD
```

| Lexer (`T_*`) | Parser (`N_*`) |
|---------------|----------------|
| `T_PIPE` one list node | `N_PIPE` binary tree node (`left`, `right`) |
| sequence of `T_WORD` + redirs | one `N_CMD` |
| `T_AND` / `T_OR` (bonus) | `N_AND` / `N_OR` |
| `( … )` | `N_SUBSHELL` wrapping an inner line |

After `parse` returns, tokens are freed. Strings that belong on the AST were **moved** out of tokens (see ownership below).

---

## Core types

### `t_parser` — cursor + sticky error

```c
typedef struct s_parser
{
	t_token	*cur;   /* next token to look at */
	int		error;  /* once 1, final parse() returns NULL */
}	t_parser;
```

Almost every helper reads/writes `p->cur` and may set `p->error`.

### `t_node` — AST node

```c
typedef struct s_node
{
	t_ntype			type;
	t_list			*words;   /* N_CMD arguments */
	t_redir			*redirs;  /* N_CMD or after subshell */
	struct s_node	*left;
	struct s_node	*right;
}	t_node;
```

| `type` | Uses |
|--------|------|
| `N_CMD` | `words`, `redirs` |
| `N_PIPE` / `N_AND` / `N_OR` | `left`, `right` |
| `N_SUBSHELL` | `left` = body; optional `redirs` after `)` |

### `T_*` vs `N_*` (same symbol, different stage)

```text
Input:  ls | wc

Lexer:  [T_WORD "ls"] → [T_PIPE] → [T_WORD "wc"]
              │              │              │
              │         consumed when        │
              │         building N_PIPE      │
              ▼              ▼               ▼
Parser:              N_PIPE
                    /      \
              N_CMD        N_CMD
               ls           wc
```

`T_PIPE` does not appear as a leaf in the tree.

---

## Call graph

### Mandatory (`make`)

```text
parse
 └─ parse_line                 # mandatory/parser_line.c
     └─ parse_pipeline         # parser.c
         └─ parse_command
             └─ parse_simple   # parser_cmd.c
```

### Bonus (`make bonus`)

```text
parse
 └─ parse_line                 # && / || between pipelines
     └─ parse_pipeline
         └─ parse_command
             ├─ parse_subshell # if T_LPAREN
             └─ parse_simple
```

---

## `parse` — entry and cleanup

```c
t_node	*parse(t_token *tokens)
{
	t_parser	p;
	t_node		*root;

	p.cur = tokens;
	p.error = 0;
	root = parse_line(&p);
	if (!p.error && p.cur)
		syntax_error(&p);      /* leftover tokens, e.g. extra ")" */
	if (p.error)
	{
		free_ast(root);
		return (NULL);
	}
	return (root);
}
```

| Step | Meaning |
|------|---------|
| Init | cursor at first token, no error yet |
| `parse_line` | build as much tree as grammar allows |
| Leftover `p.cur` | something was not consumed → syntax error |
| On error | free partial AST, return `NULL` → status `2` in the shell |

---

## Errors

### `syntax_error`

```c
if (!p->error)
	print_msg("syntax error near unexpected token `",
		token_str(p->cur), "'", NULL);
p->error = 1;
return (1);
```

- Prints **once** (further errors only set the flag).
- `token_str(NULL)` → `"newline"` (classic bash when input ends early: `ls |`).

### `parse_alloc_error`

Same sticky flag; message is malloc / `ENOMEM`.

During a failed parse the code may still return a partial `node` upward; `parse` always `free_ast`s if `error` is set.

---

## `node_new`

```c
node = ft_calloc(1, sizeof(t_node));  /* words/redirs/left/right = NULL */
node->type = type;
```

On allocation failure → `parse_alloc_error`, return `NULL`.

---

## `parse_pipeline` — pipes (right-associative)

```c
left = parse_command(p);
if (p->error || !p->cur || p->cur->type != T_PIPE)
	return (left);
node = node_new(p, N_PIPE);
node->left = left;
p->cur = p->cur->next;           /* eat T_PIPE */
node->right = parse_pipeline(p); /* recurse */
return (node);
```

### Why recursion on the right?

`a | b | c` becomes:

```text
      N_PIPE
     /      \
    a      N_PIPE
          /      \
         b        c
```

Not a flat list of three commands. The executor runs nested pipes accordingly.

### Step-by-step: `ls | wc | cat`

Tokens: `ls` → `|` → `wc` → `|` → `cat`

**Call 1**

| Step | State |
|------|-------|
| `parse_command` | `left` = `N_CMD ls`, `cur` = `T_PIPE` |
| create `N_PIPE` | `node->left = ls` |
| eat `|` | `cur` = `wc` |
| `right = parse_pipeline` | → Call 2 |

**Call 2**

| Step | State |
|------|-------|
| `left` | `N_CMD wc`, `cur` = `T_PIPE` |
| new `N_PIPE` | left = wc |
| eat `|` | `cur` = `cat` |
| `right` | → Call 3 |

**Call 3**

| Step | State |
|------|-------|
| `left` | `N_CMD cat`, `cur` = `NULL` |
| not a pipe | `return (left)` only |

Returns bubble up → full tree as above.

### No pipe: `ls -l`

`parse_command` → `N_CMD`, next token not `T_PIPE` → return that command node unchanged.

---

## `parse_simple` — one command

```c
node = node_new(p, N_CMD);
while (!p->error && p->cur
	&& (p->cur->type == T_WORD || is_redir(p->cur->type)))
{
	if (p->cur->type == T_WORD)
		add_word(p, node);
	else
		add_redir(p, &node->redirs);
}
if (!p->error && !node->words && !node->redirs)
	syntax_error(p);
return (node);
```

Accepts any mix of words and redirections in one command:

```sh
> out echo hi < in
```

All attach to the **same** `N_CMD`.

Empty (e.g. lone `|` already handled higher, or starting with `&&`) with no words/redirs → syntax error.

### `add_word`

1. `ft_lstnew(p->cur->value)` — list node owns the string pointer.
2. `p->cur->value = NULL` — token no longer owns it.
3. `ft_lstadd_back(&node->words, item)`.
4. Advance `p->cur`.

### `add_redir`

Needs **operator + word**:

```text
cur = T_OUT, cur->next = T_WORD "file"
```

1. If no next word → move `cur` and `syntax_error`.
2. Allocate `t_redir`: `type` from operator, `target` from word string, `fd = -1`.
3. Null out the word token’s `value` (ownership move).
4. Advance past **both** tokens: `p->cur = p->cur->next->next`.
5. Append to the redir list.

`is_redir`: `T_IN`, `T_OUT`, `T_APPEND`, `T_HEREDOC`.

Heredoc **body** is not read here — only the `<<` + delimiter word are stored. Bodies are filled in `read_heredocs` after a successful parse.

---

## Ownership (important)

| String | After parse |
|--------|-------------|
| Operator tokens | no `value` |
| Word used as argv | lives in `N_CMD->words` |
| Word used as redir target | lives in `t_redir->target` |
| Token list | `free_tokens` — `value` fields already `NULL` for moved strings |

If you `strdup` instead of moving, you either leak or double-free. This code moves.

---

## Mandatory `parse_line` / `parse_command`

```c
t_node	*parse_command(t_parser *p)
{
	return (parse_simple(p));
}

t_node	*parse_line(t_parser *p)
{
	return (parse_pipeline(p));
}
```

No `&&`, `||`, or `( )` in mandatory.

---

## Bonus: `parse_line` — `&&` / `||`

```c
left = parse_pipeline(p);
while (cur is T_AND or T_OR)
{
	type = N_AND or N_OR;
	node = node_new(p, type);
	node->left = left;
	eat operator;
	node->right = parse_pipeline(p);
	left = node;   /* chain: next && binds to the left structure */
}
return (left);
```

Example: `true && false || echo x`

Pipelines are the operands; `&&` / `||` sit **above** pipes in the tree (lower precedence than `|` in the sense that each side of `&&` is a full pipeline).

```text
true && ls | wc

      N_AND
     /      \
  N_CMD     N_PIPE
  true     /      \
         ls       wc
```

---

## Bonus: `parse_subshell`

When `parse_command` sees `T_LPAREN`:

1. Create `N_SUBSHELL`.
2. Eat `(`.
3. `node->left = parse_line(p)` — full bonus line inside.
4. Expect `T_RPAREN`; else syntax error.
5. Eat `)`.
6. Optional redirections after `)` → `node->redirs` (e.g. `(ls) > out`).

Executor will fork and run `left` in the child.

---

## Worked example (mandatory)

**Input:** `grep a < file | wc -l > out`

**Tokens after lex:**

```text
WORD "grep" → WORD "a" → IN → WORD "file" → PIPE
  → WORD "wc" → WORD "-l" → OUT → WORD "out"
```

**Parse:**

1. `parse_pipeline` → `parse_command` / `parse_simple`:
   - words: `grep`, `a`
   - redirs: `< file`
2. See `T_PIPE` → `N_PIPE`, left = that command, eat `|`.
3. Right side `parse_simple`:
   - words: `wc`, `-l`
   - redirs: `> out`

```text
              N_PIPE
             /      \
     N_CMD                N_CMD
  words: grep, a       words: wc, -l
  redirs: < file       redirs: > out
```

---

## Failure examples

| Input | What happens |
|-------|----------------|
| `ls \|` | pipeline expects a command after `\|`; often ends with `syntax error near unexpected token \`newline'` |
| `\| ls` | `parse_simple` gets empty command first → syntax error near `\|` |
| `ls >` | `add_redir` missing word → syntax error |
| `echo 'hi` | fails earlier in **lex** (unclosed quote), never reaches parse |
| leftover `)` | `parse` sees remaining `p.cur` → syntax error |

---

## Files

| File | Role |
|------|------|
| `src/parser/parser.c` | `parse`, `parse_pipeline`, `node_new`, `syntax_error`, `parse_alloc_error` |
| `src/parser/parser_cmd.c` | `parse_simple`, `add_word`, `add_redir`, `is_redir` |
| `src/mandatory/parser_line.c` | thin `parse_line` / `parse_command` |
| `src/bonus/parser_line_bonus.c` | `&&` `\|\|`, subshells |
| `src/lexer/lexer_utils.c` | `token_str` for error text; `free_tokens` after parse |

Previous stage: [lexer.md](lexer.md). Next: [heredoc.md](heredoc.md), then
[executor.md](executor.md) / [expand.md](expand.md) (tree overview: [ast.md](ast.md)).
