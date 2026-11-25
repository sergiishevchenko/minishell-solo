#include "minishell.h"

static int	set_op(t_tok *type, t_tok value, int len)
{
	*type = value;
	return (len);
}

int	op_len(char *s, t_tok *type)
{
	if (s[0] == '<' && s[1] == '<')
		return (set_op(type, T_HEREDOC, 2));
	if (s[0] == '>' && s[1] == '>')
		return (set_op(type, T_APPEND, 2));
	if (s[0] == '<')
		return (set_op(type, T_IN, 1));
	if (s[0] == '>')
		return (set_op(type, T_OUT, 1));
	if (s[0] == '|')
		return (set_op(type, T_PIPE, 1));
	return (0);
}
