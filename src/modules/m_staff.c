/*
 *   m_staff: Displays a file(/URL) when the /STAFF command is used.
 *   (C) Copyright 2004-2016 Syzop <syzop@vulnscan.org>
 *   (C) Copyright 2003-2004 AngryWolf <angrywolf@flashmail.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "unrealircd.h"
#ifdef USE_LIBCURL
#include "url.h"
#endif

#define MSG_STAFF	"STAFF"

#define DEF_STAFF_FILE   CONFDIR "/network.staff"
#define CONF_STAFF_FILE  (staff_file ? staff_file : DEF_STAFF_FILE)
#ifdef USE_LIBCURL
#define STAFF_FILE       (Download.path ? Download.path : CONF_STAFF_FILE)
#else
#define STAFF_FILE       CONF_STAFF_FILE
#endif

#define RPL_STAFF        ":%s 700 %s :- %s"
#define RPL_STAFFSTART   ":%s 701 %s :- %s IRC Network Staff Information -"
#define RPL_ENDOFSTAFF   ":%s 702 %s :End of /STAFF command."
#define RPL_NOSTAFF      ":%s 703 %s :Network Staff File is missing"

#define ircstrdup(x,y)   do { if (x) MyFree(x); if (!y) x = NULL; else x = strdup(y); } while(0)
#define ircfree(x)       do { if (x) MyFree(x); x = NULL; } while(0)

/* Forward declarations */
static void unload_motd_file(aMotdFile *list);
CMD_FUNC(m_staff);
static int cb_rehashflag(aClient *cptr, aClient *sptr, char *flag);
static int cb_test(ConfigFile *, ConfigEntry *, int, int *);
static int cb_conf(ConfigFile *, ConfigEntry *, int);
static int cb_rehash();
static int cb_stats(aClient *sptr, char *flag);
#ifdef USE_LIBCURL
static int download_staff_file(ConfigEntry *ce);
static void download_staff_file_complete(char *url, char *file, char *errorbuf, int cached, void *dummy);
#endif
static void InitConf();
static void FreeConf();

static aMotdFile staff;
static char *staff_file;

#ifdef USE_LIBCURL
struct {
	unsigned	is_url : 1;
	unsigned	once_completed : 1;
	unsigned	in_progress : 1;
	char		*file;			// File name
	char		*path;			// File path
	char		*url;			// Full URL address
} Download;
#endif

ModuleHeader MOD_HEADER(m_staff)
  = {
	"staff",
	"v3.8",
	"/STAFF command",
	"3.2-b8-1",
	NULL 
    };

MOD_TEST(m_staff)
{
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, cb_test);
	return MOD_SUCCESS;
}

MOD_INIT(m_staff)
{
	MARK_AS_OFFICIAL_MODULE(modinfo);
#ifdef USE_LIBCURL
	memset(&Download, 0, sizeof Download);
	ModuleSetOptions(modinfo->handle, MOD_OPT_PERM, 1);
#endif
	memset(&staff, 0, sizeof(staff));
	InitConf();

	CommandAdd(modinfo->handle, MSG_STAFF, m_staff, MAXPARA, M_USER);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, cb_conf);
	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, cb_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_REHASHFLAG, 0, cb_rehashflag);
	HookAdd(modinfo->handle, HOOKTYPE_STATS, 0, cb_stats);

	return MOD_SUCCESS;
}

MOD_LOAD(m_staff)
{
	return MOD_SUCCESS;
}

MOD_UNLOAD(m_staff)
{
	FreeConf();
	unload_motd_file(&staff);

#ifdef USE_LIBCURL
	ircfree(Download.path);
   	ircfree(Download.file);
	ircfree(Download.url);
#endif

	return MOD_SUCCESS;
}

static int cb_rehash()
{
	FreeConf();
	InitConf();
	return 1;
}

static void InitConf()
{
	staff_file = NULL;
}

static void FreeConf()
{
	if (staff_file)
		MyFree(staff_file);
}

/*** web routines */
#ifdef USE_LIBCURL
static void remove_staff_file()
{
	if (Download.path)
	{
		if (remove(Download.path) == -1)
		{
			if (config_verbose > 0)
				config_status("Cannot remove file %s: %s",
					Download.path, strerror(errno));
		}
	        MyFree(Download.path);
	        Download.path = NULL;
	}
}

static int download_staff_file(ConfigEntry *ce)
{
	int ret = 0;
	struct stat sb;
	char *file, *filename;

	if (Download.in_progress)
		return 0;

	Download.is_url = 1;
	ircstrdup(Download.url, ce->ce_vardata);

	file = url_getfilename(ce->ce_vardata);
	filename = unreal_getfilename(file);
	/* TODO: handle NULL returns */
	ircstrdup(Download.file, filename);
	MyFree(file);

	if (!loop.ircd_rehashing && !Download.once_completed)
	{
		char *error;

		if (config_verbose > 0)
			config_status("Downloading %s", displayurl(Download.url));

		if (!(file = download_file(ce->ce_vardata, &error)))
		{
			config_error("%s:%i: test: error downloading '%s': %s",
				ce->ce_fileptr->cf_filename, ce->ce_varlinenum,
				displayurl(ce->ce_vardata), error);
			return -1;
		}

		Download.once_completed = 1;
		ircstrdup(Download.path, file);
		read_motd(Download.path, &staff);

		MyFree(file);
		return 0;
	}

	file = Download.path ? Download.path : Download.file;

	if ((ret = stat(file, &sb)) && errno != ENOENT)
	{
		/* I know, stat shouldn't fail... */
		config_error("%s:%i: could not get the creation time of %s: stat() returned %d: %s",
			ce->ce_fileptr->cf_filename, ce->ce_varlinenum,
			Download.file, ret, strerror(errno));
		return -1;
	}

	if (config_verbose > 0)
		config_status("Downloading %s", displayurl(Download.url));

	Download.in_progress = 1;
	download_file_async(Download.url, sb.st_ctime, download_staff_file_complete, NULL);
	return 0;
}

