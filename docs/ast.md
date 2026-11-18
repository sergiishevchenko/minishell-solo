# AST — Abstract Syntax Tree

**AST** = **A**bstract **S**yntax **T**ree.

In minishell the AST is the in-memory tree that represents **one command line** after parsing. The lexer only produces a flat list of tokens; the parser turns that list into a tree; the executor walks the tree to run pipes, builtins, and external programs.

Field-level notes for `t_node` / `t_ntype`: [structs.md](structs.md).

---

## Breaking down the name

| Word | Plain meaning | In this project |
|------|---------------|-----------------|
| **Abstract** | Not a 1:1 copy of the typed text | Extra spaces are gone. Operators are node types, not characters. Quote characters are still stored inside word strings until **expand** time; the tree itself talks about commands and pipes, not “how it was typed”. |
| **Syntax** | Shape allowed by the language grammar | Valid shapes: a command, `A \| B`, and (bonus) `A && B`, `A \|\| B`, `( A )`. Invalid syntax never becomes a tree — the parser sets an error instead. |
| **Tree** | Hierarchy of nodes with children | Each node is a `t_node`. Binary ops use `left` / `right`. A simple command stores `words` and `redirs` on the node itself. |

Why a tree and not only a token list?

- A token list is linear: hard to see that `a | b | c` is nested pipes, or that `> out` belongs to `wc`, not to `grep`.
- A tree encodes **priority and grouping**: which command is left/right of a pipe, which redirections stick to which command, which subtree runs inside `( … )`.

---

## Place in the processing pipeline

For every line, `process_line` in `src/shell/main.c` roughly does:

```
┌────────────┐    ┌────────────┐    ┌────────────┐    ┌────────────┐
│  raw line  │───▶│   Lexer    │───▶│   Parser   │───▶│  Heredocs  │
│  char *    │    │  t_token*  │    │  t_node*   │    │  fill fds  │
└────────────┘    └────────────┘    └─────┬──────┘    └─────┬──────┘
                                          │ sh->ast         │
                                          ▼                 ▼
                                    ┌────────────┐    ┌────────────┐
                                    │ free_tokens│    │ exec_node  │
                                    └────────────┘    │ walk tree  │
                                                      └─────┬──────┘
                                                            ▼
                                                      ┌────────────┐
                                                      │  free_ast  │
                                                      └────────────┘
```

| Stage | Input | Output | Role vs AST |
|-------|-------|--------|-------------|
| Lexer | `char *` line | `t_token` list | No tree yet — only kinds like `T_WORD`, `T_PIPE`. |
| Parser | tokens | `t_node` tree | **Builds** the AST. |
| Heredoc | AST | same AST + open fds | Walks nodes to find `<<`, does not change tree shape. |
| Expand | mostly at exec of `N_CMD` | argv strings | Happens **when** a command node runs, not when the tree is built. |
| Executor | AST root | exit status | **Walks** the AST. |

So: tokens = “what was written”; AST = “how it is structured to run”.

---

## The node type: `t_node`

```c
typedef struct s_node
{
	t_ntype			type;    /* N_CMD, N_PIPE, … */
	t_list			*words;  /* command arguments (N_CMD) */
	t_redir			*redirs; /* < > >> << (N_CMD) */
	struct s_node	*left;
	struct s_node	*right;
}	t_node;
```

Which fields matter depends on `type`:

| `type` | `words` / `redirs` | `left` | `right` |
|--------|--------------------|--------|---------|
| `N_CMD` | used | unused (`NULL`) | unused |
| `N_PIPE` | unused | left side of `\|` | right side |
| `N_AND` | unused | left of `&&` | right of `&&` |
| `N_OR` | unused | left of `\|\|` | right of `\|\|` |
| `N_SUBSHELL` | unused on the wrapper | body to run in a child | unused |

`N_CMD.words` still hold **raw** word strings (possibly with quotes). Expansion (`$`, quotes, glob) runs later inside `exec_command` / expand helpers.

---

## Node kinds (`t_ntype`) in detail

### `N_CMD` — simple command

One process-worth of work: argument list + redirections.

Examples that parse to a single `N_CMD`:

```sh
ls -la
echo hello > file
cat << EOF
```

