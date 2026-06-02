import struct, os

def clamp(v):
    return max(0, min(255, int(v)))

def make_ico(sizes=[(16, 16), (32, 32), (48, 48), (64, 64)]):
    import math
    images = []
    for w, h in sizes:
        pixels = []
        for y in range(h):
            for x in range(w):
                cx, cy = w // 2, h // 2
                dx, dy = abs(x - cx) + 0.01, abs(y - cy) + 0.01
                dist = math.sqrt(dx*dx + dy*dy)
                max_r = min(cx, cy) - 0.5
                r, g, b, a = 0, 0, 0, 0

                if dist <= max_r:
                    a = 255
                    glow = max(0.0, 1.0 - dist / max_r)

                    if dist < max_r * 0.3:
                        r = 50
                        g = 190
                        b = 245
                    elif dist < max_r * 0.6:
                        frac = (dist - max_r * 0.3) / (max_r * 0.3)
                        r = int(50 + (20 - 50) * frac)
                        g = int(190 + (110 - 190) * frac)
                        b = int(245 + (200 - 245) * frac)
                    elif dist < max_r * 0.85:
                        frac = (dist - max_r * 0.6) / (max_r * 0.25)
                        r = int(20 + (10 - 20) * frac)
                        g = int(110 + (70 - 110) * frac)
                        b = int(200 + (150 - 200) * frac)
                    else:
                        frac = (dist - max_r * 0.85) / (max_r * 0.15)
                        r = int(10 * (1 - frac))
                        g = int(70 * (1 - frac))
                        b = int(150 * (1 - frac))

                    r = clamp(r + 60 * glow)
                    g = clamp(g + 80 * glow)
                    b = clamp(b + 100 * glow)

                    if dist < max_r * 0.4 and x < cx * 0.7:
                        hr = 1.0 - abs(x - cx * 0.35) / (cx * 0.3)
                        if hr > 0 and y < cy * 0.6:
                            hl = 1.0 - abs(y - cy * 0.3) / (cy * 0.3)
                            r = clamp(r + 80 * hr * hl)
                            g = clamp(g + 100 * hr * hl)
                            b = clamp(b + 140 * hr * hl)

                    if dist > max_r * 0.65 and dist < max_r * 0.8:
                        ring = 1.0 - abs(dist - max_r * 0.725) / (max_r * 0.075)
                        r = clamp(r + 30 * ring)
                        g = clamp(g + 50 * ring)
                        b = clamp(b + 70 * ring)

                elif dist <= max_r + 2:
                    alpha = max(0, 1.0 - (dist - max_r) / 2.0)
                    a = clamp(255 * alpha)
                    r, g, b = 30, 90, 170

                pixels.append((clamp(r), clamp(g), clamp(b), clamp(a)))

        xor_data = bytearray()
        for y in range(h):
            for x in range(w):
                idx = y * w + x
                pix = pixels[idx]
                xor_data += bytes([pix[2], pix[1], pix[0], pix[3]])
            row_size = w * 4
            padding = (4 - row_size % 4) % 4
            xor_data += b'\x00' * padding

        xor_size = len(xor_data)
        mask_row = ((w + 31) // 32) * 4
        and_mask = bytearray(mask_row * h)

        bih = struct.pack('<IiiHHIIiiII',
            40, w, 2 * h, 1, 32, 0, xor_size, 0, 0, 0, 0)
        img_data = bih + xor_data + and_mask
        images.append((w, h, len(img_data), img_data))

    header_size = 6 + 16 * len(images)
    offset = header_size
    data = struct.pack('<HHH', 0, 1, len(images))
    for (w, h, size, _) in images:
        iw = w if w < 256 else 0
        ih = h if h < 256 else 0
        data += struct.pack('<BBBBHHII', iw, ih, 0, 0, 1, 32, size, offset)
        offset += size
    for (_, _, _, img_data) in images:
        data += img_data
    return data

ico_data = make_ico()
out = os.path.join(os.path.dirname(__file__), 'DiskC', 'DiskC.ico')
with open(out, 'wb') as f:
    f.write(ico_data)
print(f"Icon created: {out} ({len(ico_data)} bytes)")
