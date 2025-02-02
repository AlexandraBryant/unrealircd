/*
 * Channel mode +D/+d: delayed join
 * except from opers, U-lines and servers.
 * Copyright 2014 Travis Mcarthur <Heero> and UnrealIRCd Team
 */
#include "unrealircd.h"

ModuleHeader MOD_HEADER(delayjoin)
  = {
	"delayjoin",   /* Name of module */
	"v0.0.1", /* Version */
	"delayed join (+D,+d)", /* Short description of module */
	"3.2-b8-1",
	NULL
    };

#define MOD_DATA_STR "delayjoin"
#define MOD_DATA_INVISIBLE "1"

static long UMODE_PRIVDEAF = 0;
static Cmode *CmodeDelayed = NULL;
static Cmode *CmodePostDelayed = NULL;
static Cmode_t EXTMODE_DELAYED;
static Cmode_t EXTMODE_POST_DELAYED;

int visible_in_channel( aClient *cptr, aChannel *chptr);
int moded_check_part( aClient *cptr, aChannel *chptr);
int moded_join(aClient *cptr, aChannel *chptr);
int moded_part(aClient *cptr, aClient *sptr, aChannel *chptr, char *comment);
int deny_all(aClient *cptr, aChannel *chptr, char mode, char *para, int checkt, int what);
int moded_kick(aClient *cptr, aClient *sptr, aClient *acptr, aChannel *chptr, char *comment);
int moded_chanmode(aClient *cptr, aClient *sptr, aChannel *chptr,
                           char *modebuf, char *parabuf, time_t sendts, int samode);
char *moded_prechanmsg(aClient *sptr, aChannel *chptr, char *text, int notice);
char *moded_serialize(ModData *m);
void moded_unserialize(char *str, ModData *m);

MOD_INIT(delayjoin)
{
	CmodeInfo req;
	ModDataInfo mreq;

	memset(&req, 0, sizeof(req));
	req.paracount = 0;
	req.is_ok = extcmode_default_requirechop;
	req.flag = 'D';
	CmodeDelayed = CmodeAdd(modinfo->handle, req, &EXTMODE_DELAYED);

	memset(&req, 0, sizeof(req));
	req.paracount = 0;
	req.is_ok = deny_all;
	req.flag = 'd';
	CmodePostDelayed = CmodeAdd(modinfo->handle, req, &EXTMODE_POST_DELAYED);

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = MOD_DATA_STR;
	mreq.serialize = moded_serialize;
	mreq.unserialize = moded_unserialize;
	mreq.sync = 0;
	mreq.type = MODDATATYPE_MEMBER;
	if (!ModDataAdd(modinfo->handle, mreq))
		abort();

	if (!CmodeDelayed || !CmodePostDelayed)
	{
		/* I use config_error() here because it's printed to stderr in case of a load
		 * on cmd line, and to all opers in case of a /rehash.
		 */
		config_error("delayjoin: Could not add channel mode '+D' or '+d': %s", ModuleGetErrorStr(modinfo->handle));
		return MOD_FAILED;
	}

	HookAdd(modinfo->handle, HOOKTYPE_VISIBLE_IN_CHANNEL, 0, visible_in_channel);
	HookAdd(modinfo->handle, HOOKTYPE_JOIN_DATA, 0, moded_join);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_PART, 0, moded_part);
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_PART, 0, moded_part);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_KICK, 0, moded_kick);
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_KICK, 0, moded_kick);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CHANMODE, 0, moded_chanmode);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_REMOTE_CHANMODE, 0, moded_chanmode);
	HookAddPChar(modinfo->handle, HOOKTYPE_PRE_CHANMSG, 99999999, moded_prechanmsg);

	MARK_AS_OFFICIAL_MODULE(modinfo);

	return MOD_SUCCESS;
}

MOD_LOAD(delayjoin)
{
	return MOD_SUCCESS;
}

MOD_UNLOAD(delayjoin)
{
	return MOD_SUCCESS;
}

void set_post_delayed(aChannel *chptr)
{
	chptr->mode.extmode |= EXTMODE_POST_DELAYED;
	sendto_channel_butserv(chptr, &me, ":%s MODE %s +d", me.name, chptr->chname);
}

