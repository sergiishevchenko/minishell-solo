#include "minishell.h"

static int	env_add_entry(t_env **env, char *entry)
{
	char	*eq;
	char	*key;
	int		ret;

	eq = ft_strchr(entry, '=');
	if (!eq)
		return (0);
	key = ft_substr(entry, 0, eq - entry);
	if (!key)
		return (1);
	ret = env_set(env, key, eq + 1);
	free(key);
	return (ret);
}

static int	init_shlvl(t_env **env)
{
	char	*value;
	char	*num;
	int		level;
	int		ret;

	value = env_get(*env, "SHLVL");
	level = 0;
	if (value)
		level = ft_atoi(value);
	if (level < 0)
		level = -1;
	if (level >= 999)
		level = 0;
	num = ft_itoa(level + 1);
	if (!num)
		return (1);
	ret = env_set(env, "SHLVL", num);
	free(num);
	return (ret);
}

static int	init_pwd(t_env **env)
{
	char	*cwd;
	char	*pwd;
	int		ret;

	if (!env_find(*env, "OLDPWD") && env_set(env, "OLDPWD", NULL) != 0)
		return (1);
	pwd = env_get(*env, "PWD");
	if (pwd && pwd[0] == '/' && same_dir(pwd, "."))
		return (0);
	// cwd: real path from the kernel (getcwd), not $PWD from env
	cwd = getcwd(NULL, 0);
	if (!cwd)
		return (0);
	ret = env_set(env, "PWD", cwd);
	free(cwd);
	return (ret);
}

int	env_init(t_shell *sh, char **envp)
{
	int	i;

	i = 0;
	while (envp && envp[i])
	{
		if (env_add_entry(&sh->env, envp[i]) != 0)
			break ;
		i++;
	}
	if ((envp && envp[i]) || init_shlvl(&sh->env) || init_pwd(&sh->env))
	{
		env_free(sh->env);
		sh->env = NULL;
		print_error("malloc", strerror(ENOMEM), NULL);
		return (1);
	}
	return (0);
}
