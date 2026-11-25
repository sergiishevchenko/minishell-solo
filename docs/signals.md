# Signals

minishell handles `SIGINT` (Ctrl-C) and `SIGQUIT` (Ctrl-\) differently
depending on whether it is waiting at the prompt, running a child, or
collecting a heredoc. Setup uses `sigaction` via a small wrapper — see
[sigaction.md](sigaction.md) for the structure and `prompt_handler` details.

**File:** `src/shell/signals.c`  
**Global:** [`g_signal`](structs.md#g_signal) (`volatile sig_atomic_t`)  
**Related:** `src/shell/main.c` (`shell_loop`), `src/exec/exec_wait.c`

Subject rules covered here:

- Interactive prompt: Ctrl-C → new prompt; Ctrl-\ → does nothing; Ctrl-D → exit.
- Running commands: children get default signal behaviour; parent does not die with them.
- Only one global, and it holds a signal number only.

---

## Three modes

All modes are installed with `set_handler` → `sigaction`.

| Mode | Function | When |
|------|----------|------|
| Prompt | `set_signals_prompt(interactive)` | Start of each `shell_loop` iteration |
| Exec | `set_signals_exec(child)` | Parent while waiting / child after fork |
| Heredoc | `set_signals_heredoc()` | Inside the heredoc collection child |

```text
                    interactive?
                         │
            ┌────────────┴────────────┐
            ▼                         ▼
   set_signals_prompt(1)     set_signals_prompt(0)
   SIGINT  → prompt_handler  SIGINT  → SIG_DFL
   SIGQUIT → SIG_IGN         SIGQUIT → SIG_IGN
            │
            ▼  user runs a command / heredoc
   ┌────────────────┬─────────────────────────┐
   ▼                ▼                         ▼
 parent wait     child exec              heredoc child
 set_signals_    set_signals_            set_signals_
 exec(0)         exec(1)                 heredoc()
 SIGINT  IGN     SIGINT  DFL             SIGINT  DFL
 SIGQUIT IGN     SIGQUIT DFL             SIGQUIT IGN
```

---

## Prompt mode

```c
void	set_signals_prompt(int interactive)
{
	if (interactive)
		set_handler(SIGINT, prompt_handler);
	else
		set_handler(SIGINT, SIG_DFL);
	set_handler(SIGQUIT, SIG_IGN);
}
```

### Interactive (`isatty(STDIN_FILENO)`)

| Key | Signal | Effect |
|-----|--------|--------|
| Ctrl-C | `SIGINT` | `prompt_handler`: set `g_signal`, newline, clear readline line, redisplay prompt |
| Ctrl-\ | `SIGQUIT` | Ignored — no `^\` / Quit message at an idle prompt |
| Ctrl-D | (EOF, not a signal) | `readline` returns `NULL` → shell exits the loop |

After `read_input` returns, `shell_loop` maps a caught Ctrl-C to status `130`:

```c
g_signal = 0;
sh->line = read_input(sh, get_prompt());
if (g_signal == SIGINT)
	sh->status = 130;   /* 128 + SIGINT */
```

`readline` itself may return an empty string or continue; the important part is
that the display is reset and `$?` becomes `130` when Ctrl-C hit the prompt.

### Non-interactive

`SIGINT` stays at `SIG_DFL` so piping into minishell behaves like a normal
process (Ctrl-C from the terminal group can terminate it). `SIGQUIT` remains
ignored at the “prompt” setup path for consistency with the interactive
ignore-at-rest policy for quit.

---

## Exec mode

```c
void	set_signals_exec(int child)
{
	if (child)
	{
		set_handler(SIGINT, SIG_DFL);
		set_handler(SIGQUIT, SIG_DFL);
		return ;
	}
	set_handler(SIGINT, SIG_IGN);
	set_handler(SIGQUIT, SIG_IGN);
}
```

### Child (`child != 0`)

Called right after fork in command / pipe / subshell children. Both signals
use **default** disposition:

- Ctrl-C → child dies with `SIGINT`;
- Ctrl-\ → child dies with `SIGQUIT` (and typically dumps core unless limited).

The foreground process group of the terminal delivers the key to the child;
the parent is not supposed to die with it.

### Parent (`child == 0`)

Called at the start of `wait_child`. Parent **ignores** both signals while
waiting so:

- Ctrl-C does not interrupt `waitpid` or kill the shell;
- the child’s death is observed via `waitpid` status instead.

---

## Heredoc mode

```c
void	set_signals_heredoc(void)
{
	set_handler(SIGINT, SIG_DFL);
	set_handler(SIGQUIT, SIG_IGN);
}
```

Installed only in the **heredoc child** (see [heredoc.md](heredoc.md)).

| Signal | Disposition | Why |
|--------|-------------|-----|
| `SIGINT` | `SIG_DFL` | Ctrl-C aborts collection; child exits; parent sees `130` |
| `SIGQUIT` | `SIG_IGN` | Same quiet behaviour as the prompt — no Quit while typing the body |

The parent waits with `wait_child` → `set_signals_exec(0)`, so it ignores
signals and only updates status from the child’s wait status.

---

## Decoding child status

`decode_status` in `src/exec/exec_wait.c`:

| Wait macro | Result |
|------------|--------|
| `WIFSIGNALED` | `128 + WTERMSIG(wstatus)`; for `SIGINT` also sets `g_signal` and prints `\n`; for `SIGQUIT` prints `Quit: 3` |
| `WIFEXITED` | `WEXITSTATUS(wstatus)` |
| else | `1` |

So an external command killed by Ctrl-C yields `$? == 130`; by Ctrl-\ yields
`131` (plus the Quit message on stderr).

Full table of conventional statuses: [Exit status and signals](sigaction.md#exit-status-and-signals).

---

## Lifecycle of `g_signal`

```text
shell_loop iteration
  set_signals_prompt(...)
  g_signal = 0
  readline / read_input
       │
       ├─ Ctrl-C at prompt → prompt_handler sets g_signal = SIGINT
       │                     shell_loop sets status = 130
       │
       └─ command runs → wait_child
              child dies from SIGINT → decode_status sets g_signal = SIGINT
                                       (bonus short-circuit may also check it)
```

Rules of thumb:

- Write `g_signal` only from a signal handler or from wait decoding.
- Clear it at the start of each prompt iteration.
- Never put pointers or structs in it (subject + async-signal-safety).

---

## Async-signal-safety (prompt handler)

`prompt_handler` only:

1. assigns `g_signal`;
2. `write`s a newline;
3. calls readline’s `rl_on_new_line` / `rl_replace_line` / `rl_redisplay`.

It does **not** call `printf`, `malloc`, or other non-async-signal-safe libc
functions. Readline’s redisplay helpers are required by the subject’s
interactive UX and are the project’s accepted way to refresh the prompt after
Ctrl-C. Deeper notes: [sigaction.md — What prompt_handler does](sigaction.md#what-prompt_handler-does-on-ctrl-c).

---

## Quick reference

| Context | Ctrl-C | Ctrl-\ | Ctrl-D |
|---------|--------|--------|--------|
| Idle interactive prompt | New prompt, `$?=130` | Ignored | Exit shell |
| Parent waiting on child | Ignored in parent; child may die | Ignored in parent; child may die | N/A (not waiting on readline) |
| Running child | Default → exit `130` | Default → exit `131` + Quit msg | Child’s own EOF handling |
| Heredoc child | Abort heredoc → `130` | Ignored | EOF warning, keep partial body |
