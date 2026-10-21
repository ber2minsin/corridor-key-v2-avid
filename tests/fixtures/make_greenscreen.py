"""Create a tiny synthetic greenscreen PNG for smoke tests."""

from pathlib import Path

import numpy as np
from imageio.v2 import imwrite

out = Path(__file__).with_name("greenscreen_64.png")
h, w = 64, 96
img = np.zeros((h, w, 3), dtype=np.uint8)
img[:] = (0, 180, 0)  # green screen
# subject rectangle
img[16:48, 32:64] = (200, 160, 140)
imwrite(out, img)
print("wrote", out)
