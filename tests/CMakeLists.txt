
## Test suite (WIP)
# freediag tests; these are arranged roughly from slowest to fastest
set(SCANTOOL_TESTS
#tty_patgen requires oscilloscope validation
#	tty_patgen
#tty_timing requires manual verification of output
#	tty_timing
#dumb halfdup tests are very long (15-25 sec each):
	l0_dumb_halfdup
	l0_dumb_halfdup_2
	cli_1
	l0_carsim_1
	l0_carsim_2
	l0_carsim_3
	l0_carsim_4
	l0_carsim_5
	l0_carsim_6
	l2_14230_fast
	l2_j1850p_crc
	l2_9141_reconst
	l2_14230_negresp
	l2_j1850_mrx
	l2_raw_01
	l3_j1979_9141_1
	l3_j1979_j1850_1
	l7_850_01
	l7_850_02
	)
set(TESTSRC "${CMAKE_SOURCE_DIR}/tests")

foreach (TF_ITER IN LISTS SCANTOOL_TESTS)
	add_test(NAME ${TF_ITER}
		WORKING_DIRECTORY ${TESTSRC}
		COMMAND ${CMAKE_COMMAND}
		-DTEST_PROG=$<TARGET_FILE:freediag>
		-DTESTFDIR=${TESTSRC}
		-DTESTF=${TF_ITER}
		-P ${TESTSRC}/runcli.cmake
		)

	message(STATUS "Adding test \"${TF_ITER}\"")
endforeach()
