#note : we can eventually replace genconfig.sh (for generating diag_config.c)
#either with a custom command and a "genconfig.c" program, or
#maybe with another configure_file() , foreach and a load of @var@s. or lists ?

add_library(libdiag STATIC diag_l0_me.c diag_l0_br.c diag_l0_elm.c diag_l0_sim.c
	diag_l0_dumb.c
	${OS_DIAGTTY} diag_l1.c diag_l2.c diag_l2_can.c diag_l2_raw.c
        diag_l2_iso9141.c diag_l2_iso9141.c diag_l2_iso14230.c
	diag_l2_saej1850.c diag_l2_vag.c diag_l2_mb1.c
	diag_l3.c diag_l3_saej1979.c diag_l3_iso14230.c diag_l3_vag.c
	diag_os.c diag_general.c diag_dtc.c)