/*
 *
 *  neard - Near Field Communication manager
 *
 *  Copyright (C) 2011  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <dlfcn.h>

#include <glib.h>

#ifdef NEAR_PLUGIN_BUILTIN
#undef NEAR_PLUGIN_BUILTIN
#endif

#include "near.h"

static GSList *plugins = NULL;

struct near_plugin {
	void *handle;
	bool active;
	struct near_plugin_desc *desc;
};

static gint compare_priority(gconstpointer a, gconstpointer b)
{
	const struct near_plugin *plugin1 = a;
	const struct near_plugin *plugin2 = b;

	return plugin2->desc->priority - plugin1->desc->priority;
}

static bool add_plugin(void *handle, struct near_plugin_desc *desc)
{
	struct near_plugin *plugin;

	if (!desc->init)
		return false;

	if (!g_str_equal(desc->version, NEAR_VERSION)) {
		near_error("Version mismatch for %s", desc->description);
		return false;
	}

	plugin = g_try_new0(struct near_plugin, 1);
	if (!plugin)
		return false;

	plugin->handle = handle;
	plugin->active = false;
	plugin->desc = desc;

	plugins = g_slist_insert_sorted(plugins, plugin, compare_priority);

	return true;
}

static bool check_plugin(struct near_plugin_desc *desc,
				char **patterns, char **excludes)
{
	if (excludes) {
		for (; *excludes; excludes++)
			if (g_pattern_match_simple(*excludes, desc->name))
				break;
		if (*excludes) {
			near_info("Excluding %s", desc->description);
			return false;
		}
	}

	if (patterns) {
		for (; *patterns; patterns++)
			if (g_pattern_match_simple(*patterns, desc->name))
				break;
		if (!*patterns) {
			near_info("Ignoring %s", desc->description);
			return false;
		}
	}

	return true;
}

#include "builtin.h"

int __near_plugin_init(const char *pattern, const char *exclude)
{
	gchar **patterns = NULL;
	gchar **excludes = NULL;
	GSList *list;
	GDir *dir;
	const gchar *file;
	gchar *filename;
	unsigned int i;

	DBG("");

	if (pattern)
		patterns = g_strsplit_set(pattern, ":, ", -1);

	if (exclude)
		excludes = g_strsplit_set(exclude, ":, ", -1);

	for (i = 0; __near_builtin[i]; i++) {
		if (!check_plugin(__near_builtin[i], patterns, excludes))
			continue;

		add_plugin(NULL, __near_builtin[i]);
	}

	dir = g_dir_open(PLUGINDIR, 0, NULL);
	if (dir) {
		while ((file = g_dir_read_name(dir))) {
			void *handle;
			struct near_plugin_desc *desc;

			if (g_str_has_prefix(file, "lib") ||
					!g_str_has_suffix(file, ".so"))
				continue;

			filename = g_build_filename(PLUGINDIR, file, NULL);

			handle = dlopen(filename, RTLD_NOW);
			if (!handle) {
				near_error("Can't load %s: %s",
							filename, dlerror());
				g_free(filename);
				continue;
			}

			g_free(filename);

			desc = dlsym(handle, "near_plugin_desc");
			if (!desc) {
				near_error("Can't load symbol: %s",
								dlerror());
				dlclose(handle);
				continue;
			}

			if (!check_plugin(desc, patterns, excludes)) {
				dlclose(handle);
				continue;
			}

			if (!add_plugin(handle, desc))
				dlclose(handle);
		}

		g_dir_close(dir);
	}

	for (list = plugins; list; list = list->next) {
		struct near_plugin *plugin = list->data;

		if (plugin->desc->init() < 0)
			continue;

		plugin->active = true;
	}

	g_strfreev(patterns);
	g_strfreev(excludes);

	return 0;
}

void __near_plugin_cleanup(void)
{
	GSList *list;

	DBG("");

	for (list = plugins; list; list = list->next) {
		struct near_plugin *plugin = list->data;

		if (plugin->active && plugin->desc->exit)
			plugin->desc->exit();

		if (plugin->handle)
			dlclose(plugin->handle);

		g_free(plugin);
	}

	g_slist_free(plugins);
}
