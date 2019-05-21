#script for running freediag "scantool" with a script, and matching stdout and stderr output to "desired output" files.
#Caller passes
# TEST_PROG (freediag binary)
# TESTFDIR (directory for .ini, .stdout, .stderr files)
# TESTF (root of files)

#This runs "{TEST_PROG} -f {TESTF}.ini" and compares stdout/err output to
# TESTFDIR/{TESTF}.stdout and TESTFDIR{TESTF}.stderr respectively

#execute_process(COMMAND ${TEST_PROG} -f ${TESTFDIR}/${TESTF}.ini
execute_process(COMMAND ${TEST_PROG} -f "${TESTF}.ini"
	TIMEOUT 25
	RESULT_VARIABLE HAD_ERROR
	OUTPUT_VARIABLE OUTV
	ERROR_VARIABLE ERRV
	)

#message(FATAL_ERROR ${HAD_ERROR} ${OUTV} ${ERRV})

##parse .std{o,e}_{p,f} files to retrieve regexps (stdout/stderr, pass/fail)
set(SOP "${TESTFDIR}/${TESTF}.stdo_p")
set(SOF "${TESTFDIR}/${TESTF}.stdo_f")
set(SEP "${TESTFDIR}/${TESTF}.stde_p")
set(SEF "${TESTFDIR}/${TESTF}.stde_f")

if(EXISTS ${SOP})
	file(READ ${SOP} SOP_RE)
	if(NOT "${OUTV}" MATCHES "${SOP_RE}")
		message(FATAL_ERROR "stdout_pass mismatch in:\n${OUTV}")
	endif()
endif()

if(EXISTS ${SOF})
	file(READ ${SOF} SOF_RE)
	if("${OUTV}" MATCHES "${SOF_RE}")
		message(FATAL_ERROR "stdout_fail match in:\n${OUTV}")
	endif()
endif()

if(EXISTS ${SEP})
	file(READ ${SEP} SEP_RE)
	if(NOT "${ERRV}" MATCHES "${SEP_RE}")
		message(FATAL_ERROR "stderr_pass mismatch in:\n${ERRV}")
	endif()
endif()

if(EXISTS ${SEF})
	file(READ ${SEF} SEF_RE)
	if("${ERRV}" MATCHES "${SEF_RE}")
		message(FATAL_ERROR "stderr_fail match in:\n${ERRV}")
	endif()
endif()

#file(READ "${TESTFDIR}/${TESTF}.stderr" STDERR_RE)
#if(NOT "${ERRV}" MATCHES "${STDERR_RE}")
#	message(FATAL_ERROR "stderr mismatch in :\n${ERRV}")
#endif()
