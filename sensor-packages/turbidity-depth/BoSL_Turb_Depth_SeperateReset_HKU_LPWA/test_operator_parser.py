"""Host-C scanf regression for the firmware's extracted CPSI format.

Pass sketch text on stdin. Tests format and validation boundaries, not AVR UART
timing or real modem operation. Uses Windows C runtime through ctypes.
"""
import ctypes
import re
import sys

source = sys.stdin.read()
body = source.split('int getMccmnc(char* mccmnc, size_t mccmnc_size){', 1)[1].split('\n}', 1)[0]
assert 'strstr(response, "+CPSI:")' in body
fmt = re.search(r'sscanf\(start, "([^"]+)"', body).group(1).encode()
crt = ctypes.CDLL('msvcrt')
crt.sscanf.restype = ctypes.c_int

def parse(reply, capacity=7):
    start = reply.find(b'+CPSI:')
    if start < 0 or capacity == 0:
        return None
    mcc, mnc, delimiter = (ctypes.create_string_buffer(n) for n in (4, 4, 1))
    count = crt.sscanf(ctypes.c_char_p(reply[start:]), ctypes.c_char_p(fmt), mcc, mnc, delimiter)
    if (count == 3 and delimiter.raw == b',' and len(mcc.value) == 3
            and len(mnc.value) in (2, 3) and capacity > len(mcc.value + mnc.value)):
        return (mcc.value + mnc.value).decode()
    return None

line = b'+CPSI: LTE NB-IOT,Online,505-01,0x389E,137193052\nOK\n'
cases = [
    (line, 7, '50501'),
    (b'*PSUTTZ: 26/09/09,06:22:31,"+40",0\n' + line, 7, '50501'),
    (b'+CPIN: READY\nDST: 0\n' + line, 7, '50501'),
    (line.replace(b'505-01', b'310-260'), 7, '310260'),
    (line.replace(b'505-01', b'505-1'), 7, None),
    (line.replace(b'505-01', b'505-0123'), 7, None),
    (line.replace(b'505-01', b'50A-01'), 7, None),
    (b'*PSUTTZ: " +40"\nOK\n', 7, None),
    (line, 5, None),
    (line, 6, '50501'),
    (line, 0, None),
]
for reply, capacity, expected in cases:
    actual = parse(reply, capacity)
    assert actual == expected, (reply, capacity, expected, actual)
print(f'PASS: {len(cases)} host-C CPSI format/validation cases (including reported +40 URC).')

