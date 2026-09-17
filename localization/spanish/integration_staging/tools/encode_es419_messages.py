#!/usr/bin/env python3
"""
encode_es419_messages.py — ES-419 Unicode to OoT Internal Character Codec.
Converts UTF-8 ES-419 text into internal OoT byte format according to
SPANISH_FONT_SLOT_MAP.csv without data loss.
"""

import sys

# Unicode to internal OoT single-byte mapping
UNICODE_TO_BYTE = {
    'Á': 0x80, # Replaces À
    'Ñ': 0x81, # Replaces Î
    '¡': 0x83, # Replaces Ä
    '¿': 0x85, # Replaces È
    'É': 0x86, # Native stock font
    'Í': 0x89, # Replaces Ï
    'Ó': 0x8A, # Replaces Ô
    'Ú': 0x8C, # Replaces Ù
    'á': 0x91, # Native stock font
    'ñ': 0x92, # Replaces â
    'é': 0x96, # Native stock font
    'í': 0x99, # Replaces ï
    'ó': 0x9A, # Replaces ô
    'ú': 0x9D, # Replaces û
    'ü': 0x9E, # Native stock font
}

BUTTON_TOKEN_MAP = {
    '[A]': bytes([0x9F]),
    '[B]': bytes([0xA0]),
    '[C]': bytes([0xA1]),
    '[L]': bytes([0xA2]),
    '[R]': bytes([0xA3]),
    '[Z]': bytes([0xA4]),
    '[C-Up]': bytes([0xA5]),
    '[C-Down]': bytes([0xA6]),
    '[C-Left]': bytes([0xA7]),
    '[C-Right]': bytes([0xA8]),
    '[Z-Target]': bytes([0xA9]),
    '[Control-Stick]': bytes([0xAA]),
    '[Control-Pad]': bytes([0xAB]),
    '[D-Pad]': bytes([0xAB]),
}

CONTROL_TOKEN_MAP = {
    '--- PAGE ---': bytes([0x04]),
    '[PLAYER]': bytes([0x0F]),
}

def encode_text(text: str, strict: bool = True) -> bytes:
    """
    Encode an ES-419 string into OoT internal message bytes.
    Preserves control codes, escapes, variables, and button tokens.
    Throws ValueError if strict=True and an unsupported character is encountered.
    """
    result = bytearray()
    i = 0
    n = len(text)
    
    while i < n:
        # Check PAGE break
        if text.startswith('--- PAGE ---', i):
            result.extend(CONTROL_TOKEN_MAP['--- PAGE ---'])
            i += len('--- PAGE ---')
            continue
            
        # Check PLAYER token
        if text.startswith('[PLAYER]', i):
            result.extend(CONTROL_TOKEN_MAP['[PLAYER]'])
            i += len('[PLAYER]')
            continue
            
        # Check Button tokens
        found_btn = False
        for token, bval in BUTTON_TOKEN_MAP.items():
            if text.startswith(token, i):
                result.extend(bval)
                i += len(token)
                found_btn = True
                break
        if found_btn:
            continue
            
        # Check raw hex escape e.g. \x0E\x3C or \x05@
        if text.startswith('\\x', i) and i + 4 <= n:
            try:
                hex_val = int(text[i+2:i+4], 16)
                result.append(hex_val)
                i += 4
                continue
            except ValueError:
                pass
                
        ch = text[i]
        
        # Newline
        if ch == '\n':
            result.append(0x01) # CTRL_NEWLINE
            i += 1
            continue
        if ch == '\r':
            i += 1
            continue
            
        # Spanish accented character / special symbol
        if ch in UNICODE_TO_BYTE:
            result.append(UNICODE_TO_BYTE[ch])
            i += 1
            continue
            
        # Standard ASCII
        cp = ord(ch)
        if 0x20 <= cp <= 0x7E:
            result.append(cp)
            i += 1
            continue
            
        # Unsupported character
        if strict:
            raise ValueError(f"Unsupported character '{ch}' (U+{cp:04X}) at index {i} in text: {text[max(0, i-20):min(n, i+20)]}")
        else:
            result.append(ord('?'))
            i += 1
            
    return bytes(result)

if __name__ == '__main__':
    sample = "¡Hola! ¿Estás listo, [PLAYER]? Presiona [A] para continuar.--- PAGE ---¡Obtuviste la Túnica Kokiri!"
    encoded = encode_text(sample)
    print("Sample text:", sample)
    print("Encoded hex:", encoded.hex(" "))
