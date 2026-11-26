# `e_ntype` — AST node kinds (`t_ntype`)

`e_ntype` is the enum of **abstract syntax tree node kinds**. Every `t_node` carries one of these values in `type`. The executor dispatches on this field.

Declared in `include/minishell.h` as typedef `t_ntype`.

Related: [ast.md](ast.md) (tree overview), [parser.md](parser.md) (how nodes are built), [e_tok.md](e_tok.md) (lexer kinds that feed the parser).

```c
typedef enum e_ntype
{
	N_CMD,
	N_PIPE,
	N_AND,
	N_OR,
	N_SUBSHELL
}	t_ntype;
```

---

## What a node kind is (and is not)

| Concept | Role |
|---------|------|
| `t_tok` / `T_*` | Flat **token** in a linked list |
| `t_ntype` / `N_*` | **Tree node** with children / payload |

Same English name does not mean the same object:

| Token | Node | Difference |
|-------|------|------------|
| `T_PIPE` | `N_PIPE` | list lexeme vs binary tree (`left`, `right`) |
| `T_AND` | `N_AND` | same idea |
| `T_OR` | `N_OR` | same idea |
| `T_WORD` + redirs | `N_CMD` | many tokens → one command node |
| `T_LPAREN`…`T_RPAREN` | `N_SUBSHELL` | framing tokens → wrapper with body on `left` |

There is **no** `N_IN` / `N_OUT`. Redirections stay as `t_redir` lists with `t_tok` types attached to `N_CMD` (or `N_SUBSHELL`).

---

## The carrier: `t_node`

```c
typedef struct s_node
{
	t_ntype			type;
	t_list			*words;
	t_redir			*redirs;
	struct s_node	*left;
	struct s_node	*right;
}	t_node;
```

Which fields are meaningful depends on `type`:

| `type` | `words` | `redirs` | `left` | `right` |
|--------|---------|----------|--------|---------|
| `N_CMD` | argv raw strings | `< > >> <<` list | unused (`NULL`) | unused |
| `N_PIPE` | unused | unused | left of `\|` | right of `\|` |
| `N_AND` | unused | unused | left of `&&` | right of `&&` |
| `N_OR` | unused | unused | left of `\|\|` | right of `\|\|` |
| `N_SUBSHELL` | unused | optional after `)` | body AST | unused |

Created with `node_new(p, type)` → `ft_calloc` so unused pointers start `NULL`.

---

## Quick reference

| Kind | Meaning | Built by | Run by | Build |
|------|---------|----------|--------|-------|
| `N_CMD` | simple command | `parse_simple` | `exec_command` | both |
| `N_PIPE` | `left \| right` | `parse_pipeline` | `exec_pipeline` | both |
| `N_AND` | `left && right` | bonus `parse_line` | bonus `exec_logic` | bonus |
| `N_OR` | `left \|\| right` | bonus `parse_line` | bonus `exec_logic` | bonus |
| `N_SUBSHELL` | `( body )` | bonus `parse_subshell` | bonus `exec_subshell` | bonus |

---

## Grammar layers (how kinds nest)

```text
parse_line          →  N_AND / N_OR chain of pipelines   (bonus; else = pipeline)
  └─ parse_pipeline →  N_PIPE chain of commands
       └─ parse_command
            ├─ N_SUBSHELL (bonus, if T_LPAREN)
            └─ N_CMD      (parse_simple)
```

So for a full bonus line, nesting from outer to inner is typically:

```text
N_AND / N_OR
  └── N_PIPE
        └── N_CMD or N_SUBSHELL
              └── (subshell body can again be N_AND / N_PIPE / …)
```

Precedence consequence: `|` is “inside” `&&` / `||` operands; parentheses force a new child process around an arbitrary sub-line.

---

## `N_CMD` — simple command

### Meaning

One unit of work: a list of words (command + args) and zero or more redirections. Corresponds to what bash would run as a single simple command (builtin or external), possibly with redirections only (`> file` with no words is still valid as long as there is at least one redir).

