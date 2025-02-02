/*
 * User Mode +G
 * (C) Copyright 2005-current Bram Matthys and The UnrealIRCd team.
 */

#include "unrealircd.h"


ModuleHeader MOD_HEADER(censor)
  = {
	"usermodes/censor",
	"4.2",
	"User Mode +G",
	"3.2-b8-1",
	NULL,
    };


long UMODE_CENSOR = 0L;

#define IsCensored(x) (x->umodes & UMODE_CENSOR)

char *censor_pre_usermsg(aClient *sptr, aClient *target, char *text, int notice);

int censor_config_test(ConfigFile *, ConfigEntry *, int, int *);
int censor_config_run(ConfigFile *, ConfigEntry *, int);

ModuleInfo *ModInfo = NULL;

ConfigItem_badword *conf_badword_message = NULL;

static ConfigItem_badword *copy_badword_struct(ConfigItem_badword *ca, int regex, int regflags);


MOD_TEST(censor)
{
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, censor_config_test);
	return MOD_SUCCESS;
}
	
MOD_INIT(censor)
{
	ModInfo = modinfo;

	MARK_AS_OFFICIAL_MODULE(modinfo);

	UmodeAdd(modinfo->handle, 'G', UMODE_GLOBAL, 0, NULL, &UMODE_CENSOR);

	HookAddPChar(modinfo->handle, HOOKTYPE_PRE_USERMSG, 0, censor_pre_usermsg);
	
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, censor_config_run);
	return MOD_SUCCESS;
}

MOD_LOAD(censor)
{
	return MOD_SUCCESS;
}


MOD_UNLOAD(censor)
{
ConfigItem_badword *badword, *next;

	for (badword = conf_badword_message; badword; badword = next)
	{
		next = badword->next;
		DelListItem(badword, conf_badword_message);
		badword_config_free(badword);
	}
	return MOD_SUCCESS;
}

int censor_config_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
	ConfigEntry *cep;
	char has_word = 0, has_replace = 0, has_action = 0, action = 'r';

	if (type != CONFIG_MAIN)
		return 0;
	
	if (!ce || !ce->ce_varname || strcmp(ce->ce_varname, "badword"))
		return 0; /* not interested */

	if (!ce->ce_vardata)
	{
		config_error("%s:%i: badword without type",
			ce->ce_fileptr->cf_filename, ce->ce_varlinenum);
		return 1;
	}
	else if (strcmp(ce->ce_vardata, "message") && strcmp(ce->ce_vardata, "all")) {
/*			config_error("%s:%i: badword with unknown type",
				ce->ce_fileptr->cf_filename, ce->ce_varlinenum); -- can't do that.. */
		return 0; /* unhandled */
	}
	for (cep = ce->ce_entries; cep; cep = cep->ce_next)
	{
		if (config_is_blankorempty(cep, "badword"))
		{
			errors++;
			continue;
		}
		if (!strcmp(cep->ce_varname, "word"))
		{
			char *errbuf;
			if (has_word)
			{
				config_warn_duplicate(cep->ce_fileptr->cf_filename, 
					cep->ce_varlinenum, "badword::word");
				continue;
			}
			has_word = 1;
			if ((errbuf = badword_config_check_regex(cep->ce_vardata,1,1)))
			{
				config_error("%s:%i: badword::%s contains an invalid regex: %s",
					cep->ce_fileptr->cf_filename,
					cep->ce_varlinenum,
					cep->ce_varname, errbuf);
				errors++;
			}
		}
		else if (!strcmp(cep->ce_varname, "replace"))
		{
			if (has_replace)
			{
				config_warn_duplicate(cep->ce_fileptr->cf_filename, 
					cep->ce_varlinenum, "badword::replace");
				continue;
			}
			has_replace = 1;
		}
		else if (!strcmp(cep->ce_varname, "action"))
		{
			if (has_action)
			{
				config_warn_duplicate(cep->ce_fileptr->cf_filename, 
					cep->ce_varlinenum, "badword::action");
				continue;
			}
			has_action = 1;
			if (!strcmp(cep->ce_vardata, "replace"))
				action = 'r';
			else if (!strcmp(cep->ce_vardata, "block"))
				action = 'b';
			else
			{
				config_error("%s:%d: Unknown badword::action '%s'",
					cep->ce_fileptr->cf_filename, cep->ce_varlinenum,
					cep->ce_vardata);
				errors++;
			}
				
		}
		else
		{
			config_error_unknown(cep->ce_fileptr->cf_filename, cep->ce_varlinenum,
				"badword", cep->ce_varname);
			errors++;
		}
	}

	if (!has_word)
	{
		config_error_missing(ce->ce_fileptr->cf_filename, ce->ce_varlinenum,
			"badword::word");
		errors++;
	}
	if (has_action)
	{
		if (has_replace && action == 'b')
		{
			config_error("%s:%i: badword::action is block but badword::replace exists",
				ce->ce_fileptr->cf_filename, ce->ce_varlinenum);
			errors++;
		}
	}
	
	*errs = errors;
	return errors ? -1 : 1;
}


