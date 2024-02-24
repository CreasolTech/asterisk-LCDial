/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Least Cost and Failsafe Routing application
 * 
 * Copyright (C) 2005, Wolfgang Pichler (wp@commoveo.com)
 * Modified by Paolo Subiaco (psubiaco@creasol.it)
 *
 * THIS IS JUST A BACKUP OF app_lcdial.c THAT WORKS WITH 1.8.xx version of asterisk
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
	<support_level>addons</support_level>
***/

//#define ASTERISK_VERSION_LCDIAL 10800 //asterisk version: this is for asterisk 10 or below
//#define ASTERISK_VERSION_LCDIAL 130000  //110000 or above for asterisk 11 or above
#define ASTERISK_VERSION_LCDIAL 160100  //110000 or above for asterisk 11 or above
#define AST_MODULE_SELF_SYM __app_lcdial_self
#define AST_MODULE "app_lcdial"
#include <asterisk.h>

#if ASTERISK_VERSION_LCDIAL < 110000
	/* Asterisk version 1.8 or less */
	#include <asterisk/version.h>
	ASTERISK_FILE_VERSION(__FILE__, "$Revision: 20170607 $");
#elif ASTERISK_VERSION_LCDIAL < 16000
	/* Asterisk 11 or above */
	#include <asterisk/ast_version.h>
	#define ASTERISK_VERSION_NUM 140000	//write your asterisk version here: 14.1.2 => 140102
#else 
	/* Asterisk 16 or above */
	#include <asterisk/ast_version.h>
	#define ASTERISK_VERSION_NUM 160100	//write your asterisk version here: 16.1.2 => 160102
#endif



#include <asterisk/module.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/app.h>
#include <asterisk/lock.h>
#include <asterisk/say.h>
#include <asterisk/config.h>
#include <asterisk/translate.h>
#include <asterisk/musiconhold.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>
#include <asterisk/options.h>
#include <mysql/mysql.h>
#include <mysql/errmsg.h>

#define sqlcommandsize 2048
#define DATE_FORMAT "%Y-%m-%d %T"

#if ASTERISK_VERSION_NUM >=160000
	  #undef malloc
	  #define malloc ast_malloc
	  #undef free
	  #define free ast_free
#endif



static char *app = "LCDial";

static char *config = "lcdial.conf";

static char *synopsis = "Least Cost Routing Application";

static char *descrip =
  "This application calculates to provider with the lowest rate for the given destination number, and then it invokes the Dial application to Dial the number. If the cheapest provider is unavaiable - then it triies the next one.\n";

static char *hostname = NULL, *dbname = NULL, *dbuser = NULL, *dbpassword = NULL, *dbport = NULL;
static char *sqlgetproviders = NULL;
static int hostname_alloc = 0, dbname_alloc = 0, dbuser_alloc = 0, dbpassword_alloc = 0, dbport_alloc = 0;
static int sqlgetproviders_alloc = 0;
static int connected = 0; /* Initially not connected to the DB */
struct ast_flags config_flags = { 0 };
	
#define MAX_CALLS		20		// no more than 20 calls
typedef struct {
	int providerid;					// provider ID
	char name[256];					// provider name
	char dialstr[256];			// dial string
	char prefix[256];				// prefix regexp
	float rate;							// rate (cents/minute)
	float mul;							// Multiply factor (to priorityze a provider in respect to others)
	char note[40];					// note
} PROVIDER;

PROVIDER provider[MAX_CALLS];

int sort[MAX_CALLS];			// rate sorting index

static MYSQL mysql = { { NULL }, };

//STANDARD_LOCAL_USER;

//LOCAL_USER_DECL;

// For locking the db handle
ast_mutex_t dblock;

static int db_check_connected(void)
{
	MYSQL *ret;
	if (connected) {
		/* seems that we are connected - but let us check that */
		if (mysql_ping(&mysql)==0) {
			connected = 1;
		} else {
			connected = 0;
		}
	}
	if (connected==0) {
		
		/* We are not connected - so make the initial connection */
		mysql_init(&mysql);
		ret=mysql_real_connect(&mysql, hostname, dbuser, dbpassword, dbname, atoi(dbport), NULL, 0);
		if (!ret)
		{
			ast_log(LOG_ERROR, "LCDial: FATAL - Couldn't connect to database server '%s'. \n", hostname);
			connected = 0;
		} else {
			ast_log(LOG_DEBUG, "LCDial: Successfully connected to database '%s@%s'.\n", dbname, hostname);
			connected = 1;
		}
	}
	return connected;
}

