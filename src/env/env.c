#include "minishell.h"

static t_env	*env_new(char *key, char *value)
{
	t_env	*node;

	node = ft_calloc(1, sizeof(t_env));
	if (!node)
		return (NULL);
	node->key = ft_strdup(key);
	if (value)
		node->value = ft_strdup(value);
	if (!node->key || (value && !node->value))
	{
		free(node->key);
		free(node->value);
		free(node);
		return (NULL);
	}
	return (node);
}

static int	env_replace(t_env *node, char *value)
{
	char	*dup;

	dup = NULL;
	if (value)
	{
		dup = ft_strdup(value);
		if (!dup)
			return (1);
	}
	free(node->value);
	node->value = dup;
	return (0);
}

int	env_set(t_env **env, char *key, char *value)
{
	t_env	*node;
	t_env	*cur;

	node = env_find(*env, key);
	if (node)
		return (env_replace(node, value));
	node = env_new(key, value);
	if (!node)
		return (1);
	if (!*env)
	{
		*env = node;
		return (0);
	}
	cur = *env;
	while (cur->next)
		cur = cur->next;
	cur->next = node;
	return (0);
}

void	env_unset(t_env **env, char *key)
{
	t_env	*cur;
	t_env	*prev;

	prev = NULL;
	cur = *env;
	while (cur && ft_strcmp(cur->key, key) != 0)
	{
		prev = cur;
		cur = cur->next;
	}
	if (!cur)
		return ;
	if (prev)
		prev->next = cur->next;
	else
		*env = cur->next;
	free(cur->key);
	free(cur->value);
	free(cur);
}
