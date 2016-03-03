/* freediag
 *
 * Configurable items code
 *
 * (c) fenugrec 2015
 * GPLv3
 *
 */


#include "diag.h"
#include "diag_cfg.h"
#include "diag_err.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char tty_descr[]="Serial/tty port";
static const char tty_sn[]="port";
static const char bps_descr[]="Speed(bps)";
static const char bps_sn[]="spd";

/* top decls */
void optarray_clear(struct cfgi *cfgp);


//Optional func to refresh opt[] and numopts (for tty, J2534, etc), doesn't change *val
void diag_cfg_refresh(struct cfgi *cfgp) {
	if (cfgp->refresh) cfgp->refresh(cfgp);
	return;
}

//Optional: func to reset *val to default; doesn't call refresh()
void diag_cfg_reset(struct cfgi *cfgp) {
	if (cfgp->reset) cfgp->reset(cfgp);
	return;
}

//set config value for a CFGT_STR param, copying contents of *str. Ret 0 if ok
int diag_cfg_setstr(struct cfgi *cfgp, const char *str) {
	if (cfgp->type == CFGT_STR) {
		size_t slen=strlen(str);
		if (cfgp->dyn_val && (cfgp->val.str != NULL)) {
			free(cfgp->val.str);
			cfgp->val.str = NULL;
		}
		if (diag_malloc(&cfgp->val.str, slen+1)) {
			return diag_iseterr(DIAG_ERR_NOMEM);
		}
		cfgp->dyn_val = 1;	//need to free
		strncpy(cfgp->val.str, str, slen+1);
		return 0;
	}
	return diag_iseterr(DIAG_ERR_BADCFG);
}

//set config value for a BOOL param
int diag_cfg_setbool(struct cfgi *cfgp, bool val) {
	if (cfgp->type == CFGT_BOOL) {
		cfgp->val.b = val;
		return 0;
	}
	return diag_iseterr(DIAG_ERR_BADCFG);
}

//
int diag_cfg_setu8(struct cfgi *cfgp, uint8_t val) {
	if (cfgp->type == CFGT_U8) {
		cfgp->val.u8 = val;
		return 0;
	}
	return diag_iseterr(DIAG_ERR_BADCFG);
}

int diag_cfg_setint(struct cfgi *cfgp, int val) {
	if (cfgp->type == CFGT_INT) {
		cfgp->val.i = val;
		return 0;
	}
	return diag_iseterr(DIAG_ERR_BADCFG);
}

//set config value to one of the predefined options. Ret 0 if ok
int diag_cfg_setopt(struct cfgi *cfgp, int optid) {
	if (optid > (cfgp->numopts - 1)) {
		return diag_iseterr(DIAG_ERR_BADCFG);
	}
	switch (cfgp->type) {
	case CFGT_STR:
		if (cfgp->opt[optid] == NULL) {
			return diag_iseterr(DIAG_ERR_BADCFG);
		}
		diag_cfg_setstr(cfgp, cfgp->opt[optid]);
		break;

	case CFGT_INT:
		cfgp->val.i = optid;
		break;
	case CFGT_U8:	//these don't really make sense
	case CFGT_BOOL:
	default:
		return diag_iseterr(DIAG_ERR_BADCFG);
		break;
	}
	return 0;
}

//directly set param value (caller knows correct type and handles mem management, etc) BAD
//void diag_cfg_setraw(struct cfgi *cfgp, void *val) {}

//get param value, as new string to be free'd by caller.
//for u8 / int types, sprintf with %X and %d formatters respectively
char * diag_cfg_getstr(struct cfgi *cfgp) {
	char *str;
	const char *fmt;
	size_t len;
	switch (cfgp->type) {
	case CFGT_U8:
		len=5;
		fmt="0x%02X";
		break;
	case CFGT_INT:
		//handle 5 digits.
		len=6;
		fmt="%5d";
		break;
	case CFGT_STR:
		len=strlen(cfgp->val.str)+1;
		fmt="%s";
		break;
	default:
		return diag_pseterr(DIAG_ERR_BADCFG);
		break;
	}

	if (diag_malloc(&str, len)) {
		return diag_pseterr(DIAG_ERR_NOMEM);
	}

	snprintf(str, len, fmt, cfgp->val.str);
	return str;
}

//free contents of *cfgp (prior to free'ing the struct itself, for instance)
void diag_cfg_clear(struct cfgi *cfgp) {
	/* For now, handles only CFGT_STR types */
	if (cfgp->type != CFGT_STR) return;
	if (cfgp->dyn_val && (cfgp->val.str != NULL)) {
		free(cfgp->val.str);
	}
	cfgp->dyn_val = 0;
	cfgp->val.str=NULL;

	optarray_clear(cfgp);

	if (cfgp->dyn_dval && (cfgp->dval.str != NULL)) {
		free(cfgp->dval.str);
	}
	cfgp->dyn_dval = 0;
	cfgp->dval.str=NULL;
}


/*** struct management funcs ***/

