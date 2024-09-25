import struct
import sys

import numpy as np
import os
from PIL import Image




def create_test_imgs(n, width, height):
    base_img = np.zeros(shape=(height, width, 3), dtype=np.uint8)
    for x in range(width):
        r = int(x * 256 / width)
        for y in range(height):
            b = int(y * 256 / height)
            base_img[y, x, 0] = r
            base_img[y, x, 2] = b

    imgs = []
    for i in range(n):
        img = base_img.copy()
        g = int(i * 256 / n)
        img[:, :, 1] = g
        imgs.append(Image.fromarray(img))
    return imgs


def export_imgs(imgs, block_w, block_h, blocks_x, blocks_y, outdir):
    w, h = block_w * blocks_x, block_h * blocks_y
    
    os.makedirs(outdir, exist_ok=True)
    format = struct.pack('IIIIIII', len(imgs), w, h, block_w, block_h, blocks_x, blocks_y)
    with open(f'{outdir}/format.meta', 'wb') as f:
        assert f.write(format) == len(format)

    for img_num, img in enumerate(imgs):
        img = img.resize((w, h))
        for x in range(blocks_x):
            for y in range(blocks_y):
                l, r = w * x / blocks_x, w * (x + 1) / blocks_x
                t, b = h * y / blocks_y, h * (y + 1) / blocks_y
                chunk = img.crop((l, t, r, b))
                assert chunk.size == (block_h, block_w)

                filename = f'{outdir}/{img_num}_{x}_{y}.jpg'
                chunk.save(filename, progressive=False, subsampling="4:2:0", dpi=(300, 300))


if __name__ == '__main__':
    # _, filename = sys.argv
    # img = Image.open(filename)
    # print(img)

    block_w = 16
    block_h = 16
    blocks_x = 10
    blocks_y = 10


    imgs = create_test_imgs(20, block_w * blocks_x, block_h * blocks_y)

    export_imgs(imgs, block_w, block_h, blocks_x, blocks_y, "test_slides")