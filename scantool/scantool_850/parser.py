import sys
import re

# a class representing an OBD-II Diagnostic Trouble Code (DTC)
class dtc:
    # constructor
    def __init__(self, ecu, code, prefix, suffix, description):
        self.code = code
        self.description = description
        self.ecu = ecu
        self.prefix = prefix
        self.suffix = suffix

# this loads dtcs from the xiaotec file export
def load_dtcs(file_path):
    dtcs = []

    with open(file_path, 'r') as file:
        ecu_addr = 0
        ecu_description = ''
        strings = {}
        for line in file:
            # regular expression to match 01 - description
            match = re.match(r'([0-9A-F][0-9A-F]) - (.+)', line.strip())
            if match:
                ecu_addr = match.groups()[0]
                ecu_description = match.groups()[1]
                # print(ecu_addr)
            
            # regular expression to match dtc.contains("<code>") once
            match = re.match(r'^if \(dtc.contains\("([0-9A-F]+)"\)\) ?// ?([^\-]+)\-?([0-9]+) ?\(?([a-z])?\)?', line.strip())
            if match:
                string_key = match.groups()[2]
                if match.groups()[3]:
                    string_key += match.groups()[3]
                dtcs.append(dtc(ecu_addr, match.groups()[0], match.groups()[1], match.groups()[2].strip(), string_key))

            # regular expression to match dtc.contains("<code>") twice
            match = re.match(r'^if \(dtc.contains\("([0-9A-F]+)"\) ?\|\| ?dtc.contains\("([0-9A-F]+)"\)\) ?// ?([^\-]+)\-?([0-9]+) ?\(?([a-z])?\)?', line.strip())
            if match:
                string_key = match.groups()[2]
                if match.groups()[3]:
                    string_key += match.groups()[3]
                dtcs.append(dtc(ecu_addr, match.groups()[0], match.groups()[2], match.groups()[3], string_key))
                dtcs.append(dtc(ecu_addr, match.groups()[1], match.groups()[2], match.groups()[3], string_key))
            
            match = re.match(r'<string name="([0-9A-Za-z_]+)">(.+)</string>', line.strip())
            if match:
                if not ecu_addr in strings:
                    strings[ecu_addr] = {}
                strings[ecu_addr][match.groups()[0]] = match.groups()[1]
    
    for d in dtcs:
        key = d.prefix + d.suffix
        ecu_key = d.ecu
        if ecu_key == '2E':
            ecu_key = '2F'
        if ecu_key in strings:
            string_list = strings[ecu_key]
            if key in string_list:
                d.description = string_list[key]
            # Special case for motronic 4.4
            elif key + '_7A' in string_list:
                d.description = string_list[key + '_7A']
            # Special case for EMS ECU
            elif 'V_' + key in string_list:
                d.description = string_list['V_' + key]
            else:
                print('No description found for ' + key)
        else:
            print('No ecu found for: "%s"' % ecu_key)
        
        d.description = escape(d.description)

    return dtcs


def escape(description):
    return description.replace('"', '\\"')

# write a function to parse the DTCs from a text file and return a list of DTCs
def parse_dtc_file(file_path):
    dtcs = []
    with open(file_path, 'r') as file:
        for line in file:
            match = re.match(r'([0-9]+) ([0-9A-F]+) ([A-Z]+)\-([0-9]+) (.+)', line.strip())
            if match:
                # construct a dtc object from match and append it to the list
                dtcs.append(dtc(match.groups()[0], match.groups()[1], match.groups()[2], match.groups()[3], escape(match.groups()[4])))
    return dtcs

# a function that takes a dtc object and a pointer to a mustach file and writes the dtc to the file
def write_dtc_to_mustache(dtc, template):
    # substitute the fields of the dtc object into the template
    template = template.replace('{{ecu}}', dtc.ecu)
    template = template.replace('{{code}}', dtc.code)
    template = template.replace('{{prefix}}', dtc.prefix)
    template = template.replace('{{suffix}}', dtc.suffix)
    template = template.replace('{{description}}', dtc.description)
    
    return template

# a function that given a list of dtcs groups them into a map by ecu
def group_dtcs_by_ecu(dtcs):
    ecu_map = {}
    for dtc in dtcs:
        if dtc.ecu not in ecu_map:
            ecu_map[dtc.ecu] = []
        ecu_map[dtc.ecu].append(dtc)
    return ecu_map


# write a main function to call the parse_dtc_file function with a file from the first argument
def main():
    if len(sys.argv) != 4:
        print('Usage: python parser.py <file> <template_directory> <name>')
        sys.exit(1)
    # raw_dtcs = parse_dtc_file(sys.argv[1])
    raw_dtcs = load_dtcs(sys.argv[1])
    ecu_map = group_dtcs_by_ecu(raw_dtcs)    
    
    directory = sys.argv[2]
    name = sys.argv[3]
    
    with (open(directory + '/dtc.mustache', 'r') as dtc_template_file,
          open(directory + '/dtc_list.mustache', 'r') as dtc_list_template_file,
          open(directory + '/ecu.mustache', 'r') as ecu_template_file,
          open(directory + '/prefix.mustache', 'r') as prefix_template_file,
          open(directory + '/suffix.mustache', 'r') as suffix_template_file):

        prefix_template = prefix_template_file.read()
        prefix_template = prefix_template.replace('{{name}}', name.upper())
        print(prefix_template)

        template = dtc_template_file.read()
        dtc_list_template = dtc_list_template_file.read()
        ecu_template = ecu_template_file.read()

        ecu_list = []
        for ecu in ecu_map.keys():
            out = ecu_template.replace('{{ecu}}', ecu)
            out = out.replace('{{dtc_table}}', 'dtc_list_' + ecu.lower())
            ecu_list.append(out)

            dtcs = ecu_map[ecu]
            dtc_list = []
            for dtc in dtcs:
                dtc_list.append(write_dtc_to_mustache(dtc, template))
            out = dtc_list_template.replace('{{dtc_list}}', '\n'.join(dtc_list))
            out = out.replace('{{dtc_list_name}}', 'dtc_list_' + ecu.lower())
            print(out)
        
        suffix_template = suffix_template_file.read()
        suffix_template = suffix_template.replace('{{name}}', name.upper())
        suffix_template = suffix_template.replace('{{ecu_list}}', '\n'.join(ecu_list))
        print(suffix_template)

main()