//clear / free ->opt[] array
void optarray_clear(struct cfgi *cfgp) {
	if (cfgp->dyn_opt && (cfgp->opt != NULL)) {
		/* Need to free every string, and the array of string ptrs */
		for (int i=0; i < cfgp->numopts; i++) {
			if (cfgp->opt[i] != NULL) {
				free(cfgp->opt[i]);
			}
		}
		free(cfgp->opt);
	}
	cfgp->dyn_opt = 0;
	cfgp->opt=NULL;
}

//stock reset() function
void std_reset(struct cfgi *cfgp) {
	switch (cfgp->type) {
	case CFGT_U8:
		cfgp->val.b = cfgp->dval.b;
	case CFGT_INT:
		cfgp->val.i = cfgp->dval.i;
		break;
	case CFGT_STR:
		if (cfgp->dval.str == NULL)
			return;

		if (cfgp->dyn_val && (cfgp->val.str != NULL)) {
			free(cfgp->val.str);
		}

		cfgp->val.str = cfgp->dval.str;
		cfgp->dyn_val = 0;	//don't free val, dval will be free'd
		break;
	case CFGT_BOOL:
		cfgp->val.b = cfgp->dval.b;
		break;
	default:
		break;
	}
}


/** serial port **/
void tty_refresh(struct cfgi *cfgp) {
	//TODO : call diag_tty_find()
	if (cfgp->numopts > 0)
		optarray_clear(cfgp);
	cfgp->numopts = 0;

	if (cfgp->val.str == cfgp->dval.str) {
		//don't free val; alloc new copy
		if (diag_malloc(&cfgp->val.str, strlen(cfgp->dval.str)+1)) {
			return;
		}
		strcpy(cfgp->val.str, cfgp->dval.str);	//we just used strlen; strcpy is just as dangerous...
	}
	if (cfgp->dyn_dval && (cfgp->dval.str != NULL)) {
		free(cfgp->dval.str);
	}
	//XXX populate opt[], numopts, and dval
	cfgp->dyn_dval = 0;
	cfgp->dval.str=NULL;	//will depend on tty_find() output
	return;
}

//new TTY / serial port config item
int diag_cfgn_tty(struct cfgi *cfgp) {
	//TODO : implement+call diag_tty_find()
	if (diag_cfgn_str(cfgp, "/dev/null", tty_descr, tty_sn))	//XXX fill in default str
		return DIAG_ERR_GENERAL;

	cfgp->numopts = 0;	//depending on tty_find()
	cfgp->opt=NULL;

	cfgp->refresh = &tty_refresh;
	return 0;
}

/** serial link speed **/

//serial link speed; uses caller's &val for actual parameter
int diag_cfgn_bps(struct cfgi *cfgp, int val, int def) {
	if (diag_cfgn_int(cfgp, val, def))	//start with standard int config
		return DIAG_ERR_GENERAL;

	cfgp->descr = bps_descr;
	cfgp->shortname = bps_sn;

	return 0;
}

/** generic types **/

//ordinary int param using caller's val, and def as default value for reset().
//Doesn't fill descr and shortname
int diag_cfgn_int(struct cfgi *cfgp, int val, int def) {
	cfgp->dyn_val = 0;	//caller-supplied
	cfgp->dyn_dval = 0;
	cfgp->type = CFGT_INT;
	cfgp->numopts=0;
	cfgp->dval.i = def;
	cfgp->val.i = val;
	cfgp->refresh = NULL;
	cfgp->reset = &std_reset;
	return 0;
}

//ordinary u8 param (copy of _int code) using caller's &val, and *dev as default value for reset().
//Doesn't fill descr and shortname
int diag_cfgn_u8(struct cfgi *cfgp, uint8_t val, uint8_t def) {
	cfgp->dyn_val = 0;	//managed by caller
	cfgp->dyn_dval = 0;
	cfgp->type = CFGT_U8;
	cfgp->numopts=0;
	cfgp->val.u8 = val;
	cfgp->dval.u8 = def;
	cfgp->refresh = NULL;
	cfgp->reset = &std_reset;
	return 0;
}

//ordinary bool (copy of _int code)
int diag_cfgn_bool(struct cfgi *cfgp, bool val, bool def) {
	cfgp->dyn_val = 0;	//managed by caller
	cfgp->dyn_dval = 0;
	cfgp->type = CFGT_BOOL;
	cfgp->numopts=0;
	cfgp->val.b = val;
	cfgp->dval.b = def;
	cfgp->refresh = NULL;
	cfgp->reset = &std_reset;
	return 0;
}

//ordinary string, copies *def for its default value; sets descr and shortname ptrs
int diag_cfgn_str(struct cfgi *cfgp, const char *def, const char *descr, const char *sn) {
	char *dval;

	assert(def && descr && sn);

	cfgp->type = CFGT_STR;
	if (diag_malloc(&dval, strlen(def)+1))
		return diag_iseterr(DIAG_ERR_NOMEM);
	cfgp->dval.str = dval;
	cfgp->dyn_dval = 1;
	strcpy(dval, def);	//danger

	cfgp->val.str = dval;
	cfgp->dyn_val = 0;

	cfgp->descr = descr;
	cfgp->shortname = sn;

	cfgp->refresh = NULL;
	cfgp->reset = &std_reset;
	return 0;
}

