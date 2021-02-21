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
#include "diag_tty.h"	//for diag_tty_getportlist()

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char tty_descr[]="Serial/tty port, such as \"/dev/ttyS0\" or \"\\\\.\\COM11\"";
static const char tty_sn[]="port";		/** tty cfg shortname */
static const char tty_def[]="/dev/null";	/** last resort fallback */

/* top decls */
void optarray_clear(struct cfgi *cfgp);


//Optional func to refresh opt[] and numopts (for tty, J2534, etc), doesn't change *val
void diag_cfg_refresh(struct cfgi *cfgp) {
	if (cfgp->refresh) {
		cfgp->refresh(cfgp);
	}
	return;
}

//Optional: func to reset *val to default; doesn't call refresh()
void diag_cfg_reset(struct cfgi *cfgp) {
	if (cfgp->reset) {
		cfgp->reset(cfgp);
	}
	return;
}


int diag_cfg_setstr(struct cfgi *cfgp, const char *str) {
	size_t slen;
	int rv;

	if (cfgp->type != CFGT_STR) {
		return diag_iseterr(DIAG_ERR_BADCFG);
	}

	slen=strlen(str);
	if (cfgp->dyn_val && (cfgp->val.str != NULL)) {
		free(cfgp->val.str);
		cfgp->val.str = NULL;
	}
	rv = diag_malloc(&cfgp->val.str, slen+1);
	if (rv != 0) {
		return diag_ifwderr(rv);
	}
	cfgp->dyn_val = 1;	//need to free
	strcpy(cfgp->val.str, str);
	return 0;

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
		break;
	default:
		assert(0);
		break;
	}
	return 0;
}

//directly set param value (caller knows correct type and handles mem management, etc) BAD
//void diag_cfg_setraw(struct cfgi *cfgp, void *val) {}

//get param value, as new string to be free'd by caller.
//for u8 / int types, sprintf with %X and %d formatters respectively
char *diag_cfg_getstr(struct cfgi *cfgp) {
	char *str;
	size_t len = 0;
	int rv;

	/* determine required length first */
	switch (cfgp->type) {
	case CFGT_U8:
		len=5;
		break;
	case CFGT_INT:
		//handle 7 digits.
		len=8;
		break;
	case CFGT_STR:
		len=strlen(cfgp->val.str)+1;
		break;
	default:
		assert(0);
		break;
	}

	rv = diag_malloc(&str, len);
	if (rv != 0) {
		return diag_pfwderr(rv);
	}

	/* fill str */
	switch (cfgp->type) {
	case CFGT_U8:
		snprintf(str, len, "0x%02X", (unsigned) cfgp->val.u8);
		break;
	case CFGT_INT:
		snprintf(str, len, "%7d", cfgp->val.i);
		break;
	case CFGT_STR:
		memcpy(str, cfgp->val.str, len);
		break;
	default:
		assert(0);
		break;
	}

	return str;
}

//free contents of *cfgp (prior to free'ing the struct itself, for instance)
void diag_cfg_clear(struct cfgi *cfgp) {
	/* For now, handles only CFGT_STR types */
	if (cfgp->type != CFGT_STR) {
		return;
	}
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
		strlist_free(cfgp->opt, cfgp->numopts);
	}
	cfgp->dyn_opt = 0;
	cfgp->opt=NULL;
	cfgp->numopts = 0;
}

//stock reset() function
void std_reset(struct cfgi *cfgp) {
	switch (cfgp->type) {
	case CFGT_U8:
		cfgp->val.b = cfgp->dval.b;
		break;
	case CFGT_INT:
		cfgp->val.i = cfgp->dval.i;
		break;
	case CFGT_STR:
		if (cfgp->dval.str == NULL) {
			return;
		}
		if (cfgp->dyn_val && (cfgp->val.str != NULL)) {
			free(cfgp->val.str);
			cfgp->val.str = NULL;
		}
		diag_cfg_setstr(cfgp, cfgp->dval.str);
		break;
	case CFGT_BOOL:
		cfgp->val.b = cfgp->dval.b;
		break;
	default:
		break;
	}
}


/** Refresh list of known ports
 *
 * Keep current port; update default.
 * If no ports are found, changes nothing
 */
void tty_refresh(struct cfgi *cfgp) {

	optarray_clear(cfgp);

	cfgp->opt = diag_tty_getportlist(&cfgp->numopts);

	if (cfgp->numopts == 0) {
		/* no ports found : change nothing */
		return;
	}

	/* Update default port */
	cfgp->dyn_dval = 1;
	if (diag_malloc(&cfgp->dval.str, strlen(cfgp->opt[0])+1)) {
		optarray_clear(cfgp);
		return;
	}
	strcpy(cfgp->dval.str, cfgp->opt[0]);	//we just used strlen; strcpy is just as dangerous...
	cfgp->dyn_opt = 1;

	return;
}

//new TTY / serial port config item
int diag_cfgn_tty(struct cfgi *cfgp) {
	int rv = diag_cfgn_str(cfgp, tty_def, tty_descr, tty_sn);
	if (rv != 0) {
		return rv;
	}

	cfgp->refresh = &tty_refresh;
	std_reset(cfgp);

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
	char *val, *dval;
	int rv;

	assert(def && descr && sn);

	cfgp->type = CFGT_STR;
	rv = diag_malloc(&dval, strlen(def)+1);
	if (rv != 0) {
		return diag_ifwderr(rv);
	}
	rv = diag_malloc(&val, strlen(def)+1);
	if (rv != 0) {
		free(dval);
		return diag_ifwderr(rv);
	}

	cfgp->dval.str = dval;
	cfgp->dyn_dval = 1;
	strcpy(dval, def);	//danger

	cfgp->val.str = val;
	cfgp->dyn_val = 1;
	strcpy(val, def);	//danger

	cfgp->descr = descr;
	cfgp->shortname = sn;

	cfgp->refresh = NULL;
	cfgp->reset = &std_reset;
	return 0;
}

