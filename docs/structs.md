# minishell data types

Reference for every type declared in `include/minishell.h`.

---

## Global

### `g_signal`

```c
extern volatile sig_atomic_t g_signal;
```

| Qualifier / type | Role |
|------------------|------|
| `volatile` | The value can change asynchronously in a signal handler; the compiler must not cache it in a register. |
| `sig_atomic_t` | Integer type that is safe to read/write inside a signal handler. |
| `extern` | Declared in the header; defined once in `src/main.c`. |

**Purpose:** the only global variable in the project. It stores the last caught signal number (typically `SIGINT`), never a pointer or struct.

**Lifecycle:**
1. Cleared to `0` at the start of each prompt iteration in `shell_loop`.
2. Set in the prompt `SIGINT` handler (`src/signals.c`).
3. Set when a child dies from `SIGINT` (`src/exec_wait.c`).
4. Read after `readline` / during bonus short-circuit to set status `130` or abort.

Subject constraint: one global, signal number only.

---

## Enums

### `t_tok` — token kinds

Produced by the lexer.

| Value | Meaning |
|-------|---------|
| `T_WORD` | Word / argument (may still contain quotes to expand later) |
| `T_PIPE` | `\|` |
| `T_IN` | `<` |
| `T_OUT` | `>` |
| `T_APPEND` | `>>` |
| `T_HEREDOC` | `<<` |
| `T_AND` | `&&` (bonus) |
| `T_OR` | `\|\|` (bonus) |
| `T_LPAREN` | `(` (bonus) |
| `T_RPAREN` | `)` (bonus) |

### `t_ntype` — AST node kinds

| Value | Meaning |
|-------|---------|
| `N_CMD` | Simple command: words + redirections |
| `N_PIPE` | Binary pipe: `left \| right` |
| `N_AND` | `left && right` (bonus) |
| `N_OR` | `left \|\| right` (bonus) |
| `N_SUBSHELL` | `( … )` — child process with its own AST in `left` (bonus) |

---

## Structures

### `t_token`

Linked list of lexer tokens.

| Field | Type | Meaning |
|-------|------|---------|
| `type` | `t_tok` | Kind of token |
| `value` | `char *` | Lexeme for `T_WORD`; `NULL` for operators |
| `next` | `t_token *` | Next token |

Freed with `free_tokens` after parsing.

### `t_redir`

One redirection attached to a command.

| Field | Type | Meaning |
|-------|------|---------|
| `type` | `t_tok` | `T_IN`, `T_OUT`, `T_APPEND`, or `T_HEREDOC` |
| `target` | `char *` | Filename, or heredoc delimiter (quotes may still be present) |
| `fd` | `int` | For heredoc: open read fd of the temp file; otherwise unused until apply |
| `next` | `t_redir *` | Next redirection (order matters) |

### `t_node`

AST node. Shape depends on `type`.

| Field | Type | Meaning |
|-------|------|---------|
| `type` | `t_ntype` | Node kind |
| `words` | `t_list *` | Word list for `N_CMD` (raw tokens before expand) |
| `redirs` | `t_redir *` | Redirection list for `N_CMD` / subshell body |
| `left` | `t_node *` | Left child (pipe / and / or) or subshell body |
| `right` | `t_node *` | Right child |

Example: `ls \| wc` → `N_PIPE` with two `N_CMD` children.

### `t_env`

Environment as a linked list (not a flat `environ` array until `execve`).

| Field | Type | Meaning |
|-------|------|---------|
| `key` | `char *` | Variable name |
| `value` | `char *` | Value, or `NULL` if exported without assignment |
| `next` | `t_env *` | Next variable |

Used by expand (`$VAR`), builtins (`export` / `unset` / `env` / `cd`), and `env_to_array` for external commands.

### `t_shell`

Process-wide shell state passed through the pipeline.

| Field | Type | Meaning |
|-------|------|---------|
| `env` | `t_env *` | Environment list |
| `ast` | `t_node *` | Current line’s AST (cleared after exec) |
| `line` | `char *` | Current input line from readline / stdin |
| `argv` | `char **` | Expanded argv for the command being run |
| `status` | `int` | Last exit status (`$?`) |
| `interactive` | `int` | Non-zero if stdin is a TTY (prompt + signals) |
| `in_child` | `int` | Non-zero inside a forked child (affects `exit` printing, cleanup) |
| `hd_count` | `int` | Counter for unique heredoc temp file names |

### `t_parser`

Parser cursor while building the AST.

| Field | Type | Meaning |
|-------|------|---------|
| `cur` | `t_token *` | Token currently being examined |
| `error` | `int` | Non-zero after a syntax / allocation error |

### `t_exp`

State for word expansion (quotes, `$`, field splitting, glob marks).

| Field | Type | Meaning |
|-------|------|---------|
| `sh` | `t_shell *` | Shell (for env and `$?`) |
| `fields` | `t_list *` | Finished argument fields |
| `buf` | `char *` | Characters of the field currently being built |
| `active` | `int` | Whether a field is open |
| `error` | `int` | Allocation / expand failure |

### `t_heredoc`

Transient state while collecting one heredoc body.

| Field | Type | Meaning |
|-------|------|---------|
| `delim` | `char *` | Delimiter after quote removal |
| `path` | `char *` | Temp path (`HD_PREFIX` + id) |
| `fd` | `int` | Write fd in the heredoc child / read fd after open |
| `quoted` | `int` | Non-zero if the delimiter was quoted → no `$` expand in the body |

---

## Related macros

| Macro | Value | Role |
|-------|-------|------|
| `HD_PREFIX` | `"/tmp/.minishell_heredoc_"` | Prefix for heredoc temp files |
| `GLOB_MARK` | `31` | Internal stand-in for an unquoted `*`; mandatory restores `*`, bonus expands globs |

---

## How they connect

```
t_shell
  ├── env ──────────────▶ t_env → t_env → …
  ├── line ─────────────▶ "ls | wc"
  └── ast ──────────────▶ t_node (N_PIPE)
                            ├── left  → t_node (N_CMD)
                            │            ├── words → t_list ("ls")
                            │            └── redirs → t_redir → …
                            └── right → t_node (N_CMD)
                                         └── words → t_list ("wc")

lexer builds:  t_token → t_token → …
parser reads:  t_parser.cur walks that list → t_node tree
expand uses:   t_exp (+ t_shell.env / status)
heredoc uses:  t_heredoc per << redirection
```
