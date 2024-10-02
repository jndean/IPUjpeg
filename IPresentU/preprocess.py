from collections import namedtuple
from enum import IntEnum
import os
import struct

import numpy as np
import pdf2image as pdf
from PIL import Image
from tqdm import tqdm
import cv2


class TransitionType(IntEnum):
    INSTANT = 0
    FADE = 1
    LOCAL_H_WIPE = 2
    CIRCLE_WIPE = 3
    DISSOLVE = 4

Slide = namedtuple("Slide", ["numFrames", "loops", "transition", "transitionFrames"])

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
        imgs.append(img)
    return imgs


def video_to_imgs(filename):
    print("Loading video:", filename)
    vidcap = cv2.VideoCapture(filename)
    imgs = []
    success = True
    while success:
        success, image = vidcap.read()
        if success:
            imgs.append(Image.fromarray(cv2.cvtColor(image, cv2.COLOR_BGR2RGB)))
    return imgs


def export_imgs(nested_imgs, slides, block_w, block_h, blocks_x, blocks_y, outdir):

    # Flatten image list
    imgs = []
    all(not (imgs.extend if isinstance(x, list) else imgs.append)(x) for x in nested_imgs)

    w, h = block_w * blocks_x, block_h * blocks_y
    
    os.makedirs(outdir, exist_ok=True)
    format = struct.pack('IIIIIII', len(imgs), w, h, block_w, block_h, blocks_x, blocks_y)
    with open(f'{outdir}/format.meta', 'wb') as f:
        assert f.write(format) == len(format)
        for slide in slides:
            assert f.write(struct.pack('IIII', *slide)) == 16

    print("Exporting jpegs...")
    for img_num, img in tqdm(enumerate(imgs), total=len(imgs)):
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

    images = pdf.convert_from_path('materials/DoBeWeird.pdf')
    slides = [Slide(1, False, TransitionType.INSTANT, 1) for _ in images]
    slides[0] = Slide(1, False, TransitionType.FADE, 25)
    slides[1] = Slide(1, False, TransitionType.FADE, 25)

    agenda_imgs = video_to_imgs('materials/agenda.mp4')
    images[6] = agenda_imgs
    slides[6] = Slide(len(agenda_imgs), True, TransitionType.INSTANT, 1)

    slides[7] = Slide(1, False, TransitionType.LOCAL_H_WIPE, 25)
    slides[8] = Slide(1, False, TransitionType.LOCAL_H_WIPE, 25)
    slides[14] = Slide(1, False, TransitionType.FADE, 40)
    slides[24] = Slide(1, False, TransitionType.FADE, 25)
    
    xcom_imgs = video_to_imgs('materials/xcom.mp4')
    images[30] = xcom_imgs
    slides[30] = Slide(len(xcom_imgs), False, TransitionType.INSTANT, 1)
    
    re_imgs = video_to_imgs('materials/reverseengineer.mp4')
    images[42] = re_imgs
    slides[42] = Slide(len(re_imgs), True, TransitionType.INSTANT, 1)
    
    doom_imgs = video_to_imgs('materials/doomtransition.mp4')
    images[51] = doom_imgs
    slides[51] = Slide(len(doom_imgs), False, TransitionType.INSTANT, 1)
    doom_imgs = video_to_imgs('materials/doomdance.mp4')
    images[52] = doom_imgs
    slides[52] = Slide(len(doom_imgs), True, TransitionType.INSTANT, 1)
    
    export_imgs(
        images, slides,
        block_w = 32,
        block_h = 32,
        blocks_x = 40,
        blocks_y = 23,
        # blocks_x = 40,
        # blocks_y = 23,
        outdir = 'do_be_weird',
    )

    # block_w = 32
    # block_h = 32
    # blocks_x = 2
    # blocks_y = 2
    # imgs = create_test_imgs(3, block_w * blocks_x, block_h * blocks_y)
    # slides = [
    #     Slide(1, False, TransitionType.INSTANT, 1),
    #     Slide(2, True, TransitionType.INSTANT, 1),
    # ]
    # export_imgs(imgs, slides, block_w, block_h, blocks_x, blocks_y, "tiny_slides")