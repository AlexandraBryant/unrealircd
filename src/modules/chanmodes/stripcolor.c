/*
 * Strip Color UnrealIRCd Module (Channel Mode +S)
 * (C) Copyright 2000-.. Bram Matthys (Syzop) and the UnrealIRCd team
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

CMD_FUNC(stripcolor);

ModuleHeader MOD_HEADER(stripcolor)
  = {
	"chanmodes/stripcolor",
	"4.2",
	"Channel Mode +S",
	"3.2-b8-1",
	NULL 
    };

Cmode_t EXTCMODE_STRIPCOLOR;

#define IsStripColor(chptr)    (chptr->mode.extmode & EXTCMODE_STRIPCOLOR)

char *stripcolor_prechanmsg(aClient *sptr, aChannel *chptr, char *text, int notice);
char *stripcolor_prelocalpart(aClient *sptr, aChannel *chptr, char *comment);
char *stripcolor_prelocalquit(aClient *sptr, char *comment);

MOD_TEST(stripcolor)
{
	return MOD_SUCCESS;
}

MOD_INIT(stripcolor)
{
CmodeInfo req;

	/* Channel mode */
	memset(&req, 0, sizeof(req));
	req.paracount = 0;
	req.flag = 'S';
	req.is_ok = extcmode_default_requirechop;
	CmodeAdd(modinfo->handle, req, &EXTCMODE_STRIPCOLOR);
	
	HookAddPChar(modinfo->handle, HOOKTYPE_PRE_CHANMSG, 0, stripcolor_prechanmsg);
	HookAddPChar(modinfo->handle, HOOKTYPE_PRE_LOCAL_PART, 0, stripcolor_prelocalpart);
	HookAddPChar(modinfo->handle, HOOKTYPE_PRE_LOCAL_QUIT, 0, stripcolor_prelocalquit);
	
	MARK_AS_OFFICIAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

MOD_LOAD(stripcolor)
{
	return MOD_SUCCESS;
}

MOD_UNLOAD(stripcolor)
{
	return MOD_SUCCESS;
}

char *stripcolor_prechanmsg(aClient *sptr, aChannel *chptr, char *text, int notice)
{
	Hook *h;
	int i;

	if (MyClient(sptr) && IsStripColor(chptr))
	{
		for (h = Hooks[HOOKTYPE_CAN_BYPASS_CHANNEL_MESSAGE_RESTRICTION]; h; h = h->next)
		{
			i = (*(h->func.intfunc))(sptr, chptr, BYPASS_CHANMSG_COLOR);
			if (i != HOOK_CONTINUE)
				break;
		}
		if (i == HOOK_ALLOW)
			return text; /* bypass */

		text = StripColors(text);
	}

	return text;
}

char *stripcolor_prelocalpart(aClient *sptr, aChannel *chptr, char *comment)
{
	if (!comment)
		return NULL;

	if (MyClient(sptr) && IsStripColor(chptr))
		comment = StripColors(comment);

	return comment;
}

/** Is any channel where the user is in +S? */
static int IsAnyChannelStripColor(aClient *sptr)
{
	Membership *lp;

	for (lp = sptr->user->channel; lp; lp = lp->next)
		if (IsStripColor(lp->chptr))
			return 1;
	return 0;
}


char *stripcolor_prelocalquit(aClient *sptr, char *comment)
{
	if (!comment)
		return NULL;

	if (MyClient(sptr) && !BadPtr(comment) && IsAnyChannelStripColor(sptr))
		comment = StripColors(comment);
        
	return comment;
}

