#!/bin/sh
#This script uses l0config and l2config to generate diag_config.c
#automatically invoked during Make

set -e

# Generate the L0 and L2 lists
l0=`sed -e '/^#.*/d' -e '/^$/d' < l0config`
l2=`sed -e '/^#.*/d' -e '/^$/d' < l2config`

echo '/*'
echo ' *             Automatically Generated File'
echo ' *     This file is generated automatically by "genconfig.sh",'
echo ' *     from the files "l0config" and "l2config".'
echo ' *     Do not manually edit this file. Your changes will be lost.'
echo ' *  '
echo ' */'
echo "#include \"diag.h\""
echo "#if defined(__cplusplus)"
echo "extern \"C\" {"
echo "#endif"

for i in $l0; do
	echo "extern int diag_l0_${i}_add(void);"
done
for i in $l2; do
	echo "extern int diag_l2_${i}_add(void);"
done

echo "#if defined(__cplusplus)"
echo "}"
echo "#endif"

echo "int diag_l0_config(void) {"
echo "	int rv = 0;"

for i in $l0; do
	echo "	rv |= diag_l0_${i}_add();"
done

echo "	return rv;"
echo "}"

echo "int diag_l2_config(void) {"
echo "	int rv = 0;"

for i in $l2; do
	echo "	rv |= diag_l2_${i}_add();"
done

echo "	return rv;"
echo "}"

