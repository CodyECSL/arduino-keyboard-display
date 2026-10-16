import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import crossy_art as art


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


class SpriteChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sprites, cls.groups = art.build()

    def test_every_sprite_has_pixels_and_a_valid_anchor(self):
        for name, (img, (ax, ay)) in self.sprites.items():
            self.assertTrue(1 <= img.width <= 260 and 1 <= img.height <= 130, name)
            self.assertTrue(0 <= ax <= img.width and 0 <= ay <= img.height, name)
            self.assertTrue(any(p[3] for p in img.getdata()), name)

    def test_no_art_pixel_uses_the_key_colour(self):
        for name, (img, _) in self.sprites.items():
            for r, g, b, a in img.getdata():
                if a:
                    self.assertNotEqual(rgb565(r, g, b), art.KEY565, name)

    def test_no_partial_alpha(self):
        for name, (img, _) in self.sprites.items():
            self.assertTrue(all(p[3] in (0, 255) for p in img.getdata()), name)

    def test_groups_name_existing_sprites(self):
        for cname, dims, names in self.groups:
            n = 1
            for d in dims:
                n *= d
            self.assertEqual(len(names), n, cname)
            for nm in names:
                self.assertTrue(nm is None or nm in self.sprites, (cname, nm))

    def test_every_sprite_is_in_a_group(self):
        used = {nm for _, _, names in self.groups for nm in names if nm}
        self.assertEqual(used, set(self.sprites), set(self.sprites) ^ used)

    def test_eagle_carry_shows_the_chicken(self):
        def whites(name):
            img = self.sprites[name][0].convert("RGB")
            return sum(1 for p in img.getdata() if p == (250, 250, 248))
        self.assertGreater(whites("eagle_carry"), 0)
        self.assertEqual(whites("eagle1"), 0)

    def test_signal_frames_are_visibly_different(self):
        names = next(n for g, _, n in self.groups if g == "ca_signal")
        self.assertEqual(len(names), 3)
        imgs = [self.sprites[n][0] for n in names]
        self.assertTrue(all(i.width < 200 for i in imgs))
        for i in range(3):
            for j in range(i + 1, 3):
                self.assertEqual(imgs[i].size, imgs[j].size)
                diff = sum(1 for p, q in zip(imgs[i].getdata(), imgs[j].getdata()) if p != q)
                self.assertGreater(diff, 0, (i, j))
        for k in (1, 2):
            diff = sum(1 for p, q in zip(imgs[0].getdata(), imgs[k].getdata()) if p != q)
            self.assertGreaterEqual(diff, 20, k)

    def test_vehicle_headings_differ(self):
        names = next(n for g, _, n in self.groups if g == "ca_vehicle")
        self.assertEqual(len(names), 12)
        for k in range(6):
            a, b = self.sprites[names[2 * k]][0], self.sprites[names[2 * k + 1]][0]
            self.assertNotEqual((a.size, list(a.getdata())), (b.size, list(b.getdata())), k)

    def test_budget(self):
        total = sum(img.width * img.height * 2 for img, _ in self.sprites.values())
        self.assertLessEqual(total, 400000, total)

    def test_deterministic(self):
        again, _ = art.build()
        for name, (img, anchor) in self.sprites.items():
            self.assertEqual(img.tobytes(), again[name][0].tobytes(), name)
            self.assertEqual(anchor, again[name][1], name)


if __name__ == "__main__":
    unittest.main()
