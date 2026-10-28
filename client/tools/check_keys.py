import os
import base64, re, os
from cryptography.hazmat.primitives import serialization

base = os.environ.get('GS71_SERVER', '.')
xml_path = ros.environ.get('GS71_ROOT', '.')
xml = open(xml_path).read()
m = re.search(r'<Modulus>(.*?)</Modulus>', xml)
e = re.search(r'<Exponent>(.*?)</Exponent>', xml)
xmlmod = base64.b64decode(m.group(1))
xmle = base64.b64decode(e.group(1))
print('xml exponent', xmle.hex(), 'modulus bytes', len(xmlmod))

for name in ('private_key.der', 'auth_private-key.der', 'SigningKey.der'):
    p = os.path.join(base, name)
    k = serialization.load_der_private_key(open(p, 'rb').read(), password=None)
    n = k.public_key().public_numbers().n
    nb = n.to_bytes((n.bit_length() + 7) // 8, 'big')
    a = nb.lstrip(b'\x00')
    b = xmlmod.lstrip(b'\x00')
    match = (a == b)
    print(name, 'bits=', k.key_size, 'match_xml=', match)
    if not match:
        print('   server modulus[:16]', nb[:16].hex())
        print('   xml    modulus[:16]', xmlmod[:16].hex())