int censor_config_run(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep, *word = NULL;
	ConfigItem_badword *ca;

	if (type != CONFIG_MAIN)
		return 0;
	
	if (!ce || !ce->ce_varname || strcmp(ce->ce_varname, "badword"))
		return 0; /* not interested */

	if (strcmp(ce->ce_vardata, "message") && strcmp(ce->ce_vardata, "all"))
	        return 0; /* not for us */

	ca = MyMallocEx(sizeof(ConfigItem_badword));
	ca->action = BADWORD_REPLACE;

	for (cep = ce->ce_entries; cep; cep = cep->ce_next)
	{
		if (!strcmp(cep->ce_varname, "action"))
		{
			if (!strcmp(cep->ce_vardata, "block"))
			{
				ca->action = BADWORD_BLOCK;
			}
		}
		else if (!strcmp(cep->ce_varname, "replace"))
		{
			safestrdup(ca->replace, cep->ce_vardata);
		}
		else if (!strcmp(cep->ce_varname, "word"))
		{
			word = cep;
		}
	}

	badword_config_process(ca, word->ce_vardata);

	if (!strcmp(ce->ce_vardata, "message"))
	{
		AddListItem(ca, conf_badword_message);
	} else
	if (!strcmp(ce->ce_vardata, "all"))
	{
		AddListItem(ca, conf_badword_message);
		return 0; /* pretend we didn't see it, so other modules can handle 'all' as well */
	}

	return 1;
}

char *stripbadwords_message(char *str, int *blocked)
{
	return stripbadwords(str, conf_badword_message, blocked);
}

char *censor_pre_usermsg(aClient *sptr, aClient *target, char *text, int notice)
{
int blocked;

	if (MyClient(sptr) && IsCensored(target))
	{
		text = stripbadwords_message(text, &blocked);
		if (blocked)
		{
			if (!notice)
				sendto_one(sptr, rpl_str(ERR_NOSWEAR),  me.name, sptr->name, target->name);
			return NULL;
		}
	}

	return text;
}

// TODO: when stats is modular, make it call this for badwords
int stats_badwords(aClient *sptr, char *para)
{
	ConfigItem_badword *words;

	for (words = conf_badword_message; words; words = words->next)
	{
		sendto_one(sptr, ":%s %i %s :m %c %s%s%s %s",
		           me.name, RPL_TEXT, sptr->name, words->type & BADW_TYPE_REGEX ? 'R' : 'F',
		           (words->type & BADW_TYPE_FAST_L) ? "*" : "", words->word,
		           (words->type & BADW_TYPE_FAST_R) ? "*" : "",
		           words->action == BADWORD_REPLACE ? (words->replace ? words->replace : "<censored>") : "");
	}
	return 0;
}