static int lcdial_dial(struct ast_channel *chan, void *data)
{
	struct ast_app *app;
	int ret;

	app = pbx_findapp("Dial");
	if (app) {
		ret = pbx_exec(chan, app, data);
		ast_log(LOG_NOTICE, "LCDial: Dial exited with Return Code: %d\n",ret);
	} else {
		ast_log(LOG_WARNING, "LCDial: Could not find application (Dial)\n");
		ret = -2;
	}
	return ret;
}

// This function loads the provider list
static int getprovider(char *number,int enable) {
	memset(provider,0,sizeof(provider));	// reset structure
	memset(sort,-1,sizeof(sort));					// array used to sort rates
	if (db_check_connected()) {
		MYSQL_RES *result;
		MYSQL_ROW row;
		char query[1024];
		int ret;
		int i=0;
		int j,k;
		float current_rate=-1.0;
		float previous_rate=-1.0;
		int		current_index=0;
		
		
		sprintf(query,sqlgetproviders,enable,number);
		ast_mutex_lock(&dblock);
		if (mysql_query(&mysql,query) != 0) {
			// DB Tech Error
			ast_log(LOG_DEBUG,"LCDial: FATAL DB ERROR AT QUERY: %s - (%s)\n", query,mysql_error(&mysql));
			ast_mutex_unlock(&dblock);
			return 0;
		}
		result = mysql_store_result(&mysql);
		ast_mutex_unlock(&dblock);

		/* Check for Errors */
		if ((!result) || (mysql_num_rows(result)<=0)) {
			// There is no provider configured for this destination
			return 0;
		} else {
			// One or more providers... check them
			for (i=0,j=0;j<MAX_CALLS && i<mysql_num_rows(result);i++) {
				row = mysql_fetch_row(result);
				provider[j].providerid = atoi(row[2]);
				// now verify that the provider ID is not already added to the provider list
				for (k=0;k<j;k++) {
					if (provider[j].providerid==provider[k].providerid) break;
				}
				if (k<j) {
					provider[j].providerid=0;
					continue; // provider already exists in the provider list!
				}
				// provider does not exists: add this provider to the list
				provider[j].rate=atof(row[0]);
				provider[j].mul=atof(row[4]);
				strncpy(provider[j].dialstr,row[1],sizeof(provider[j].dialstr));
				strncpy(provider[j].name,row[3],sizeof(provider[j].name));
				strncpy(provider[j].note,row[5],sizeof(provider[j].note));
				j++;
			}
			ret=j;
			mysql_free_result(result);
			// now sort the array (really, write in sort[] the index of each provider element
			for (i=0;i<ret;i++) {
				current_rate=100000.0;
				for (k=0;k<ret;k++) {
					if (provider[k].rate>=previous_rate && provider[k].rate<current_rate) {
						// check that this element is not already in the provider[] array....
						for (j=0;j<i;j++) {
							if (k==sort[j]) {
								j=-1;	// already present
								break;
							}
						}
						if (j>=0) {
							// element not present
							current_rate=provider[k].rate;
							current_index=k;
						}
					}
				}
				sort[i]=current_index;
				ast_log(LOG_NOTICE, "LCDial:%d r=%.3f p=%s \[%s]\n",i,provider[current_index].rate,provider[current_index].name,provider[current_index].note);
			}
			return ret;
		}
	} else {
		return 0;
	}
}

