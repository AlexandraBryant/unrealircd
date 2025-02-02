/*
 * hideserver.c - Hide certain or all servers from /MAP & /LINKS
 *
 * Note that this module simple hides servers. It does not truly
 * increase security. Use as you wish.
 *
 * (C) Copyright 2003-2004 AngryWolf <angrywolf@flashmail.com>
 * (C) Copyright 2016 Bram Matthys <syzop@vulnscan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 1, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "unrealircd.h"

CMD_OVERRIDE_FUNC(override_map);
CMD_OVERRIDE_FUNC(override_links);
static int cb_test(ConfigFile *, ConfigEntry *, int, int *);
static int cb_conf(ConfigFile *, ConfigEntry *, int);

ConfigItem_ulines *HiddenServers;

static struct
{
	unsigned	disable_map : 1;
	unsigned	disable_links : 1;
	char		*map_deny_message;
	char		*links_deny_message;
} Settings;

static ModuleInfo	*MyModInfo;
#define MyMod		MyModInfo->handle
#define SAVE_MODINFO	MyModInfo = modinfo;

ModuleHeader MOD_HEADER(hideserver)
  = {
	"hideserver",
	"v5.0",
	"Hide servers from /MAP & /LINKS",
	"3.2-b8-1",
	NULL 
    };

static void InitConf()
{
	memset(&Settings, 0, sizeof Settings);
}

static void FreeConf()
{
	ConfigItem_ulines	*h, *next;

	safefree(Settings.map_deny_message);
	safefree(Settings.links_deny_message);

	for (h = HiddenServers; h; h = next)
	{
		next = h->next;
		DelListItem(h, HiddenServers);
		MyFree(h->servername);
		MyFree(h);
	}
}

MOD_TEST(hideserver)
{
	SAVE_MODINFO
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, cb_test);

	return MOD_SUCCESS;
}

MOD_INIT(hideserver)
{
	MARK_AS_OFFICIAL_MODULE(modinfo);
	SAVE_MODINFO
	HiddenServers = NULL;
	InitConf();

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, cb_conf);

	return MOD_SUCCESS;
}

MOD_LOAD(hideserver)
{
	if (!CmdoverrideAdd(MyMod, "MAP", override_map))
		return MOD_FAILED;

	if (!CmdoverrideAdd(MyMod, "LINKS", override_links))
		return MOD_FAILED;

	return MOD_SUCCESS;
}

MOD_UNLOAD(hideserver)
{
	FreeConf();

	return MOD_SUCCESS;
}

static int cb_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	ConfigEntry	*cep, *cepp;
	int		errors = 0;

	if (type == CONFIG_MAIN)
	{
		if (!strcmp(ce->ce_varname, "hideserver"))
		{
			for (cep = ce->ce_entries; cep; cep = cep->ce_next)
			{
				if (!strcmp(cep->ce_varname, "hide"))
				{
					/* No checking needed */
				}
				else if (!cep->ce_vardata)
				{
					config_error("%s:%i: %s::%s without value",
						cep->ce_fileptr->cf_filename,
						cep->ce_varlinenum,
						ce->ce_varname, cep->ce_varname);
					errors++;
					continue;
				}
				else if (!strcmp(cep->ce_varname, "disable-map"))
					;
				else if (!strcmp(cep->ce_varname, "disable-links"))
					;
				else if (!strcmp(cep->ce_varname, "map-deny-message"))
					;
				else if (!strcmp(cep->ce_varname, "links-deny-message"))
					;
				else
				{
					config_error("%s:%i: unknown directive hideserver::%s",
						cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_varname);
					errors++;
				}
			}
			*errs = errors;
			return errors ? -1 : 1;
		}
	}

	return 0;
}

static int cb_conf(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry		*cep, *cepp;
	ConfigItem_ulines	*ca;

	if (type == CONFIG_MAIN)
	{
		if (!strcmp(ce->ce_varname, "hideserver"))
		{
			for (cep = ce->ce_entries; cep; cep = cep->ce_next)
			{
				if (!strcmp(cep->ce_varname, "disable-map"))
					Settings.disable_map = config_checkval(cep->ce_vardata, CFG_YESNO);
				else if (!strcmp(cep->ce_varname, "disable-links"))
					Settings.disable_links = config_checkval(cep->ce_vardata, CFG_YESNO);
				else if (!strcmp(cep->ce_varname, "map-deny-message"))
				{
					safestrdup(Settings.map_deny_message, cep->ce_vardata);
				}
				else if (!strcmp(cep->ce_varname, "links-deny-message"))
				{
					safestrdup(Settings.links_deny_message, cep->ce_vardata);
				}
				else if (!strcmp(cep->ce_varname, "hide"))
				{
					for (cepp = cep->ce_entries; cepp; cepp = cepp->ce_next)
					{
						if (!strcasecmp(cepp->ce_varname, me.name))
							continue;

						ca = MyMallocEx(sizeof(ConfigItem_ulines));
						safestrdup(ca->servername, cepp->ce_varname);
						AddListItem(ca, HiddenServers);
					}
				}
			}

			return 1;
		}
	}

	return 0;
}

ConfigItem_ulines *FindHiddenServer(char *servername)
{
	ConfigItem_ulines *h;

	for (h = HiddenServers; h; h = h->next)
		if (!strcasecmp(servername, h->servername))
			break;

	return h;
}

/*
 * New /MAP format -Potvin
 * dump_map function.
 */
