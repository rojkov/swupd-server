/*
 *   Software Updater - server side
 *
 *      Copyright © 2012-2016 Intel Corporation.
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, version 2 or later of the License.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Authors:
 *         Arjan van de Ven <arjan@linux.intel.com>
 *         Sebastien Boeuf <sebastien.boeuf@intel.com>
 *
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <ctype.h>
#include <dirent.h>

#include <glib.h>

#include "swupd.h"

int current_version;


void read_current_version(char *filename)
{
	FILE *file;
	int v = 0;
	char *fullfile = NULL;
	char *conf;

	conf =  config_image_base();


	string_or_die(&fullfile, "%s/%s", conf, filename);

	file = fopen(fullfile, "rm");

	free(conf);

	if (!file) {
		fprintf(stderr, "Cannot read current version, failed to open %s (%s)\n", fullfile, strerror(errno));
		free(fullfile);
		return;
	}

	if (fscanf(file, "%i", &v) < 0) {
		/* not even a single int there --> make sure to return 0 */
		fprintf(stderr, "Version file %s does not have a number in it. Setting version to 0\n", fullfile);
	}

	fclose(file);
	free(fullfile);

	current_version = v;
}

void write_new_version(char *filename, int version)
{
	FILE *file;
	char *fullfile = NULL;
	char *conf;

	conf =  config_image_base();


	string_or_die(&fullfile, "%s/%s", conf, filename);

	file = fopen(fullfile, "w");

	free(conf);

	if (!file) {
		fprintf(stderr, "Cannot write new version, failed to open %s (%s)\n", fullfile, strerror(errno));
		free(fullfile);
		return;
	}

	fprintf(file, "%i\n", version);

	fclose(file);
	free(fullfile);
}

void ensure_version_image_exists(int version)
{
	char *conf;
	char *path = NULL;
	struct stat buf;
	int ret;

	conf = config_image_base();

	string_or_die(&path, "%s/%i", conf, version);

	free(conf);

	ret = stat(path, &buf);
	if (ret < 0) {
		fprintf(stderr, "Failed to stat image directory %s (%s).. exiting\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (!S_ISDIR(buf.st_mode)) {
		fprintf(stderr, "Assumed image directory %s is not a directory.. exiting\n", path);
		exit(EXIT_FAILURE);
	}

	free(path);
}

void write_cookiecrumbs_to_download_area(int version)
{
	char *conf;
	char *cmd;

	conf =  config_output_dir();
	if (conf == NULL) {
		assert(0);
	}

	string_or_die(&cmd, "echo %s > %s/%i/swupd-server-src-version", VERSION, conf, version);
	if (system(cmd) != 0) {
		assert(0);
	}
	free(cmd);

	string_or_die(&cmd, "echo %s > %s/%i/format", format_string, conf, version);
	if (system(cmd) != 0) {
		assert(0);
	}
	free(cmd);

/* written from pack_maker.sh */
#if 0
	FILE *file;
	char *fullfile = NULL;

	string_or_die(&cmd, "mkdir -p %s/version/formatstaging/", conf);
	if (system(cmd) != 0) {
		assert(0);
	}
	free(cmd);

	string_or_die(&fullfile, "%s/version/formatstaging/latest", conf);
	file = fopen(fullfile, "w");
	if (!file) {
		fprintf(stderr, "Cannot write new version to download area, failed to open %s (%s)\n", fullfile, strerror(errno));
		free(fullfile);
		return;
	}
	fprintf(file, "%i\n", version);
	fclose(file);
	free(fullfile);
#endif
	free(conf);
}

static int compare_versions(gconstpointer a, gconstpointer b)
{
	return GPOINTER_TO_INT(b) - GPOINTER_TO_INT(a);
}

static int get_build_type(int build_num) {
	int build_type = build_num % 10;
	if (build_type == 0) {
		return REGULAR_BUILD;
	} else if (build_type == 9) {
		return DEV_BUILD;
	} else {
		return FIX_BUILD;
	}
}

static int get_jump_point(int build_num) {
	int jump_point = (build_num / 100) * 100;
	if (jump_point == build_num) {
		jump_point -= 100;
	}
	if (jump_point < 0) {
		jump_point = 0;
	}
	return jump_point;
}

/* create a list of max_versions most recent version numbers, excluding
 * next_version */
GList *get_last_versions_list(int next_version, int max_versions)
{
	DIR *dir;
	GList *list = NULL;
	GList *cur_item, *next_item;
	struct dirent entry;
	struct dirent *result;
	struct stat stat;
	char *filename = NULL;
	int idx, build_num, build_type, jump_point;
	int jump_point_found;

	dir = opendir(staging_dir);
	if (dir == NULL) {
		LOG(NULL, "Cannot open directory", "dir_path= %s, strerror= %s",
				staging_dir, strerror(errno));
		return NULL;
	}

	while (readdir_r(dir, &entry, &result) == 0 && result != NULL) {
		if (strspn(entry.d_name, "0123456789") != strlen(entry.d_name)) {
			continue;
		}

		free(filename);
		string_or_die(&filename, "%s/%s", staging_dir, entry.d_name);

		if (lstat(filename, &stat)) {
			LOG(NULL, "lstat failed", "path= %s, strerror= %s",
					filename, strerror(errno));
			continue;
		}

		if (!S_ISDIR(stat.st_mode)) {
			continue;
		}

		if (atoi(entry.d_name) >= next_version) {
			continue;
		}

		list = g_list_prepend(list, GINT_TO_POINTER(atoi(entry.d_name)));
	}
	free(filename);
	closedir(dir);

	/* Exit without error if list is empty */
	if (list == NULL) {
		return NULL;
	}

	/* Sort list top down */
	list = g_list_sort(list, compare_versions);

	/*
	 * Keep last max_versions regular build versions,
	 * all fix build versions between, and the most
	 * recent jump point version
	 */
	idx = 0;
	jump_point = -1;
	jump_point_found = 0;
	next_item = list = g_list_first(list);
	while (next_item) {
		cur_item = next_item;
		build_num = GPOINTER_TO_INT(cur_item->data);
		build_type = get_build_type(build_num);
		next_item = g_list_next(cur_item);
		if ((idx >= max_versions || build_type == DEV_BUILD) &&
				build_num != jump_point && build_num != 0) {
			list = g_list_delete_link(list, cur_item);
		} else if (idx == max_versions - 1 && build_num != 0) {
			list = g_list_delete_link(list, cur_item);
			idx--;
		}
		if (build_num == jump_point) {
			jump_point_found = 1;
		}
		if (build_type == REGULAR_BUILD) {
			idx++;
			if (idx >= max_versions && !jump_point_found) {
				if (jump_point < 0) {
					jump_point = get_jump_point(build_num);
				} else if (build_num < jump_point) {
					jump_point = get_jump_point(jump_point);
				}
			}
		}
	}

	return list;
}
