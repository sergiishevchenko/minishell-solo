#include "minishell.h"

static int	exp_status(t_exp *e)
{
	char	*value;

	value = ft_itoa(e->sh->status); // convert the last exit status to a string | e.g. 127 -> "127"
	if (!value)
		e->error = 1;
	exp_value(e, value, 1);
	free(value);
	return (2);
}

int	exp_dollar(t_exp *e, char *s, char quote)
{
	int		len;
	char	*name;

	if (s[1] == '?')
		return (exp_status(e)); // $? -> expand the last exit status
	if (ft_isdigit(s[1]))
		return (2);
	if (!quote && (s[1] == '\'' || s[1] == '"'))
		return (1);
	if (!ft_isalpha(s[1]) && s[1] != '_')
	{
		exp_push(e, '$', 1);
		return (1);
	}
	len = 1;
	while (ft_isalnum(s[len]) || s[len] == '_')
		len++;
	name = ft_substr(s, 1, len - 1);
	if (!name)
		e->error = 1;
	else
		exp_value(e, env_get(e->sh->env, name), quote != 0);
	free(name);
	return (len); // $USER! -> len=5 (without the !)
}