void clear_post_delayed(aChannel *chptr)
{
	chptr->mode.extmode &= ~EXTMODE_POST_DELAYED;
	sendto_channel_butserv(chptr, &me, ":%s MODE %s -d", me.name, chptr->chname);
}

bool moded_member_invisible(Member* m, aChannel *chptr)
{
	ModDataInfo *md;

	if (!m)
		return false;

	md = findmoddata_byname(MOD_DATA_STR, MODDATATYPE_MEMBER);
	if (!md)
		return false;

	if (!moddata_member(m,md).str)
		return false;

	return true;

}

bool moded_user_invisible(aClient *cptr, aChannel *chptr)
{
	return moded_member_invisible(find_member_link(chptr->members, cptr),chptr);
}

bool channel_has_invisible_users(aChannel *chptr)
{
	Member* i;
	for (i = chptr->members; i; i = i->next)
	{
		if (moded_member_invisible(i,chptr))
		{
			return true;
		}
	}
	return false;
}

bool channel_is_post_delayed(aChannel *chptr)
{
	if (chptr->mode.extmode & EXTMODE_POST_DELAYED)
		return true;
	return false;
}

bool channel_is_delayed(aChannel *chptr)
{
	if (chptr->mode.extmode & EXTMODE_DELAYED)
		return true;
	return false;
}

void clear_user_invisible(aChannel *chptr, aClient *sptr)
{
	Member *i;
	ModDataInfo *md;
	bool should_clear = true, found_member = false;

	md = findmoddata_byname(MOD_DATA_STR, MODDATATYPE_MEMBER);
	if (!md)
		return;
	for (i = chptr->members; i; i = i->next)
	{
		if (i->cptr == sptr)
		{

			if (md)
				memset(&moddata_member(i, md), 0, sizeof(ModData));

			found_member = true;

			if (!should_clear)
				break;
		}

		else if (moddata_member(i,md).str)
		{
			should_clear = false;
			if (found_member)
				break;
		}
	}

	if (should_clear && (chptr->mode.extmode & EXTMODE_POST_DELAYED))
	{
		clear_post_delayed(chptr);
	}
}

void clear_user_invisible_announce(aChannel *chptr, aClient *sptr)
{
	Member *i;
	char joinbuf[512];
	char exjoinbuf[512];

	clear_user_invisible(chptr,sptr);

	ircsnprintf(joinbuf, sizeof(joinbuf), ":%s!%s@%s JOIN %s",
				sptr->name, sptr->user->username, GetHost(sptr), chptr->chname);

	ircsnprintf(exjoinbuf, sizeof(exjoinbuf), ":%s!%s@%s JOIN %s %s :%s",
		sptr->name, sptr->user->username, GetHost(sptr), chptr->chname,
		!isdigit(*sptr->user->svid) ? sptr->user->svid : "*",
		sptr->info);

	for (i = chptr->members; i; i = i->next)
	{
		aClient *acptr = i->cptr;
		if (!is_skochanop(acptr,chptr) && acptr != sptr && MyConnect(acptr))
		{
			if (acptr->local->proto & PROTO_CAP_EXTENDED_JOIN)
				sendbufto_one(acptr, exjoinbuf, 0);
			else
				sendbufto_one(acptr, joinbuf, 0);
		}
	}
}

void set_user_invisible(aChannel *chptr, aClient *sptr)
{
	Member *m = find_member_link(chptr->members,sptr);
	ModDataInfo *md;

	if (!m)
		return;

	md = findmoddata_byname(MOD_DATA_STR, MODDATATYPE_MEMBER);

	if (!md || !md->unserialize)
		return;

	md->unserialize(MOD_DATA_INVISIBLE, &moddata_member(m, md));
}


int deny_all(aClient *cptr, aChannel *chptr, char mode, char *para, int checkt, int what)
{
	return EX_ALWAYS_DENY;
}


int visible_in_channel(aClient *cptr, aChannel *chptr)
{
	return channel_is_delayed(chptr) && moded_user_invisible(cptr,chptr);
}


int moded_join(aClient *cptr, aChannel *chptr)
{
	if (channel_is_delayed(chptr))
		set_user_invisible(chptr,cptr);

	return 0;
}