#if ASTERISK_VERSION_NUM < 10800
static int lcdial_exec(struct ast_channel *chan, void *data)
#else
static int lcdial_exec(struct ast_channel *chan, const char *data)
#endif
{	struct ast_module_user *u;
	char arguments[256], *number, *args, *arg1;
	int enable=1;
	int i,n;
	char dialstr[512];
	int ret;
	const char *dialstatus;
	int priority;
	char unique=0;
	
	//LOCAL_USER_ADD(u);
	u = ast_module_user_add(chan);

	strncpy(arguments, (char *)data, sizeof(arguments) - 1);
	/* Syntax: LCDial([U][ENABLE],number,dial_args)    where 
		 * U is optional and indicate to try the call only one time
		 * ENABLE indicate which path to follow (usable for PBX shared by 2 or more companies)
		 * number is the number to call to
		 * dial_args is the optional arguments for Dial() application
	*/
	arg1 = arguments;	// get the number that should be called
	if (*arg1=='U') {
		// U option specified => make only ONE call, with the best provider
		arg1++;
		unique=1;
	}
	if (*arg1!=',') {
		// Enable specified
		if (sscanf(arg1,"%d",&enable)!=1 || enable<=0 || enable>10) {
			ast_log(LOG_WARNING, "LCDial: third argument invalid: %d. Should be 1, 2, 3, ...",enable);
			enable=1;
		}
	}
	/* 2nd parameter of LCDial: the number to dial */
	number=strchr(arg1, ',');
	args=number;
	if (number) {
		*number='\0';
		number++;
		args=strchr(number,',');
		if (args) {
			// third argument of LCDial exists: Dial() options
			*args='\0';
			args++;
		}
	}
#if ASTERISK_VERSION_NUM >=110000
	priority = ast_channel_priority(chan);
#else 
	priority = chan->priority;
#endif

	// read all providers in one shot
	n=getprovider(number,enable);

	// Get one provider by the next
	if (unique) n=1;
	for (i = 0;i<n;i++) {
		ast_log(LOG_NOTICE, "LCDial: Try #%d via %s, rate=%.4f, dialstr=%s\n",i,provider[sort[i]].name,provider[sort[i]].rate,provider[sort[i]].dialstr);
		sprintf(dialstr,provider[sort[i]].dialstr,number); //dialstr="IAX/provider/NUMBER,30,m"
		sprintf(dialstr,"%s,%s",dialstr, args);
		ret = lcdial_dial(chan, dialstr);
		// Check the return code
		dialstatus = pbx_builtin_getvar_helper(chan, "DIALSTATUS");
		ast_log(LOG_NOTICE, "LCDial: Got Dial Return Code: %d, Got DIALSTATUS: %s\n", ret, dialstatus);
		if ((strncmp(dialstatus,"CHANUNAVAIL",strlen("CHANUNAVAIL"))==0) || (strncmp(dialstatus, "CONGESTION",strlen("CONGESTION"))==0)) {
			ast_log(LOG_NOTICE, "LCDial: Trying the next provider\n");
#if ASTERISK_VERSION_NUM >=110000
			ast_channel_priority_set(chan,priority);
#else
			chan->priority = priority;
#endif
		} else {
			return ret;
		}
	}
	if (i==0) {
		// Do set the variable DIALSTATUS to NOPROVIDER
		pbx_builtin_setvar_helper(chan,"DIALSTATUS","NOPROVIDER");
		ast_log(LOG_WARNING, "LCDial: No Provider available for the destination: %s\n",number );
		// jump to priority n + 101
#if ASTERISK_VERSION_NUM >=110000
		if (ast_exists_extension(chan, ast_channel_context(chan), ast_channel_exten(chan), ast_channel_priority(chan) + 101, ast_channel_caller(chan)->id.number.str)) 
#elif ASTERISK_VERSION_NUM < 10800
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num)) 
#else
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->caller.id.number.str)) 
#endif
		{
#if ASTERISK_VERSION_NUM >=110000
			ast_channel_priority_set(chan, ast_channel_priority(chan)+100);
			ast_log (LOG_DEBUG, "LCDial: Jumping to Priority '%i'.\n", ast_channel_priority(chan));
#else
			chan->priority+=100; 
			ast_log (LOG_DEBUG, "LCDial: Jumping to Priority '%i'.\n", chan->priority);
#endif
		} else {
#if ASTERISK_VERSION_NUM >=110000
			ast_log (LOG_DEBUG, "LCDial: Priority '%i' is not defined.\n", ast_channel_priority(chan)+101);
#else
			ast_log (LOG_DEBUG, "LCDial: Priority '%i' is not defined.\n", chan->priority+101);
#endif
		}

		return 0;
	}	
		
	//LOCAL_USER_REMOVE(u);
	ast_module_user_remove(u);
	return 0;
}

