#include "minishell.h"

void	exp_push(t_exp *e, char c, int quoted)
{
	if (!quoted && c == '*')
		c = GLOB_MARK; // glob: * matches filenames (e.g. *.c → main.c utils.c)
	if (str_push(&e->buf, c) != 0)
		e->error = 1;
	e->active = 1; // set to 1 to indicate that the field is active
}

void	exp_end_field(t_exp *e)
{
	t_list	*item;

	if (!e->active)
		return ;
	if (!e->buf)
		e->buf = ft_strdup("");
	item = NULL;
	if (e->buf)
		item = ft_lstnew(e->buf); // e.g. input $X, X="a b" → first close buf "a" -> item = "a"
	if (!item)
	{
		free(e->buf);
		e->error = 1;
	}
	else
		ft_lstadd_back(&e->fields, item); // then later "b" → fields: "a" → "b"
	e->buf = NULL;
	e->active = 0;
}

void	exp_value(t_exp *e, char *value, int quoted)
{
	int	i;

	if (!value)
		return ;
	i = 0;
	while (value[i])
	{
		if (!quoted && ft_isspace(value[i]))
			exp_end_field(e);
		else
			exp_push(e, value[i], quoted);
		i++;
	}
}

void	restore_marks(char *s)
{
	while (s && *s)
	{
		if (*s == GLOB_MARK)
			*s = '*';
		s++;
	}
}