Built by `parse_simple` (`src/parser/parser_cmd.c`): while the current token is a word or a redirection operator, attach it to the same node.

### `N_PIPE` — pipeline

Binary node: run `left` and `right` with stdout of left connected to stdin of right.

A longer pipeline is a **right-leaning** chain of `N_PIPE` nodes, not a flat array:

```sh
ls | grep a | wc
```

```
                    ┌──────────┐
                    │  N_PIPE  │  ← root
                    └────┬─────┘
               left      │      right
          ┌──────────────┼──────────────┐
          ▼                             ▼
   ┌────────────┐                ┌──────────┐
   │   N_CMD    │                │  N_PIPE  │
   │ words: ls  │                └────┬─────┘
   └────────────┘           left      │      right
                               ┌──────┼──────┐
                               ▼             ▼
                        ┌────────────┐ ┌────────────┐
                        │   N_CMD    │ │   N_CMD    │
                        │ words:     │ │ words: wc  │
                        │  grep, a   │ └────────────┘
                        └────────────┘
```

Mandatory `exec_node` only distinguishes pipe vs command: pipes go to `exec_pipeline`, everything else to `exec_command`.

### `N_AND` / `N_OR` — logical lists (bonus)

- `N_AND`: run `right` only if `left` exited 0 (`&&`).
- `N_OR`: run `right` only if `left` exited non-zero (`||`).

Short-circuit is done in `src/bonus/exec_node_bonus.c`.

### `N_SUBSHELL` — parentheses (bonus)

The body AST hangs on `left`. Executor forks; the child runs that subtree. Parent waits. Builtin side effects inside `( … )` do not change the parent shell (e.g. `(cd /tmp)`).

---

## Lexer scheme (flat list)

Input:

```sh
ls -la | grep src | wc -l > out
```

The lexer does **not** build a tree. It builds a **singly linked list** of `t_token`:

```
  head
   │
   ▼
┌──────────┬──────┐    ┌──────────┬──────┐    ┌──────────┬──────┐
│ T_WORD   │ next │───▶│ T_WORD   │ next │───▶│ T_PIPE   │ next │──┐
│ "ls"     │      │    │ "-la"    │      │    │ value=∅  │      │  │
└──────────┴──────┘    └──────────┴──────┘    └──────────┴──────┘  │
                                                                   │
┌──────────────────────────────────────────────────────────────────┘
│
▼
┌──────────┬──────┐    ┌──────────┬──────┐    ┌──────────┬──────┐
│ T_WORD   │ next │───▶│ T_WORD   │ next │───▶│ T_PIPE   │ next │──┐
│ "grep"   │      │    │ "src"    │      │    │ value=∅  │      │  │
└──────────┴──────┘    └──────────┴──────┘    └──────────┴──────┘  │
                                                                   │
┌──────────────────────────────────────────────────────────────────┘
│
▼
┌──────────┬──────┐    ┌──────────┬──────┐    ┌──────────┬──────┐
│ T_WORD   │ next │───▶│ T_WORD   │ next │───▶│ T_OUT    │ next │──┐
│ "wc"     │      │    │ "-l"     │      │    │ value=∅  │      │  │
└──────────┴──────┘    └──────────┴──────┘    └──────────┴──────┘  │
                                                                   │
                                                                   ▼
                                                          ┌──────────┬──────┐
                                                          │ T_WORD   │ next │──▶ NULL
                                                          │ "out"    │  ∅   │
                                                          └──────────┴──────┘
```

Same list, compact view:

```
[WORD ls] → [WORD -la] → [PIPE] → [WORD grep] → [WORD src] → [PIPE]
    → [WORD wc] → [WORD -l] → [OUT] → [WORD out] → NULL
```

Properties of the lexer output:

- **Order** = left-to-right order of the input.
- **No hierarchy** — a pipe is just another token between words.
- Operators (`PIPE`, `OUT`, …) usually have `value == NULL`; words keep their lexeme in `value`.
- After `parse()`, this list is freed (`free_tokens`); only the AST remains.

---

## AST scheme (tree)

The **parser** consumes that list and builds a tree. For the same line:

```sh
ls -la | grep src | wc -l > out
```

