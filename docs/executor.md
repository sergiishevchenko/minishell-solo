# Executor

The executor **walks the AST** and runs commands: expand argv, apply
redirections, set up pipes, fork when needed, and wait for status.

**Files:** `src/exec/*.c`, plus `src/mandatory/exec_node.c` or
`src/bonus/exec_node_bonus.c`  
**Entry:** `exec_node(t_shell *sh, t_node *node)` — called from `process_line`
after heredocs succeed.  
**Related:** [expand.md](expand.md), [heredoc.md](heredoc.md), [ast.md](ast.md),
[e_ntype.md](e_ntype.md).

---

## Place in the pipeline

```text
process_line
   │
   ├─ lex → parse → AST
   ├─ read_heredocs(AST)     # << bodies already in redir->fd
   └─ exec_node(AST)
          │
          ▼
     walk tree (pipe / && / || / subshell / cmd)
          │
          ▼
     expand → redirs → builtin or execve
          │
          ▼
     status → sh->status / $?
```

Return value of `exec_node` becomes `sh->status` in `process_line`.

---

## Dispatch: `exec_node`

### Mandatory (`src/mandatory/exec_node.c`)

```text
N_PIPE  → exec_pipeline
else    → exec_command   (N_CMD)
```

### Bonus (`src/bonus/exec_node_bonus.c`)

```text
N_PIPE     → exec_pipeline
N_SUBSHELL → exec_subshell   (fork, apply redirs, exec left, exit)
N_AND/N_OR → exec_logic      (run left; maybe right; short-circuit)
else       → exec_command
```

```text
                    exec_node(root)
                           │
          ┌────────────────┼────────────────┐
          ▼                ▼                ▼
      N_PIPE           N_AND/OR         N_CMD / …
          │                │                │
   exec_pipeline      exec_logic      exec_command
   fork each side     left then ?     expand + run
```

---

## Simple command — `exec_command`

```text
exec_command(node)
   │
   ▼ expand_args(node->words) → sh->argv
   │
   ├─ no argv[0] or is_builtin?
   │     └─ exec_builtin_parent
   │           dup stdin/stdout
   │           apply_redirs
   │           run_builtin (if argv[0])
   │           restore fds
   │
   └─ else
         └─ exec_forked
               fork → child: set_signals_exec, run_cmd_child
               parent: wait_child
   │
   ▼ free_strs(argv)
```

Why builtins in the parent when not in a pipe: `cd` / `export` / `unset` /
`exit` must change the **shell’s** environment and cwd, not a child’s.

Inside a pipeline every segment is already a child (`exec_in_child`), so
builtins there run in the child and cannot affect the parent shell (bash-like).

---

## Child path — `run_cmd_child` / `exec_in_child`

```text
exec_in_child          # used from pipeline children
   expand_args
   run_cmd_child
      apply_redirs
      empty argv     → exit 0
      builtin        → run_builtin → exit
      else           → exec_external
```

`exec_external`:

```text
resolve_cmd (PATH / absolute / relative)
   │
   ├─ fail → child_exit(127 / 126)
   └─ ok
        env_to_array
        shell_cleanup
        execve(path, argv, envp)
        on failure → print error, exit 126/127
```

---

## Pipelines — `exec_pipeline`

Right-leaning `N_PIPE` chain: walk `node`, `node->right`, … For each hop
except the last, open a `pipe()`, fork a child, wire fds, keep the read end
as the next command’s stdin.

```text
cmd1 | cmd2 | cmd3

in_fd=-1
  fork cmd1:  stdin=in_fd(none)  stdout=pipe1[1]
  in_fd=pipe1[0]
  fork cmd2:  stdin=in_fd         stdout=pipe2[1]
  in_fd=pipe2[0]
  fork cmd3:  stdin=in_fd         stdout=unchanged
  wait_pipeline(last pid)   # status of last command; reap the rest
```

```text
  ┌──── cmd1 ────┐     ┌──── cmd2 ────┐     ┌──── cmd3 ────┐
  │              │     │              │     │              │
  │   stdout ────┼──▶──┼── stdin      │     │              │
  │              │     │   stdout ────┼──▶──┼── stdin      │
  └──────────────┘     └──────────────┘     └──────────────┘
```

`pipe_child` unwraps one level if the “left” of a pipe node is still an
`N_PIPE` (right-associative tree), then either `exec_in_child` for `N_CMD`
or recursive `exec_node` for nested structures (bonus).

---

## Redirections — `apply_redirs`

Applied **after** expand of the command words, left-to-right on `node->redirs`.

| Type | Action |
|------|--------|
| `T_HEREDOC` | `dup2(redir->fd, STDIN)` — fd already filled by `read_heredocs` |
| `T_IN` | expand target → `open` O_RDONLY → `dup2` stdin |
| `T_OUT` | expand → create/trunc → stdout |
| `T_APPEND` | expand → create/append → stdout |

Target expand uses `expand_word`. **0 or 2+ fields** → `ambiguous redirect`
(same idea as bash).

Parent builtins save/restore stdin/stdout with `dup` so the interactive shell
keeps its terminal fds after `echo > file` etc.

---

## Logic ops & subshell (bonus)

**`exec_logic` (`&&` / `||`):**

```text
status = exec_node(left)
sh->status = status
if SIGINT caught → stop
if (AND && status == 0) || (OR && status != 0)
    status = exec_node(right)
```

**`exec_subshell` (`(...)`):**

```text
fork
  child: apply node->redirs, exec_node(left), child_exit
  parent: wait_child
```

---

## Waiting & exit status

| Function | Role |
|----------|------|
| `wait_child` | `waitpid` one pid; map signal → `128 + sig`, exit → `WEXITSTATUS` |
| `wait_pipeline` | Wait last pid for status; `wait` the rest |
| `decode_status` | Sets `g_signal` on SIGINT; prints `Quit: 3` on SIGQUIT |

That status is what `$?` expands to on the next line.

---

## Worked example

Line: `echo hi | wc -l`

```text
AST:  N_PIPE
      /    \
  N_CMD    N_CMD
  echo hi  wc -l

exec_node → exec_pipeline
  fork left:  expand → ["echo","hi"] ; redirs none ; builtin in child → write "hi\n" into pipe
  fork right: expand → ["wc","-l"]  ; read pipe ; execve wc
  wait last → status of wc
```

Line: `cd /tmp` (no pipe)

```text
exec_command
  expand → ["cd","/tmp"]
  is_builtin → exec_builtin_parent
  apply_redirs (none) → builtin_cd updates sh->env / cwd
  restore fds → return status
```