static void dump_map(aClient *cptr, aClient *server, char *mask, int prompt_length, int length)
{
	static char prompt[64];
	char *p = &prompt[prompt_length];
	int  cnt = 0;
	aClient *acptr;

	*p = '\0';

	if (prompt_length > 60)
		sendto_one(cptr, rpl_str(RPL_MAPMORE), me.name, cptr->name,
		    prompt, length, server->name);
	else
	{
		sendto_one(cptr, rpl_str(RPL_MAP), me.name, cptr->name, prompt,
		    length, server->name, server->serv->users, IsOper(cptr) ? server->id : "");
		cnt = 0;
	}

	if (prompt_length > 0)
	{
		p[-1] = ' ';
		if (p[-2] == '`')
			p[-2] = ' ';
	}
	if (prompt_length > 60)
		return;

	strcpy(p, "|-");

	list_for_each_entry(acptr, &global_server_list, client_node)
	{
		if (acptr->srvptr != server ||
 		    (IsULine(acptr) && HIDE_ULINES && !ValidatePermissionsForPath("server:info:map:ulines",cptr,NULL,NULL,NULL)))
			continue;
		if (FindHiddenServer(acptr->name))
			break;
		acptr->flags |= FLAGS_MAP;
		cnt++;
	}

	list_for_each_entry(acptr, &global_server_list, client_node)
	{
		if (IsULine(acptr) && HIDE_ULINES && !ValidatePermissionsForPath("server:info:map:ulines",cptr,NULL,NULL,NULL))
			continue;
		if (FindHiddenServer(acptr->name))
			break;
		if (acptr->srvptr != server)
			continue;
		if (!(acptr->flags & FLAGS_MAP))
			continue;
		if (--cnt == 0)
			*p = '`';
		dump_map(cptr, acptr, mask, prompt_length + 2, length - 2);
	}

	if (prompt_length > 0)
		p[-1] = '-';
}

void dump_flat_map(aClient *cptr, aClient *server, int length)
{
char buf[4];
aClient *acptr;
int cnt = 0, hide_ulines;

	hide_ulines = (HIDE_ULINES && !ValidatePermissionsForPath("server:info:map:ulines",cptr,NULL,NULL,NULL)) ? 1 : 0;

	sendto_one(cptr, rpl_str(RPL_MAP), me.name, cptr->name, "",
	    length, server->name, server->serv->users, "");

	list_for_each_entry(acptr, &global_server_list, client_node)
	{
		if ((IsULine(acptr) && hide_ulines) || (acptr == server))
			continue;
		if (FindHiddenServer(acptr->name))
			break;
		cnt++;
	}

	strcpy(buf, "|-");
	list_for_each_entry(acptr, &global_server_list, client_node)
	{
		if ((IsULine(acptr) && hide_ulines) || (acptr == server))
			continue;
		if (FindHiddenServer(acptr->name))
			break;
		if (--cnt == 0)
			*buf = '`';
		sendto_one(cptr, rpl_str(RPL_MAP), me.name, cptr->name, buf,
		    length-2, acptr->name, acptr->serv->users, "");
	}
}

/*
** New /MAP format. -Potvin
** m_map (NEW)
**
**      parv[1] = server mask
**/
CMD_OVERRIDE_FUNC(override_map)
{
	aClient *acptr;
	int  longest = strlen(me.name);

	if (parc < 2)
		parv[1] = "*";
	
	if (IsOper(sptr))
		return CallCmdoverride(ovr, cptr, sptr, parc, parv);

	if (Settings.disable_map)
	{
		if (Settings.map_deny_message)
			sendnotice(sptr, "%s", Settings.map_deny_message);
		else
			sendto_one(sptr, rpl_str(RPL_MAPEND), me.name, sptr->name);

		return 0;
	}

	list_for_each_entry(acptr, &global_server_list, client_node)
	{
		if (FindHiddenServer(acptr->name))
			break;
		if ((strlen(acptr->name) + acptr->hopcount * 2) > longest)
			longest = strlen(acptr->name) + acptr->hopcount * 2;
	}

	if (longest > 60)
		longest = 60;
	longest += 2;

	if (FLAT_MAP && !ValidatePermissionsForPath("server:info:map:real-map",sptr,NULL,NULL,NULL))
		dump_flat_map(sptr, &me, longest);
	else
		dump_map(sptr, &me, "*", 0, longest);

	sendto_one(sptr, rpl_str(RPL_MAPEND), me.name, sptr->name);

	return 0;
}

CMD_OVERRIDE_FUNC(override_links)
{
	aClient *acptr;
	int flat = (FLAT_MAP && !IsOper(sptr)) ? 1 : 0;

	if (IsOper(sptr))
		return CallCmdoverride(ovr, cptr, sptr, parc, parv);

	if (Settings.disable_links)
	{
		if (Settings.links_deny_message)
			sendnotice(sptr, "%s", Settings.links_deny_message);
		else
			sendto_one(sptr, rpl_str(RPL_ENDOFLINKS), me.name, sptr->name, "*");

		return 0;
	}

	list_for_each_entry(acptr, &global_server_list, client_node)
	{
		/* Some checks */
		if (HIDE_ULINES && IsULine(acptr) && !ValidatePermissionsForPath("server:info:map:ulines",cptr,NULL,NULL,NULL))
			continue;
		if (FindHiddenServer(acptr->name))
			continue;
		if (flat)
			sendto_one(sptr, rpl_str(RPL_LINKS),
			    me.name, sptr->name, acptr->name, me.name,
			    1, (acptr->info[0] ? acptr->info : "(Unknown Location)"));
		else
			sendto_one(sptr, rpl_str(RPL_LINKS),
			    me.name, sptr->name, acptr->name, acptr->serv->up,
			    acptr->hopcount, (acptr->info[0] ? acptr->info : "(Unknown Location)"));
	}

	sendto_one(sptr, rpl_str(RPL_ENDOFLINKS), me.name, sptr->name, "*");
	return 0;
}