### Built by

`parse_simple` (`src/parser/parser_cmd.c`):

- allocate `N_CMD`
- while current token is `T_WORD` or a redir kind → `add_word` / `add_redir`
- if neither words nor redirs → syntax error

### Payload

| Field | Content |
|-------|---------|
| `words` | `t_list` of **raw** strings (quotes still present) |
| `redirs` | linked `t_redir` (`type` is `T_IN` / `T_OUT` / `T_APPEND` / `T_HEREDOC`) |
| `left` / `right` | unused |

Expansion happens at **exec** time (`expand_args` / `expand_word`), not at parse time.

### Execution (`exec_command`)

Typical path:

1. Expand `words` → `sh->argv`
2. Apply `redirs` (`apply_redirs`)
3. Builtin in parent if not in a pipeline child path; else fork / `execve` / builtin in child

### Examples → single `N_CMD`

```sh
ls -la
echo hello > file
cat << EOF
> out
```

### Diagram

```text
┌─────────────────────┐
│       N_CMD         │
│ words: "grep","a"   │
│ redirs: T_IN→"file" │
│ left/right: ∅       │
└─────────────────────┘
```

---

## `N_PIPE` — pipeline

### Meaning

Binary node: connect stdout of `left` to stdin of `right` and run both sides (pipeline semantics).

### Built by

`parse_pipeline` (`src/parser/parser.c`):

```text
left = parse_command(…)
if next token is T_PIPE:
    node = N_PIPE
    node->left = left
    eat T_PIPE
    node->right = parse_pipeline(…)   # recurse
    return node
else:
    return left
```

### Associativity

**Right-leaning** chain. `a | b | c` is:

```text
        N_PIPE
       /      \
   N_CMD      N_PIPE
     a       /      \
          N_CMD    N_CMD
            b        c
```

Not a flat array of three commands. The executor walks that nesting (`exec_pipeline`).

### Payload

Only `left` and `right`. Words/redirs on the pipe node itself are unused; redirections belong to the `N_CMD` children (e.g. `wc -l > out` keeps `> out` on the last command).

### Execution

Mandatory and bonus: `if (node->type == N_PIPE) return exec_pipeline(sh, node);`

### Example

```sh
ls -la | grep src | wc -l > out
```

```text
                 N_PIPE
                /      \
          N_CMD          N_PIPE
        ls -la          /      \
                   N_CMD        N_CMD
                 grep src     wc -l > out
```

---

## `N_AND` — logical AND (bonus)

### Meaning

Run `right` **only if** `left` exited with status `0` (bash `&&`).

### Built by

Bonus `parse_line` (`src/bonus/parser_line_bonus.c`):

- start with a pipeline as `left`
- while next token is `T_AND` or `T_OR`, wrap: new node, `left` = previous structure, eat operator, `right` = next pipeline
- chaining is left-associative at this level

### Payload

`left` / `right` only (each is typically `N_PIPE`, `N_CMD`, or `N_SUBSHELL`).

### Execution

`exec_logic` in `src/bonus/exec_node_bonus.c`:

```text
status = exec_node(left)
if SIGINT → return status
if type == N_AND && status == 0 → status = exec_node(right)
return status
```

### Example

```sh
true && echo ok
```

```text
      N_AND
     /     \
  N_CMD   N_CMD
  true    echo ok
```

With a pipe on the right:

```sh
true && ls | wc
```

```text
      N_AND
     /     \
  N_CMD   N_PIPE
  true    /    \
        ls      wc
```

---

## `N_OR` — logical OR (bonus)

### Meaning

Run `right` **only if** `left` exited **non-zero** (bash `||`).

### Built by

Same loop as `N_AND`; token `T_OR` selects `type = N_OR`.

### Execution

Same `exec_logic`:

```text
if type == N_OR && status != 0 → exec_node(right)
```

Short-circuit: if left already succeeded, right is skipped.

### Example

```sh
false || echo fallback
```

