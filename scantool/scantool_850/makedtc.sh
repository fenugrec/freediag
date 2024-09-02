#!/bin/sh

# An equivalent to this script was used to massage and merge the available
# DTC lists into a starting point for dtc.c.

# Make a copy of Aleksi's table
cp DTC_List_850OBDII_D2.txt dtc.txt
# split lines that have two dtc.contains predicates
sed -i 's/^\(.*)\) *||* *\(dtc[^)]*\))\(.*\)/\1\3\nif (\2)\3/' dtc.txt
# EFI-742 has three of them, so do it again
sed -i 's/^\(.*)\) *||* *\(dtc[^)]*\))\(.*\)/\1\3\nif (\2)\3/' dtc.txt
# where dtc.contains line has an (a), (b), etc suffix, fold that into the string name
sed -i 's/\(-...\).*(\(.\))/\1\2/' dtc.txt
# adjust the string names for the multiple ecus with address 7A
sed -i '/EMS2000/,/####/s/EFI-/V_EFI-/' dtc.txt
sed -i '/M4.4/,/####/s/\(EFI-[0-9a-z]*\)/\1_7A/' dtc.txt
# based on the dtc.contains lines, create a space-delimited file mapping ecu name / dtc number to string name
# CCU and Denso don't have dtc.contains lines, so those are omitted from dtcx.txt and will be added to dtcy.txt separately
sed -n '/^01 - ABS/,/####/{/if/{s,.*("\([^"]*\).*//\([^-]*\)-\([0-9A-Za-z_]*\).*,abs \1 \2\3,;p}}' dtc.txt > dtcx.txt
sed -n '/^02 - DSA/,/####/{/if/{s,.*("\([^"]*\).*//\([^-]*\)-\([0-9A-Za-z_]*\).*,dsa \1 \2\3,;p}}' dtc.txt >> dtcx.txt
sed -n '/^11 - MSA/,/####/{/if/{s,.*("\([^"]*\).*//\([^-]*\)-\([0-9A-Za-z_]*\).*,msa \1 \2\3,;p}}' dtc.txt >> dtcx.txt
sed -n '/^18 - Add/,/####/{/if/{s,.*("\([^"]*\).*//\([^-]*\)-\([0-9A-Za-z_]*\).*,add \1 \2\3,;p}}' dtc.txt >> dtcx.txt
sed -n '/^29 - ECC/,/####/{/if/{s,.*("\([^"]*\).*//\([^-]*\)-\([0-9A-Za-z_]*\).*,ecc \1 \2\3,;p}}' dtc.txt >> dtcx.txt
sed -n '/^2D - VGLA/,/####/{/if/{s,.*("\([^"]*\).*//\([^-]*\)-\([0-9A-Za-z_]*\).*,vgla \1 \2\3,;p}}' dtc.txt >> dtcx.txt
sed -n '/^2. - PS/,/####/{/if/{s,.*("\([^"]*\).*//\([^-]*\)-\([0-9A-Za-z_]*\).*,ps \1 \2\3,;p}}' dtc.txt >> dtcx.txt
sed -n '/^41 - IMMO/,/####/{/if/{s,.*("\([^"]*\).*//\([^-]*\)-\([0-9A-Za-z_]*\).*,immo \1 \2\3,;p}}' dtc.txt >> dtcx.txt
sed -n '/^51 - COMBI/,/####/{/if/{s,.*("\([^"]*\).*// *\([^-]*\)-\([0-9A-Za-z_]*\).*,combi \1 \2\3,;p}}' dtc.txt >> dtcx.txt
sed -n '/^SRS.*96/,/^SRS.*99/{/if/{s,.*("\([^"]*\).*// *\([^-]*\)-\([0-9A-Za-z_]*\).*,srs \1 \2\3,;p}}' dtc.txt >> dtcx.txt
sed -n '/^SRS.*99/,/####/{/if/{s,.*("\([^"]*\).*// *\([^-]*\)-\([0-9A-Za-z_]*\).*,srs_temic \1 \2\3,;p}}' dtc.txt >> dtcx.txt
sed -n '/^ROP/,/####/{/if/{s,.*("\([^"]*\).*// *\([^-]*\)-\([0-9A-Za-z_]*\).*,rop \1 \2\3,;p}}' dtc.txt >> dtcx.txt
sed -n '/^62 - RTI/,/####/{/if/{s,.*("\([^"]*\).*// *\([^-]*\)-\([0-9A-Za-z_]*\).*,rti \1 \2\3,;p}}' dtc.txt >> dtcx.txt
sed -n '/^6E - Auto/,/####/{/if/{s,.*("\([^"]*\).*// *\([^-]*\)-\([0-9A-Za-z_]*\).*,aw50 \1 \2\3,;p}}' dtc.txt >> dtcx.txt
sed -n '/^7A - EMS/,/####/{/if/{s,.*("\([^"]*\).*// *\([^-]*\)-\([0-9A-Za-z_]*\).*,ems2k \1 \2\3,;p}}' dtc.txt >> dtcx.txt
sed -n '/^7A - Mot/,/####/{/if/{s,.*("\([^"]*\).*// *\([^-]*\)-\([0-9A-Za-z_]*\).*,m44 \1 \2\3,;p}}' dtc.txt >> dtcx.txt
# look up the strings from dtcx.txt and generate dtcy.txt with the string names replaced by string text
(while read table value string; do printf %s "$table $value "; sed -n "/<string name=.$string.>/{s,.*\">\\(.*\\)</.*,\\1,;s/://;p}" dtc.txt ; done) < dtcx.txt > dtcy.txt
# add the CCU and Denso dtcs
sed -n '/^42 - CCU/,/^51 - COMBI/{/CCU-/{s/^\(..\).*\(CCU-.*\)/ccu \1 \2/; s/\.$//; p}}' dtc.txt >> dtcy.txt
sed -n '/B5244S2/,${/ECM-/{s/^ECM-\(....\),* \(.*\)/denso \1 ECM-\1 \2/;p}}' dtc.txt >> dtcy.txt
# Make a copy of Richard's table
cp export_2024-04-06_frobbed.txt dtc2.txt
# replace addresses with symbolic names
sed -i 's/^01/abs/' dtc2.txt
sed -i 's/^11/msa/' dtc2.txt
sed -i 's/^29/ecc/' dtc2.txt
sed -i 's/^2D/vgla/' dtc2.txt
sed -i 's/^2[EF]/ps/' dtc2.txt
sed -i 's/^41/immo/' dtc2.txt
sed -i 's/^51/combi/' dtc2.txt
sed -i 's/^58/srs/' dtc2.txt
sed -i 's/^6E/aw50/' dtc2.txt
sed -i 's/^7A/m44/' dtc2.txt
# remove trailing periods from descriptions, where present
sed -i 's/\.$//' dtc2.txt
# merge Richard's list with Aleksi's, retaining only one copy of duplicated entries
cat dtc2.txt dtcy.txt | sort | uniq > merged.txt
# make a list with the tips removed
sed 's/\\n.*//; s/\[[^]]*]//g; s/([^)]*)//g; s/  */ /g; s/ $//' merged.txt | uniq > notips.txt
# make a list with only the tips
sed -n '/\\n/{s/^\([^ ]* [^ ]* [^ ]* \)[^\\]*\\n\(.*\)/\1\2/;p }' < merged.txt > tips.txt
sed 's/\\n.*//' merged.txt | ruby -ne 'p=$_[/^[^ ]+ [^ ]+ [^ ]+ /]; $_.scan(/\[([^\]]*)\]/).each {|x| puts p+x[0]}' >> tips.txt
sed 's/\\n.*//; s/\[[^]]*]//g;' merged.txt | ruby -ne 'p=$_[/^[^ ]+ [^ ]+ [^ ]+ /]; $_.scan(/\(([^)]*)\)/).each {|x| puts p+x[0]}' >> tips.txt
# eliminate extra copies of duplicated tips
sort tips.txt | uniq > tipsuniq.txt
# convert the no-tips list to C source code
ruby -ne 'BEGIN{lastecu="";firstecu=true;};END{puts "\t{0, 0, NULL, NULL}\n};"};(ecu,hex,str,desc)=$_.chomp.split(" ",4);suf=str[/[-A-Z]+(.*)/,1];print "\t{0, 0, NULL, NULL}\n};\n\n" if !firstecu && ecu!=lastecu;firstecu=false;puts "static const struct dtc_table_entry dtc_list_#{ecu}[] = {" if ecu!=lastecu;lastecu=ecu;puts "\t{0x#{hex}, #{suf}, \"#{desc}\", NULL},"' notips.txt > foo.c
