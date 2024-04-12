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

# a class representing an ecu address and description
class ecu:
    # constructor
    def __init__(self, address, description, prefix):
        self.address = address
        self.description = description
        self.prefix = prefix
    

# this loads dtcs from the xiaotec file export
def load_dtcs(file_path):
    dtcs = []
    ecus = []

    with open(file_path, 'r') as file:
        current_ecu = None
        strings = {}
        for line in file:
            # regular expression to match 01 - description
            match = re.match(r'([0-9A-F][0-9A-F]) - (.+)', line.strip())
            if match:
                current_ecu = ecu(match.groups()[0], match.groups()[1], 'unknown')
                ecus.append(current_ecu)
                # print(current_ecu.address)
            
            # regular expression to match dtc.contains("<code>") once
            match = re.match(r'^if \(dtc.contains\("([0-9A-F]+)"\)\) ?// ?([^\-]+)\-?([0-9]+) ?\(?([a-z])?\)?', line.strip())
            if match:
                string_key = match.groups()[2]
                if match.groups()[3]:
                    string_key += match.groups()[3]
                dtcs.append(dtc(current_ecu.address, match.groups()[0], match.groups()[1], match.groups()[2].strip(), string_key))
                current_ecu.prefix = match.groups()[1]

            # regular expression to match dtc.contains("<code>") twice
            match = re.match(r'^if \(dtc.contains\("([0-9A-F]+)"\) ?\|\| ?dtc.contains\("([0-9A-F]+)"\)\) ?// ?([^\-]+)\-?([0-9]+) ?\(?([a-z])?\)?', line.strip())
            if match:
                string_key = match.groups()[2]
                if match.groups()[3]:
                    string_key += match.groups()[3]
                dtcs.append(dtc(current_ecu.address, match.groups()[0], match.groups()[2], match.groups()[3], string_key))
                dtcs.append(dtc(current_ecu.address, match.groups()[1], match.groups()[2], match.groups()[3], string_key))
            
            match = re.match(r'<string name="([0-9A-Za-z_]+)">(.+)</string>', line.strip())
            if match:
                if not current_ecu.address in strings:
                    strings[current_ecu.address] = {}
                strings[current_ecu.address][match.groups()[0]] = match.groups()[1]
    
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

    return (dtcs, ecus)


def escape(description):
    result = description.replace('"', '\\"')
    # Sadly some strings are already escaped :( so we need to unescape the double escape.
    result = result.replace('\\\\"', '\\"')
    # This is a weird typo in the original file :(
    result = result.replace('\\A', '\\nA'
    )
    return result

# write a function to parse the DTCs from a text file and return a list of DTCs
def parse_dtc_file(file_path):
    dtcs = []
    ecus = {}
    with open(file_path, 'r') as file:
        for line in file:
            match = re.match(r'([0-9]+) ([0-9A-F]+) ([A-Z]+)\-([0-9]+) (.+)', line.strip())
            if match:
                # construct a dtc object from match and append it to the list
                dtcs.append(dtc(match.groups()[0], match.groups()[1], match.groups()[2], match.groups()[3], escape(match.groups()[4])))
                current_ecu = ecu(match.groups()[0], match.groups()[2], match.groups()[2])
                ecus[current_ecu.address] = current_ecu
    ecu_list = []
    for key in ecus:
        ecu_list.append(ecus[key])
    return (dtcs, ecu_list)

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
    if len(sys.argv) != 5:
        print('Usage: python parser.py <format> <input-file> <template_directory> <header-name>')
        sys.exit(1)
    format = sys.argv[1]
    if format == 'aleksi':
        (raw_dtcs, ecus) = load_dtcs(sys.argv[2])
    elif format == 'richard':
        (raw_dtcs, ecus) = parse_dtc_file(sys.argv[2])
    else:
        print('Unknown format: ' + format)
        sys.exit(1)

    ecu_map = group_dtcs_by_ecu(raw_dtcs)    
    
    directory = sys.argv[3]
    name = sys.argv[4]
    
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
        
        print('static struct ecu_info ecu_list[] = {')
        for ecu in ecus:
            print('    { .addr = 0x' + ecu.address + ', .desc = "' + ecu.description + '", .dtc_prefix = "' + ecu.prefix + '" },')
        print('};')

        suffix_template = suffix_template_file.read()
        suffix_template = suffix_template.replace('{{name}}', name.upper())
        suffix_template = suffix_template.replace('{{ecu_list}}', '\n'.join(ecu_list))
        print(suffix_template)

# Example call: python3 parser.py richard export_2024-04-06_frobbed.txt templates/ frobbed > frobbed.h
# Example call: python3 parser.py aleksi DTC_List_850OBDII_D2.txt templates/ xiaotec > xiaotec.h
main()