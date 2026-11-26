# Heredoc (`<<`)

Heredocs collect multi-line input **before** the command runs, then feed that
body to the command as stdin.

**Files:** `src/heredoc/heredoc.c`, `src/heredoc/heredoc_utils.c`  
**Entry:** `read_heredocs(t_shell *sh, t_node *node)` — called from
`process_line` after parse, before `exec_node`.  
**Types:** [`t_heredoc`](structs.md#t_heredoc), [`t_redir`](structs.md) with
`type == T_HEREDOC`.  
**Related:** [expand.md](expand.md) (`expand_heredoc_line`),
[executor.md](executor.md) (`apply_redirs`).

---

## Place in the pipeline

```text
lex → parse → AST
               │
               ▼
         read_heredocs()     ← fork child, write /tmp file, open read fd
               │
               ▼
         exec_node()
               │
               ▼
         apply_redirs()      ← dup2(redir->fd, STDIN_FILENO)
```

Collection is **eager**: every `<<` on the AST is filled before any command
runs. That matches bash when several heredocs appear on one line or in a
pipeline.

---

## User-facing behaviour

| Topic | Behaviour |
|-------|-----------|
| Syntax | `cmd << DELIM` then lines until a line equal to `DELIM` |
| Prompt | Interactive heredoc uses `> ` (`get_hd_prompt`) |
| History | Heredoc body lines are **not** added to readline history |
| Ctrl-D (EOF) | Ends the body early; prints a warning naming the wanted delimiter |
| Ctrl-C | Aborts the heredoc child (default `SIGINT`); parent gets status `130` |
| Quoted delimiter | If the delimiter token contains `'` or `"`, `$` is **not** expanded in the body |
| Unquoted delimiter | Each body line goes through `expand_heredoc_line` (`$VAR`, `$?`) |

Examples:

```sh
cat << EOF
$HOME
EOF
# → expands $HOME

cat << 'EOF'
$HOME
EOF
# → literal $HOME
```

---

## Data flow (one `<<`)

```text
redir->target  (may still contain quotes, e.g. 'EOF' or "EOF")
       │
       ▼ heredoc_init
  quoted?  delim = remove_quotes(target)
  path     = /tmp/.minishell_heredoc_<n>   (O_CREAT|O_EXCL, 0600)
  fd       = write end of that file
       │
       ▼ fork
  child:  set_signals_heredoc → read lines → write file → exit 0
  parent: close write fd → wait_child → open path O_RDONLY → unlink
       │
       ▼
  redir->fd = read fd   (ready for apply_redirs)
```

Temp files use `HD_PREFIX` (`/tmp/.minishell_heredoc_`) plus `sh->hd_count`.
`O_EXCL` avoids clobbering an existing name; on `EEXIST` the counter advances
and another name is tried. After the parent reopens for reading, the path is
**unlinked** so the inode disappears once the fd is closed (no leftover files
on a normal run).

---

## Functions

### `read_heredocs`

Walks the AST depth-first (`left`, then `right`), then each node’s `redirs`
list. For every `T_HEREDOC`, calls `heredoc_open`. Returns `1` on the first
failure (exec is skipped); `0` on success.

Order matters: left-to-right, tree before siblings’ redirs, so nested /
pipeline heredocs are filled in a stable, bash-like order.

### `heredoc_init`

1. Sets `hd->quoted` if `'` or `"` appears in `redir->target`.
2. Builds `hd->delim` with `remove_quotes` (delimiter comparison is unquoted).
3. Creates a unique temp path and write fd via `heredoc_path`.

### `heredoc_open`

Forks after init. Child runs `heredoc_child`. Parent closes its copy of the
write fd, waits with `wait_child`, then `heredoc_finish`.

### `heredoc_child`

```text
set_signals_heredoc()
loop:
  line = read_input(sh, get_hd_prompt())
  if !line → EOF warning, break
  if line == delim → break
  heredoc_write(...)
close fd, free, child_exit(0)
```

### `heredoc_write`

If the delimiter was **not** quoted, expands the line with
`expand_heredoc_line`, then writes `line + '\n'` to the temp file.

Expansion here only handles `$` (as if inside double quotes): no word-split,
no glob. The original line is freed; the expanded buffer is freed after write.

### `heredoc_finish`

On success (`st == 0`), opens the temp path read-only into `redir->fd`.
Always unlinks and frees path/delimiter. Non-zero `st` is stored in
`sh->status`.

---

## Why a temp file (not a pipe)?

A pipe would force the writer and reader to run concurrently and would block
once the pipe buffer fills. Writing the full body to `/tmp` first lets the
shell:

- support arbitrarily large heredocs;
- finish collection before exec;
- hand the command a seekable/read-only fd via `dup2`.

---

## Redirection at exec time

In `apply_redirs` / `apply_one`, `T_HEREDOC` does **not** re-expand a path.
It only:

```c
dup2(redir->fd, STDIN_FILENO);
```

So the open read fd from `heredoc_finish` becomes the command’s stdin. Later
`< file` on the same command can still replace stdin (redir order on the
node’s list).

---

## Signals during heredoc

See [signals.md](signals.md#heredoc-mode). Short version:

| Who | `SIGINT` | `SIGQUIT` |
|-----|----------|-----------|
| Heredoc child | `SIG_DFL` (die → parent status `130`) | `SIG_IGN` |
| Parent while waiting | Ignored via `set_signals_exec(0)` inside `wait_child` | Ignored |

---

## Error / edge cases

| Case | Result |
|------|--------|
| `malloc` / temp `open` fails | Error message; status `1`; `read_heredocs` returns `1` |
| `fork` fails | Same path through `heredoc_finish(..., 1)` |
| Child killed by Ctrl-C | `wait_child` → `128 + SIGINT` (`130`); exec skipped |
| Unexpected EOF | Warning on stderr; body so far is kept; status still success if open works |
| Quoted empty delimiter `<< ""` | Delimiter is empty string; a blank line ends the body |