int moded_part(aClient *cptr, aClient *sptr, aChannel *chptr, char *comment)
{
	if (channel_is_delayed(chptr) || channel_is_post_delayed(chptr))
		clear_user_invisible(chptr,cptr);

	return 0;
}

int moded_kick(aClient *cptr, aClient *sptr, aClient *acptr, aChannel *chptr, char *comment)
{
	if (channel_is_delayed(chptr) || channel_is_post_delayed(chptr))
		if (moded_user_invisible(acptr, chptr))
			clear_user_invisible_announce(chptr,acptr);

	return 0;
}


int moded_chanmode(aClient *cptr, aClient *sptr, aChannel *chptr,
                           char *modebuf, char *parabuf, time_t sendts, int samode)
{
	// Handle case where we just unset +D but have invisible users
	if (!channel_is_delayed(chptr) && !channel_is_post_delayed(chptr) && channel_has_invisible_users(chptr))
		set_post_delayed(chptr);
	else if (channel_is_delayed(chptr) && channel_is_post_delayed(chptr))
		clear_post_delayed(chptr);

	if ((channel_is_delayed(chptr) || channel_is_post_delayed(chptr)))
	{
		ParseMode pm;
		int ret;
		for (ret = parse_chanmode(&pm, modebuf, parabuf); ret; ret = parse_chanmode(&pm, NULL, NULL))
		{
			if (pm.what == MODE_ADD && (pm.modechar == 'o' || pm.modechar == 'h' || pm.modechar == 'a' || pm.modechar == 'q' || pm.modechar == 'v'))
			{
				Member* i;
				aClient* user = find_client(pm.param,NULL);
				if (!user)
					continue;

				if (moded_user_invisible(user,chptr))
					clear_user_invisible_announce(chptr,user);

				if (pm.modechar == 'v' || !MyConnect(user))
					continue;

				/* Our user 'user' just got ops (oaq) - send the joins for all the users (s)he doesn't know about */
				for (i = chptr->members; i; i = i->next)
				{
					if (i->cptr == user)
						continue;
					if (moded_user_invisible(i->cptr,chptr))
					{
						if (user->local->proto & PROTO_CAP_EXTENDED_JOIN)
						{
							sendto_one(user,":%s!%s@%s JOIN %s %s :%s",
							           i->cptr->name, i->cptr->user->username, GetHost(i->cptr),
							           chptr->chname,
							           !isdigit(*i->cptr->user->svid) ? i->cptr->user->svid : "*",
							           i->cptr->info);
						} else {
							sendto_one(user,":%s!%s@%s JOIN :%s", i->cptr->name, i->cptr->user->username, GetHost(i->cptr), chptr->chname);
						}
					}
				}

			}
			if (pm.what == MODE_DEL && (pm.modechar == 'o' || pm.modechar == 'h' || pm.modechar == 'a' || pm.modechar == 'q' || pm.modechar == 'v'))
			{
				Member* i;
				aClient* user = find_client(pm.param,NULL);
				if (!user)
					continue;

				if (moded_user_invisible(user,chptr))
					clear_user_invisible_announce(chptr,user);

				if (pm.modechar == 'v' || !MyConnect(user))
					continue;

				/* Our user 'user' just lost ops (oaq) - send the parts for all users (s)he won't see anymore */
				for (i = chptr->members; i; i = i->next)
				{
					if (i->cptr == user)
						continue;
					if (moded_user_invisible(i->cptr,chptr))
						sendto_one(user,":%s!%s@%s PART :%s", i->cptr->name, i->cptr->user->username, GetHost(i->cptr), chptr->chname);
				}

			}
		}
	}

	return 0;
}

char *moded_prechanmsg(aClient *sptr, aChannel *chptr, char *text, int notice)
{

	if ((channel_is_delayed(chptr) || channel_is_post_delayed(chptr)) && (moded_user_invisible(sptr,chptr)))
		clear_user_invisible_announce(chptr,sptr);

	return text;
}

char *moded_serialize(ModData *m)
{
	return m->i ? "1" : "0";
}

void moded_unserialize(char *str, ModData *m)
{
	m->i = atoi(str);
}