```
                         ┌─────────────────┐
                         │     N_PIPE      │  ← sh->ast (root)
                         │ left │   │ right│
                         └──┬───┘   └──┬───┘
                   left     │          │     right
              ┌─────────────┘          └─────────────┐
              ▼                                      ▼
     ┌─────────────────┐                    ┌─────────────────┐
     │     N_CMD       │                    │     N_PIPE      │
     │ words:          │                    │ left │   │ right│
     │   "ls", "-la"   │                    └──┬───┘   └──┬───┘
     │ redirs: ∅       │                 left  │          │  right
     └─────────────────┘              ┌────────┘          └────────┐
                                      ▼                            ▼
                             ┌─────────────────┐          ┌─────────────────┐
                             │     N_CMD       │          │     N_CMD       │
                             │ words:          │          │ words:          │
                             │  "grep","src"   │          │  "wc", "-l"     │
                             │ redirs: ∅       │          │ redirs:         │
                             └─────────────────┘          │  T_OUT → "out"  │
                                                          └─────────────────┘
```

How to read it:

1. Root is a pipe: run left subtree, pipe into right subtree.
2. Left of the root = first command `ls -la`.
3. Right of the root = another pipe: `grep src` | `wc -l > out`.
4. **`> out` lives on the last `N_CMD`**, not on `N_PIPE` — that is why redirections “stick” to the right command.

Side-by-side summary:

```
LEXER (list)                         PARSER (tree)
────────────                         ─────────────
WORD ls  ──┐                            N_PIPE
WORD -la ──┤                           /      \
PIPE     ──┤                      N_CMD        N_PIPE
WORD grep──┤                     ls -la       /      \
WORD src ──┤                              N_CMD      N_CMD
PIPE     ──┤                            grep src   wc -l
WORD wc  ──┤                                       > out
WORD -l  ──┤
OUT      ──┤
WORD out ──┘
```

---

## Bonus example (lexer + AST)

```sh
(false || echo ok) && ls | wc
```

### Lexer list

```
[LPAREN] → [WORD false] → [OR] → [WORD echo] → [WORD ok] → [RPAREN]
     → [AND] → [WORD ls] → [PIPE] → [WORD wc] → NULL
```

### AST tree

```
                         ┌─────────────────┐
                         │     N_AND       │  ← root
                         │ left │   │ right│
                         └──┬───┘   └──┬───┘
                   left     │          │     right
          ┌─────────────────┘          └─────────────────┐
          ▼                                              ▼
 ┌─────────────────┐                            ┌─────────────────┐
 │   N_SUBSHELL    │                            │     N_PIPE      │
 │ body = left     │                            │ left │   │ right│
 └────────┬────────┘                            └──┬───┘   └──┬───┘
          │                                 left   │          │  right
          ▼                                        ▼          ▼
 ┌─────────────────┐                      ┌────────────┐ ┌────────────┐
 │      N_OR       │                      │   N_CMD    │ │   N_CMD    │
 │ left │   │ right│                      │ words: ls  │ │ words: wc  │
 └──┬───┘   └──┬───┘                      └────────────┘ └────────────┘
    │          │
    ▼          ▼
┌────────┐ ┌────────────┐
│ N_CMD  │ │   N_CMD    │
│ false  │ │ echo, ok   │
└────────┘ └────────────┘
```

Mandatory build never creates `N_AND` / `N_OR` / `N_SUBSHELL` (see `src/mandatory/` vs `src/bonus/`).

---

## How the executor uses the tree

Mandatory (`src/mandatory/exec_node.c`):

```c
if (node->type == N_PIPE)
	return (exec_pipeline(sh, node));
return (exec_command(sh, node));
```

Bonus extends this with `N_AND`, `N_OR`, `N_SUBSHELL` before falling through to pipe/command.

Typical `N_CMD` path inside `exec_command`:

1. Expand `words` → `argv`
2. Apply `redirs` (`dup2`, open files, heredoc fd)
3. If builtin and not in a pipeline → run in the **parent**
4. Else → `fork` + `execve` (or builtin in child)

So the AST decides **control flow** (pipe / and / or / subshell); expand + builtins decide **what string actually runs**.
