#!/usr/bin/env python3
"""
decode_es419_messages.py — OoT Internal Character Codec to ES-419 Unicode.
Decodes internal OoT message byte sequences back to UTF-8 ES-419 text
for round-trip verification and QA auditing.
"""

from encode_es419_messages import UNICODE_TO_BYTE, BUTTON_TOKEN_MAP

# Invert Unicode map
BYTE_TO_UNICODE = {v: k for k, v in UNICODE_TO_BYTE.items()}

# Invert Button token map
BYTE_TO_BUTTON_TOKEN = {bval[0]: token for token, bval in BUTTON_TOKEN_MAP.items() if token != '[Control-Pad]'}

def decode_bytes(data: bytes) -> str:
    """
    Decode internal OoT message bytes back to canonical ES-419 Unicode string.
    """
    result = []
    i = 0
    n = len(data)
    
    while i < n:
        b = data[i]
        
        # Control characters
        if b == 0x01: # CTRL_NEWLINE
            result.append('\n')
            i += 1
            continue
        elif b == 0x04: # CTRL_BOX_BREAK
            result.append('--- PAGE ---')
            i += 1
            continue
        elif b == 0x0F: # CTRL_NAME
            result.append('[PLAYER]')
            i += 1
            continue
        elif b == 0x0E and i + 1 < n: # CTRL_FADE + duration argument
            arg = data[i+1]
            result.append(f"\\x0E\\x{arg:02X}")
            i += 2
            continue
            
        # Button tokens
        if b in BYTE_TO_BUTTON_TOKEN:
            result.append(BYTE_TO_BUTTON_TOKEN[b])
            i += 1
            continue
            
        # Spanish accented characters
        if b in BYTE_TO_UNICODE:
            result.append(BYTE_TO_UNICODE[b])
            i += 1
            continue
            
        # Standard ASCII
        if 0x20 <= b <= 0x7E:
            result.append(chr(b))
            i += 1
            continue
            
        # Raw hex control byte
        result.append(f"\\x{b:02X}")
        i += 1
        
    return "".join(result)

if __name__ == '__main__':
    from encode_es419_messages import encode_text
    sample = "¡Hola! ¿Estás listo, [PLAYER]? Presiona [A] para continuar.--- PAGE ---¡Obtuviste la Túnica Kokiri!\\x0E\\x3C"
    encoded = encode_text(sample)
    decoded = decode_bytes(encoded)
    print("Original:", sample)
    print("Decoded: ", decoded)
    print("Match:   ", sample == decoded)