static int unload_module(void) {
	int res;

	mysql_close(&mysql);
	connected = 0;
	if (hostname && hostname_alloc) {
		free(hostname);
		hostname = NULL;
		hostname_alloc = 0;
	}
	if (dbname && dbname_alloc) {
		free(dbname);
		dbname = NULL;
		dbname_alloc = 0;
	}
	if (dbuser && dbuser_alloc) {
		free(dbuser);
		dbuser = NULL;
		dbuser_alloc = 0;
	}
	if (dbpassword && dbpassword_alloc) {
		free(dbpassword);
		dbpassword = NULL;
		dbpassword_alloc = 0;
	}
	if (dbport && dbport_alloc) {
		free(dbport);
		dbport = NULL;
		dbport_alloc = 0;
	}
	if (sqlgetproviders && sqlgetproviders_alloc) {
		free(sqlgetproviders);
		sqlgetproviders = NULL;
		sqlgetproviders_alloc = 0;
	}

	res=ast_unregister_application(app);
	//STANDARD_HANGUP_LOCALUSERS;
	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	int res;
	struct ast_config *cfg;
	struct ast_variable *var;
	const char *tmp;


	cfg = ast_config_load(config,config_flags);

	if (!cfg) {
		ast_log(LOG_WARNING, "LCDial: Unable to load config: %s\n", config);
		return 0;
	}

	var = ast_variable_browse(cfg, "global");
	if (!var) {
		/* nothing configured */
		ast_log(LOG_WARNING, "LCDial: Unable to get values from the global section\n");
		return 0;
	}



	tmp = ast_variable_retrieve(cfg,"global","hostname");
	if (tmp) {
		hostname = malloc(strlen(tmp) + 1);
		if (hostname != NULL) {
			hostname_alloc = 1;
			strcpy(hostname, tmp);
		} else {
			ast_log(LOG_ERROR,"Out of memory error.\n");
			return -1;
		}
	} else {
		ast_log(LOG_WARNING,"LCDial: Database server hostname not specified.  Assuming localhost\n");
		hostname = "localhost";
	}

	tmp = ast_variable_retrieve(cfg,"global","dbname");
	if (tmp) {
		dbname = malloc(strlen(tmp) + 1);
		if (dbname != NULL) {
			dbname_alloc = 1;
			strcpy(dbname, tmp);
		} else {
			ast_log(LOG_ERROR,"Out of memory error.\n");
			return -1;
		}
	} else {
		ast_log(LOG_WARNING,"Database not specified.  Assuming asterisk\n");
		dbname = "asterisk";
	}

	tmp = ast_variable_retrieve(cfg,"global","user");
	if (tmp) {
		dbuser = malloc(strlen(tmp) + 1);
		if (dbuser != NULL) {
			dbuser_alloc = 1;
			strcpy(dbuser, tmp);
		} else {
			ast_log(LOG_ERROR,"Out of memory error.\n");
			return -1;
		}
	} else {
		ast_log(LOG_WARNING,"Database user not specified.  Assuming asterisk\n");
		dbuser = "asterisk";
	}

	tmp = ast_variable_retrieve(cfg,"global","password");
	if (tmp) {
		dbpassword = malloc(strlen(tmp) + 1);
		if (dbpassword != NULL) {
			dbpassword_alloc = 1;
			strcpy(dbpassword, tmp);
		} else {
			ast_log(LOG_ERROR,"Out of memory error.\n");
			return -1;
		}
	} else {
		ast_log(LOG_WARNING,"Database password not specified.  Assuming blank\n");
		dbpassword = "";
	}

	tmp = ast_variable_retrieve(cfg,"global","port");
	if (tmp) {
		dbport = malloc(strlen(tmp) + 1);
		if (dbport != NULL) {
			dbport_alloc = 1;
			strcpy(dbport, tmp);
		} else {
			ast_log(LOG_ERROR,"Out of memory error.\n");
			return -1;
		}
	} else {
		ast_log(LOG_WARNING,"Prepaid database port not specified.  Using 3306.\n");
		dbport = "3306";
	}

	tmp = ast_variable_retrieve(cfg,"sql","getproviders");
	if (tmp) {
		sqlgetproviders = malloc(strlen(tmp) + 1);
		if (sqlgetproviders != NULL) {
			sqlgetproviders_alloc = 1;
			strcpy(sqlgetproviders, tmp);
		} else {
			ast_log(LOG_ERROR,"Out of memory error.\n");
			return -1;
		}
	} else {
		ast_log(LOG_WARNING,"getproviders query not specified.  Using default.\n");
		sqlgetproviders = "SELECT DISTINCT (lcdial_rates.rate+0.000001)*lcdial_providers.ratemul, lcdial_providers.dialstr, lcdial_providers.id, lcdial_providers.provider, lcdial_providers.ratemul, note FROM lcdial_rates LEFT JOIN lcdial_providers ON lcdial_providers.ratename=lcdial_rates.ratename WHERE lcdial_providers.enabled = %d AND '%s' REGEXP prefix ORDER BY length(prefix) DESC, lcdial_rates.rate*lcdial_providers.ratemul ASC";
	}


	ast_config_destroy(cfg);


	ast_log(LOG_DEBUG,"LCDial: got hostname of %s\n",hostname);
	ast_log(LOG_DEBUG,"LCDial: got port of %s\n",dbport);
	ast_log(LOG_DEBUG,"LCDial: got user of %s\n",dbuser);
	ast_log(LOG_DEBUG,"LCDial: got dbname of %s\n",dbname);
	ast_log(LOG_DEBUG,"LCDial: got password of %s\n",dbpassword);
	ast_log(LOG_DEBUG,"LCDial: got getproviders query of '%s'\n",sqlgetproviders);

	db_check_connected();

	res = ast_register_application(app, lcdial_exec, synopsis, descrip);
	if (res) {
		ast_log(LOG_ERROR, "Unable to register Least Cost Dial Application\n");
	}

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Least Cost Routing Application");
