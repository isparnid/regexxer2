import sys
src, dst = sys.argv[1], sys.argv[2]
text = open(src).read().replace('<_', '<').replace('</_', '</')
open(dst, 'w').write(text)
