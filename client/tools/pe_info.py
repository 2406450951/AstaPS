import pefile, sys, os

for path in sys.argv[1:]:
    print("=" * 70)
    print(path)
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[
        pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],
        pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT'],
    ])
    print("machine=%X  ts=%s  size=%d" % (pe.FILE_HEADER.Machine, pe.FILE_HEADER.TimeDateStamp, os.path.getsize(path)))
    print("subsystem=%d dllchar=%X" % (pe.OPTIONAL_HEADER.Subsystem, pe.OPTIONAL_HEADER.DllCharacteristics))
    for s in pe.sections:
        print("  sec %-8s va=%08X vs=%9d raw=%9d" % (s.Name.rstrip(b'\0').decode('latin1'), s.VirtualAddress, s.Misc_VirtualSize, s.SizeOfRawData))
    dlls = []
    for entry in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []) + getattr(pe, 'DIRECTORY_ENTRY_DELAY_IMPORT', []):
        name = entry.dll.decode('latin1')
        funcs = []
        for imp in entry.imports:
            if imp.name:
                funcs.append(imp.name.decode('latin1'))
        dlls.append((name, funcs))
    print("imports: %d dlls" % len(dlls))
    for name, funcs in dlls:
        low = name.lower()
        print("  %-30s %3d" % (name, len(funcs)))
        interesting = [f for f in funcs if any(k in f.lower() for k in
                       ('crypt', 'rsa', 'hash', 'cert', 'ssl', 'http', 'socket', 'wininet',
                        'bcrypt', 'ncrypt', 'cert', 'sign', 'verify', 'dns', 'ws2'))]
        if interesting:
            print("      * " + ", ".join(sorted(set(interesting))[:40]))
    pe.close()
