#!/usr/bin/env python3
"""Print "name path" for the IPSW members the root build needs (from BuildManifest.plist, D22AP Erase variant)."""
import plistlib
import sys

m = plistlib.load(open(sys.argv[1], 'rb'))
ident = next(i for i in m['BuildIdentities']
             if i['Info']['DeviceClass'].lower() == 'd22ap' and 'Erase' in i['Info'].get('Variant', ''))
want = {'OS': 'os', 'Cryptex1,SystemOS': 'systemos', 'Cryptex1,AppOS': 'appos'}
for key, name in want.items():
    print(name, ident['Manifest'][key]['Info']['Path'])
