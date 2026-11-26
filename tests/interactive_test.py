#!/usr/bin/env python3
import os
import pty
import re
import select
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "minishell")
PROMPT = b"minishell$ "
ANSI_RE = re.compile(rb"\x1b\[[0-9;]*[A-Za-z]")


def strip_ansi(data):
    return ANSI_RE.sub(b"", data)


class Shell:
    def __init__(self):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execv(BIN, [BIN])
        self.buf = b""
        self.read_until(PROMPT)

    def read(self, timeout=0.5):
        out = b""
        end = time.time() + timeout
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if r:
                try:
                    data = os.read(self.fd, 4096)
                except OSError:
                    break
                if not data:
                    break
                out += data
        return strip_ansi(out)

    def read_until(self, token, timeout=3.0):
        raw = b""
        end = time.time() + timeout
        while time.time() < end and token not in strip_ansi(raw):
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if r:
                try:
                    data = os.read(self.fd, 4096)
                except OSError:
                    break
                if not data:
                    break
                raw += data
        return strip_ansi(raw)

    def send(self, data):
        os.write(self.fd, data)

    def wait(self):
        _, status = os.waitpid(self.pid, 0)
        return os.waitstatus_to_exitcode(status)


results = []


def check(name, cond, detail=b""):
    results.append(cond)
    mark = "\033[32mOK\033[0m  " if cond else "\033[31mFAIL\033[0m"
    print(f"{mark} {name}")
    if not cond:
        print("      output:", detail)


def test_prompt_and_command():
    sh = Shell()
    sh.send(b"echo interactive\n")
    out = sh.read_until(PROMPT)
    check("command output then new prompt",
          b"interactive\r\n" in out and out.endswith(PROMPT), out)
    sh.send(b"\x04")
    sh.read()
    check("ctrl-D on empty prompt exits with 0", sh.wait() == 0)


def test_ctrl_c_prompt():
    sh = Shell()
    sh.send(b"partial text")
    sh.read(0.2)
    sh.send(b"\x03")
    out = sh.read_until(PROMPT)
    check("ctrl-C shows a new prompt on a new line", PROMPT in out, out)
    sh.send(b"echo $?\n")
    out = sh.read_until(PROMPT)
    check("$? is 130 after ctrl-C", b"130" in out, out)
    sh.send(b"echo alive\n")
    out = sh.read_until(PROMPT)
    check("ctrl-C cleared the partial line", b"partial" not in out
          and b"alive" in out, out)
    sh.send(b"exit 7\n")
    sh.read()
    check("exit 7 returns 7", sh.wait() == 7)


def test_ctrl_backslash_prompt():
    sh = Shell()
    sh.send(b"\x1c")
    out = sh.read(0.3)
    sh.send(b"echo still\n")
    out += sh.read_until(PROMPT)
    check("ctrl-\\ at prompt does nothing", b"still" in out
          and b"Quit" not in out, out)
    sh.send(b"exit\n")
    sh.read()
    sh.wait()


def test_ctrl_c_child():
    sh = Shell()
    sh.send(b"sleep 5\n")
    sh.read(0.3)
    sh.send(b"\x03")
    out = sh.read_until(PROMPT)
    check("ctrl-C interrupts a blocking child", PROMPT in out, out)
    sh.send(b"echo $?\n")
    out = sh.read_until(PROMPT)
    check("$? is 130 after interrupted child", b"130" in out, out)
    sh.send(b"cat\n")
    sh.read(0.3)
    sh.send(b"\x1c")
    out = sh.read_until(PROMPT)
    check("ctrl-\\ quits a blocking child", b"Quit" in out, out)
    sh.send(b"echo $?\n")
    out = sh.read_until(PROMPT)
    check("$? is 131 after ctrl-\\", b"131" in out, out)
    sh.send(b"exit\n")
    sh.read()
    sh.wait()


def test_heredoc_ctrl_c():
    sh = Shell()
    sh.send(b"cat << EOF\n")
    sh.read_until(b"> ")
    sh.send(b"line\n")
    sh.read_until(b"> ")
    sh.send(b"\x03")
    out = sh.read_until(PROMPT)
    check("ctrl-C aborts heredoc", PROMPT in out and b"line\r\nline" not in out,
          out)
    sh.send(b"echo $?\n")
    out = sh.read_until(PROMPT)
    check("$? is 130 after heredoc ctrl-C", b"130" in out, out)
    sh.send(b"cat << EOF\n")
    sh.read_until(b"> ")
    sh.send(b"x $HOME\n")
    sh.read_until(b"> ")
    sh.send(b"EOF\n")
    out = sh.read_until(PROMPT)
    check("heredoc works after an aborted one",
          os.environ["HOME"].encode() in out, out)
    sh.send(b"exit\n")
    sh.read()
    sh.wait()


def test_heredoc_ctrl_d():
    sh = Shell()
    sh.send(b"cat << EOF\n")
    sh.read_until(b"> ")
    sh.send(b"body\n")
    sh.read_until(b"> ")
    sh.send(b"\x04")
    out = sh.read_until(PROMPT)
    check("ctrl-D ends heredoc with a warning",
          b"warning" in out and b"body" in out, out)
    sh.send(b"exit\n")
    sh.read()
    sh.wait()


def test_history():
    sh = Shell()
    sh.send(b"echo from_history\n")
    sh.read_until(PROMPT)
    sh.send(b"\x1b[A\n")
    out = sh.read_until(PROMPT)
    check("up arrow recalls previous command",
          out.count(b"from_history") >= 2, out)
    sh.send(b"exit\n")
    sh.read()
    sh.wait()


def test_exit_message():
    sh = Shell()
    sh.send(b"exit\n")
    out = sh.read()
    check("exit prints 'exit'", b"exit" in out, out)
    sh.wait()


def test_nested_minishell():
    sh = Shell()
    sh.send(b"echo $SHLVL\n")
    outer = sh.read_until(PROMPT).split(b"\r\n")[1]
    sh.send(('"' + BIN + '"\n').encode())
    sh.read_until(PROMPT)
    sh.send(b"echo $SHLVL\n")
    out = sh.read_until(PROMPT)
    check("nested minishell increments SHLVL",
          str(int(outer) + 1).encode() + b"\r\n" in out, out)
    sh.send(b"sleep 5\n")
    sh.read(0.3)
    sh.send(b"\x03")
    out = sh.read_until(PROMPT)
    sh.send(b"exit\n")
    sh.read_until(PROMPT)
    sh.send(b"echo outer_alive\n")
    out = sh.read_until(PROMPT)
    check("outer shell survives ctrl-C in nested shell",
          b"outer_alive" in out, out)
    sh.send(b"exit\n")
    sh.read()
    sh.wait()


for test in [test_prompt_and_command, test_ctrl_c_prompt,
             test_ctrl_backslash_prompt, test_ctrl_c_child,
             test_heredoc_ctrl_c, test_heredoc_ctrl_d, test_history,
             test_exit_message, test_nested_minishell]:
    test()

print(f"\nPassed: {sum(results)}  Failed: {len(results) - sum(results)}")
sys.exit(0 if all(results) else 1)
