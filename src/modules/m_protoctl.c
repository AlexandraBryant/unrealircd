/*
 *   IRC - Internet Relay Chat, src/modules/m_protoctl.c
 *   (C) 2004- The UnrealIRCd Team
 *
 *   See file AUTHORS in IRC package for additional names of
 *   the programmers.
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

CMD_FUNC(m_protoctl);

#define MSG_PROTOCTL 	"PROTOCTL"	

ModuleHeader MOD_HEADER(m_protoctl)
  = {
	"m_protoctl",
	"4.2",
	"command /protoctl", 
	"3.2-b8-1",
	NULL 
    };

MOD_INIT(m_protoctl)
{
	CommandAdd(modinfo->handle, MSG_PROTOCTL, m_protoctl, MAXPARA, M_UNREGISTERED|M_SERVER|M_USER);
	MARK_AS_OFFICIAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

MOD_LOAD(m_protoctl)
{
	return MOD_SUCCESS;
}

MOD_UNLOAD(m_protoctl)
{
	return MOD_SUCCESS;
}

#define MAX_SERVER_TIME_OFFSET 60

/* The PROTOCTL command is used for negotiating capabilities with
 * directly connected servers.
 * See https://www.unrealircd.org/docs/Server_protocol:PROTOCTL_command
 * for all technical documentation, especially if you are a server
 * or services coder.
 */
