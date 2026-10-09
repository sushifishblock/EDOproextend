"""Reapplies the change to Irrlicht's CNullDriver::addTexture (irrlicht/src is downloaded, not part of the git repo).
Run from edopro-src:  python patches/apply_irrlicht_patch.py
Without it every new texture re-sorts the whole texture list (a heap sort), which makes adding thousands of card
pictures take quadratic time."""
import os
path = os.path.join('irrlicht', 'src', 'CNullDriver.cpp')
s = open(path, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
old = '''		Textures.push_back(s);

		// the new texture is now at the end of the texture list. when searching for
		// the next new texture, the texture array will be sorted and the index of this texture
		// will be changed. to let the order be more consistent to the user, sort
		// the textures now already although this isn't necessary:

		Textures.sort();'''.replace('\n', nl)
new = '''		// The texture list is kept sorted by name (findTexture does a binary search on it). Sorting the whole
		// list again for every new texture (a heap sort) makes adding thousands of textures take quadratic time,
		// so insert the new texture at its place instead (after any textures with an equal name).
		Textures.sort(); // does nothing when the list is already sorted
		u32 low = 0;
		u32 high = Textures.size();
		while (low < high)
		{
			const u32 mid = (low + high) >> 1;
			if (s < Textures[mid])
				high = mid;
			else
				low = mid + 1;
		}
		Textures.insert(s, low);
		Textures.set_sorted(true);'''.replace('\n', nl)
if 'insert the new texture at its place' in s:
    print('already applied')
else:
    assert old in s, 'CNullDriver.cpp does not look like the expected version'
    open(path, 'w', encoding='utf-8', newline='').write(s.replace(old, new, 1))
    print('applied')