```text
       N_OR
      /    \
  N_CMD    N_CMD
  false    echo fallback
```

### Mixed chain

```sh
true && false || echo x
```

Parsed as nested left structure (first `&&`, then `||` wraps that):

```text
          N_OR
         /    \
     N_AND    N_CMD
    /     \    echo x
 N_CMD   N_CMD
 true    false
```

---

## `N_SUBSHELL` — parentheses (bonus)

### Meaning

Run an inner AST in a **forked child**. Parent waits. Side effects of builtins inside the subshell (e.g. `cd`) do not affect the parent shell environment.

### Built by

`parse_subshell` when `parse_command` sees `T_LPAREN`:

1. `node_new(…, N_SUBSHELL)`
2. Eat `(`
3. `node->left = parse_line(p)` — full bonus line inside
4. Require `T_RPAREN`, eat `)`
5. Optional trailing redirections → `node->redirs` (e.g. `(ls) > out`)

### Payload

| Field | Role |
|-------|------|
| `left` | body to execute in the child |
| `redirs` | applied in the child **before** running `left` |
| `words` / `right` | unused |

### Execution

`exec_subshell`:

1. `fork`
2. Child: `sh->in_child = 1`, apply `node->redirs`, `exec_node(sh, node->left)`, `child_exit`
3. Parent: `wait_child`

### Example

```sh
(false || echo ok) && ls
```

```text
              N_AND
             /     \
     N_SUBSHELL    N_CMD
          │         ls
          ▼
        N_OR
       /    \
   N_CMD    N_CMD
   false    echo ok
```

---

## Executor dispatch

### Mandatory (`src/mandatory/exec_node.c`)

```c
if (node->type == N_PIPE)
	return (exec_pipeline(sh, node));
return (exec_command(sh, node));
```

Only `N_PIPE` vs everything else (expected: `N_CMD`). Bonus node kinds are never created by the mandatory parser.

### Bonus (`src/bonus/exec_node_bonus.c`)

```c
if (node->type == N_PIPE)
	return (exec_pipeline(sh, node));
if (node->type == N_SUBSHELL)
	return (exec_subshell(sh, node));
if (node->type == N_AND || node->type == N_OR)
	return (exec_logic(sh, node));
return (exec_command(sh, node));   /* N_CMD */
```

---

## Worked example (all five kinds)

```sh
(false || echo ok) && ls | wc
```

### Tokens (`T_*`)

```text
LPAREN → WORD false → OR → WORD echo → WORD ok → RPAREN
  → AND → WORD ls → PIPE → WORD wc
```

### Tree (`N_*`)

```text
                         N_AND
                        /     \
               N_SUBSHELL       N_PIPE
                    │          /      \
                    ▼       N_CMD    N_CMD
                  N_OR       ls       wc
                 /    \
             N_CMD    N_CMD
             false    echo ok
```

| Node | Why it exists |
|------|----------------|
| `N_AND` | `&&` between subshell and pipeline |
| `N_SUBSHELL` | `( … )` |
| `N_OR` | `\|\|` inside the subshell |
| `N_PIPE` | `ls \| wc` |
| `N_CMD` ×4 | `false`, `echo ok`, `ls`, `wc` |

---

## Mandatory vs bonus

| Kind | `make` | `make bonus` |
|------|--------|--------------|
| `N_CMD` | yes | yes |
| `N_PIPE` | yes | yes |
| `N_AND` | never built | yes |
| `N_OR` | never built | yes |
| `N_SUBSHELL` | never built | yes |

Mandatory `parse_line` / `parse_command` are thin wrappers around `parse_pipeline` / `parse_simple` only.

---

## Lifecycle of an AST

1. `parse` builds the tree (`sh->ast`)
2. Token list freed (`free_tokens`) — strings already moved into nodes/redirs
3. `read_heredocs` walks the tree for `T_HEREDOC` redirs (shape unchanged)
4. `exec_node(sh, sh->ast)` walks kinds as above
5. `free_ast` frees the whole tree