static void download_staff_file_complete(char *url, char *file, char *errorbuf, int cached, void *dummy)
{
	Download.in_progress = 0;
	Download.once_completed = 1;

	if (!cached)
	{
		if (!file)
		{
			config_error("Error downloading %s: %s",
				displayurl(url), errorbuf);
			return;
		}

		remove_staff_file();
		Download.path = strdup(file);
		read_motd(Download.path, &staff);
	} else
	{
		char *urlfile = url_getfilename(url);
		char *file = unreal_getfilename(urlfile);
		char *tmp = unreal_mktemp("tmp", file);
		/* TODO: handle null returns ? */
		unreal_copyfile(Download.path, tmp);
		remove_staff_file();
		Download.path = strdup(tmp);
		MyFree(urlfile);
	}
}
#endif

static void unload_motd_file(aMotdFile *list)
{
	aMotdLine *old, *new;

	if (!list)
		return;

	new = list->lines;

	if (!new)
		return;

	while (new)
	{
		old = new->next;
		MyFree(new->line);
		MyFree(new);
		new = old;
	}
}

static int cb_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
#ifdef USE_LIBCURL
	char *file = NULL, *filename = NULL;
#endif

	if (type == CONFIG_SET)
	{
		if (!strcmp(ce->ce_varname, "staff-file"))
		{
#ifdef USE_LIBCURL
			if (url_is_valid(ce->ce_vardata))
			{
				/* TODO: hm, relax this one? */
				if (!(file = url_getfilename(ce->ce_vardata)) || !(filename = unreal_getfilename(file)))
				{
					config_error("%s:%i: invalid filename in URL",
						ce->ce_fileptr->cf_filename, ce->ce_varlinenum);
					errors++;
				}
				ircfree(file);
			}
#endif

			*errs = errors;
			return errors ? -1 : 1;
		}
	}

	return 0;
}

static int cb_conf(ConfigFile *cf, ConfigEntry *ce, int type)
{
	if (type == CONFIG_SET)
	{
		if (!strcmp(ce->ce_varname, "staff-file"))
		{
#ifdef USE_LIBCURL
			if (!Download.in_progress)
			{
				ircstrdup(staff_file, ce->ce_vardata);
				if (url_is_valid(ce->ce_vardata))
				{
					download_staff_file(ce);
				}
				else
#endif
				{
					convert_to_absolute_path(&ce->ce_vardata, CONFDIR);
					read_motd(ce->ce_vardata, &staff);
				}
#ifdef USE_LIBCURL
			}

#endif
			return 1;
		}
	}

	return 0;
}

static int cb_stats(aClient *sptr, char *flag)
{
	if (*flag == 'S')
		sendto_one(sptr, ":%s %i %s :staff-file: %s",
			me.name, RPL_TEXT, sptr->name, STAFF_FILE);

	return 0;
}

static int cb_rehashflag(aClient *cptr, aClient *sptr, char *flag)
{
	int myflag = 0;

	/* "-all" only keeps compatibility with beta19 */
	if (!_match("-all", flag) || (myflag = !_match("-staff", flag)))
	{
		if (myflag)
			sendto_ops("%sRehashing network staff file on the request of %s",
                                cptr != sptr ? "Remotely " : "", sptr->name);

#ifdef USE_LIBCURL
		if (Download.is_url)
			read_motd(Download.path, &staff);
		else
#endif
			read_motd(CONF_STAFF_FILE, &staff);
	}

	return 0;
}

/** The routine that actual does the /STAFF command */
CMD_FUNC(m_staff)
{
	aMotdFile *temp;
	aMotdLine *aLine;

	if (!IsPerson(sptr))
		return -1;

	if (hunt_server(cptr, sptr, ":%s STAFF", 1, parc, parv) != HUNTED_ISME)
		return 0;

	if (!staff.lines)
	{
		sendto_one(sptr, RPL_NOSTAFF, me.name, sptr->name);
		return 0;
	}

	sendto_one(sptr, RPL_STAFFSTART, me.name, sptr->name, ircnetwork);

	temp = &staff;

	for (aLine = temp->lines; aLine; aLine = aLine->next)
		sendto_one(sptr, RPL_STAFF, me.name, sptr->name, aLine->line);

	sendto_one(sptr, RPL_ENDOFSTAFF, me.name, sptr->name);

	return 0;
}
