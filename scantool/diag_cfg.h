#ifndef DIAG_CFG_H
#define DIAG_CFG_H

/* freediag
 * API for configuring L0 devices
 *
 * (c) fenugrec 2015
 *
 *
 */

#include <stdint.h>
#include <stdbool.h>

/* This struct describes one configurable param,
 * including description,type, *value, etc.
 *
 * Typically an L0 driver will alloc an array of (struct l0cfg_param) and fill appropriately, for example
 * struct *l0cfg_param[]={&cfg_port, &cfg_speed, &cfg_fastbreak, &cfg_initmode,...}
 */
struct l0cfg_param {
	const char *descr;		//description; not mallocd
	const char *shortname;		//for CLI use, must be unique in any array of l0cfg_param. Not mallocd.
	int type;		//indicate type of *val
		#define CFGT_U8		1	//uint8_t, *val is a static buf
		#define CFGT_INT	2	//int, *val is a static buf
		#define CFGT_STR	3	//const char *; generic string; re-alloc/manage *val every time
		//#define CFGT_TTY	4	//const char *; special string ?
		//#define CFGT_ENUM	5	//redundant with ((type==CFGT_INT) && (numopts >0) )?
		#define CFGT_BOOL	6
	void *val;		//actual param
	bool dyn_val;	//if *val must be free'd

	int numopts;		//if > 0 : number of predefined string options / enum values. If ==0 : value set directly.
	char **opt;	//description for each predefined option, i.e. numopts==1 means
							//*opt[0]=="option_id 0 descr", etc.
							// given { const char opt0_descr[]="option_id 0 descr"; const char *opt0_table[]={opt0_descr, opt1_descr}; }
							// use { cfg_param->opt = opt0_table; }
	bool dyn_opt;	//if *opt[] must be free'd (recursively)

	void *dval;		//default value;  used for reset()
	bool dyn_dval;	//dval needs to be free'd

	void (*refresh)(struct l0cfg_param *_this);	//called by diag_cfg_refresh()
		//  Possible problem with refresh() if numopts>0; and refresh() makes *val invalid / illegal !
	void (*reset)(struct l0cfg_param *_this);	//called by diag_cfg_reset()
};

void diag_cfg_refresh(struct l0cfg_param *cfgp);	//Optional func to refresh opt[] and numopts (for tty, J2534, etc), doesn't change *val
void diag_cfg_reset(struct l0cfg_param *cfgp);	//Optional: func to reset *val to default; doesn't call refresh()

//set config value for a param; caller must use the right func ... not super efficient
int diag_cfg_setstr(struct l0cfg_param *cfgp, const char *str);
int diag_cfg_setbool(struct l0cfg_param *cfgp, bool val);
int diag_cfg_setu8(struct l0cfg_param *cfgp, uint8_t val);
int diag_cfg_setint(struct l0cfg_param *cfgp, int val);

//set config value to one of the predefined options. Ret 0 if ok. Note: optid is 0-based
int diag_cfg_setopt(struct l0cfg_param *cfgp, int optid);

//directly set param value (caller knows correct type and handles mem management, etc) BAD
//void diag_cfg_setraw(struct l0cfg_param *cfgp, void *val);

//get param value, as new string to be free'd by caller
const char * diag_cfg_getstr(struct l0cfg_param *cfgp);

//free contents of *cfgp but not the struct itself
void diag_cfg_clear(struct l0cfg_param *cfgp);

/****** re-usable, typical configurable params ******/
/* after alloc'ing a new struct l0cfg_param, calling these funcs will prepare the struct */
/* Ret 0 if ok. Some members of the struct may need to be filled after calling these. */

//new TTY / serial port config item
int diag_cfgn_tty(struct l0cfg_param *cfgp);

//serial link speed;
int diag_cfgn_bps(struct l0cfg_param *cfgp, int *val, int *def);

//ordinary int param using caller's &val, and *dev as default value for reset().
//Doesn't fill descr and shortname
int diag_cfgn_int(struct l0cfg_param *cfgp, int *val, int *def);

//ordinary u8 param (copy of _int code); use dval as default value for reset(). Don't fill descr and shortname
int diag_cfgn_u8(struct l0cfg_param *cfgp, uint8_t *val, uint8_t *def);

//ordinary string, copies *dval for its default value.
int diag_cfgn_str(struct l0cfg_param *cfgp, const char *dval);

//ordinary bool (copy of _int code)
int diag_cfgn_bool(struct l0cfg_param *cfgp, bool *val, bool *def);

#endif // DIAG_CFG_H