CMD_FUNC(m_protoctl)
{
	int  i;
	int first_protoctl = (GotProtoctl(sptr)) ? 0 : 1; /**< First PROTOCTL we receive? Special ;) */
	char proto[512];
	char *name, *value, *p;

	if (!MyConnect(sptr))
		return 0; /* Remote PROTOCTL's are not supported */

	cptr->flags |= FLAGS_PROTOCTL;

	for (i = 1; i < parc; i++)
	{
		strlcpy(proto, parv[i], sizeof proto);
		p = strchr(proto, '=');
		if (p)
		{
			name = proto;
			*p++ = '\0';
			value = p;
		} else {
			name = proto;
			value = NULL;
		}

		if (!strcmp(name, "NAMESX"))
		{
			SetNAMESX(cptr);
		}
		else if (!strcmp(name, "UHNAMES") && UHNAMES_ENABLED)
		{
			SetUHNAMES(cptr);
		}
		else if (!strcmp(name, "NOQUIT"))
		{
			SetNoQuit(cptr);
		}
		else if (!strcmp(name, "SJOIN"))
		{
			SetSJOIN(cptr);
		}
		else if (!strcmp(name, "SJOIN2"))
		{
			SetSJOIN2(cptr);
		}
		else if (!strcmp(name, "NICKv2"))
		{
			SetNICKv2(cptr);
		}
		else if (!strcmp(name, "UMODE2"))
		{
			SetUMODE2(cptr);
		}
		else if (!strcmp(name, "VL"))
		{
			SetVL(cptr);
		}
		else if (!strcmp(name, "VHP"))
		{
			SetVHP(cptr);
		}
		else if (!strcmp(name, "CLK"))
		{
			SetCLK(cptr);
		}
		else if (!strcmp(name, "SJ3"))
		{
			SetSJ3(cptr);
		}
		else if (!strcmp(name, "SJSBY") && iConf.ban_setter_sync)
		{
			SetSJSBY(cptr);
		}
		else if (!strcmp(name, "TKLEXT"))
		{
			SetTKLEXT(cptr);
		}
		else if (!strcmp(name, "TKLEXT2"))
		{
			SetTKLEXT2(cptr);
			SetTKLEXT(cptr); /* TKLEXT is implied as well. always. */
		}
		else if (!strcmp(name, "NICKIP"))
		{
			cptr->local->proto |= PROTO_NICKIP;
		}
		else if (!strcmp(name, "NICKCHARS") && value)
		{
			if (!IsServer(cptr) && !IsEAuth(cptr) && !IsHandshake(cptr))
				continue;
			/* Ok, server is either authenticated, or is an outgoing connect... */
			/* Some combinations are fatal because they would lead to mass-kills:
			 * - use of 'utf8' on our server but not on theirs
			 */
			if (strstr(charsys_get_current_languages(), "utf8") && !strstr(value, "utf8"))
			{
				char buf[512];
				snprintf(buf, sizeof(buf), "Server %s has utf8 in set::allowed-nickchars but %s does not. Link rejected.",
					me.name, *sptr->name ? sptr->name : "other side");
				sendto_realops("\002ERROR\001 %s", buf);
				return exit_client(cptr, sptr, &me, buf);
			}
			/* We compare the character sets to see if we should warn opers about any mismatch... */
			if (strcmp(value, charsys_get_current_languages()))
			{
				sendto_realops("\002WARNING!!!!\002 Link %s does not have the same set::allowed-nickchars settings (or is "
							"a different UnrealIRCd version), this MAY cause display issues. Our charset: '%s', theirs: '%s'",
					get_client_name(cptr, FALSE), charsys_get_current_languages(), value);
				/* return exit_client(cptr, cptr, &me, "Nick charset mismatch"); */
			}
			if (cptr->serv)
				safestrdup(cptr->serv->features.nickchars, value);

			/* If this is a runtime change (so post-handshake): */
			if (IsServer(sptr))
				broadcast_sinfo(sptr, NULL, cptr);
		}
		else if (!strcmp(name, "SID") && value)
		{
			aClient *acptr;
			char *sid = value;

			if (!IsServer(cptr) && !IsEAuth(cptr) && !IsHandshake(cptr))
				return exit_client(cptr, cptr, &me, "Got PROTOCTL SID before EAUTH, that's the wrong order!");

			if (*sptr->id && (strlen(sptr->id)==3))
				return exit_client(cptr, cptr, &me, "Got PROTOCTL SID twice");

			if ((acptr = hash_find_id(sid, NULL)) != NULL)
			{
				sendto_one(sptr, "ERROR :SID %s already exists from %s", acptr->id, acptr->name);
				sendto_snomask(SNO_SNOTICE, "Link %s rejected - SID %s already exists from %s",
						get_client_name(cptr, FALSE), acptr->id, acptr->name);
				return exit_client(cptr, cptr, &me, "SID collision");
			}

			if (*sptr->id)
				del_from_id_hash_table(sptr->id, sptr); /* delete old UID entry (created on connect) */
			strlcpy(cptr->id, sid, IDLEN);
			add_to_id_hash_table(cptr->id, cptr); /* add SID */
			cptr->local->proto |= PROTO_SID;
		}
		else if (!strcmp(name, "EAUTH") && value && NEW_LINKING_PROTOCOL)
		{
			/* Early authorization: EAUTH=servername,protocol,flags,software
			 * (Only servername is mandatory, rest is optional)
			 */
			int ret;
			char *p;
			char *servername = NULL, *protocol = NULL, *flags = NULL, *software = NULL;
			char buf[512];
			ConfigItem_link *aconf = NULL;

			strlcpy(buf, value, sizeof(buf));
			p = strchr(buf, ' ');
			if (p)
			{
				*p = '\0';
				p = NULL;
			}
			
			servername = strtoken(&p, buf, ",");
			if (!servername || (strlen(servername) > HOSTLEN) || !index(servername, '.'))
			{
				sendto_one(sptr, "ERROR :Bogus server name in EAUTH (%s)", servername ? servername : "");
				sendto_snomask
				    (SNO_JUNK,
				    "WARNING: Bogus server name (%s) from %s in EAUTH (maybe just a fishy client)",
				    servername ? servername : "", get_client_name(cptr, TRUE));

				return exit_client(cptr, sptr, &me, "Bogus server name");
			}
			
			
			protocol = strtoken(&p, NULL, ",");
			if (protocol)
			{
				flags = strtoken(&p, NULL, ",");
				if (flags)
				{
					software = strtoken(&p, NULL, ",");
				}
			}
			
			ret = verify_link(cptr, sptr, servername, &aconf);
			if (ret < 0)
				return ret; /* FLUSH_BUFFER */

			/* note: software, protocol and flags may be NULL */
			ret = check_deny_version(sptr, software, protocol ? atoi(protocol) : 0, flags);
			if (ret < 0)
				return ret; /* FLUSH_BUFFER */

			SetEAuth(cptr);
			make_server(cptr); /* allocate and set cptr->serv */
			/* Set cptr->name but don't add to hash list. The real work on
			 * that is done in m_server. We just set it here for display
			 * purposes of error messages (such as reject due to clock).
			 */
			strlcpy(cptr->name, servername, sizeof(cptr->name));
			if (protocol)
				cptr->serv->features.protocol = atoi(protocol);
			if (software)
				cptr->serv->features.software = strdup(software);
			if (!IsHandshake(cptr) && aconf) /* Send PASS early... */
				sendto_one(sptr, "PASS :%s", (aconf->auth->type == AUTHTYPE_PLAINTEXT) ? aconf->auth->data : "*");
		}
		else if (!strcmp(name, "SERVERS") && value && NEW_LINKING_PROTOCOL)
		{
			aClient *acptr, *srv;
			char *sid = NULL;
			
			if (!IsEAuth(cptr))
				continue;
				
			if (cptr->serv->features.protocol < 2351)
				continue; /* old SERVERS= version */
			
			/* Other side lets us know which servers are behind it.
			 * SERVERS=<sid-of-server-1>[,<sid-of-server-2[,..etc..]]
			 * Eg: SERVER=001,002,0AB,004,005
			 */

			add_pending_net(sptr, value);

			acptr = find_non_pending_net_duplicates(sptr);
			if (acptr)
			{
				sendto_one(sptr, "ERROR :Server with SID %s (%s) already exists",
					acptr->id, acptr->name);
				sendto_realops("Link %s cancelled, server with SID %s (%s) already exists",
					get_client_name(acptr, TRUE), acptr->id, acptr->name);
				return exit_client(sptr, sptr, sptr, "Server Exists (or non-unique me::sid)");
			}
			
			acptr = find_pending_net_duplicates(sptr, &srv, &sid);
			if (acptr)
			{
				sendto_one(sptr, "ERROR :Server with SID %s is being introduced by another server as well. "
				                 "Just wait a moment for it to synchronize...", sid);
				sendto_realops("Link %s cancelled, server would introduce server with SID %s, which "
				               "server %s is also about to introduce. Just wait a moment for it to synchronize...",
				               get_client_name(acptr, TRUE), sid, get_client_name(srv, TRUE));
				return exit_client(sptr, sptr, sptr, "Server Exists (just wait a moment)");
			}

			/* Send our PROTOCTL SERVERS= back if this was NOT a response */
			if (*value != '*')
				send_protoctl_servers(sptr, 1);
		}
		else if (!strcmp(name, "TS") && value && (IsServer(sptr) || IsEAuth(sptr)))
		{
			long t = atol(value);
			char msg[512], linkerr[512];
			
			if (t < 10000)
				continue; /* ignore */
			
			*msg = *linkerr = '\0';
			
			if ((TStime() - t) > MAX_SERVER_TIME_OFFSET)
			{
				snprintf(linkerr, sizeof(linkerr), "Your clock is %ld seconds behind my clock. Please verify both your clock and mine, fix it and try linking again.", TStime() - t);
				snprintf(msg, sizeof(msg), "Rejecting link %s: our clock is %ld seconds ahead. Correct time is very important in IRC. Please verify the clock on both %s (them) and %s (us), fix it and then try linking again",
					get_client_name(cptr, TRUE), TStime() - t, sptr->name, me.name);
			} else
			if ((t - TStime()) > MAX_SERVER_TIME_OFFSET)
			{
				snprintf(linkerr, sizeof(linkerr), "Your clock is %ld seconds ahead of my clock. Please verify both your clock and mine, fix it, and try linking again.", t - TStime());
				snprintf(msg, sizeof(msg), "Rejecting link %s: our clock is %ld seconds behind. Correct time is very important in IRC. Please verify the clock on both %s (them) and %s (us), fix it and then try linking again",
					get_client_name(cptr, TRUE), t - TStime(), sptr->name, me.name);
			}
			
			if (*msg)
			{
				sendto_realops("%s", msg);
				ircd_log(LOG_ERROR, "%s", msg);
				return exit_client(sptr, sptr, sptr, linkerr);
			}
		}
		else if (!strcmp(name, "MLOCK"))
		{
			cptr->local->proto |= PROTO_MLOCK;
		}
		else if (!strcmp(name, "CHANMODES") && value && sptr->serv)
		{
			parse_chanmodes_protoctl(sptr, value);
			/* If this is a runtime change (so post-handshake): */
			if (IsServer(sptr))
				broadcast_sinfo(sptr, NULL, cptr);
		}
		else if (!strcmp(name, "USERMODES") && value && sptr->serv)
		{
			safestrdup(sptr->serv->features.usermodes, value);
			/* If this is a runtime change (so post-handshake): */
			if (IsServer(sptr))
				broadcast_sinfo(sptr, NULL, cptr);
		}
		else if (!strcmp(name, "BOOTED") && value && sptr->serv)
		{
			sptr->serv->boottime = atol(value);
		}
		else if (!strcmp(name, "EXTSWHOIS"))
		{
			cptr->local->proto |= PROTO_EXTSWHOIS;
		}
		/* You can add protocol extensions here.
		 * Use 'name' and 'value' (the latter may be NULL).
		 *
		 * DO NOT error or warn on unknown proto; we just don't
		 * support it.
		 */
	}

	if (first_protoctl && IsHandshake(cptr) && sptr->serv && !IsServerSent(sptr)) /* first & outgoing connection to server */
	{
		/* SERVER message moved from completed_connection() to here due to EAUTH/SERVERS PROTOCTL stuff,
		 * which needed to be delayed until after both sides have received SERVERS=xx (..or not.. in case
		 * of older servers).
		 * Actually, this is often not reached, as the PANGPANG stuff in numeric.c is reached first.
		 */
		send_server_message(cptr);
	}

	return 0;
}
