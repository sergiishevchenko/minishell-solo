# `struct sigaction`

POSIX structure used with `sigaction(2)` to install or query a signal handler.

In this project it appears in `src/shell/signals.c` inside `set_handler`.

Related: global `g_signal` notes in [structs.md](structs.md).

---

## Why not only `signal()`?

Older API:

```c
signal(SIGINT, handler);
```

`sigaction` is preferred because it is more portable and lets you control:

- which handler runs;
- which signals are blocked while the handler runs;
- flags (restart interrupted syscalls, etc.).

minishell wraps `sigaction` in `set_handler` so every place uses the same setup.

---

## The structure (conceptual)

Exact member layout can vary by OS, but the fields you care about are:

```c
struct sigaction {
	void	(*sa_handler)(int);		/* or SIG_DFL / SIG_IGN */
	void	(*sa_sigaction)(int, siginfo_t *, void *);
	sigset_t	sa_mask;
	int		sa_flags;
	/* ... possibly more platform fields ... */
};
```

`sa_handler` and `sa_sigaction` share the same storage (often a union). You use **one** of them, depending on flags.

| Field | Role |
|-------|------|
| `sa_handler` | Simple handler: `void f(int sig)`, or `SIG_DFL`, or `SIG_IGN`. |
| `sa_sigaction` | Extended handler with `siginfo_t` (only if `SA_SIGINFO` is set). |
| `sa_mask` | Extra signals blocked for the duration of the handler. |
| `sa_flags` | Behaviour bits (`SA_RESTART`, `SA_SIGINFO`, …). |

minishell only uses the **simple** form: `sa_handler`, empty `sa_mask`, flags left at `0`.

---

## Special handler values

| Value | Meaning |
|-------|---------|
| function pointer | Call that function when the signal arrives. |
| `SIG_DFL` | Default OS action (for `SIGINT`: terminate the process). |
| `SIG_IGN` | Ignore the signal. |

Examples in this codebase:

- prompt + interactive: `SIGINT` → `prompt_handler`;
- prompt: `SIGQUIT` → `SIG_IGN`;
- parent during exec: `SIGINT` / `SIGQUIT` → `SIG_IGN`;
- child during exec: both → `SIG_DFL`.

---

## How minishell fills it

```c
static void	set_handler(int sig, void (*handler)(int))
{
	struct sigaction	sa;

	ft_bzero(&sa, sizeof(sa));
	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sigaction(sig, &sa, NULL);
}
```

Step by step:

1. **`struct sigaction sa`** — local config object for one call.
2. **`ft_bzero`** — zero everything (including `sa_flags`). Avoids leftover garbage.
3. **`sa.sa_handler = handler`** — store *who* to call later (`prompt_handler`, `SIG_DFL`, or `SIG_IGN`). This does **not** call the handler now.
4. **`sigemptyset(&sa.sa_mask)`** — no extra signals blocked inside the handler (beyond the one being delivered, which is blocked by default on many systems).
5. **`sigaction(sig, &sa, NULL)`** — tell the kernel: for signal `sig`, use this config. Third argument `NULL` means “do not return the previous action”.

---

## Registration vs delivery

Two different moments:

```text
NOW (setup)
  set_handler(SIGINT, prompt_handler)
    → sigaction stores: SIGINT ⇒ prompt_handler

LATER (Ctrl-C)
  kernel calls prompt_handler(SIGINT)
    → parameter `sig` comes from the kernel, not from set_handler
```

- First argument of `set_handler` / `sigaction`: **which** signal to configure.
- Argument of `prompt_handler(int sig)`: **which** signal was delivered when the kernel invoked the handler.

---

## What `prompt_handler` does on Ctrl-C

```c
static void	prompt_handler(int sig)
{
	g_signal = sig;
	write(STDOUT_FILENO, "\n", 1);
	rl_on_new_line();
	rl_replace_line("", 0);
	rl_redisplay();
}
```

| Step | Role |
|------|------|
| `g_signal = sig` | Remember that `SIGINT` arrived (`shell_loop` later sets status `130`). |
| `write(..., "\n")` | Move the cursor to a new line on the terminal. |
| `rl_on_new_line()` | Tell readline the cursor is already on a **new** line (after that `\n`). Without this, readline still thinks it is on the old line. |
| `rl_replace_line("", 0)` | Clear the input buffer (discard what the user typed). The `0` means do not clear readline’s undo list. |
| `rl_redisplay()` | Redraw prompt + buffer → a clean `minishell$` line. |

Without the three `rl_*` calls, Ctrl-C can leave a broken display: a newline was printed, but readline’s idea of the line/prompt is stale.

---

## `sigaction` prototype

```c
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
```

| Argument | Meaning |
|----------|---------|
| `signum` | Signal number (`SIGINT`, `SIGQUIT`, …). |
| `act` | New disposition (`&sa`), or `NULL` to only query. |
| `oldact` | If non-`NULL`, previous disposition is written here. |

Returns `0` on success, `-1` on error (`errno` set).

---

## Mental model

`struct sigaction` is a **settings form** you fill and hand to the kernel once.

The kernel keeps that form in the process signal table. When the signal fires, the kernel uses the form to decide: ignore, default, or call your function with the signal number.
